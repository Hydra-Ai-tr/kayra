#include "core/access.h"

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <set>

#include "core/http.h"
#include "core/i18n.h"
#include "core/util.h"

namespace kayra {

namespace {

struct Alias {
    const char* id;
    std::vector<const char*> stems;
    const char *tr, *en;
};

const std::vector<Alias>& aliases() {
    static const std::vector<Alias> a = {
        {"desktop", {"masaust", "desktop"}, "Masaüstü", "Desktop"},
        {"downloads", {"indiril", "indirme", "indirdik", "download"}, "İndirilenler", "Downloads"},
        {"documents", {"belge", "dokuman", "document", "my documents"}, "Belgeler", "Documents"},
        {"pictures", {"resim", "fotograf", "picture", "photo"}, "Resimler", "Pictures"},
        {"music", {"muzik", "music"}, "Müzik", "Music"},
        {"videos", {"video"}, "Videolar", "Videos"},
        {"home", {"kullanici klasor", "ev klasor", "home"}, "kullanıcı klasörü", "home folder"},
    };
    return a;
}

std::string lower_ascii(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

int64_t to_unix(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (int64_t)(u.QuadPart / 10000000ULL) - 11644473600LL;
}

FileEntry entry_of(const std::string& folder, const WIN32_FIND_DATAW& d) {
    FileEntry e;
    e.name = narrow(d.cFileName);
    e.path = folder + "\\" + e.name;
    e.dir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    e.size = ((int64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    e.modified = to_unix(d.ftLastWriteTime);
    return e;
}

bool hidden(const WIN32_FIND_DATAW& d) {
    return (d.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0 || d.cFileName[0] == L'.';
}

void append_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

std::string decode_entities(const std::string& s) {
    static const std::pair<const char*, const char*> named[] = {
        {"amp", "&"},      {"lt", "<"},       {"gt", ">"},       {"quot", "\""},     {"apos", "'"},
        {"nbsp", " "},     {"ndash", "-"},    {"mdash", "-"},    {"hellip", "..."},  {"laquo", "\""},
        {"raquo", "\""},   {"lsquo", "'"},    {"rsquo", "'"},    {"ldquo", "\""},    {"rdquo", "\""},
        {"middot", "·"},   {"copy", "©"},     {"deg", "°"},      {"euro", "€"},      {"bull", "·"},
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) {
            out += '&';
            continue;
        }
        std::string name = s.substr(i + 1, semi - i - 1);
        bool ok = false;
        if (!name.empty() && name[0] == '#') {
            unsigned cp = name.size() > 1 && (name[1] == 'x' || name[1] == 'X') ? std::strtoul(name.c_str() + 2, nullptr, 16)
                                                                                  : std::strtoul(name.c_str() + 1, nullptr, 10);
            if (cp > 0 && cp < 0x110000) {
                append_utf8(out, cp == 0xA0 ? 0x20 : cp);
                ok = true;
            }
        } else {
            for (auto& n : named)
                if (name == n.first) {
                    out += n.second;
                    ok = true;
                    break;
                }
        }
        if (ok) i = semi;
        else out += '&';
    }
    return out;
}

std::string tag_text(const std::string& xml, const std::string& tag, size_t from = 0, size_t to = std::string::npos) {
    size_t a = xml.find("<" + tag, from);
    if (a == std::string::npos || a >= to) return "";
    a = xml.find('>', a);
    if (a == std::string::npos) return "";
    size_t b = xml.find("</" + tag + ">", a);
    if (b == std::string::npos || b > to) return "";
    std::string v = xml.substr(a + 1, b - a - 1);
    if (starts_with(v, "<![CDATA[")) v = v.substr(9, v.size() >= 12 ? v.size() - 12 : 0);
    return trim(decode_entities(v));
}

std::string strip_tags(const std::string& s) {
    std::string out;
    bool in = false;
    for (char c : s) {
        if (c == '<') in = true;
        else if (c == '>') in = false;
        else if (!in) out += c;
    }
    out = decode_entities(out);
    // zero-width and direction marks (U+200B..U+200F)
    for (const char* z : {"\xE2\x80\x8B", "\xE2\x80\x8C", "\xE2\x80\x8D", "\xE2\x80\x8E", "\xE2\x80\x8F"}) out = replace_all(out, z, "");
    return trim(out);
}

std::vector<WebResult> rss_items(const std::string& xml) {
    std::vector<WebResult> out;
    size_t pos = 0;
    for (;;) {
        size_t a = xml.find("<item>", pos);
        if (a == std::string::npos) break;
        size_t b = xml.find("</item>", a);
        if (b == std::string::npos) break;
        WebResult r;
        r.title = strip_tags(tag_text(xml, "title", a, b));
        r.url = tag_text(xml, "link", a, b);
        r.snippet = strip_tags(tag_text(xml, "description", a, b));
        r.source = strip_tags(tag_text(xml, "source", a, b));
        if (!r.title.empty()) out.push_back(r);
        pos = b;
    }
    return out;
}

// Bing's result page when the RSS answer is empty: <li class="b_algo"><h2><a href=..>title</a></h2>..<p>snippet</p>
std::vector<WebResult> bing_html(const std::string& html) {
    std::vector<WebResult> out;
    size_t pos = 0;
    while (out.size() < 10) {
        size_t a = html.find("class=\"b_algo\"", pos);
        if (a == std::string::npos) break;
        size_t end = html.find("class=\"b_algo\"", a + 10);
        if (end == std::string::npos) end = html.size();
        WebResult r;
        size_t h = html.find("<h2", a);
        if (h != std::string::npos && h < end) {
            size_t href = html.find("href=\"", h);
            if (href != std::string::npos && href < end) {
                size_t q = html.find('"', href + 6);
                r.url = decode_entities(html.substr(href + 6, q - href - 6));
                size_t t0 = html.find('>', href), t1 = html.find("</a>", href);
                if (t0 != std::string::npos && t1 != std::string::npos && t1 > t0) r.title = strip_tags(html.substr(t0 + 1, t1 - t0 - 1));
            }
        }
        size_t p = html.find("<p", a);
        if (p != std::string::npos && p < end) {
            size_t p0 = html.find('>', p), p1 = html.find("</p>", p);
            if (p0 != std::string::npos && p1 != std::string::npos && p1 > p0) r.snippet = strip_tags(html.substr(p0 + 1, p1 - p0 - 1));
        }
        if (!r.title.empty() && starts_with(r.url, "http")) out.push_back(r);
        pos = end;
    }
    return out;
}

}  // namespace

std::string folder_alias(const std::string& f) {
    if (f == "~") return "home";
    for (auto& a : aliases())
        for (const char* s : a.stems)
            if (starts_with(f, s)) return a.id;
    return "";
}

std::string resolve_path(const std::string& in) {
    std::string p = trim(in);
    while (p.size() >= 2 && (p.front() == '"' || p.front() == '\'') && p.back() == p.front()) p = p.substr(1, p.size() - 2);
    p = replace_all(p, "/", "\\");
    if (contains(p, "%")) {
        wchar_t buf[4096];
        DWORD n = ExpandEnvironmentStringsW(widen(p).c_str(), buf, 4096);
        if (n > 0 && n < 4096) p = narrow(buf);
    }
    if (p.empty()) return known_folder("desktop");
    if (p[0] == '~') p = known_folder("home") + p.substr(1);
    bool absolute = (p.size() >= 2 && p[1] == ':') || starts_with(p, "\\\\");
    if (!absolute) {
        size_t cut = p.find('\\');
        std::string first = cut == std::string::npos ? p : p.substr(0, cut);
        std::string rest = cut == std::string::npos ? "" : p.substr(cut);
        std::string id = folder_alias(fold(first));
        // "rapor.txt" is a file name, not a folder alias: only words without a dot count
        if (!id.empty() && first.find('.') == std::string::npos) p = known_folder(id) + rest;
        else p = known_folder("desktop") + "\\" + p;
    }
    while (p.size() > 3 && p.back() == '\\') p.pop_back();
    return p;
}

std::string place_name(const std::string& path) {
    std::string lp = lower_ascii(path);
    for (auto& a : aliases())
        if (lower_ascii(known_folder(a.id)) == lp) return is_turkish() ? a.tr : a.en;
    size_t k = path.find_last_of('\\');
    return k == std::string::npos ? path : path.substr(k + 1);
}

bool list_folder(const std::string& path, std::vector<FileEntry>& out, std::string* error) {
    out.clear();
    DWORD attr = GetFileAttributesW(widen(path).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if (error) *error = "folder not found: " + path;
        return false;
    }
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileExW(widen(path + "\\*").c_str(), FindExInfoBasic, &d, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        if (error) *error = "cannot read the folder";
        return false;
    }
    do {
        if (!wcscmp(d.cFileName, L".") || !wcscmp(d.cFileName, L"..") || hidden(d)) continue;
        out.push_back(entry_of(path, d));
    } while (FindNextFileW(h, &d));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.dir != b.dir) return a.dir;
        return a.modified > b.modified;
    });
    return true;
}

std::vector<FileEntry> find_files(const std::string& query, const std::string& root, size_t max_results,
                                  double max_seconds) {
    std::vector<FileEntry> hits;
    std::wstring wq = widen(trim(query));
    bool wildcard = wq.find_first_of(L"*?") != std::wstring::npos;
    std::vector<std::string> words;
    static const std::set<std::string> filler = {"dosya", "dosyasi", "dosyasini", "dosyalari", "dosyalarini", "belgesi",
                                                  "belgesini", "klasoru", "klasorunu", "adli", "isimli", "file", "files",
                                                  "folder", "document", "the", "my", "named", "called"};
    for (auto& w : split_words(fold(query)))
        if (!filler.count(w)) words.push_back(w);
    if (!wildcard && words.empty()) return hits;
    std::string want;
    for (auto& w : words) want += (want.empty() ? "" : " ") + w;

    std::vector<std::string> roots;
    if (!root.empty()) roots.push_back(root);
    else
        for (const char* k : {"desktop", "documents", "downloads", "pictures", "music", "videos", "home"}) roots.push_back(known_folder(k));
    std::set<std::string> seen;
    struct Item {
        std::string path;
        int depth;
    };
    std::vector<Item> stack;
    for (auto it = roots.rbegin(); it != roots.rend(); ++it)
        if (!it->empty()) stack.push_back({*it, 0});
    double t0 = now_seconds();
    std::vector<std::pair<int, FileEntry>> scored;
    while (!stack.empty() && now_seconds() - t0 < max_seconds && scored.size() < max_results * 4) {
        Item cur = stack.back();
        stack.pop_back();
        if (!seen.insert(lower_ascii(cur.path)).second) continue;
        WIN32_FIND_DATAW d;
        HANDLE h = FindFirstFileExW(widen(cur.path + "\\*").c_str(), FindExInfoBasic, &d, FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) continue;
        std::vector<Item> subdirs;
        do {
            if (!wcscmp(d.cFileName, L".") || !wcscmp(d.cFileName, L"..") || hidden(d)) continue;
            FileEntry e = entry_of(cur.path, d);
            std::string fn = fold(e.name);
            bool match;
            if (wildcard) {
                match = PathMatchSpecW(d.cFileName, wq.c_str()) != 0;
            } else {
                match = true;
                for (auto& w : words) match = match && contains(fn, w);
            }
            if (match) {
                std::string stem = fold(e.dir ? e.name : e.name.substr(0, e.name.find_last_of('.')));
                int score = stem == want || fn == want ? 2 : starts_with(fn, want) ? 1 : 0;
                scored.push_back({score, e});
            }
            if (e.dir && cur.depth < 7) {
                std::string n = lower_ascii(e.name);
                if (n != "node_modules" && n != "appdata" && n != "__pycache__" && n != "$recycle.bin" &&
                    !(d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    subdirs.push_back({e.path, cur.depth + 1});
            }
        } while (FindNextFileW(h, &d));
        FindClose(h);
        for (auto it = subdirs.rbegin(); it != subdirs.rend(); ++it) stack.push_back(*it);
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second.modified > b.second.modified;
    });
    for (auto& s : scored) {
        if (hits.size() >= max_results) break;
        hits.push_back(s.second);
    }
    return hits;
}

bool read_text_file(const std::string& path, std::string& out, size_t max_bytes, std::string* error) {
    std::ifstream f(std::filesystem::path(widen(path)), std::ios::binary);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::string raw(max_bytes, '\0');
    f.read(&raw[0], (std::streamsize)max_bytes);
    raw.resize((size_t)f.gcount());
    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        std::wstring w((raw.size() - 2) / 2, L'\0');
        memcpy(&w[0], raw.data() + 2, w.size() * 2);
        out = narrow(w);
        return true;
    }
    if (raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw = raw.substr(3);
    if (raw.find('\0') != std::string::npos) {
        if (error) *error = "not a text file";
        return false;
    }
    // not UTF-8: the Windows code page (Turkish files saved by old Notepad)
    if (!raw.empty() && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), (int)raw.size(), nullptr, 0) == 0) {
        int n = MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), &w[0], n);
        raw = narrow(w);
    }
    out = raw;
    return true;
}

bool recycle_path(const std::string& path) {
    std::wstring w = widen(path);
    w.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = w.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

std::string size_text(int64_t b, bool tr) {
    if (b < 1024) return std::to_string(b) + " B";
    if (b < 1024 * 1024) return format_number(b / 1024.0, 0, tr) + " KB";
    if (b < 1024LL * 1024 * 1024) return format_number(b / 1048576.0, 1, tr) + " MB";
    return format_number(b / 1073741824.0, 1, tr) + " GB";
}

std::vector<WebResult> search_web(const std::string& query, bool tr, std::string* error) {
    std::string q = url_encode(query);
    std::string loc = tr ? "&setlang=tr&cc=TR" : "&setlang=en&cc=US";
    HttpResponse r = http_get("https://www.bing.com/search?format=rss&q=" + q + loc, 15000);
    std::vector<WebResult> out;
    if (r.ok()) out = rss_items(r.body);
    if (out.empty()) {
        HttpResponse h = http_get("https://www.bing.com/search?q=" + q + loc, 15000);
        if (h.ok()) out = bing_html(h.body);
        if (out.empty() && error) *error = h.error.empty() ? "no results (HTTP " + std::to_string(h.status) + ")" : h.error;
    }
    if (out.size() > 8) out.resize(8);
    return out;
}

std::vector<WebResult> news_headlines(const std::string& topic, bool tr, std::string* error) {
    std::string loc = tr ? "hl=tr&gl=TR&ceid=TR:tr" : "hl=en-US&gl=US&ceid=US:en";
    std::string url = trim(topic).empty() ? "https://news.google.com/rss?" + loc
                                          : "https://news.google.com/rss/search?q=" + url_encode(topic) + "&" + loc;
    HttpResponse r = http_get(url, 15000);
    std::vector<WebResult> out;
    if (!r.ok()) {
        if (error) *error = r.error.empty() ? "news HTTP " + std::to_string(r.status) : r.error;
        return out;
    }
    out = rss_items(r.body);
    for (auto& x : out) {
        // "Headline - Source"
        if (!x.source.empty() && ends_with(x.title, " - " + x.source)) x.title = x.title.substr(0, x.title.size() - x.source.size() - 3);
        x.snippet.clear();
    }
    if (out.size() > 10) out.resize(10);
    if (out.empty() && error) *error = "no news";
    return out;
}

std::string html_to_text(const std::string& html, std::string* title) {
    std::string low = lower_ascii(html);
    if (title) {
        size_t a = low.find("<title");
        if (a != std::string::npos) {
            a = low.find('>', a);
            size_t b = low.find("</title>", a);
            if (a != std::string::npos && b != std::string::npos) *title = strip_tags(html.substr(a + 1, b - a - 1));
        }
    }
    std::string out;
    out.reserve(html.size() / 3);
    static const char* skip_tags[] = {"script", "style", "noscript", "svg", "nav", "footer", "aside", "form", "head", "template"};
    static const char* block_tags[] = {"p", "div", "br", "li", "h1", "h2", "h3", "h4", "h5", "h6", "tr", "section", "article",
                                       "blockquote", "pre", "dt", "dd", "header", "table", "ul", "ol"};
    size_t i = 0;
    while (i < html.size()) {
        if (html[i] != '<') {
            out += html[i++];
            continue;
        }
        if (low.compare(i, 4, "<!--") == 0) {
            size_t e = low.find("-->", i);
            i = e == std::string::npos ? html.size() : e + 3;
            continue;
        }
        size_t e = low.find('>', i);
        if (e == std::string::npos) break;
        size_t n0 = i + 1 + (low[i + 1] == '/' ? 1 : 0);
        size_t n1 = n0;
        while (n1 < e && std::isalnum((unsigned char)low[n1])) n1++;
        std::string name = low.substr(n0, n1 - n0);
        bool closing = low[i + 1] == '/';
        bool skipped = false;
        if (!closing) {
            for (const char* s : skip_tags)
                if (name == s) {
                    size_t c = low.find("</" + name, e);
                    i = c == std::string::npos ? html.size() : low.find('>', c) + 1;
                    if (i == 0) i = html.size();
                    skipped = true;
                    break;
                }
        }
        if (skipped) continue;
        for (const char* b : block_tags)
            if (name == b) {
                out += '\n';
                break;
            }
        i = e + 1;
    }
    out = decode_entities(out);
    // tidy: trim lines, collapse spaces and blank runs
    std::string tidy, line;
    int blank = 0;
    auto flush = [&] {
        std::string t;
        bool sp = false;
        for (char c : line) {
            if (c == ' ' || c == '\t' || c == '\r') sp = true;
            else {
                if (sp && !t.empty()) t += ' ';
                sp = false;
                t += c;
            }
        }
        if (t.empty()) {
            if (++blank == 1 && !tidy.empty()) tidy += '\n';
        } else {
            blank = 0;
            tidy += t + '\n';
        }
        line.clear();
    };
    for (char c : out) {
        if (c == '\n') flush();
        else line += c;
    }
    flush();
    return trim(tidy);
}

bool read_webpage(const std::string& url_in, std::string& title, std::string& text, std::string* error) {
    std::string url = trim(url_in);
    if (!starts_with(url, "http://") && !starts_with(url, "https://")) url = "https://" + url;
    HttpResponse r = http_get(url, 20000);
    if (!r.ok()) {
        if (error) *error = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error;
        return false;
    }
    std::string head = lower_ascii(r.body.substr(0, 2000));
    if (contains(head, "<html") || contains(head, "<!doctype") || contains(head, "<head") || contains(head, "<body")) {
        text = html_to_text(r.body, &title);
    } else {
        text = r.body;
    }
    if (trim(text).empty()) {
        if (error) *error = "the page has no readable text";
        return false;
    }
    return true;
}

}  // namespace kayra

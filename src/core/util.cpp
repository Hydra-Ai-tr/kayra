#include "core/util.h"

#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace kayra {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static wchar_t lower_char(wchar_t c) {
    switch (c) {
        case L'I': return L'\u0131';
        case L'\u0130': return L'i';
        case L'\u011E': return L'\u011F';
        case L'\u015E': return L'\u015F';
        default: break;
    }
    if (c >= L'A' && c <= L'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    return c;
}

std::string lower(const std::string& s) {
    std::wstring w = widen(s);
    for (auto& c : w) c = lower_char(c);
    return narrow(w);
}

static wchar_t fold_char(wchar_t c) {
    c = lower_char(c);
    switch (c) {
        case L'\u0131': case 0xEE: case 0xEC: case 0xED: case 0xEF: return L'i';
        case 0xE7: return L'c';
        case L'\u011F': return L'g';
        case 0xF6: case 0xF4: case 0xF2: case 0xF3: return L'o';
        case L'\u015F': return L's';
        case 0xFC: case 0xFB: case 0xF9: case 0xFA: return L'u';
        case 0xE2: case 0xE0: case 0xE1: case 0xE4: case 0xE3: return L'a';
        case 0xE9: case 0xE8: case 0xEA: case 0xEB: return L'e';
        case 0xF1: return L'n';
        default: return c;
    }
}

std::string fold(const std::string& s) {
    std::wstring w = widen(s);
    std::string out;
    out.reserve(w.size());
    auto digit = [&](size_t i) { return i < w.size() && w[i] >= L'0' && w[i] <= L'9'; };
    for (size_t i = 0; i < w.size(); i++) {
        wchar_t c = fold_char(w[i]);
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) {
            out += (char)c;
        } else if (c == L'\'' || c == 0x2019 || c == 0x2018 || c == L'`') {
            // "discord'u" -> "discordu"
        } else if ((c == L'.' || c == L',') && i > 0 && digit(i - 1) && digit(i + 1)) {
            out += (char)c;
        } else if (c == L'-' && (digit(i + 1) || (i > 0 && digit(i - 1)))) {
            out += '-';
        } else if (c == L'%' || c == L'+' || c == L'*' || c == L'/' || c == L'^' || c == L'(' || c == L')' ||
                   c == L'=') {
            out += ' ';
            out += (char)c;
            out += ' ';
        } else if (c == 0xD7) {
            out += " * ";
        } else if (c == 0xF7) {
            out += " / ";
        } else {
            out += ' ';
        }
    }
    // collapse whitespace
    std::string r;
    bool sp = true;
    for (char c : out) {
        if (c == ' ') {
            if (!sp) r += ' ';
            sp = true;
        } else {
            r += c;
            sp = false;
        }
    }
    while (!r.empty() && r.back() == ' ') r.pop_back();
    return r;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
    return s.substr(a, b - a);
}

std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string w;
    while (is >> w) out.push_back(w);
    return out;
}

bool starts_with(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool ends_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
bool contains(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string capitalize(const std::string& s) {
    std::wstring w = widen(s);
    if (w.empty()) return s;
    wchar_t& c = w[0];
    switch (c) {
        case L'i': c = L'\u0130'; break;
        case L'\u0131': c = L'I'; break;
        case L'\u011F': c = L'\u011E'; break;
        case L'\u015F': c = L'\u015E'; break;
        default:
            if (c >= L'a' && c <= L'z') c -= 32;
            else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) c -= 32;
    }
    return narrow(w);
}

double similarity(const std::string& a, const std::string& b) {
    if (a.empty() && b.empty()) return 1.0;
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) prev[j] = (int)j;
    for (size_t i = 1; i <= a.size(); i++) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= b.size(); j++) {
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    double d = prev[b.size()];
    return 1.0 - d / (double)std::max(a.size(), b.size());
}

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else if (c == ' ') {
            out += "%20";
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string format_number(double v, int max_decimals, bool turkish) {
    if (std::isnan(v) || std::isinf(v)) return turkish ? "tanımsız" : "undefined";
    char buf[64];
    snprintf(buf, sizeof buf, "%.*f", max_decimals, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    if (turkish) std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

static std::string known_path(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = narrow(p);
    if (p) CoTaskMemFree(p);
    return out;
}

std::string app_dir() {
    static std::string dir = [] {
        std::string d = known_path(FOLDERID_RoamingAppData) + "\\Kayra";
        CreateDirectoryW(widen(d).c_str(), nullptr);
        return d;
    }();
    return dir;
}

std::string models_dir() {
    std::string d = app_dir() + "\\models";
    CreateDirectoryW(widen(d).c_str(), nullptr);
    return d;
}

std::string exe_dir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    std::wstring p(buf, n);
    size_t k = p.find_last_of(L"\\/");
    return narrow(k == std::wstring::npos ? p : p.substr(0, k));
}

std::string known_folder(const std::string& which) {
    if (which == "desktop") return known_path(FOLDERID_Desktop);
    if (which == "downloads") return known_path(FOLDERID_Downloads);
    if (which == "documents") return known_path(FOLDERID_Documents);
    if (which == "pictures") return known_path(FOLDERID_Pictures);
    if (which == "music") return known_path(FOLDERID_Music);
    if (which == "videos") return known_path(FOLDERID_Videos);
    if (which == "home") return known_path(FOLDERID_Profile);
    if (which == "startmenu") return known_path(FOLDERID_StartMenu);
    if (which == "common_startmenu") return known_path(FOLDERID_CommonStartMenu);
    if (which == "public_desktop") return known_path(FOLDERID_PublicDesktop);
    return {};
}

bool file_exists(const std::string& path) {
    DWORD a = GetFileAttributesW(widen(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES;
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(std::filesystem::path(widen(path)), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool write_file(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(std::filesystem::path(widen(tmp)), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), (std::streamsize)data.size());
        if (!f) return false;
    }
    return MoveFileExW(widen(tmp).c_str(), widen(path).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

int64_t file_size(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(widen(path).c_str(), GetFileExInfoStandard, &d)) return -1;
    return ((int64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

void log(const std::string& line) {
    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char ts[32];
    std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    std::ofstream f(std::filesystem::path(widen(app_dir() + "\\kayra.log")), std::ios::app | std::ios::binary);
    f << ts << "  " << line << "\n";
}

double now_seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string clock_text(bool seconds) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[16];
    std::strftime(buf, sizeof buf, seconds ? "%H:%M:%S" : "%H:%M", &tm);
    return buf;
}

}  // namespace kayra

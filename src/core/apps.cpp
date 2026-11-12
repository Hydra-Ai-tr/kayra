#include "core/apps.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <functional>

#include "core/i18n.h"
#include "core/system.h"
#include "core/util.h"

namespace kayra {

namespace {

struct Builtin {
    std::vector<const char*> names;
    const char* target;
    const char* fallback;
    const char* display;
    const char* kind;  // "tool", "folder:<known>", "site"
};

const std::vector<Builtin>& builtins() {
    static const std::vector<Builtin> b = {
        {{"not defteri", "notdefteri", "notepad"}, "notepad.exe", "", "Not Defteri|Notepad", "tool"},
        {{"hesap makinesi", "hesap makinasi", "calculator", "calc"}, "calc.exe", "", "Hesap Makinesi|Calculator", "tool"},
        {{"paint"}, "mspaint.exe", "", "Paint", "tool"},
        {{"dosya gezgini", "gezgin", "explorer", "file explorer", "dosyalar", "bilgisayarim", "bu bilgisayar", "this pc"},
         "explorer.exe", "", "Dosya Gezgini|File Explorer", "tool"},
        {{"ayarlar", "windows ayarlari", "settings"}, "ms-settings:", "", "Ayarlar|Settings", "tool"},
        {{"bluetooth", "bluetooth ayarlari"}, "ms-settings:bluetooth", "", "Bluetooth", "tool"},
        {{"wifi", "wi fi", "ag ayarlari", "kablosuz", "network settings"}, "ms-settings:network-wifi", "", "Wi-Fi", "tool"},
        {{"ekran ayarlari", "display settings"}, "ms-settings:display", "", "Ekran ayarları|Display settings", "tool"},
        {{"ses ayarlari", "sound settings"}, "ms-settings:sound", "", "Ses ayarları|Sound settings", "tool"},
        {{"windows update", "guncellemeler", "guncelleme"}, "ms-settings:windowsupdate", "", "Windows Update", "tool"},
        {{"gorev yoneticisi", "task manager", "taskmgr"}, "taskmgr.exe", "", "Görev Yöneticisi|Task Manager", "tool"},
        {{"komut istemi", "cmd", "command prompt", "konsol"}, "cmd.exe", "", "Komut İstemi|Command Prompt", "tool"},
        {{"terminal", "windows terminal"}, "wt.exe", "cmd.exe", "Terminal", "tool"},
        {{"powershell"}, "powershell.exe", "", "PowerShell", "tool"},
        {{"denetim masasi", "control panel"}, "control.exe", "", "Denetim Masası|Control Panel", "tool"},
        {{"kayit defteri", "registry", "regedit"}, "regedit.exe", "", "Kayıt Defteri|Registry Editor", "tool"},
        {{"kamera", "camera"}, "microsoft.windows.camera:", "", "Kamera|Camera", "tool"},
        {{"magaza", "microsoft store", "store"}, "ms-windows-store:", "", "Microsoft Store", "tool"},
        {{"geri donusum kutusu", "cop kutusu", "recycle bin"}, "shell:RecycleBinFolder", "", "Geri Dönüşüm Kutusu|Recycle Bin", "tool"},
        {{"ekran alintisi", "snipping tool", "ekran alinti araci"}, "ms-screenclip:", "", "Ekran Alıntısı|Snipping Tool", "tool"},
        {{"indirilenler", "indirmeler", "indirilen dosyalar", "downloads"}, "", "", "İndirilenler|Downloads", "folder:downloads"},
        {{"belgeler", "belgelerim", "dokumanlar", "documents"}, "", "", "Belgeler|Documents", "folder:documents"},
        {{"masaustu", "desktop"}, "", "", "Masaüstü|Desktop", "folder:desktop"},
        {{"resimler", "resimlerim", "fotograflar", "pictures", "photos folder"}, "", "", "Resimler|Pictures", "folder:pictures"},
        {{"muzik", "muzikler", "muziklerim", "music"}, "", "", "Müzik|Music", "folder:music"},
        {{"videolar", "videolarim", "videos"}, "", "", "Videolar|Videos", "folder:videos"},
        {{"kullanici klasoru", "ev klasoru", "home folder"}, "", "", "Kullanıcı klasörü|home", "folder:home"},
    };
    return b;
}

const std::vector<Builtin>& sites() {
    static const std::vector<Builtin> s = {
        {{"tarayici", "internet", "browser", "web tarayicisi"}, "https://www.google.com", "", "tarayıcı|browser", "site"},
        {{"youtube", "yutup", "yutub"}, "https://www.youtube.com", "", "YouTube", "site"},
        {{"google"}, "https://www.google.com", "", "Google", "site"},
        {{"gmail", "e posta", "eposta", "mail"}, "https://mail.google.com", "", "Gmail", "site"},
        {{"github"}, "https://github.com", "", "GitHub", "site"},
        {{"twitter", "x"}, "https://x.com", "", "X", "site"},
        {{"instagram"}, "https://www.instagram.com", "", "Instagram", "site"},
        {{"whatsapp", "whatsapp web"}, "https://web.whatsapp.com", "", "WhatsApp", "site"},
        {{"chatgpt", "chat gpt"}, "https://chatgpt.com", "", "ChatGPT", "site"},
        {{"spotify"}, "https://open.spotify.com", "", "Spotify", "site"},
        {{"netflix"}, "https://www.netflix.com", "", "Netflix", "site"},
        {{"vikipedi", "wikipedia"}, "https://www.wikipedia.org", "", "Vikipedi|Wikipedia", "site"},
        {{"haritalar", "harita", "google haritalar", "maps", "google maps"}, "https://maps.google.com", "", "Haritalar|Maps", "site"},
        {{"ceviri", "google ceviri", "translate", "google translate"}, "https://translate.google.com", "", "Google Çeviri|Google Translate", "site"},
        {{"reddit"}, "https://www.reddit.com", "", "Reddit", "site"},
        {{"linkedin"}, "https://www.linkedin.com", "", "LinkedIn", "site"},
        {{"twitch"}, "https://www.twitch.tv", "", "Twitch", "site"},
        {{"discord"}, "https://discord.com/app", "", "Discord", "site"},
    };
    return s;
}

const char* kFiller[] = {"klasoru", "klasorunu", "klasorum", "klasorumu", "uygulamasi", "uygulamasini", "uygulama",
                         "programi", "programini", "program", "sitesi", "sitesini", "site", "app", "the", "folder",
                         "application", "lutfen", "bana", "benim", "icin", "my", "please", "up", "hemen", "simdi",
                         "bir", "a", "an", "kayra", "hey"};

const char* kSuffixes[] = {"yu", "yi", "nu", "ni", "ya", "ye", "na", "ne", "dan", "den", "tan", "ten", "da", "de",
                           "ta", "te", "u", "i", "a", "e"};

// Shell GUIDs, spelled out so we do not depend on libuuid having them.
const GUID kFolderAppsFolder = {0x1e87508d, 0x89c2, 0x42f0, {0x8a, 0x7e, 0x64, 0x5a, 0x0f, 0x50, 0xca, 0x58}};
const GUID kBhidEnumItems = {0x94f60519, 0x2850, 0x4924, {0xaa, 0x5a, 0xd1, 0x5e, 0x84, 0x86, 0x80, 0x39}};
const GUID kIidIShellItem = {0x43826d1e, 0xe718, 0x42ee, {0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe}};
const GUID kIidIEnumShellItems = {0x70629033, 0xe363, 0x4a28, {0xa5, 0x67, 0x0d, 0xb7, 0x80, 0x06, 0xe6, 0xd7}};

bool skip_name(const std::string& f) {
    return contains(f, "uninstall") || contains(f, "kaldir") || contains(f, "readme") || contains(f, "release notes") ||
           contains(f, "documentation") || contains(f, "yardim") || f == "help";
}

}  // namespace

std::vector<std::string> name_candidates(const std::string& folded) {
    std::vector<std::string> out;
    auto add = [&](const std::string& s) {
        std::string t = trim(s);
        if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    };
    add(folded);
    std::vector<std::string> words;
    for (auto& w : split_words(folded)) {
        bool filler = false;
        for (const char* f : kFiller) filler = filler || w == f;
        if (!filler) words.push_back(w);
    }
    auto join = [](const std::vector<std::string>& v) {
        std::string s;
        for (auto& w : v) s += (s.empty() ? "" : " ") + w;
        return s;
    };
    add(join(words));
    if (!words.empty()) {
        std::string last = words.back();
        for (const char* suf : kSuffixes) {
            std::string s = suf;
            if (last.size() > s.size() + 2 && ends_with(last, s)) {
                auto w2 = words;
                w2.back() = last.substr(0, last.size() - s.size());
                add(join(w2));
            }
        }
    }
    return out;
}

void AppIndex::build() {
    std::vector<Entry> list;
    HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // Every app in the Start menu, Store apps included.
    IShellItem* apps = nullptr;
    if (SUCCEEDED(SHGetKnownFolderItem(kFolderAppsFolder, KF_FLAG_DEFAULT, nullptr, kIidIShellItem, (void**)&apps))) {
        IEnumShellItems* en = nullptr;
        if (SUCCEEDED(apps->BindToHandler(nullptr, kBhidEnumItems, kIidIEnumShellItems, (void**)&en))) {
            IShellItem* item = nullptr;
            while (en->Next(1, &item, nullptr) == S_OK) {
                PWSTR name = nullptr, id = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) &&
                    SUCCEEDED(item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &id))) {
                    Entry e;
                    e.display = narrow(name);
                    e.folded = fold(e.display);
                    e.target = "shell:AppsFolder\\" + narrow(id);
                    if (!e.folded.empty() && !skip_name(e.folded)) list.push_back(e);
                }
                if (name) CoTaskMemFree(name);
                if (id) CoTaskMemFree(id);
                item->Release();
            }
            en->Release();
        }
        apps->Release();
    }
    // Desktop shortcuts (games and tools often live only there).
    std::function<void(const std::string&, int)> scan = [&](const std::string& dir, int depth) {
        if (dir.empty()) return;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(widen(dir + "\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            std::string n = narrow(fd.cFileName);
            if (n == "." || n == "..") continue;
            std::string full = dir + "\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (depth > 0) scan(full, depth - 1);
                continue;
            }
            std::string ln = lower(n);
            for (const char* ext : {".lnk", ".url", ".appref-ms", ".exe"}) {
                if (ends_with(ln, ext)) {
                    Entry e;
                    e.display = n.substr(0, n.size() - strlen(ext));
                    e.folded = fold(e.display);
                    e.target = full;
                    if (!e.folded.empty() && !skip_name(e.folded)) list.push_back(e);
                    break;
                }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    };
    scan(known_folder("desktop"), 0);
    scan(known_folder("public_desktop"), 0);
    if (list.size() < 5) {  // AppsFolder unavailable: fall back to the Start menu folders
        scan(known_folder("startmenu"), 4);
        scan(known_folder("common_startmenu"), 4);
    }
    if (SUCCEEDED(co)) CoUninitialize();
    std::lock_guard<std::mutex> lk(mu_);
    entries_ = std::move(list);
    ready_ = true;
}

bool AppIndex::ready() const {
    std::lock_guard<std::mutex> lk(mu_);
    return ready_;
}

size_t AppIndex::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return entries_.size();
}

bool AppIndex::match_index(const std::vector<std::string>& candidates, OpenTarget& out) const {
    std::lock_guard<std::mutex> lk(mu_);
    double best = 0;
    const Entry* hit = nullptr;
    for (auto& c : candidates) {
        if (c.size() < 2) continue;
        std::vector<std::string> cw = split_words(c);
        for (auto& e : entries_) {
            double s = 0;
            if (e.folded == c) {
                s = 1.0;
            } else if (starts_with(e.folded, c + " ")) {
                s = 0.92;
            } else {
                std::vector<std::string> ew = split_words(e.folded);
                bool all = !cw.empty();
                for (auto& w : cw) all = all && std::find(ew.begin(), ew.end(), w) != ew.end();
                if (all) s = 0.86;
                s = std::max(s, similarity(e.folded, c));
                // "opera" vs "opera gx tarayici": compare with the leading words
                if (ew.size() > cw.size()) {
                    std::string head;
                    for (size_t i = 0; i < cw.size(); i++) head += (i ? " " : "") + ew[i];
                    s = std::max(s, similarity(head, c) * 0.9);
                }
            }
            // shorter names win ties ("Discord" over "Discord PTB")
            s -= e.folded.size() * 0.0005;
            if (s > best) {
                best = s;
                hit = &e;
            }
        }
    }
    if (!hit || best < 0.78) return false;
    out.target = hit->target;
    out.display = hit->display;
    out.kind = "app";
    return true;
}

bool AppIndex::resolve(const std::string& spoken, const std::map<std::string, std::string>& user_apps,
                       OpenTarget& out) const {
    std::vector<std::string> cands = name_candidates(fold(spoken));
    if (cands.empty()) return false;
    for (auto& c : cands) {
        for (auto& [name, target] : user_apps) {
            if (fold(name) == c) {
                out = {target, "", "", name, "app"};
                return true;
            }
        }
    }
    auto builtin = [&](const std::vector<Builtin>& list) {
        for (auto& c : cands) {
            for (auto& b : list) {
                for (const char* n : b.names) {
                    if (c == n) {
                        std::string kind = b.kind, disp = b.display;
                        size_t bar = disp.find('|');
                        if (bar != std::string::npos) disp = is_turkish() ? disp.substr(0, bar) : disp.substr(bar + 1);
                        out = {b.target, "", b.fallback, disp, kind};
                        if (starts_with(kind, "folder:")) {
                            out.target = known_folder(kind.substr(7));
                            out.kind = "folder";
                        }
                        return true;
                    }
                }
            }
        }
        return false;
    };
    if (builtin(builtins())) return true;
    if (match_index(cands, out)) return true;
    if (builtin(sites())) return true;
    // something that looks like a domain: "hepsiburada com", "github.com"
    for (auto& c : cands) {
        std::vector<std::string> w = split_words(c);
        if (w.size() == 2 && (w[1] == "com" || w[1] == "net" || w[1] == "org" || w[1] == "io" || w[1] == "tr" ||
                              w[1] == "dev" || w[1] == "ai" || w[1] == "app")) {
            out = {"https://" + w[0] + "." + w[1], "", "", w[0] + "." + w[1], "site"};
            return true;
        }
        if (w.size() == 3 && w[1] == "com" && w[2] == "tr") {
            out = {"https://" + w[0] + ".com.tr", "", "", w[0] + ".com.tr", "site"};
            return true;
        }
    }
    return false;
}

bool AppIndex::open(const OpenTarget& t) const {
    if (shell_open(t.target, t.args)) return true;
    if (!t.fallback.empty()) return shell_open(t.fallback);
    return false;
}

AppIndex& app_index() {
    static AppIndex idx;
    return idx;
}

}  // namespace kayra

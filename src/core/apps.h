// Finds what "aç X" / "open X" means: installed apps (Start menu, including
// Store apps), desktop shortcuts, Windows tools, folders and web sites.
#pragma once
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace kayra {

struct OpenTarget {
    std::string target;    // path, URL or shell: URI
    std::string args;
    std::string fallback;  // tried when target fails to start
    std::string display;   // name to say back
    std::string kind;      // "app", "folder", "site", "tool"
};

class AppIndex {
public:
    void build();          // scans the Start menu and desktops (call from a worker thread)
    bool ready() const;
    size_t size() const;
    bool resolve(const std::string& spoken, const std::map<std::string, std::string>& user_apps,
                 OpenTarget& out) const;
    bool open(const OpenTarget& t) const;

private:
    struct Entry {
        std::string folded, display, target;
    };
    mutable std::mutex mu_;
    std::vector<Entry> entries_;
    bool ready_ = false;
    bool match_index(const std::vector<std::string>& candidates, OpenTarget& out) const;
};

AppIndex& app_index();

// Candidate names for a spoken target: as said, without filler words, and with
// Turkish case endings removed ("discordu" -> "discord").
std::vector<std::string> name_candidates(const std::string& folded);

}  // namespace kayra

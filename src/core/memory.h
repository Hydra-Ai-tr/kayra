// Long-term memory: facts the user asked Kayra to remember and recent
// conversation turns, in %APPDATA%\Kayra\memory.json.
#pragma once
#include <mutex>
#include <string>
#include <vector>

namespace kayra {

struct Turn {
    std::string role;  // "user" or "assistant"
    std::string text;
    long long time = 0;
};

class Memory {
public:
    explicit Memory(std::string path = "");
    void load();
    void save() const;

    void remember(const std::string& fact);
    // Removes facts that mention the words; returns how many were removed.
    int forget(const std::string& about);
    void remove_at(size_t index);
    std::vector<std::string> facts() const;

    void add_turn(const std::string& role, const std::string& text);
    std::vector<Turn> recent(size_t n) const;
    void clear_history();

    void set_persistent(bool on) { persistent_ = on; }  // off: changes stay in memory only

private:
    bool persistent_ = true;
    std::string path_;
    mutable std::mutex mu_;
    std::vector<std::pair<std::string, long long>> facts_;
    std::vector<Turn> history_;
};

}  // namespace kayra

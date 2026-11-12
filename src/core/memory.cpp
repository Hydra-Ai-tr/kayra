#include "core/memory.h"

#include <ctime>
#include <nlohmann/json.hpp>

#include "core/util.h"

using json = nlohmann::json;

namespace kayra {

Memory::Memory(std::string path) : path_(path.empty() ? app_dir() + "\\memory.json" : std::move(path)) {}

void Memory::load() {
    std::string text;
    if (!read_file(path_, text)) return;
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return;
    std::lock_guard<std::mutex> lk(mu_);
    facts_.clear();
    history_.clear();
    for (auto& f : j.value("facts", json::array()))
        if (f.is_object()) facts_.push_back({f.value("text", ""), f.value("time", 0LL)});
    for (auto& t : j.value("history", json::array()))
        if (t.is_object()) history_.push_back({t.value("role", ""), t.value("text", ""), t.value("time", 0LL)});
}

void Memory::save() const {
    if (!persistent_) return;
    json j = {{"facts", json::array()}, {"history", json::array()}};
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [t, tm] : facts_) j["facts"].push_back({{"text", t}, {"time", tm}});
        size_t start = history_.size() > 200 ? history_.size() - 200 : 0;
        for (size_t i = start; i < history_.size(); i++)
            j["history"].push_back({{"role", history_[i].role}, {"text", history_[i].text}, {"time", history_[i].time}});
    }
    write_file(path_, j.dump(1));
}

void Memory::remember(const std::string& fact) {
    std::string f = trim(fact);
    if (f.empty()) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [t, tm] : facts_)
            if (fold(t) == fold(f)) return;
        facts_.push_back({f, (long long)std::time(nullptr)});
    }
    save();
}

int Memory::forget(const std::string& about) {
    std::vector<std::string> words = split_words(fold(about));
    int removed = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<std::pair<std::string, long long>> keep;
        for (auto& f : facts_) {
            std::string ff = fold(f.first);
            bool all = !words.empty();
            for (auto& w : words) all = all && (w.size() < 3 || contains(ff, w));
            if (all || words.empty()) removed++;
            else keep.push_back(f);
        }
        facts_ = keep;
    }
    if (removed) save();
    return removed;
}

void Memory::remove_at(size_t index) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (index >= facts_.size()) return;
        facts_.erase(facts_.begin() + (long)index);
    }
    save();
}

std::vector<std::string> Memory::facts() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    for (auto& f : facts_) out.push_back(f.first);
    return out;
}

void Memory::add_turn(const std::string& role, const std::string& text) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        history_.push_back({role, text, (long long)std::time(nullptr)});
        if (history_.size() > 400) history_.erase(history_.begin(), history_.begin() + 200);
    }
    save();
}

std::vector<Turn> Memory::recent(size_t n) const {
    std::lock_guard<std::mutex> lk(mu_);
    size_t start = history_.size() > n ? history_.size() - n : 0;
    return std::vector<Turn>(history_.begin() + (long)start, history_.end());
}

void Memory::clear_history() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        history_.clear();
    }
    save();
}

}  // namespace kayra

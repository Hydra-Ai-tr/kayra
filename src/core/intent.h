// Built-in understanding of common commands in Turkish and English. Works
// offline; with an LLM brain only the sure matches skip the model.
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "core/config.h"

namespace kayra {

struct Intent {
    std::string tool;          // tool to run
    nlohmann::json args = nlohmann::json::object();
    std::string reply_key;     // or a fixed reply (i18n key)
    std::vector<std::string> reply_args;
    // "stop", "yes", "no", "wake_only", "time", "date", "set_name", "get_name",
    // "set_title", "clipboard_say", "goodbye"
    std::string special;
    double confidence = 1.0;   // >= 0.9: run even when an LLM brain is connected
};

// Removes a leading wake word ("Kayra, ...", "hey Kayra ..."); reports whether there was one.
std::string strip_wake_word(const std::string& text, const std::vector<std::string>& wake_words, bool* had);
// True when the text contains a wake word anywhere (for "... Kayra" at the end).
bool mentions_wake_word(const std::string& text, const std::vector<std::string>& wake_words);

bool parse_intent(const std::string& text, Intent& out);

}  // namespace kayra

#include "core/config.h"

#include <nlohmann/json.hpp>

#include "core/util.h"

using json = nlohmann::json;

namespace kayra {

const std::vector<BrainPreset>& brain_presets() {
    static const std::vector<BrainPreset> p = {
        {"none", "Yerleşik komutlar / Built-in only", "", "", false},
        {"ollama", "Ollama (yerel / local)", "http://127.0.0.1:11434/v1", "qwen2.5:7b", false},
        {"lmstudio", "LM Studio (yerel / local)", "http://127.0.0.1:1234/v1", "local-model", false},
        {"kor", "Kor Studio (kor-cli serve)", "http://127.0.0.1:8765/v1", "kor", false},
        {"openai", "OpenAI", "https://api.openai.com/v1", "gpt-4o-mini", true},
        {"gemini", "Google Gemini", "https://generativelanguage.googleapis.com/v1beta/openai", "gemini-2.0-flash", true},
        {"groq", "Groq", "https://api.groq.com/openai/v1", "llama-3.3-70b-versatile", true},
        {"openrouter", "OpenRouter", "https://openrouter.ai/api/v1", "openai/gpt-4o-mini", true},
        {"custom", "OpenAI uyumlu / OpenAI-compatible", "http://127.0.0.1:8000/v1", "", false},
    };
    return p;
}

const BrainPreset* find_preset(const std::string& id) {
    for (auto& p : brain_presets())
        if (id == p.id) return &p;
    return nullptr;
}

std::string Config::address() const {
    if (!title.empty()) return title;
    return turkish() ? "efendim" : "sir";
}

std::string Config::path() { return app_dir() + "\\config.json"; }

template <class T>
static void get(const json& j, const char* k, T& v) {
    auto it = j.find(k);
    if (it == j.end() || it->is_null()) return;
    try {
        v = it->get<T>();
    } catch (...) {
    }
}

bool Config::load() {
    std::string text;
    if (!read_file(path(), text)) return false;
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        log("config.json is not valid JSON; using defaults");
        return false;
    }
    get(j, "language", language);
    get(j, "user_name", user_name);
    get(j, "title", title);
    get(j, "city", city);
    get(j, "provider", provider);
    get(j, "base_url", base_url);
    get(j, "api_key", api_key);
    get(j, "model", model);
    get(j, "temperature", temperature);
    get(j, "use_tools", use_tools);
    get(j, "speak", speak);
    get(j, "voice_engine", voice_engine);
    get(j, "piper_voice", piper_voice);
    get(j, "edge_voice", edge_voice);
    get(j, "voice", voice);
    get(j, "voice_rate", voice_rate);
    get(j, "voice_volume", voice_volume);
    get(j, "listen_on_start", listen_on_start);
    get(j, "listen_mode", listen_mode);
    get(j, "wake_words", wake_words);
    get(j, "stt_model", stt_model);
    get(j, "mic_device", mic_device);
    get(j, "vad_sensitivity", vad_sensitivity);
    get(j, "follow_up_seconds", follow_up_seconds);
    get(j, "allow_files", allow_files);
    get(j, "allow_web", allow_web);
    get(j, "allow_shell", allow_shell);
    get(j, "apps", apps);
    if (voice_engine != "edge" && voice_engine != "sapi") voice_engine = "piper";
    get(j, "start_with_windows", start_with_windows);
    if (language != "en") language = "tr";
    if (wake_words.empty()) wake_words = {"kayra"};
    return true;
}

bool Config::save() const {
    json j = {
        {"language", language},
        {"user_name", user_name},
        {"title", title},
        {"city", city},
        {"provider", provider},
        {"base_url", base_url},
        {"api_key", api_key},
        {"model", model},
        {"temperature", temperature},
        {"use_tools", use_tools},
        {"speak", speak},
        {"voice_engine", voice_engine},
        {"piper_voice", piper_voice},
        {"edge_voice", edge_voice},
        {"voice", voice},
        {"voice_rate", voice_rate},
        {"voice_volume", voice_volume},
        {"listen_on_start", listen_on_start},
        {"listen_mode", listen_mode},
        {"wake_words", wake_words},
        {"stt_model", stt_model},
        {"mic_device", mic_device},
        {"vad_sensitivity", vad_sensitivity},
        {"follow_up_seconds", follow_up_seconds},
        {"allow_files", allow_files},
        {"allow_web", allow_web},
        {"allow_shell", allow_shell},
        {"apps", apps},
        {"start_with_windows", start_with_windows},
    };
    return write_file(path(), j.dump(2));
}

}  // namespace kayra

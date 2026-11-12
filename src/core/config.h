// User settings, stored as JSON in %APPDATA%\Kayra\config.json.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace kayra {

struct BrainPreset {
    const char* id;
    const char* name;
    const char* base_url;
    const char* model;
    bool needs_key;
};
const std::vector<BrainPreset>& brain_presets();
const BrainPreset* find_preset(const std::string& id);

struct Config {
    std::string language = "tr";      // "tr" or "en": UI, voice and replies
    std::string user_name;            // how Kayra knows you
    std::string title;                // how Kayra addresses you; empty = "efendim" / "sir"
    std::string city;                 // for the weather; empty = guess from IP

    // brain: any OpenAI-compatible chat API, or none (built-in commands only)
    std::string provider = "none";
    std::string base_url;
    std::string api_key;
    std::string model;
    double temperature = 0.6;
    bool use_tools = true;

    // voice
    bool speak = true;
    std::string voice_engine = "piper";  // "piper" (neural, offline), "edge" (neural, online), "sapi" (Windows)
    std::string piper_voice;          // e.g. "tr_TR-fahrettin-medium"; empty = default for the language
    std::string edge_voice;           // e.g. "tr-TR-AhmetNeural"; empty = default for the language
    std::string voice;                // Windows voice: part of the name; empty = first voice for the language
    int voice_rate = 1;               // -10..10
    int voice_volume = 100;           // 0..100

    // listening
    bool listen_on_start = false;
    std::string listen_mode = "wake"; // "wake" (say "Kayra"), "always", "push" (hotkey/button only)
    std::vector<std::string> wake_words = {"kayra"};
    std::string stt_model = "small-q5_1";
    int mic_device = -1;              // -1 = default
    double vad_sensitivity = 0.5;     // 0..1
    double follow_up_seconds = 6.0;   // keep listening without the wake word after a reply

    // access
    bool allow_files = true;          // list, find, read and write files (deleting asks first)
    bool allow_web = true;            // read search results, news and web pages
    bool allow_shell = false;         // lets the brain run console commands

    // extra app names: "spor" -> "C:\\...\\app.exe" or a URL
    std::map<std::string, std::string> apps;

    bool start_with_windows = false;

    bool turkish() const { return language != "en"; }
    std::string address() const;      // title or default ("efendim" / "sir")

    static std::string path();
    bool load();
    bool save() const;
};

}  // namespace kayra

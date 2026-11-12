// The main screen: sidebar, top tabs, the particle core with the K emblem,
// system / performance / weather / timer cards and the conversation. Drawing
// is a pure function of HudState (plus the page the user picked), so the live
// app and the demo recorder render exactly the same screen.
#pragma once
#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

#include "core/assistant.h"
#include "core/config.h"
#include "core/system.h"
#include "core/tools.h"

namespace kayra {

enum class Look { Standby, Waiting, Listening, Hearing, Transcribing, Thinking, Speaking, Offline, Error };
enum class Page { Home, System, Chat, Tools, Modules, Timers };

struct HudState {
    double t = 0;                  // animation clock (seconds)
    Look look = Look::Standby;
    float level = 0;               // voice or microphone level 0..1
    std::vector<float> wave;       // recent microphone levels
    std::string status;            // e.g. "DİNLİYORUM"
    std::string hint;              // small line under the core
    std::string heard;             // last thing heard (quoted)
    std::string clock, date;

    SysStats stats;
    std::vector<float> cpu_hist, ram_hist;  // 0..1, one sample per second
    std::string cpu_name, pc_name;
    Weather weather;
    bool weather_loading = false;
    std::vector<TimerInfo> timers;
    double now = 0;                // now_seconds() for the timers

    std::vector<ChatEntry> chat;
    size_t chat_version = 0;
    std::string typing;            // demo: text being typed into the input

    bool mic_on = false, voice_on = true;
    std::string mode_label, brain_label, voice_label;
    std::string user_name;
    bool brain_on = false, online = true;
    bool tools_on = true, files_on = true, web_on = true, shell_on = false;
    float download = -1;           // 0..1 while the speech model downloads
    bool show_input = true;
    std::string version = KAYRA_VERSION;
};

struct HudActions {
    std::string submit;
    bool toggle_mic = false, toggle_voice = false, open_settings = false, talk = false, clear = false, attach = false;
    int settings_tab = 0;          // with open_settings: 0 general, 1 brain, 2 voice, 3 listening, 4 memory
    std::string toggle_module;     // "tools", "files", "web", "shell"
};

class Hud {
public:
    // scale: font/layout scale baked into the fonts
    void init(float scale);
    HudActions draw(const HudState& s);
    float scale() const { return scale_; }
    // Puts text into the input box (cursor at the end) and shows the conversation.
    void set_input(const std::string& text);
    void set_page(Page p) { page_ = p; }

    ImFont* f_body = nullptr;
    ImFont* f_small = nullptr;
    ImFont* f_semi = nullptr;      // Segoe UI Semibold: names, card titles
    ImFont* f_head = nullptr;      // Bahnschrift: labels
    ImFont* f_big = nullptr;       // Bahnschrift: numbers, clock, headline
    ImFont* f_title = nullptr;     // Bahnschrift: KAYRA
    ImFont* f_mono = nullptr;
    ImFont* f_icon = nullptr;      // Segoe Fluent Icons / MDL2 Assets

private:
    void background(ImDrawList* dl, const HudState& s, ImVec2 size, ImVec2 core);
    void sidebar(ImDrawList* dl, const HudState& s, ImVec2 size, HudActions& act);
    void top_bar(ImDrawList* dl, const HudState& s, ImVec2 size, HudActions& act);
    void greeting(ImDrawList* dl, const HudState& s, ImVec4 r);
    void chips(ImDrawList* dl, const HudState& s, float right, float y);
    void core(ImDrawList* dl, const HudState& s, ImVec2 c, float r);
    void core_title(ImDrawList* dl, const HudState& s, float cx, float y, float maxw);
    void dock(ImDrawList* dl, const HudState& s, float cx, float y, HudActions& act);
    void system_card(ImDrawList* dl, const HudState& s, ImVec4 r);
    void perf_card(ImDrawList* dl, const HudState& s, ImVec4 r);
    void weather_card(ImDrawList* dl, const HudState& s, ImVec4 r);
    void timers_card(ImDrawList* dl, const HudState& s, ImVec4 r, bool big, HudActions& act);
    void chat_panel(const HudState& s, ImVec4 r, HudActions& act);
    void page_system(ImDrawList* dl, const HudState& s, ImVec4 r);
    void page_tools(ImDrawList* dl, const HudState& s, ImVec4 r, HudActions& act);
    void page_modules(ImDrawList* dl, const HudState& s, ImVec4 r, HudActions& act);
    void page_timers(ImDrawList* dl, const HudState& s, ImVec4 r, HudActions& act);

    // drawing helpers that need the fonts
    void card(ImDrawList* dl, ImVec4 r, bool lit = false);
    void card_title(ImDrawList* dl, ImVec4 r, unsigned icon, const std::string& title);
    void icon(ImDrawList* dl, ImVec2 center, float size, unsigned cp, ImU32 color);
    bool hit(const char* id, ImVec2 a, ImVec2 b);  // invisible button; true when clicked

    float scale_ = 1;
    float k_ = 1;                  // current layout unit
    unsigned int glow_tex_ = 0, logo_tex_ = 0, k_tex_ = 0;
    ImVec4 color_{0.0f, 0.78f, 1.0f, 1.0f};
    size_t seen_version_ = (size_t)-1;
    char input_[1024] = {};
    bool refocus_ = false, cursor_end_ = false;
    Page page_ = Page::Home;
    bool perf_ram_ = false;
    double last_t_ = 0;
    float yaw_ = 0, ring_ = 0;
};

ImVec4 look_color(Look l);
void apply_style(float scale);

}  // namespace kayra

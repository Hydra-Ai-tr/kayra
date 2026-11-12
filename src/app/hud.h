// The holographic HUD: arc-reactor core, telemetry, weather, timers and the
// conversation. Drawing is a pure function of HudState, so the live app and
// the demo recorder render exactly the same screen.
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

struct HudState {
    double t = 0;                  // animation clock (seconds)
    Look look = Look::Standby;
    float level = 0;               // voice or microphone level 0..1
    std::vector<float> wave;       // recent microphone levels
    std::string status;            // big line under the core
    std::string hint;              // small line under it
    std::string heard;             // last thing heard (quoted)
    std::string clock, date;

    SysStats stats;
    std::vector<float> cpu_hist, ram_hist;
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
    float download = -1;           // 0..1 while the speech model downloads
    bool show_input = true;
    std::string version = KAYRA_VERSION;
};

struct HudActions {
    std::string submit;
    bool toggle_mic = false, toggle_voice = false, open_settings = false, talk = false, clear = false;
};

class Hud {
public:
    // scale: font/layout scale baked into the fonts
    void init(float scale);
    HudActions draw(const HudState& s);
    float scale() const { return scale_; }

    ImFont* f_body = nullptr;
    ImFont* f_small = nullptr;
    ImFont* f_head = nullptr;      // Bahnschrift: labels
    ImFont* f_big = nullptr;       // Bahnschrift: numbers, status
    ImFont* f_title = nullptr;     // Bahnschrift bold: KAYRA
    ImFont* f_mono = nullptr;

private:
    void background(ImDrawList* dl, const HudState& s, ImVec2 size);
    void top_bar(ImDrawList* dl, const HudState& s, ImVec2 size);
    void reactor(ImDrawList* dl, const HudState& s, ImVec2 c, float r);
    void system_panel(ImDrawList* dl, const HudState& s, ImVec4 rect);
    void weather_panel(ImDrawList* dl, const HudState& s, ImVec4 rect);
    void timers_panel(ImDrawList* dl, const HudState& s, ImVec4 rect);
    void chat_panel(const HudState& s, ImVec4 rect, HudActions& act);
    void bottom_bar(ImDrawList* dl, const HudState& s, ImVec2 size, HudActions& act);

    float scale_ = 1;
    float k_ = 1;                  // current layout unit
    unsigned int glow_tex_ = 0;
    ImVec4 color_{0.0f, 0.78f, 1.0f, 1.0f};
    size_t seen_version_ = (size_t)-1;
    char input_[1024] = {};
    bool refocus_ = false;
};

ImVec4 look_color(Look l);
void apply_style(float scale);

}  // namespace kayra

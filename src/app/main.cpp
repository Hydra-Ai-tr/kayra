// KAYRA desktop app: the HUD window, the global hotkey and the settings.
#include <windows.h>
#include <objbase.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/hud.h"
#include "app/record.h"
#include "core/apps.h"
#include "core/assistant.h"
#include "core/config.h"
#include "core/i18n.h"
#include "core/llm.h"
#include "core/system.h"
#include "core/tools.h"
#include "core/util.h"
#include "voice/listener.h"
#include "voice/mic.h"
#include "voice/neural.h"
#include "voice/stt.h"
#include "voice/tts.h"

namespace kayra {
namespace {

constexpr int kHotkeyId = 1;

std::string short_voice(const std::string& name) {
    std::string s = name;
    size_t d = s.find(" - ");
    if (d != std::string::npos) s = s.substr(0, d);
    if (starts_with(s, "Microsoft ")) s = s.substr(10);
    size_t sp = s.find(" Desktop");
    if (sp != std::string::npos) s = s.substr(0, sp);
    return s;
}

std::string upper_ascii(std::string s) {
    for (auto& c : s)
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    return s;
}

std::string today_line() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    if (is_turkish())
        return std::to_string(tm.tm_mday) + " " + month_name(tm.tm_mon) + " " + std::to_string(tm.tm_year + 1900) + " · " +
               weekday_name(tm.tm_wday);
    return weekday_name(tm.tm_wday) + " · " + month_name(tm.tm_mon) + " " + std::to_string(tm.tm_mday) + ", " +
           std::to_string(tm.tm_year + 1900);
}

float layout_unit(ImVec2 size) { return std::clamp(std::min(size.y / 900.0f, size.x / 1600.0f), 0.6f, 2.0f); }

class App {
public:
    int run(bool minimized);

private:
    bool create_window(bool minimized);
    void start_threads();
    void stop_threads();
    void update();
    HudState gather();
    void handle(const HudActions& a);
    void talk();
    void toggle_mic();
    void open_settings(int tab = 0);
    void settings_window();
    void tab_general();
    void tab_brain();
    void tab_voice();
    void tab_listen();
    void tab_memory();
    void save_settings();
    void configure_voice(const Config& c);

    GLFWwindow* win_ = nullptr;
    Config cfg_;
    Speaker tts_;
    std::unique_ptr<Assistant> as_;
    std::unique_ptr<Listener> ls_;
    Hud hud_;

    // background threads
    std::thread hotkey_th_, telemetry_th_;
    std::atomic<DWORD> hotkey_tid_{0};
    std::atomic<bool> hotkey_{false}, quit_{false};
    std::mutex tele_mu_;
    SysStats stats_;
    std::vector<float> cpu_hist_, ram_hist_;
    Weather weather_;
    bool weather_loading_ = true;
    std::string weather_city_;
    bool weather_tr_ = true;
    std::atomic<bool> weather_refresh_{false};
    std::string cpu_name_, pc_name_;

    // conversation flow
    std::atomic<bool> voice_turn_{false}, reply_done_{false}, notice_{false};
    bool awaiting_follow_ = false;
    bool pending_arm_ = false;
    std::mutex cfg_mu_;
    std::unique_ptr<Config> changed_cfg_;
    std::string heard_;
    double heard_at_ = -100;
    std::mutex heard_mu_;

    std::vector<ChatEntry> chat_;
    size_t chat_version_ = (size_t)-1;

    // settings window
    bool settings_open_ = false;
    int settings_tab_ = -1;  // selected on the next frame
    Config edit_;
    std::string wake_buf_;
    std::vector<VoiceInfo> voices_;
    std::vector<std::string> mics_;
    std::atomic<bool> testing_{false};
    std::atomic<bool> installing_{false};
    std::mutex test_mu_;
    std::string test_result_;
    double saved_at_ = -100;
};

// ------------------------------------------------------------------ window

bool App::create_window(bool minimized) {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    int w = 1600, h = 900, x = 100, y = 100;
    if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
        int mx, my, mw, mh;
        glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
        h = (int)(mh * 0.86);
        w = std::min((int)(h * 16.0 / 9.0), (int)(mw * 0.94));
        x = mx + (mw - w) / 2;
        y = my + (mh - h) / 2;
    }
    win_ = glfwCreateWindow(w, h, "KAYRA", nullptr, nullptr);
    if (!win_) return false;
    glfwSetWindowPos(win_, x, y);
    glfwSetWindowSizeLimits(win_, 960, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
    HWND hwnd = glfwGetWin32Window(win_);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
    COLORREF caption = RGB(4, 12, 20), border = RGB(10, 60, 80);
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof caption);
    DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof border);
    glfwMakeContextCurrent(win_);
    glfwSwapInterval(1);
    if (minimized) glfwIconifyWindow(win_);
    glfwShowWindow(win_);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplGlfw_InitForOpenGL(win_, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    int fw, fh;
    glfwGetFramebufferSize(win_, &fw, &fh);
    hud_.init(layout_unit(ImVec2((float)fw, (float)fh)));
    return true;
}

// ------------------------------------------------------------------ threads

void App::start_threads() {
    hotkey_th_ = std::thread([this] {
        hotkey_tid_ = GetCurrentThreadId();
        if (!RegisterHotKey(nullptr, kHotkeyId, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'K'))
            log("hotkey Ctrl+Shift+K is taken by another program");
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0)
            if (m.message == WM_HOTKEY && m.wParam == kHotkeyId) {
                hotkey_ = true;
                glfwPostEmptyEvent();
            }
        UnregisterHotKey(nullptr, kHotkeyId);
    });

    telemetry_th_ = std::thread([this] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        app_index().build();
        {
            std::string cn = cpu_name(), pn = computer_name();
            std::lock_guard<std::mutex> lk(tele_mu_);
            cpu_name_ = cn;
            pc_name_ = pn;
        }
        double next_weather = 0, next_stats = 0;
        system_stats();
        while (!quit_) {
            double now = now_seconds();
            if (now >= next_stats) {
                SysStats s = system_stats();
                std::lock_guard<std::mutex> lk(tele_mu_);
                stats_ = s;
                cpu_hist_.push_back((float)s.cpu / 100.0f);
                ram_hist_.push_back((float)s.ram / 100.0f);
                if (cpu_hist_.size() > 60) cpu_hist_.erase(cpu_hist_.begin());
                if (ram_hist_.size() > 60) ram_hist_.erase(ram_hist_.begin());
                next_stats = now + 1.0;
            }
            if (weather_refresh_.exchange(false)) next_weather = 0;
            if (now >= next_weather) {
                std::string city;
                bool tr;
                {
                    std::lock_guard<std::mutex> lk(tele_mu_);
                    city = weather_city_;
                    tr = weather_tr_;
                    weather_loading_ = true;
                }
                Weather w = fetch_weather(city, tr);
                std::lock_guard<std::mutex> lk(tele_mu_);
                weather_ = w;
                weather_loading_ = false;
                next_weather = now_seconds() + (w.ok ? 20 * 60 : 120);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        CoUninitialize();
    });
}

void App::stop_threads() {
    quit_ = true;
    if (hotkey_tid_) PostThreadMessageW(hotkey_tid_, WM_QUIT, 0, 0);
    if (hotkey_th_.joinable()) hotkey_th_.join();
    if (telemetry_th_.joinable()) telemetry_th_.join();
}

// ------------------------------------------------------------------ flow

void App::configure_voice(const Config& c) { tts_.configure(voice_settings(c)); }

void App::talk() {
    if (tts_.speaking()) tts_.stop();
    if (ls_->active()) {
        ls_->arm();
    } else {
        ls_->stop();
        ls_->start();
        pending_arm_ = true;
    }
}

void App::toggle_mic() {
    if (ls_->active()) {
        ls_->stop();
        pending_arm_ = false;
    } else {
        ls_->stop();
        ls_->start();
    }
}

void App::update() {
    if (hotkey_.exchange(false)) talk();
    if (pending_arm_ && ls_->phase() == Listener::Phase::Waiting) {
        ls_->arm();
        pending_arm_ = false;
    }
    if (pending_arm_ && ls_->phase() == Listener::Phase::Error) pending_arm_ = false;
    ls_->set_suppressed(tts_.speaking());

    if (reply_done_.exchange(false) && voice_turn_.exchange(false)) awaiting_follow_ = true;
    if (awaiting_follow_ && !tts_.speaking() && !as_->thinking()) {
        awaiting_follow_ = false;
        if (cfg_.listen_mode != "push") ls_->follow_up(cfg_.follow_up_seconds);
    }
    {
        std::lock_guard<std::mutex> lk(cfg_mu_);
        if (changed_cfg_) {
            cfg_ = *changed_cfg_;
            changed_cfg_.reset();
            ls_->set_config(cfg_);
        }
    }
    if (notice_.exchange(false) && !glfwGetWindowAttrib(win_, GLFW_FOCUSED)) {
        FLASHWINFO fi{sizeof(FLASHWINFO), glfwGetWin32Window(win_), FLASHW_ALL | FLASHW_TIMERNOFG, 0, 0};
        FlashWindowEx(&fi);
    }
}

HudState App::gather() {
    HudState s;
    double now = now_seconds();
    s.t = glfwGetTime();
    s.now = now;
    s.clock = clock_text(true);
    s.date = today_line();
    {
        std::lock_guard<std::mutex> lk(tele_mu_);
        s.stats = stats_;
        s.cpu_hist = cpu_hist_;
        s.ram_hist = ram_hist_;
        s.cpu_name = cpu_name_;
        s.pc_name = pc_name_;
        s.weather = weather_;
        s.weather_loading = weather_loading_;
    }
    s.timers = as_->timers();
    size_t v = as_->log_version();
    if (v != chat_version_) {
        chat_ = as_->log_entries();
        chat_version_ = v;
    }
    s.chat = chat_;
    s.chat_version = v;

    using P = Listener::Phase;
    P ph = ls_->phase();
    bool mic = ls_->active();
    const std::string& mode = cfg_.listen_mode;
    if (as_->thinking()) {
        s.look = Look::Thinking;
        s.status = T("ui_thinking");
    } else if (tts_.speaking()) {
        s.look = Look::Speaking;
        s.status = T("ui_speaking");
    } else {
        switch (ph) {
            case P::Hearing:
                s.look = Look::Hearing;
                s.status = T("ui_hearing");
                break;
            case P::Transcribing:
                s.look = Look::Transcribing;
                s.status = T("ui_transcribing");
                break;
            case P::Waiting:
                if (ls_->expecting_command() || mode == "always") {
                    s.look = Look::Listening;
                    s.status = T("ui_listening");
                    if (mode == "always") s.hint = T("ui_mode_always");
                } else {
                    s.look = Look::Waiting;
                    s.status = T("ui_idle");
                    s.hint = mode == "push" ? T("ui_push_hint") : T("ui_waiting_wake");
                }
                break;
            case P::Downloading:
            case P::Loading:
                s.look = Look::Standby;
                s.status = T("ui_preparing");
                s.hint = T("ui_loading_model");
                break;
            case P::Error: {
                s.look = Look::Error;
                s.status = T("ui_error");
                std::string e = ls_->error();
                s.hint = !Stt::available() ? T("ui_stt_missing") : contains(e, "microphone") ? T("ui_mic_error") + " · " + e : e;
                break;
            }
            default:
                s.look = Look::Standby;
                s.status = T("ui_idle");
                s.hint = T("ui_mic_off_hint");
        }
    }
    s.level = tts_.speaking() ? tts_.level() : ls_->level();
    if (mic) s.wave = ls_->levels();
    if (ph == P::Downloading) s.download = ls_->download_progress();
    {
        std::lock_guard<std::mutex> lk(heard_mu_);
        if (now - heard_at_ < 8.0) s.heard = heard_;
    }
    s.mic_on = mic;
    s.voice_on = cfg_.speak;
    s.mode_label = !mic ? T("ui_mode_off") : mode == "always" ? T("ui_mode_always") : mode == "push" ? T("ui_mode_push") : T("ui_mode_wake");
    if (as_->brain_connected()) {
        const BrainPreset* p = find_preset(cfg_.provider);
        s.brain_label = cfg_.model.empty() ? (p ? p->name : cfg_.provider) : cfg_.model;
    } else {
        s.brain_label = T("ui_builtin");
    }
    s.voice_label = upper_ascii(short_voice(tts_.voice_name()));
    s.user_name = cfg_.user_name;
    s.brain_on = as_->brain_connected();
    s.online = s.weather.ok || s.weather_loading;
    s.tools_on = cfg_.use_tools;
    s.files_on = cfg_.allow_files;
    s.web_on = cfg_.allow_web;
    s.shell_on = cfg_.allow_shell;
    return s;
}

std::string pick_file(GLFWwindow* win) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW of{};
    of.lStructSize = sizeof of;
    of.hwndOwner = glfwGetWin32Window(win);
    of.lpstrFile = path;
    of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&of) ? narrow(path) : "";
}

void App::handle(const HudActions& a) {
    if (!a.submit.empty()) {
        voice_turn_ = false;
        as_->submit(a.submit);
    }
    if (a.toggle_mic) toggle_mic();
    if (a.toggle_voice) {
        cfg_.speak = !cfg_.speak;
        cfg_.save();
        as_->set_config(cfg_);
        if (!cfg_.speak) tts_.stop();
    }
    if (a.talk) talk();
    if (a.clear) as_->clear_log();
    if (a.open_settings) open_settings(a.settings_tab);
    if (!a.toggle_module.empty()) {
        const std::string& m = a.toggle_module;
        bool& flag = m == "files" ? cfg_.allow_files : m == "web" ? cfg_.allow_web : m == "shell" ? cfg_.allow_shell : cfg_.use_tools;
        flag = !flag;
        cfg_.save();
        as_->set_config(cfg_);
    }
    if (a.attach) {
        std::string f = pick_file(win_);
        if (!f.empty()) hud_.set_input(F("attach_cmd", {f}));
    }
}

// ------------------------------------------------------------------ settings

void App::open_settings(int tab) {
    settings_tab_ = tab;
    if (settings_open_) return;
    settings_open_ = true;
    edit_ = cfg_;
    wake_buf_.clear();
    for (auto& w : cfg_.wake_words) wake_buf_ += (wake_buf_.empty() ? "" : ", ") + w;
    voices_ = Speaker::voices();
    mics_ = Mic::devices();
    std::lock_guard<std::mutex> lk(test_mu_);
    test_result_.clear();
}

void App::tab_general() {
    const char* langs[] = {"Türkçe", "English"};
    int li = edit_.language == "en" ? 1 : 0;
    if (ImGui::Combo(U("set_language"), &li, langs, 2)) edit_.language = li == 1 ? "en" : "tr";
    ImGui::InputText(U("set_name"), &edit_.user_name);
    ImGui::InputText(U("set_title_addr"), &edit_.title);
    ImGui::InputText(U("set_city"), &edit_.city);
    ImGui::Spacing();
    ImGui::Checkbox(U("set_listen_start"), &edit_.listen_on_start);
    ImGui::Checkbox(U("set_autostart"), &edit_.start_with_windows);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("%s", U("set_hotkey_note"));
    ImGui::TextDisabled("KAYRA %s · github.com/Hydra-Ai-tr/kayra", KAYRA_VERSION);
}

void App::tab_brain() {
    const auto& presets = brain_presets();
    const BrainPreset* cur = find_preset(edit_.provider);
    if (ImGui::BeginCombo(U("set_provider"), cur ? cur->name : edit_.provider.c_str())) {
        for (auto& p : presets) {
            bool sel = edit_.provider == p.id;
            if (ImGui::Selectable(p.name, sel)) {
                edit_.provider = p.id;
                if (std::string(p.id) != "custom") {
                    edit_.base_url = p.base_url;
                    edit_.model = p.model;
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("%s", U("set_preset_hint"));
    if (edit_.provider != "none") {
        ImGui::InputText(U("set_base_url"), &edit_.base_url);
        ImGui::InputTextWithHint(U("set_api_key"), U("set_key_hint"), &edit_.api_key, ImGuiInputTextFlags_Password);
        ImGui::InputText(U("set_model"), &edit_.model);
        float temp = (float)edit_.temperature;
        if (ImGui::SliderFloat(U("set_temperature"), &temp, 0.0f, 1.5f, "%.2f")) edit_.temperature = temp;
        ImGui::Checkbox(U("set_tools"), &edit_.use_tools);
        ImGui::Checkbox(U("set_files"), &edit_.allow_files);
        ImGui::Checkbox(U("set_web"), &edit_.allow_web);
        ImGui::Checkbox(U("set_shell"), &edit_.allow_shell);
        ImGui::Spacing();
        ImGui::BeginDisabled(testing_);
        if (ImGui::Button(U("set_test"))) {
            testing_ = true;
            Config c = edit_;
            std::thread([this, c] {
                CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                LlmClient client(c);
                std::string info;
                bool ok = client.test(info);
                {
                    std::lock_guard<std::mutex> lk(test_mu_);
                    test_result_ = (ok ? "✔ " : "✖ ") + info;
                }
                testing_ = false;
                CoUninitialize();
            }).detach();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (testing_) {
            ImGui::TextDisabled("%s", U("set_testing"));
        } else {
            std::lock_guard<std::mutex> lk(test_mu_);
            if (!test_result_.empty()) {
                bool ok = starts_with(test_result_, "✔");
                ImGui::PushStyleColor(ImGuiCol_Text, ok ? ImVec4(0.4f, 1.0f, 0.7f, 1) : ImVec4(1.0f, 0.45f, 0.4f, 1));
                ImGui::TextWrapped("%s", test_result_.c_str());
                ImGui::PopStyleColor();
            }
        }
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("%s", U("set_brain_note"));
}

void App::tab_voice() {
    ImGui::Checkbox(U("set_speak"), &edit_.speak);
    ImGui::Spacing();
    const char* engines[] = {"piper", "edge", "sapi"};
    std::string engine_names[] = {T("set_engine_piper"), T("set_engine_edge"), T("set_engine_sapi")};
    int ei = edit_.voice_engine == "edge" ? 1 : edit_.voice_engine == "sapi" ? 2 : 0;
    if (ImGui::BeginCombo(U("set_engine"), engine_names[ei].c_str())) {
        for (int i = 0; i < 3; i++)
            if (ImGui::Selectable(engine_names[i].c_str(), ei == i)) {
                edit_.voice_engine = engines[i];
                configure_voice(edit_);
            }
        ImGui::EndCombo();
    }
    std::string lang = edit_.turkish() ? "tr" : "en";
    if (edit_.voice_engine != "sapi") {
        bool edge = edit_.voice_engine == "edge";
        std::string& chosen = edge ? edit_.edge_voice : edit_.piper_voice;
        std::string cur = pick_neural_voice(edit_.voice_engine, chosen, lang);
        std::string shown_n = neural_label(edit_.voice_engine, cur);
        if (ImGui::BeginCombo(U("set_voice_name"), shown_n.c_str())) {
            for (auto& v : edge ? edge_voices() : piper_voices()) {
                if (lang != v.lang) continue;
                std::string label = std::string(v.label) + "##" + v.id;
                if (ImGui::Selectable(label.c_str(), cur == v.id)) {
                    chosen = v.id;
                    configure_voice(edit_);
                }
            }
            ImGui::EndCombo();
        }
        float p = tts_.engine_progress();
        if (p >= 0.0f) {
            ImGui::ProgressBar(p, ImVec2(-1, 0), F("set_engine_downloading", {std::to_string((int)(p * 100))}).c_str());
        } else if (tts_.engine_ready()) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.7f, 1), "%s", U("set_engine_ready"));
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.35f, 1), "%s", U("set_engine_missing"));
            if (edge) {
                ImGui::SameLine();
                ImGui::BeginDisabled(installing_);
                if (ImGui::Button(U("set_engine_install"))) {
                    installing_ = true;
                    std::thread([this] {
                        std::string out;
                        bool ok = edge_install(&out);
                        log(std::string("edge-tts install ") + (ok ? "ok" : "failed: ") + (ok ? "" : out));
                        tts_.refresh_engine();
                        installing_ = false;
                    }).detach();
                }
                ImGui::EndDisabled();
            }
        }
        ImGui::TextWrapped("%s", U(edge ? "set_engine_note_edge" : "set_engine_note_piper"));
        ImGui::Spacing();
    }
    std::string shown = edit_.voice.empty() ? T("set_auto_voice") : edit_.voice;
    if (ImGui::BeginCombo(U(edit_.voice_engine == "sapi" ? "set_voice_name" : "set_windows_voice"), shown.c_str())) {
        if (ImGui::Selectable(U("set_auto_voice"), edit_.voice.empty())) edit_.voice.clear();
        for (auto& v : voices_) {
            std::string label = v.name + "  [" + v.lang + "]";
            if (ImGui::Selectable(label.c_str(), edit_.voice == v.name)) edit_.voice = v.name;
        }
        ImGui::EndCombo();
    }
    ImGui::SliderInt(U("set_rate"), &edit_.voice_rate, -10, 10);
    ImGui::SliderInt(U("set_volume"), &edit_.voice_volume, 0, 100);
    if (ImGui::Button(U("set_try_voice"))) {
        configure_voice(edit_);
        tts_.stop();
        std::string sample = T("set_voice_sample");
        if (edit_.turkish() != cfg_.turkish()) {
            // the sample in the language being picked
            set_language(edit_.turkish());
            sample = T("set_voice_sample");
            set_language(cfg_.turkish());
        }
        tts_.speak(sample);
    }
}

void App::tab_listen() {
    ImGui::TextUnformatted(U("set_listen_mode"));
    int m = edit_.listen_mode == "always" ? 1 : edit_.listen_mode == "push" ? 2 : 0;
    ImGui::RadioButton(U("set_mode_wake"), &m, 0);
    ImGui::RadioButton(U("set_mode_always"), &m, 1);
    ImGui::RadioButton(U("set_mode_push"), &m, 2);
    edit_.listen_mode = m == 1 ? "always" : m == 2 ? "push" : "wake";
    ImGui::Spacing();
    ImGui::InputText(U("set_wake_words"), &wake_buf_);
    std::string cur;
    for (auto& md : Stt::models())
        if (edit_.stt_model == md.name) cur = md.label;
    if (cur.empty()) cur = edit_.stt_model;
    if (ImGui::BeginCombo(U("set_stt_model"), cur.c_str())) {
        for (auto& md : Stt::models()) {
            bool have = file_exists(Stt::model_path(md.name));
            std::string label = std::string(md.label) + "  ·  " + std::to_string(md.size_mb) + " MB  ·  " +
                                (have ? T("set_model_present") : T("set_model_missing"));
            if (ImGui::Selectable(label.c_str(), edit_.stt_model == md.name)) edit_.stt_model = md.name;
        }
        ImGui::EndCombo();
    }
    std::string mic = edit_.mic_device >= 0 && edit_.mic_device < (int)mics_.size() ? mics_[edit_.mic_device] : T("set_default_device");
    if (ImGui::BeginCombo(U("set_mic_device"), mic.c_str())) {
        if (ImGui::Selectable(U("set_default_device"), edit_.mic_device < 0)) edit_.mic_device = -1;
        for (int i = 0; i < (int)mics_.size(); i++)
            if (ImGui::Selectable((mics_[i] + "##" + std::to_string(i)).c_str(), edit_.mic_device == i)) edit_.mic_device = i;
        ImGui::EndCombo();
    }
    float sens = (float)edit_.vad_sensitivity;
    if (ImGui::SliderFloat(U("set_sensitivity"), &sens, 0.0f, 1.0f, "%.2f")) edit_.vad_sensitivity = sens;
    float fu = (float)edit_.follow_up_seconds;
    if (ImGui::SliderFloat(U("set_follow_up"), &fu, 0.0f, 15.0f, "%.0f")) edit_.follow_up_seconds = fu;
}

void App::tab_memory() {
    ImGui::TextUnformatted(U("set_facts"));
    ImGui::Spacing();
    auto facts = as_->memory().facts();
    if (facts.empty()) ImGui::TextDisabled("%s", U("set_no_facts"));
    for (size_t i = 0; i < facts.size(); i++) {
        ImGui::PushID((int)i);
        if (ImGui::SmallButton(U("set_delete"))) as_->memory().remove_at(i);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", facts[i].c_str());
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button(U("set_clear_history"))) {
        as_->memory().clear_history();
        as_->clear_log();
        saved_at_ = now_seconds();
    }
}

void App::save_settings() {
    Config c = edit_;
    c.wake_words.clear();
    size_t start = 0;
    while (start <= wake_buf_.size()) {
        size_t comma = wake_buf_.find(',', start);
        if (comma == std::string::npos) comma = wake_buf_.size();
        std::string x = trim(lower(wake_buf_.substr(start, comma - start)));
        if (!x.empty()) c.wake_words.push_back(x);
        start = comma + 1;
    }
    if (c.wake_words.empty()) c.wake_words = {"kayra"};
    bool restart = c.stt_model != cfg_.stt_model || c.mic_device != cfg_.mic_device;
    bool weather = c.city != cfg_.city || c.language != cfg_.language;
    cfg_ = c;
    cfg_.save();
    as_->set_config(cfg_);
    configure_voice(cfg_);
    ls_->set_config(cfg_);
    if (restart && ls_->active()) {
        ls_->stop();
        ls_->start();
    }
    set_start_with_windows(cfg_.start_with_windows);
    if (weather) {
        std::lock_guard<std::mutex> lk(tele_mu_);
        weather_city_ = cfg_.city;
        weather_tr_ = cfg_.turkish();
        weather_refresh_ = true;
    }
    saved_at_ = now_seconds();
}

void App::settings_window() {
    if (!settings_open_) return;
    ImGuiIO& io = ImGui::GetIO();
    float k = layout_unit(io.DisplaySize);
    ImGui::SetNextWindowSize(ImVec2(780 * k, 600 * k), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushFont(hud_.f_body);
    bool open = true;
    std::string title = T("set_title") + "###settings";
    if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushItemWidth(-260 * k);
        ImGui::BeginChild("##body", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.5f));
        if (ImGui::BeginTabBar("##tabs")) {
            struct Tab {
                const char* key;
                void (App::*fn)();
            };
            static const Tab tabs[] = {{"set_general", &App::tab_general}, {"set_brain", &App::tab_brain},
                                       {"set_voice", &App::tab_voice},     {"set_listen", &App::tab_listen},
                                       {"set_memory", &App::tab_memory}};
            for (int i = 0; i < 5; i++) {
                const Tab& t = tabs[i];
                std::string label = T(t.key) + "###" + t.key;
                if (ImGui::BeginTabItem(label.c_str(), nullptr, settings_tab_ == i ? ImGuiTabItemFlags_SetSelected : 0)) {
                    ImGui::Spacing();
                    (this->*t.fn)();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
            settings_tab_ = -1;
        }
        ImGui::EndChild();
        ImGui::PopItemWidth();
        ImGui::Separator();
        if (ImGui::Button(U("set_save"), ImVec2(140 * k, 0))) save_settings();
        ImGui::SameLine();
        if (ImGui::Button(U("set_close"), ImVec2(140 * k, 0))) open = false;
        if (now_seconds() - saved_at_ < 2.5) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.7f, 1), "✔ %s", U("set_saved"));
        }
    }
    ImGui::End();
    ImGui::PopFont();
    if (!open) {
        settings_open_ = false;
        configure_voice(cfg_);  // undo an unsaved "try voice"
    }
}

// ------------------------------------------------------------------ main loop

int App::run(bool minimized) {
    cfg_.load();
    set_language(cfg_.turkish());
    set_address(cfg_.address());
    if (!create_window(minimized)) {
        MessageBoxW(nullptr, L"OpenGL 3 penceresi açılamadı / Could not create an OpenGL 3 window.", L"KAYRA", MB_ICONERROR);
        return 1;
    }
    tts_.start();
    configure_voice(cfg_);
    as_ = std::make_unique<Assistant>(cfg_, &tts_);
    ls_ = std::make_unique<Listener>(cfg_);
    weather_city_ = cfg_.city;
    weather_tr_ = cfg_.turkish();

    as_->on_reply_done = [this] { reply_done_ = true; };
    as_->on_notice = [this](const std::string&) {
        notice_ = true;
        glfwPostEmptyEvent();
    };
    as_->on_config_changed = [this](const Config& c) {
        std::lock_guard<std::mutex> lk(cfg_mu_);
        changed_cfg_ = std::make_unique<Config>(c);
    };
    ls_->on_command = [this](const std::string& cmd) {
        voice_turn_ = true;
        as_->submit(cmd);
    };
    ls_->on_wake = [this] {
        tts_.stop();
        as_->say(T("wake_ack"), false);
    };
    ls_->on_heard = [this](const std::string& text) {
        std::lock_guard<std::mutex> lk(heard_mu_);
        heard_ = text;
        heard_at_ = now_seconds();
    };

    start_threads();
    as_->start();
    as_->greet();
    if (cfg_.listen_on_start) ls_->start();

    double next_frame = glfwGetTime();
    while (!glfwWindowShouldClose(win_)) {
        if (glfwGetWindowAttrib(win_, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.1);
            update();
            continue;
        }
        // a fixed frame budget: vsync alone does not throttle a covered window
        bool focused = glfwGetWindowAttrib(win_, GLFW_FOCUSED);
        next_frame += focused ? 1.0 / 30.0 : 1.0 / 10.0;
        double now = glfwGetTime();
        if (next_frame < now) next_frame = now;
        for (double rem; (rem = next_frame - glfwGetTime()) > 0.001;) glfwWaitEventsTimeout(rem);
        glfwPollEvents();
        update();
        int fw, fh;
        glfwGetFramebufferSize(win_, &fw, &fh);
        if (fw <= 0 || fh <= 0) continue;
        float want = layout_unit(ImVec2((float)fw, (float)fh));
        if (std::fabs(want - hud_.scale()) / hud_.scale() > 0.25f) {
            hud_.init(want);
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            ImGui_ImplOpenGL3_CreateFontsTexture();
        }
        glfwSwapInterval(focused ? 1 : 0);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        HudActions act = hud_.draw(gather());
        settings_window();
        ImGui::Render();
        glViewport(0, 0, fw, fh);
        glClearColor(0.01f, 0.03f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win_);
        handle(act);
    }

    tts_.stop();
    ls_->stop();
    as_->stop();
    stop_threads();
    tts_.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win_);
    glfwTerminate();
    return 0;
}

}  // namespace
}  // namespace kayra

int main() {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) args.push_back(kayra::narrow(wargv[i]));
    LocalFree(wargv);

    bool minimized = false;
    for (auto& a : args) {
        if (a == "--record") return kayra::run_record(args);
        if (a == "--minimized") minimized = true;
    }

    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\KayraHud");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND w = FindWindowW(nullptr, L"KAYRA")) {
            ShowWindow(w, SW_RESTORE);
            SetForegroundWindow(w);
        }
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int rc = 0;
    {
        kayra::App app;
        rc = app.run(minimized);
    }
    CoUninitialize();
    if (single) CloseHandle(single);
    return rc;
}

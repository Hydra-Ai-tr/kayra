#include "app/record.h"

#include <windows.h>
#include <objbase.h>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <nlohmann/json.hpp>
#include <random>

#include "app/hud.h"
#include "core/assistant.h"
#include "core/config.h"
#include "core/i18n.h"
#include "core/intent.h"
#include "core/system.h"
#include "core/tools.h"
#include "core/util.h"
#include "voice/tts.h"

using json = nlohmann::json;

namespace kayra {
namespace {

// OpenGL 3 framebuffer objects (not in the Windows GL 1.1 headers)
constexpr GLenum kFramebuffer = 0x8D40, kColorAttachment0 = 0x8CE0, kFramebufferComplete = 0x8CD5;
using GenFb = void(APIENTRY*)(GLsizei, GLuint*);
using BindFb = void(APIENTRY*)(GLenum, GLuint);
using FbTex = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using CheckFb = GLenum(APIENTRY*)(GLenum);
using DelFb = void(APIENTRY*)(GLsizei, const GLuint*);

struct Ffmpeg {
    HANDLE in = nullptr;
    PROCESS_INFORMATION pi{};

    bool start(const std::string& exe, const std::string& out, int w, int h, int fps) {
        std::string cmd = "\"" + exe + "\" -y -loglevel error -f rawvideo -pix_fmt rgba -s " + std::to_string(w) + "x" +
                          std::to_string(h) + " -r " + std::to_string(fps) +
                          " -i - -vf vflip -c:v libx264 -preset fast -crf 12 -pix_fmt yuv420p \"" + out + "\"";
        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
        HANDLE rd = nullptr, wr = nullptr;
        if (!CreatePipe(&rd, &wr, &sa, 8 << 20)) return false;
        SetHandleInformation(wr, HANDLE_FLAG_INHERIT, 0);
        std::wstring logp = widen(out + ".ffmpeg.log");
        HANDLE lg = CreateFileW(logp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
        STARTUPINFOW si{};
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = rd;
        si.hStdOutput = lg;
        si.hStdError = lg;
        std::wstring wcmd = widen(cmd);
        BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(rd);
        if (lg != INVALID_HANDLE_VALUE) CloseHandle(lg);
        if (!ok) {
            CloseHandle(wr);
            return false;
        }
        in = wr;
        return true;
    }
    bool write(const void* data, size_t n) {
        const char* p = (const char*)data;
        while (n) {
            DWORD done = 0;
            if (!WriteFile(in, p, (DWORD)std::min<size_t>(n, 1 << 24), &done, nullptr) || !done) return false;
            p += done;
            n -= done;
        }
        return true;
    }
    int finish() {
        if (in) CloseHandle(in);
        in = nullptr;
        DWORD code = 1;
        if (pi.hProcess) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        return (int)code;
    }
};

// RMS envelope of a 16-bit mono WAV, one value per video frame, 0..1.
std::vector<float> wav_envelope(const std::string& path, int fps) {
    std::string data;
    std::vector<float> env;
    if (!read_file(path, data) || data.size() < 44) return env;
    uint32_t rate = *(const uint32_t*)(data.data() + 24);
    size_t pos = 12;
    const int16_t* pcm = nullptr;
    size_t count = 0;
    while (pos + 8 <= data.size()) {
        std::string id = data.substr(pos, 4);
        uint32_t len = *(const uint32_t*)(data.data() + pos + 4);
        if (id == "data") {
            pcm = (const int16_t*)(data.data() + pos + 8);
            count = std::min<size_t>(len, data.size() - pos - 8) / 2;
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (!pcm || !rate) return env;
    size_t per = rate / (size_t)fps;
    float peak = 0.0001f;
    for (size_t i = 0; i + per <= count; i += per) {
        double sum = 0;
        for (size_t j = 0; j < per; j++) sum += (double)pcm[i + j] * pcm[i + j];
        float r = (float)std::sqrt(sum / (double)per) / 32768.0f;
        env.push_back(r);
        peak = std::max(peak, r);
    }
    for (auto& v : env) v = std::min(1.0f, v / (peak * 0.75f));
    return env;
}

std::string hhmm(std::time_t t, bool seconds) {
    std::tm tm{};
    localtime_s(&tm, &t);
    char b[16];
    if (seconds) snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    else snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return b;
}

class Recorder {
public:
    int run(const std::vector<std::string>& args);

private:
    void frame();
    void hold(double secs);
    void idle_look();
    void step_voice(const std::string& said, const json& step);
    void step_typed(const std::string& typed, const json& step);
    void respond(const std::string& question, const json& step);
    void speak(const std::string& text, const std::string& tool);

    // output
    int W = 1920, H = 1080, fps = 30;
    std::string out_, dir_, ffmpeg_;
    Ffmpeg ff_;
    GLuint fbo_ = 0, tex_ = 0;
    std::vector<unsigned char> px_;
    BindFb bind_fb_ = nullptr;
    long long frames_ = 0;
    bool failed_ = false;

    // virtual clock
    double vt_ = 0;
    std::time_t wall0_ = 0;

    // world
    Config cfg_;
    std::unique_ptr<Assistant> as_;
    Hud hud_;
    GLFWwindow* win_ = nullptr;
    std::mt19937 rng_{7};
    HudState s_;
    std::deque<float> wave_;
    float mic_target_ = 0.02f;
    std::vector<float> speak_env_;
    double speak_start_ = -1;
    std::string heard_;
    double heard_until_ = -1;
    struct VTimer {
        std::string label;
        double due, total;
    };
    std::vector<VTimer> timers_;
    double next_stats_ = 0;
    json events_ = json::array();
    int voice_n_ = 0;
};

void Recorder::frame() {
    if (failed_) return;
    double dt = 1.0 / fps;
    // microphone wave
    for (int i = 0; i < 2; i++) {
        std::normal_distribution<float> n(0.0f, 1.0f);
        float v = mic_target_;
        if (mic_target_ > 0.1f) {
            double syl = 0.5 + 0.5 * std::sin(vt_ * 2 * 3.14159 * 4.3 + std::sin(vt_ * 1.7) * 2.0);
            v = (float)(mic_target_ * (0.25 + 0.75 * syl)) * (0.75f + 0.25f * std::fabs(n(rng_)));
        } else {
            v = std::max(0.0f, mic_target_ * (0.6f + 0.4f * std::fabs(n(rng_))));
        }
        wave_.push_back(std::min(1.0f, v));
        while (wave_.size() > 96) wave_.pop_front();
    }
    s_.wave.assign(wave_.begin(), wave_.end());
    // speaking level from the voice envelope
    if (speak_start_ >= 0) {
        size_t i = (size_t)((vt_ - speak_start_) * fps);
        s_.level = i < speak_env_.size() ? speak_env_[i] : 0.0f;
    } else if (s_.look == Look::Hearing) {
        s_.level = wave_.empty() ? 0 : wave_.back();
    } else {
        s_.level = 0;
    }
    if (vt_ >= next_stats_) {
        SysStats st = system_stats();
        // the encoder keeps the CPU busy while recording; show a calmer, realistic load
        st.cpu = std::min(st.cpu, 9.0 + 7.0 * std::fabs(std::sin(vt_ * 0.37)) + (s_.look == Look::Thinking ? 14.0 : 0.0));
        s_.stats = st;
        s_.cpu_hist.push_back((float)st.cpu / 100.0f);
        s_.ram_hist.push_back((float)st.ram / 100.0f);
        if (s_.cpu_hist.size() > 60) s_.cpu_hist.erase(s_.cpu_hist.begin());
        if (s_.ram_hist.size() > 60) s_.ram_hist.erase(s_.ram_hist.begin());
        next_stats_ = vt_ + 1.0;
    }
    std::time_t wall = wall0_ + (std::time_t)vt_;
    s_.t = vt_;
    s_.clock = hhmm(wall, true);
    s_.now = vt_;
    s_.timers.clear();
    for (auto& t : timers_)
        if (t.due > vt_) s_.timers.push_back({0, t.label, t.due, t.total});
    s_.heard = vt_ < heard_until_ ? heard_ : "";

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)W, (float)H);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    io.DeltaTime = (float)dt;
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    ImGui::NewFrame();
    hud_.draw(s_);
    ImGui::Render();
    bind_fb_(kFramebuffer, fbo_);
    glViewport(0, 0, W, H);
    glClearColor(0.01f, 0.03f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px_.data());
    if (!ff_.write(px_.data(), px_.size())) {
        failed_ = true;
        log("record: ffmpeg pipe closed");
    }
    glfwPollEvents();
    frames_++;
    vt_ += dt;
    if (speak_start_ >= 0 && (vt_ - speak_start_) * fps >= speak_env_.size()) speak_start_ = -1;
}

void Recorder::hold(double secs) {
    int n = (int)std::lround(secs * fps);
    for (int i = 0; i < n && !failed_; i++) frame();
}

void Recorder::idle_look() {
    s_.look = Look::Waiting;
    s_.status = T("ui_idle");
    s_.hint = T("ui_waiting_wake");
    mic_target_ = 0.025f;
}

void Recorder::speak(const std::string& text, const std::string& tool) {
    (void)tool;
    std::string clean = Speaker::speakable(text);
    char name[32];
    snprintf(name, sizeof name, "voice_%02d.wav", ++voice_n_);
    std::string wav = dir_ + "\\" + name;
    double dur = Speaker::synth_to_wav(clean, wav, cfg_.turkish() ? "tr" : "en", cfg_.voice, cfg_.voice_rate);
    if (dur <= 0) {
        log("record: voice synthesis failed for: " + text);
        hold(2.0);
        return;
    }
    speak_env_ = wav_envelope(wav, fps);
    speak_start_ = vt_;
    events_.push_back({{"t", vt_}, {"type", "voice"}, {"wav", name}, {"dur", dur}, {"text", clean}});
    s_.look = Look::Speaking;
    s_.status = T("ui_speaking");
    s_.hint.clear();
    mic_target_ = 0.02f;
    hold(dur + 0.15);
    speak_start_ = -1;
}

void Recorder::respond(const std::string& question, const json& step) {
    // thinking
    s_.look = Look::Thinking;
    s_.status = T("ui_thinking");
    s_.hint.clear();
    mic_target_ = 0.015f;
    auto before = as_->timers();
    std::string reply = step.contains("reply") ? step["reply"].get<std::string>() : as_->ask(question);
    std::string tool = step.contains("tool") ? step["tool"].get<std::string>() : as_->last_tool();
    auto after = as_->timers();
    if (after.size() > before.size())
        for (size_t i = before.size(); i < after.size(); i++) timers_.push_back({after[i].label, 0, after[i].total});
    bool used_tool = false;
    for (auto& e : as_->log_entries())
        if (!e.user && e.text == reply && !e.tool.empty()) used_tool = true;
    if (!used_tool && !step.contains("tool")) tool.clear();
    hold(step.value("think", tool == "get_weather" || tool == "wikipedia" ? 1.1 : 0.6));
    for (auto& t : timers_)
        if (t.due == 0) t.due = vt_ + t.total;
    ChatEntry e;
    e.text = reply;
    e.time = hhmm(wall0_ + (std::time_t)vt_, false);
    e.tool = tool;
    s_.chat.push_back(e);
    s_.chat_version++;
    speak(reply, tool);
    hold(step.value("after", 0.7));
    idle_look();
}

void Recorder::step_voice(const std::string& said, const json& step) {
    idle_look();
    hold(step.value("before", 0.5));
    // hearing
    s_.look = Look::Hearing;
    s_.status = T("ui_hearing");
    s_.hint.clear();
    mic_target_ = 0.55f;
    double dur = step.value("speech", 0.55 + 0.062 * (double)said.size());
    events_.push_back({{"t", vt_}, {"type", "user"}, {"text", said}, {"dur", dur}});
    hold(dur);
    mic_target_ = 0.02f;
    s_.look = Look::Transcribing;
    s_.status = T("ui_transcribing");
    hold(0.45);
    heard_ = said;
    heard_until_ = vt_ + 7.0;
    bool had_wake = false;
    std::string rest = trim(strip_wake_word(said, cfg_.wake_words, &had_wake));
    if (fold(rest).empty()) {
        // the wake word alone
        ChatEntry u;
        u.user = true;
        u.text = said;
        u.time = hhmm(wall0_ + (std::time_t)vt_, false);
        s_.chat.push_back(u);
        ChatEntry e;
        e.text = T("wake_ack");
        e.time = u.time;
        s_.chat.push_back(e);
        s_.chat_version++;
        speak(e.text, "");
        s_.look = Look::Listening;
        s_.status = T("ui_listening");
        s_.hint.clear();
        hold(step.value("after", 0.4));
        return;
    }
    ChatEntry u;
    u.user = true;
    u.text = capitalize(rest);
    u.time = hhmm(wall0_ + (std::time_t)vt_, false);
    s_.chat.push_back(u);
    s_.chat_version++;
    respond(rest, step);
}

void Recorder::step_typed(const std::string& typed, const json& step) {
    idle_look();
    hold(step.value("before", 0.4));
    events_.push_back({{"t", vt_}, {"type", "typed"}, {"text", typed}});
    // type it at a human pace, character by character (UTF-8 aware)
    std::vector<size_t> cuts;
    for (size_t i = 1; i <= typed.size(); i++)
        if (i == typed.size() || ((unsigned char)typed[i] & 0xC0) != 0x80) cuts.push_back(i);
    double cps = step.value("cps", 17.0);
    double acc = 0;
    for (size_t c : cuts) {
        s_.typing = typed.substr(0, c);
        acc += 1.0 / cps;
        while (acc >= 1.0 / fps) {
            frame();
            acc -= 1.0 / fps;
        }
    }
    hold(0.35);
    s_.typing.clear();
    ChatEntry u;
    u.user = true;
    u.text = typed;
    u.time = hhmm(wall0_ + (std::time_t)vt_, false);
    s_.chat.push_back(u);
    s_.chat_version++;
    respond(typed, step);
}

int Recorder::run(const std::vector<std::string>& args) {
    std::string script_path, lang;
    for (size_t i = 0; i + 1 < args.size(); i++) {
        if (args[i] == "--record") out_ = args[i + 1];
        if (args[i] == "--script") script_path = args[i + 1];
        if (args[i] == "--ffmpeg") ffmpeg_ = args[i + 1];
        if (args[i] == "--lang") lang = args[i + 1];
        if (args[i] == "--fps") fps = std::max(10, std::atoi(args[i + 1].c_str()));
        if (args[i] == "--size") std::sscanf(args[i + 1].c_str(), "%dx%d", &W, &H);
    }
    std::string text;
    if (out_.empty() || !read_file(script_path, text)) {
        log("record: usage: kayra --record out.mp4 --script demo.json --ffmpeg ffmpeg.exe");
        return 2;
    }
    json script = json::parse(text, nullptr, false);
    if (!script.is_object()) {
        log("record: bad script " + script_path);
        return 2;
    }
    if (ffmpeg_.empty()) ffmpeg_ = "ffmpeg";
    size_t slash = out_.find_last_of("\\/");
    dir_ = slash == std::string::npos ? "." : out_.substr(0, slash);

    // configuration: built-in engine only, the demo never touches the PC
    cfg_.load();
    if (lang.empty()) lang = script.value("lang", "tr");
    cfg_.language = lang;
    cfg_.provider = "none";
    cfg_.user_name = script.value("user_name", "");
    cfg_.title = script.value("title", "");
    if (script.contains("city")) cfg_.city = script["city"].get<std::string>();
    if (script.contains("voice")) cfg_.voice = script["voice"].get<std::string>();
    cfg_.voice_rate = script.value("voice_rate", cfg_.voice_rate);
    cfg_.listen_mode = "wake";
    set_language(cfg_.turkish());
    set_address(cfg_.address());
    as_ = std::make_unique<Assistant>(cfg_, nullptr);
    as_->set_speech(false);
    as_->set_dry_run(true);

    if (!glfwInit()) return 3;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    win_ = glfwCreateWindow(640, 360, "KAYRA recorder", nullptr, nullptr);
    if (!win_) return 3;
    glfwMakeContextCurrent(win_);
    auto gen_fb = (GenFb)glfwGetProcAddress("glGenFramebuffers");
    bind_fb_ = (BindFb)glfwGetProcAddress("glBindFramebuffer");
    auto fb_tex = (FbTex)glfwGetProcAddress("glFramebufferTexture2D");
    auto check_fb = (CheckFb)glfwGetProcAddress("glCheckFramebufferStatus");
    auto del_fb = (DelFb)glfwGetProcAddress("glDeleteFramebuffers");
    if (!gen_fb || !bind_fb_ || !fb_tex || !check_fb || !del_fb) return 3;
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gen_fb(1, &fbo_);
    bind_fb_(kFramebuffer, fbo_);
    fb_tex(kFramebuffer, kColorAttachment0, GL_TEXTURE_2D, tex_, 0);
    if (check_fb(kFramebuffer) != kFramebufferComplete) {
        log("record: framebuffer incomplete");
        return 3;
    }
    px_.resize((size_t)W * H * 4);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(win_, false);
    ImGui_ImplOpenGL3_Init("#version 130");
    hud_.init(std::clamp(std::min(H / 900.0f, W / 1600.0f), 0.6f, 2.0f));

    if (!ff_.start(ffmpeg_, out_, W, H, fps)) {
        log("record: could not start ffmpeg: " + ffmpeg_);
        return 4;
    }

    // the scene
    wall0_ = std::time(nullptr);
    if (script.contains("clock")) {
        // "21:04:30" keeps the shown time consistent with the greeting
        int hh = 0, mm = 0, ss = 0;
        std::sscanf(script["clock"].get<std::string>().c_str(), "%d:%d:%d", &hh, &mm, &ss);
        std::tm tm{};
        localtime_s(&tm, &wall0_);
        tm.tm_hour = hh;
        tm.tm_min = mm;
        tm.tm_sec = ss;
        wall0_ = std::mktime(&tm);
    }
    s_.date = [&] {
        std::tm tm{};
        localtime_s(&tm, &wall0_);
        if (is_turkish())
            return std::to_string(tm.tm_mday) + " " + month_name(tm.tm_mon) + " " + std::to_string(tm.tm_year + 1900) +
                   " · " + weekday_name(tm.tm_wday);
        return weekday_name(tm.tm_wday) + " · " + month_name(tm.tm_mon) + " " + std::to_string(tm.tm_mday) + ", " +
               std::to_string(tm.tm_year + 1900);
    }();
    s_.cpu_name = cpu_name();
    s_.pc_name = computer_name();
    s_.weather = fetch_weather(cfg_.city, cfg_.turkish());
    s_.mic_on = true;
    s_.voice_on = true;
    s_.mode_label = T("ui_mode_wake");
    s_.brain_label = script.value("brain_label", T("ui_builtin"));
    s_.voice_label = script.value("voice_label", std::string("TOLGA"));
    s_.show_input = true;
    s_.user_name = cfg_.user_name;
    s_.brain_on = script.value("brain_on", true);
    s_.online = true;
    s_.tools_on = true;
    s_.files_on = cfg_.allow_files;
    s_.web_on = cfg_.allow_web;
    s_.shell_on = cfg_.allow_shell;
    system_stats();
    idle_look();

    for (auto& step : script.value("steps", json::array())) {
        if (failed_) break;
        if (step.contains("mark")) events_.push_back({{"t", vt_}, {"type", "mark"}, {"name", step["mark"]}});
        if (step.contains("page")) {
            std::string p = step["page"].get<std::string>();
            hud_.set_page(p == "system" ? Page::System : p == "chat" ? Page::Chat : p == "tools" ? Page::Tools
                          : p == "modules" ? Page::Modules : p == "timers" ? Page::Timers : Page::Home);
        }
        if (step.contains("wait")) {
            idle_look();
            hold(step["wait"].get<double>());
        } else if (step.contains("greet")) {
            s_.look = Look::Standby;
            s_.status = T("ui_online");
            s_.hint.clear();
            hold(step.value("before", 1.0));
            std::string key = step["greet"].get<std::string>();
            if (key == "auto") {
                std::tm tm{};
                std::time_t now = wall0_ + (std::time_t)vt_;
                localtime_s(&tm, &now);
                key = tm.tm_hour >= 5 && tm.tm_hour < 12    ? "greet_morning"
                      : tm.tm_hour >= 12 && tm.tm_hour < 18 ? "greet_day"
                      : tm.tm_hour >= 18 && tm.tm_hour < 23 ? "greet_evening"
                                                            : "greet_night";
            }
            ChatEntry e;
            e.text = T(key.c_str()) + " " + T("online");
            e.time = hhmm(wall0_ + (std::time_t)vt_, false);
            s_.chat.push_back(e);
            s_.chat_version++;
            speak(e.text, "");
            hold(step.value("after", 0.6));
            idle_look();
        } else if (step.contains("say")) {
            step_voice(step["say"].get<std::string>(), step);
        } else if (step.contains("type")) {
            step_typed(step["type"].get<std::string>(), step);
        }
    }
    hold(script.value("outro", 1.5));

    int code = ff_.finish();
    json manifest = {{"video", out_.substr(slash == std::string::npos ? 0 : slash + 1)},
                     {"fps", fps},
                     {"width", W},
                     {"height", H},
                     {"duration", vt_},
                     {"frames", frames_},
                     {"events", events_}};
    std::string mpath = out_.substr(0, out_.find_last_of('.')) + ".json";
    write_file(mpath, manifest.dump(2));

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    del_fb(1, &fbo_);
    glDeleteTextures(1, &tex_);
    glfwDestroyWindow(win_);
    glfwTerminate();
    log("record: " + std::to_string(frames_) + " frames, ffmpeg exit " + std::to_string(code));
    return failed_ || code != 0 ? 5 : 0;
}

}  // namespace

int run_record(const std::vector<std::string>& args) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int rc = 0;
    {
        Recorder r;
        rc = r.run(args);
    }
    CoUninitialize();
    return rc;
}

}  // namespace kayra

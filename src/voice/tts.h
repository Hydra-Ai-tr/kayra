// Text to speech. Neural voices (Piper on this PC, Edge online) are synthesized
// sentence by sentence and played through waveOut while the next sentence is
// prepared; the Windows voices (SAPI 5 and OneCore, e.g. "Microsoft Tolga")
// run on their own thread and are the fallback when a neural voice fails.
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kayra {

struct Config;

struct VoiceInfo {
    std::string name;   // "Microsoft Tolga - Turkish (Turkey)"
    std::string lang;   // "tr", "en", ...
    std::string id;     // registry token id
};

struct VoiceSettings {
    std::string engine = "sapi";  // "piper", "edge", "sapi"
    std::string lang = "tr";
    std::string sapi_hint;        // part of a Windows voice name
    std::string piper_voice, edge_voice;
    int rate = 0;                 // -10..10
    int volume = 100;             // 0..100
};
VoiceSettings voice_settings(const Config& c);

class Speaker {
public:
    Speaker();
    ~Speaker();
    void start();
    void shutdown();

    void configure(const VoiceSettings& s);
    void speak(const std::string& text);
    void stop();
    bool speaking() const;
    float level() const;
    std::string voice_name() const;

    // neural engine state for the settings window
    bool engine_ready() const { return engine_ready_; }
    float engine_progress() const { return progress_; }  // 0..1 while Piper downloads, else < 0
    void refresh_engine();                               // check again, e.g. after installing edge-tts

    static std::vector<VoiceInfo> voices();
    // Speaks into a 16-bit mono WAV with a Windows voice; returns the length in seconds, < 0 on failure.
    static double synth_to_wav(const std::string& text, const std::string& path, const std::string& lang,
                               const std::string& hint, int rate);
    // Text cleaned for speech: no markdown, links or emoji.
    static std::string speakable(const std::string& text);

private:
    struct Cmd {
        enum Kind { Speak, Stop, Configure, Quit } kind;
        std::string text, lang, hint;
        int rate = 0, volume = 100;
    };
    void run();
    void push(Cmd c);
    void neural_run();
    void prepare_engine(const VoiceSettings& s, bool recheck = false);
    bool neural() const;

    std::thread th_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Cmd> q_;
    std::atomic<bool> speaking_{false};
    std::atomic<int> pending_{0};
    std::atomic<double> last_viseme_{0};
    std::atomic<float> viseme_level_{0};
    std::string voice_name_;
    bool started_ = false;

    // neural path
    struct Player;
    std::thread nth_, prep_th_;
    std::mutex nmu_;
    std::condition_variable ncv_;
    std::deque<std::pair<std::string, unsigned>> nq_;  // text, generation
    std::atomic<unsigned> gen_{0};
    std::atomic<int> npending_{0};
    std::atomic<bool> nquit_{false};
    VoiceSettings vs_;                                  // under nmu_
    std::unique_ptr<Player> player_;
    std::atomic<bool> engine_ready_{false};
    std::atomic<bool> neural_on_{false};
    bool started_once_ = false;
    std::string neural_label_;                          // under mu_
    std::atomic<float> progress_{-1.0f};
    std::atomic<bool> prep_cancel_{false};
};

}  // namespace kayra

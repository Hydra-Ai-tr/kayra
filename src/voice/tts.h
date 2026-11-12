// Text to speech with the Windows voices (SAPI 5 and the OneCore voices such as
// "Microsoft Tolga" for Turkish). Speech runs on its own thread; visemes drive
// the level the HUD core pulses with.
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kayra {

struct VoiceInfo {
    std::string name;   // "Microsoft Tolga - Turkish (Turkey)"
    std::string lang;   // "tr", "en", ...
    std::string id;     // registry token id
};

class Speaker {
public:
    Speaker();
    ~Speaker();
    void start();
    void shutdown();

    // lang "tr"/"en"; hint picks a voice whose name contains it
    void configure(const std::string& lang, const std::string& hint, int rate, int volume);
    void speak(const std::string& text);
    void stop();
    bool speaking() const;
    float level() const;
    std::string voice_name() const;

    static std::vector<VoiceInfo> voices();
    // Speaks into a 16-bit mono WAV; returns the length in seconds, < 0 on failure.
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
};

}  // namespace kayra

// Always-on listening: voice activity detection on the microphone, whisper
// transcription, the "Kayra" wake word, push-to-talk and follow-up windows.
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/config.h"
#include "voice/mic.h"
#include "voice/stt.h"

namespace kayra {

class Listener {
public:
    enum class Phase { Off, Downloading, Loading, Waiting, Hearing, Transcribing, Error };

    explicit Listener(const Config& cfg);
    ~Listener();

    // Mode, wake words and sensitivity apply immediately; model and microphone on the next start().
    void set_config(const Config& cfg);

    void start();   // loads (downloading if needed) the model and opens the microphone
    void stop();
    bool active() const { return phase() != Phase::Off && phase() != Phase::Error; }

    void arm();                       // next utterance is a command (hotkey / button)
    bool armed() const { return armed_; }
    bool expecting_command() const;   // armed, or inside a follow-up window
    void follow_up(double seconds);   // accept commands without the wake word for a while
    void set_suppressed(bool s);      // ignore audio while Kayra speaks

    Phase phase() const { return phase_; }
    float level() const { return level_; }
    float download_progress() const { return progress_; }
    std::string error() const;
    std::string last_heard() const;
    std::vector<float> levels() const;  // recent input levels for the HUD

    // Called on the recognition thread.
    std::function<void(const std::string& command)> on_command;
    std::function<void()> on_wake;    // the wake word alone
    std::function<void(const std::string& heard)> on_heard;

private:
    void on_frame(const int16_t* s, size_t n);
    void worker();
    void handle_text(const std::string& text, bool armed);

    Config config() const;

    Config cfg_;  // guarded by mu_
    std::atomic<bool> push_only_{false};
    std::atomic<float> sensitivity_{0.5f};
    Mic mic_;
    Stt stt_;
    std::thread th_;
    std::atomic<bool> quit_{false};
    std::atomic<Phase> phase_{Phase::Off};
    std::atomic<float> level_{0}, progress_{0};
    std::atomic<bool> armed_{false}, suppressed_{false};
    std::atomic<double> follow_until_{0}, suppressed_until_{0};

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::pair<std::vector<float>, bool>> utterances_;  // samples, armed
    std::string error_, heard_;
    std::deque<float> levels_;

    // VAD state (microphone thread)
    std::vector<float> speech_;
    std::deque<std::vector<float>> preroll_;
    float noise_ = 0.004f;
    int above_ = 0, below_ = 0;
    bool in_speech_ = false;
};

}  // namespace kayra

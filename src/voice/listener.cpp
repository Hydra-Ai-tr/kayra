#include "voice/listener.h"

#include <algorithm>
#include <cmath>

#include "core/http.h"
#include "core/intent.h"
#include "core/util.h"

namespace kayra {

namespace {
constexpr int kPrerollFrames = 15;   // 300 ms kept before speech starts
constexpr int kStartFrames = 3;      // 60 ms above the threshold starts speech
constexpr int kEndFrames = 38;       // 760 ms of quiet ends it
constexpr int kMinFrames = 15;       // shorter than 300 ms is a click, not speech
constexpr int kMaxFrames = 750;      // 15 s
}  // namespace

Listener::Listener(const Config& cfg) { set_config(cfg); }

Listener::~Listener() { stop(); }

void Listener::set_config(const Config& cfg) {
    std::lock_guard<std::mutex> lk(mu_);
    cfg_ = cfg;
    push_only_ = cfg.listen_mode == "push";
    sensitivity_ = (float)std::max(0.0, std::min(1.0, cfg.vad_sensitivity));
}

Config Listener::config() const {
    std::lock_guard<std::mutex> lk(mu_);
    return cfg_;
}

std::string Listener::error() const {
    std::lock_guard<std::mutex> lk(mu_);
    return error_;
}

std::string Listener::last_heard() const {
    std::lock_guard<std::mutex> lk(mu_);
    return heard_;
}

std::vector<float> Listener::levels() const {
    std::lock_guard<std::mutex> lk(mu_);
    return std::vector<float>(levels_.begin(), levels_.end());
}

void Listener::arm() {
    armed_ = true;
    follow_until_ = now_seconds() + 8.0;
}

bool Listener::expecting_command() const { return armed_ || now_seconds() < follow_until_; }

void Listener::follow_up(double seconds) {
    if (seconds > 0) follow_until_ = std::max(follow_until_.load(), now_seconds() + seconds);
}

void Listener::set_suppressed(bool s) {
    if (s) {
        suppressed_ = true;
    } else if (suppressed_) {
        suppressed_ = false;
        suppressed_until_ = now_seconds() + 0.35;  // the room echo of the last word
    }
}

void Listener::start() {
    if (th_.joinable()) return;
    quit_ = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        error_.clear();
    }
    th_ = std::thread([this] { worker(); });
}

void Listener::stop() {
    quit_ = true;
    cv_.notify_all();
    mic_.stop();
    if (th_.joinable()) th_.join();
    phase_ = Phase::Off;
    level_ = 0;
    in_speech_ = false;
    speech_.clear();
    preroll_.clear();
}

void Listener::on_frame(const int16_t* s, size_t n) {
    std::vector<float> f(n);
    double sum = 0;
    for (size_t i = 0; i < n; i++) {
        f[i] = s[i] / 32768.0f;
        sum += (double)f[i] * f[i];
    }
    float rms = (float)std::sqrt(sum / (double)n);
    level_ = level_ * 0.6f + std::min(1.0f, rms * 6.0f) * 0.4f;
    {
        std::lock_guard<std::mutex> lk(mu_);
        levels_.push_back(std::min(1.0f, rms * 8.0f));
        while (levels_.size() > 96) levels_.pop_front();
    }
    bool muted = suppressed_ || now_seconds() < suppressed_until_;
    if (muted || (push_only_ && !armed_)) {
        in_speech_ = false;
        speech_.clear();
        above_ = below_ = 0;
        if (!muted) noise_ = noise_ * 0.98f + rms * 0.02f;
        preroll_.push_back(std::move(f));
        while ((int)preroll_.size() > kPrerollFrames) preroll_.pop_front();
        return;
    }
    double sens = sensitivity_;
    float base = (float)(0.03 - 0.022 * sens);           // 0.030 .. 0.008
    float on = std::max(base, noise_ * (float)(4.0 - 2.0 * sens));
    float off = on * 0.55f;
    if (!in_speech_) {
        noise_ = std::max(0.0008f, noise_ * 0.985f + std::min(rms, noise_ * 3.0f) * 0.015f);
        above_ = rms > on ? above_ + 1 : 0;
        preroll_.push_back(f);
        while ((int)preroll_.size() > kPrerollFrames) preroll_.pop_front();
        if (above_ >= kStartFrames) {
            in_speech_ = true;
            below_ = 0;
            speech_.clear();
            for (auto& p : preroll_) speech_.insert(speech_.end(), p.begin(), p.end());
            preroll_.clear();
            if (phase_ == Phase::Waiting) phase_ = Phase::Hearing;
        }
        return;
    }
    speech_.insert(speech_.end(), f.begin(), f.end());
    below_ = rms < off ? below_ + 1 : 0;
    int frames = (int)(speech_.size() / Mic::kFrame);
    if (below_ >= kEndFrames || frames >= kMaxFrames) {
        in_speech_ = false;
        above_ = 0;
        if (frames - below_ >= kMinFrames) {
            // trim most of the trailing silence
            size_t keep = speech_.size() - (size_t)std::max(0, below_ - 10) * Mic::kFrame;
            speech_.resize(keep);
            {
                std::lock_guard<std::mutex> lk(mu_);
                utterances_.push_back({std::move(speech_), armed_.load()});
                while (utterances_.size() > 3) utterances_.pop_front();
            }
            armed_ = false;
            cv_.notify_one();
        } else if (phase_ == Phase::Hearing) {
            phase_ = Phase::Waiting;
        }
        speech_.clear();
    }
}

void Listener::worker() {
    auto set_error = [&](const std::string& e) {
        std::lock_guard<std::mutex> lk(mu_);
        error_ = e;
        phase_ = Phase::Error;
        log("listener: " + e);
    };
    if (!Stt::available()) {
        set_error("built without speech recognition");
        return;
    }
    const Config cfg = config();
    std::string name = cfg.stt_model.empty() ? "small-q5_1" : cfg.stt_model;
    std::string path = Stt::model_path(name);
    if (!file_exists(path)) {
        phase_ = Phase::Downloading;
        progress_ = 0;
        std::string err;
        std::atomic<bool> cancel{false};
        std::thread watch([&] {
            while (!cancel && !quit_) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            cancel = true;
        });
        bool ok = http_download(
            Stt::model_url(name), path,
            [&](int64_t got, int64_t total) {
                if (total > 0) progress_ = (float)got / (float)total;
                else progress_ = std::min(0.99f, (float)got / (60.0f * 1024 * 1024));
            },
            &cancel, &err);
        cancel = true;
        watch.join();
        if (quit_) return;
        if (!ok) {
            set_error("model download failed: " + err);
            return;
        }
    }
    phase_ = Phase::Loading;
    std::string err;
    if (!stt_.loaded() && !stt_.load(path, &err)) {
        set_error(err);
        return;
    }
    if (quit_) return;
    if (!mic_.start(cfg.mic_device, [this](const int16_t* s, size_t n) { on_frame(s, n); }, &err)) {
        set_error("microphone: " + err);
        return;
    }
    phase_ = Phase::Waiting;
    log("listening with " + name);
    while (!quit_) {
        std::pair<std::vector<float>, bool> u;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return quit_ || !utterances_.empty(); });
            if (quit_) break;
            u = std::move(utterances_.front());
            utterances_.pop_front();
        }
        phase_ = Phase::Transcribing;
        bool tr = config().turkish();
        std::string prompt = tr ? "Kayra, saat kaç? Kayra, Spotify'ı aç." : "Kayra, what time is it? Kayra, open Spotify.";
        std::string text = stt_.transcribe(u.first, tr ? "tr" : "en", prompt);
        log("heard (" + std::to_string(stt_.last_seconds()).substr(0, 4) + " s): " + text);
        if (!quit_) phase_ = in_speech_ ? Phase::Hearing : Phase::Waiting;
        if (text.empty() || is_hallucination(text)) continue;
        handle_text(text, u.second);
    }
}

void Listener::handle_text(const std::string& text, bool was_armed) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        heard_ = text;
    }
    if (on_heard) on_heard(text);
    const Config cfg = config();
    auto command = [&](const std::string& c) {
        follow_until_ = 0;
        if (on_command) on_command(capitalize(trim(c)));
    };
    bool had_wake = false;
    std::string rest = strip_wake_word(text, cfg.wake_words, &had_wake);
    bool following = now_seconds() < follow_until_;
    if (had_wake) {
        if (trim(rest).empty() || fold(rest).empty()) {
            follow_until_ = now_seconds() + 8.0;
            if (on_wake) on_wake();
            return;
        }
        command(rest);
        return;
    }
    if (was_armed || following || cfg.listen_mode == "always") {
        command(text);
        return;
    }
    if (cfg.listen_mode == "wake" && mentions_wake_word(text, cfg.wake_words)) {
        // "saat kaç Kayra"
        std::string cleaned;
        for (auto& w : split_words(text))
            if (!mentions_wake_word(w, cfg.wake_words)) cleaned += (cleaned.empty() ? "" : " ") + w;
        if (!trim(cleaned).empty()) {
            command(cleaned);
        } else {
            follow_until_ = now_seconds() + 8.0;
            if (on_wake) on_wake();
        }
    }
}

}  // namespace kayra

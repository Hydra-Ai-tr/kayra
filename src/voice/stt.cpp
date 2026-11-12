#include "voice/stt.h"

#include <algorithm>
#include <thread>

#include "core/util.h"

#ifdef KAYRA_HAVE_WHISPER
#include <whisper.h>
#endif

namespace kayra {

const std::vector<SttModel>& Stt::models() {
    static const std::vector<SttModel> m = {
        {"small-q5_1", "Small · önerilen / recommended", 190},
        {"base-q5_1", "Base · en hızlı / fastest", 60},
        {"medium-q5_0", "Medium · daha doğru, yavaş / more accurate, slower", 540},
        {"large-v3-turbo-q5_0", "Large v3 Turbo · en doğru / most accurate", 575},
    };
    return m;
}

std::string Stt::model_path(const std::string& name) { return models_dir() + "\\ggml-" + name + ".bin"; }

std::string Stt::model_url(const std::string& name) {
    return "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-" + name + ".bin";
}

bool is_hallucination(const std::string& text) {
    std::string f = fold(text);
    if (f.size() < 2) return true;
    std::string t = trim(text);
    if (!t.empty() && (t.front() == '[' || t.front() == '(' || t.front() == '*') &&
        (t.back() == ']' || t.back() == ')' || t.back() == '*'))
        return true;
    static const char* known[] = {
        "altyazi m k", "altyazi", "abone olmayi unutmayin", "izlediginiz icin tesekkurler", "izlediginiz icin tesekkur ederim",
        "tesekkurler", "muzik", "alkis", "thank you", "thanks for watching", "thank you for watching", "you", "bye",
        "subtitles by the amara org community", "music", "applause", "silence", "sessizlik", "hmm", "mm", "eee", "ii",
        "bir sonraki videoda gorusmek uzere", "altyazilar", "kanalima abone olun", "abone ol",
    };
    for (const char* k : known)
        if (f == k) return true;
    if (contains(f, "altyazi m k") || contains(f, "amara org")) return true;
    return false;
}

#ifdef KAYRA_HAVE_WHISPER

namespace {
void quiet_log(enum ggml_log_level level, const char* text, void*) {
    if (level == GGML_LOG_LEVEL_ERROR && text) log(std::string("whisper: ") + text);
}
}  // namespace

bool Stt::available() { return true; }

Stt::~Stt() { unload(); }

bool Stt::load(const std::string& path, std::string* error) {
    std::lock_guard<std::mutex> lk(mu_);
    if (ctx_) {
        whisper_free((whisper_context*)ctx_);
        ctx_ = nullptr;
    }
    whisper_log_set(quiet_log, nullptr);
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = false;
    cp.flash_attn = true;
    whisper_context* c = whisper_init_from_file_with_params(path.c_str(), cp);
    if (!c) {
        if (error) *error = "cannot load " + path;
        return false;
    }
    ctx_ = c;
    return true;
}

bool Stt::loaded() const {
    std::lock_guard<std::mutex> lk(mu_);
    return ctx_ != nullptr;
}

void Stt::unload() {
    std::lock_guard<std::mutex> lk(mu_);
    if (ctx_) whisper_free((whisper_context*)ctx_);
    ctx_ = nullptr;
}

std::string Stt::transcribe(const std::vector<float>& pcm, const std::string& lang, const std::string& prompt) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!ctx_ || pcm.empty()) return "";
    double t0 = now_seconds();
    auto* c = (whisper_context*)ctx_;
    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.n_threads = std::max(1, std::min(8, (int)std::thread::hardware_concurrency() - 1));
    p.language = lang == "auto" ? "auto" : lang.c_str();
    p.translate = false;
    p.no_context = true;
    p.single_segment = true;
    p.print_special = false;
    p.print_progress = false;
    p.print_realtime = false;
    p.print_timestamps = false;
    p.no_timestamps = true;
    p.suppress_blank = true;
    p.suppress_nst = true;
    p.temperature = 0.0f;
    p.initial_prompt = prompt.empty() ? nullptr : prompt.c_str();
    // pad short clips to 1.2 s: whisper struggles below one second
    std::vector<float> buf = pcm;
    if (buf.size() < 19200) buf.resize(19200, 0.0f);
    // Encode only as much of whisper's 30 s window as the command needs (50 frames per second,
    // with headroom): several times faster on short commands.
    p.audio_ctx = std::min(1500, (int)(buf.size() / 16000.0 * 50.0) + 128);
    if (whisper_full(c, p, buf.data(), (int)buf.size()) != 0) return "";
    std::string out;
    int n = whisper_full_n_segments(c);
    for (int i = 0; i < n; i++) out += whisper_full_get_segment_text(c, i);
    last_seconds_ = now_seconds() - t0;
    return trim(out);
}

#else

bool Stt::available() { return false; }
Stt::~Stt() = default;
bool Stt::load(const std::string&, std::string* error) {
    if (error) *error = "built without whisper.cpp";
    return false;
}
bool Stt::loaded() const { return false; }
void Stt::unload() {}
std::string Stt::transcribe(const std::vector<float>&, const std::string&, const std::string&) { return ""; }

#endif

}  // namespace kayra

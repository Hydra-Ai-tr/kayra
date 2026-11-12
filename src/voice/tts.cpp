#include "voice/tts.h"

#include <windows.h>
#include <mmsystem.h>
#include <sapi.h>

#include <algorithm>
#include <cmath>

#include "core/config.h"
#include "core/util.h"
#include "voice/neural.h"

namespace kayra {

namespace {

const GUID kWaveFormatEx = {0xC31ADBAE, 0x527F, 0x4FF5, {0xA2, 0x30, 0xF6, 0x2B, 0xB6, 0x1F, 0xF7, 0x0C}};

const wchar_t* kCategories[] = {
    L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech_OneCore\\Voices",
    L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices",
};

std::string lang_of(const std::wstring& lcid_list) {
    std::wstring first = lcid_list.substr(0, lcid_list.find(L';'));
    unsigned long lcid = wcstoul(first.c_str(), nullptr, 16);
    switch (lcid & 0x3FF) {
        case 0x1F: return "tr";
        case 0x09: return "en";
        case 0x07: return "de";
        case 0x0C: return "fr";
        case 0x0A: return "es";
        case 0x19: return "ru";
        case 0x01: return "ar";
        default: return "other";
    }
}

// Walks the voice tokens; fn returns true to keep the token (it is then not released).
template <class Fn>
void each_token(Fn fn) {
    for (const wchar_t* cat : kCategories) {
        ISpObjectTokenCategory* c = nullptr;
        if (FAILED(CoCreateInstance(__uuidof(SpObjectTokenCategory), nullptr, CLSCTX_ALL, __uuidof(ISpObjectTokenCategory),
                                    (void**)&c)))
            continue;
        if (SUCCEEDED(c->SetId(cat, FALSE))) {
            IEnumSpObjectTokens* en = nullptr;
            if (SUCCEEDED(c->EnumTokens(nullptr, nullptr, &en)) && en) {
                ISpObjectToken* t = nullptr;
                while (en->Next(1, &t, nullptr) == S_OK) {
                    VoiceInfo v;
                    LPWSTR id = nullptr, desc = nullptr, lang = nullptr;
                    if (SUCCEEDED(t->GetId(&id))) v.id = narrow(id);
                    if (SUCCEEDED(t->GetStringValue(nullptr, &desc))) v.name = narrow(desc);
                    ISpDataKey* attr = nullptr;
                    if (SUCCEEDED(t->OpenKey(L"Attributes", &attr)) && attr) {
                        if (SUCCEEDED(attr->GetStringValue(L"Language", &lang))) v.lang = lang_of(lang);
                        attr->Release();
                    }
                    if (id) CoTaskMemFree(id);
                    if (desc) CoTaskMemFree(desc);
                    if (lang) CoTaskMemFree(lang);
                    if (!fn(v, t)) t->Release();
                }
                en->Release();
            }
        }
        c->Release();
    }
}

ISpObjectToken* find_token(const std::string& lang, const std::string& hint, std::string* name) {
    ISpObjectToken* best = nullptr;
    int best_score = -1;
    std::string hl = lower(hint);
    each_token([&](const VoiceInfo& v, ISpObjectToken* t) {
        int score = 0;
        if (!hl.empty() && contains(lower(v.name), hl)) score += 100;
        if (v.lang == lang) score += 10;
        if (contains(v.id, "OneCore")) score += 1;  // the OneCore voices sound better
        if (score > best_score) {
            if (best) best->Release();
            best = t;
            best_score = score;
            if (name) *name = v.name;
            return true;
        }
        return false;
    });
    return best;
}

void clear_event(SPEVENT& e) {
    switch (e.elParamType) {
        case SPET_LPARAM_IS_POINTER:
        case SPET_LPARAM_IS_STRING:
            CoTaskMemFree((void*)e.lParam);
            break;
        case SPET_LPARAM_IS_TOKEN:
        case SPET_LPARAM_IS_OBJECT:
            if (e.lParam) ((IUnknown*)e.lParam)->Release();
            break;
        default:
            break;
    }
    e.lParam = 0;
}

float viseme_open(int v) {
    static const float open[22] = {0.0f, 0.9f, 1.0f, 0.85f, 0.7f, 0.6f, 0.5f, 0.45f, 0.75f, 0.9f, 0.8f,
                                   0.9f, 0.4f, 0.4f, 0.45f, 0.3f, 0.35f, 0.3f, 0.25f, 0.35f, 0.4f, 0.15f};
    return v >= 0 && v < 22 ? open[v] : 0.5f;
}

}  // namespace

// Neural speech is queued on waveOut sentence by sentence; the playback position
// gives the level the HUD core pulses with.
struct Speaker::Player {
    static constexpr int kRate = 24000;
    struct Buf {
        WAVEHDR h{};
        std::vector<int16_t> pcm;
        uint64_t start = 0;
    };
    std::mutex mu;
    HWAVEOUT wo = nullptr;
    std::deque<std::unique_ptr<Buf>> bufs;
    uint64_t written = 0;

    int reap_locked() {
        int n = 0;
        while (!bufs.empty() && (bufs.front()->h.dwFlags & WHDR_DONE)) {
            waveOutUnprepareHeader(wo, &bufs.front()->h, sizeof(WAVEHDR));
            bufs.pop_front();
            n++;
        }
        return n;
    }
    int reap() {
        std::lock_guard<std::mutex> lk(mu);
        return reap_locked();
    }
    // false when the generation is stale (stopped meanwhile) or the device fails
    bool play(std::vector<int16_t>&& pcm, unsigned gen, const std::atomic<unsigned>& current) {
        std::lock_guard<std::mutex> lk(mu);
        if (gen != current || pcm.empty()) return false;
        if (!wo) {
            WAVEFORMATEX f{};
            f.wFormatTag = WAVE_FORMAT_PCM;
            f.nChannels = 1;
            f.nSamplesPerSec = kRate;
            f.wBitsPerSample = 16;
            f.nBlockAlign = 2;
            f.nAvgBytesPerSec = kRate * 2;
            if (waveOutOpen(&wo, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
                wo = nullptr;
                return false;
            }
        }
        auto b = std::make_unique<Buf>();
        b->pcm = std::move(pcm);
        b->start = written;
        b->h.lpData = (LPSTR)b->pcm.data();
        b->h.dwBufferLength = (DWORD)(b->pcm.size() * 2);
        if (waveOutPrepareHeader(wo, &b->h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return false;
        if (waveOutWrite(wo, &b->h, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(wo, &b->h, sizeof(WAVEHDR));
            return false;
        }
        written += b->pcm.size();
        bufs.push_back(std::move(b));
        return true;
    }
    bool busy() {
        std::lock_guard<std::mutex> lk(mu);
        reap_locked();
        return !bufs.empty();
    }
    void reset() {
        std::lock_guard<std::mutex> lk(mu);
        if (!wo) return;
        waveOutReset(wo);
        reap_locked();
        written = 0;
    }
    float level() {
        std::lock_guard<std::mutex> lk(mu);
        if (!wo || bufs.empty()) return 0.0f;
        MMTIME t{};
        t.wType = TIME_SAMPLES;
        if (waveOutGetPosition(wo, &t, sizeof t) != MMSYSERR_NOERROR || t.wType != TIME_SAMPLES) return 0.5f;
        uint64_t pos = t.u.sample;
        for (auto& b : bufs) {
            if (pos < b->start || pos >= b->start + b->pcm.size()) continue;
            size_t i0 = (size_t)(pos - b->start), i1 = std::min(b->pcm.size(), i0 + 720);
            double acc = 0;
            for (size_t i = i0; i < i1; i++) acc += (double)b->pcm[i] * b->pcm[i];
            double rms = i1 > i0 ? std::sqrt(acc / (i1 - i0)) / 32768.0 : 0;
            return (float)std::min(1.0, rms * 5.0);
        }
        return 0.0f;
    }
    void close() {
        reset();
        std::lock_guard<std::mutex> lk(mu);
        if (wo) waveOutClose(wo);
        wo = nullptr;
    }
};

Speaker::Speaker() : player_(new Player) {}
Speaker::~Speaker() { shutdown(); }

void Speaker::start() {
    if (started_) return;
    started_ = true;
    nquit_ = false;
    th_ = std::thread([this] { run(); });
    nth_ = std::thread([this] { neural_run(); });
}

void Speaker::shutdown() {
    if (!started_) return;
    push({Cmd::Quit});
    if (th_.joinable()) th_.join();
    nquit_ = true;
    ncv_.notify_all();
    if (nth_.joinable()) nth_.join();
    prep_cancel_ = true;
    if (prep_th_.joinable()) prep_th_.join();
    started_ = false;
}

bool Speaker::neural() const { return neural_on_; }

VoiceSettings voice_settings(const Config& c) {
    VoiceSettings s;
    s.engine = c.voice_engine;
    s.lang = c.turkish() ? "tr" : "en";
    s.sapi_hint = c.voice;
    s.piper_voice = c.piper_voice;
    s.edge_voice = c.edge_voice;
    s.rate = c.voice_rate;
    s.volume = c.voice_volume;
    return s;
}

void Speaker::refresh_engine() {
    VoiceSettings s;
    {
        std::lock_guard<std::mutex> lk(nmu_);
        s = vs_;
    }
    prepare_engine(s, true);
}

void Speaker::prepare_engine(const VoiceSettings& s, bool recheck) {
    prep_cancel_ = true;
    if (prep_th_.joinable()) prep_th_.join();
    prep_cancel_ = false;
    progress_ = -1.0f;
    if (s.engine == "sapi") {
        engine_ready_ = true;
        return;
    }
    std::string voice = pick_neural_voice(s.engine, s.engine == "edge" ? s.edge_voice : s.piper_voice, s.lang);
    {
        std::lock_guard<std::mutex> lk(mu_);
        neural_label_ = neural_label(s.engine, voice);
    }
    if (s.engine == "piper" && piper_ready(voice)) {
        engine_ready_ = true;
        return;
    }
    engine_ready_ = false;
    prep_th_ = std::thread([this, s, voice, recheck] {
        if (s.engine == "piper") {
            progress_ = 0.0f;
            std::string err;
            bool ok = piper_install(voice, &progress_, &prep_cancel_, &err);
            progress_ = -1.0f;
            if (!ok) log("piper install: " + err);
            engine_ready_ = ok;
        } else {
            bool ok = edge_available(recheck);
            if (!ok) log("edge-tts is not installed; using the Windows voice");
            engine_ready_ = ok;
        }
    });
}

void Speaker::neural_run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    PiperEngine piper;
    EdgeEngine edge;
    std::string fallback_wav = app_dir() + "\\tts_fallback_" + std::to_string(GetCurrentProcessId()) + ".wav";
    auto drop = [this](int n) {
        while (n-- > 0) {
            int v = npending_.load();
            while (v > 0 && !npending_.compare_exchange_weak(v, v - 1)) {
            }
        }
    };
    for (;;) {
        std::pair<std::string, unsigned> item;
        VoiceSettings vs;
        {
            std::unique_lock<std::mutex> lk(nmu_);
            ncv_.wait_for(lk, std::chrono::milliseconds(30), [&] { return nquit_ || !nq_.empty(); });
            if (nquit_) break;
            if (nq_.empty()) {
                lk.unlock();
                drop(player_->reap());
                continue;
            }
            item = nq_.front();
            nq_.pop_front();
            vs = vs_;
        }
        drop(player_->reap());
        if (item.second != gen_) continue;
        Pcm pcm;
        std::string err;
        bool ok = false;
        if (engine_ready_ && vs.engine == "piper")
            ok = piper.synth(item.first, pick_neural_voice("piper", vs.piper_voice, vs.lang), 1.0 - 0.04 * vs.rate, pcm, &err);
        else if (engine_ready_ && vs.engine == "edge")
            ok = edge.synth(item.first, pick_neural_voice("edge", vs.edge_voice, vs.lang), vs.rate * 5, pcm, &err);
        if (!ok) {
            if (!err.empty()) log("neural voice: " + err);
            ok = synth_to_wav(item.first, fallback_wav, vs.lang, vs.sapi_hint, vs.rate) > 0 && read_wav(fallback_wav, pcm);
            DeleteFileW(widen(fallback_wav).c_str());
        }
        if (item.second != gen_) continue;
        if (!ok) {
            drop(1);
            continue;
        }
        resample(pcm, Player::kRate);
        if (vs.volume < 100) {
            double g = std::max(0, vs.volume) / 100.0;
            for (auto& x : pcm.samples) x = (int16_t)(x * g);
        }
        if (!player_->play(std::move(pcm.samples), item.second, gen_)) {
            if (item.second == gen_) drop(1);
        }
    }
    player_->close();
    piper.close();
    edge.close();
    CoUninitialize();
}

void Speaker::push(Cmd c) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (c.kind == Cmd::Stop) q_.clear();
        q_.push_back(std::move(c));
    }
    cv_.notify_one();
}

void Speaker::configure(const VoiceSettings& s) {
    Cmd c{Cmd::Configure};
    c.lang = s.lang;
    c.hint = s.sapi_hint;
    c.rate = s.rate;
    c.volume = s.volume;
    push(c);
    bool changed;
    {
        std::lock_guard<std::mutex> lk(nmu_);
        changed = vs_.engine != s.engine || vs_.lang != s.lang || vs_.piper_voice != s.piper_voice ||
                  vs_.edge_voice != s.edge_voice || !started_once_;
        vs_ = s;
    }
    started_once_ = true;
    neural_on_ = s.engine != "sapi";
    if (changed) prepare_engine(s);
}

namespace {
// Sentences, short ones merged, so the first one plays while the rest is synthesized.
std::vector<std::string> sentences(const std::string& text) {
    std::vector<std::string> raw;
    std::string cur;
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        cur += c;
        bool end = (c == '.' || c == '!' || c == '?' || c == ';' || c == ':') && (i + 1 == text.size() || text[i + 1] == ' ');
        if (end) {
            raw.push_back(trim(cur));
            cur.clear();
        }
    }
    if (!trim(cur).empty()) raw.push_back(trim(cur));
    std::vector<std::string> out;
    for (auto& s : raw) {
        if (s.empty()) continue;
        if (!out.empty() && (out.back().size() < 40 || s.size() < 12) && out.back().size() + s.size() < 260)
            out.back() += " " + s;
        else
            out.push_back(s);
    }
    return out;
}
}  // namespace

void Speaker::speak(const std::string& text) {
    std::string t = speakable(text);
    if (t.empty()) return;
    if (neural()) {
        auto parts = sentences(t);
        {
            std::lock_guard<std::mutex> lk(nmu_);
            for (auto& p : parts) {
                npending_++;
                nq_.push_back({p, gen_.load()});
            }
        }
        ncv_.notify_one();
        return;
    }
    pending_++;
    speaking_ = true;
    Cmd c{Cmd::Speak};
    c.text = t;
    push(c);
}

void Speaker::stop() {
    pending_ = 0;
    push({Cmd::Stop});
    gen_++;
    {
        std::lock_guard<std::mutex> lk(nmu_);
        nq_.clear();
    }
    player_->reset();
    npending_ = 0;
}

bool Speaker::speaking() const {
    if (npending_ > 0 || player_->busy()) return true;
    return speaking_ || pending_ > 0;
}

float Speaker::level() const {
    if (npending_ > 0 || player_->busy()) return player_->level();
    if (!speaking()) return 0.0f;
    double t = now_seconds();
    double since = t - last_viseme_.load();
    if (since < 0.35) return (float)(viseme_level_.load() * std::exp(-since * 6.0));
    // no viseme events from this voice: a lively synthetic envelope
    return (float)(0.45 + 0.25 * std::sin(t * 13.0) * std::sin(t * 3.1) + 0.12 * std::sin(t * 29.0));
}

std::string Speaker::voice_name() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (neural_on_ && engine_ready_ && !neural_label_.empty()) return neural_label_;
    return voice_name_;
}

void Speaker::run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ISpVoice* voice = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(SpVoice), nullptr, CLSCTX_ALL, __uuidof(ISpVoice), (void**)&voice))) {
        log("SAPI: cannot create a voice");
        voice = nullptr;
    }
    if (voice) {
        ULONGLONG interest = SPFEI(SPEI_VISEME) | SPFEI(SPEI_START_INPUT_STREAM) | SPFEI(SPEI_END_INPUT_STREAM);
        voice->SetInterest(interest, interest);
        voice->SetNotifyWin32Event();
    }
    int in_flight = 0;
    for (;;) {
        std::deque<Cmd> cmds;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait_for(lk, std::chrono::milliseconds(15), [&] { return !q_.empty(); });
            cmds.swap(q_);
        }
        bool quit = false;
        for (auto& c : cmds) {
            if (c.kind == Cmd::Quit) {
                quit = true;
                break;
            }
            if (!voice) {
                if (c.kind == Cmd::Speak) pending_--;
                continue;
            }
            if (c.kind == Cmd::Configure) {
                std::string name;
                ISpObjectToken* t = find_token(c.lang, c.hint, &name);
                if (t) {
                    voice->SetVoice(t);
                    t->Release();
                    std::lock_guard<std::mutex> lk(mu_);
                    voice_name_ = name;
                }
                voice->SetRate(std::max(-10, std::min(10, c.rate)));
                voice->SetVolume((USHORT)std::max(0, std::min(100, c.volume)));
            } else if (c.kind == Cmd::Speak) {
                std::wstring w = widen(c.text);
                if (SUCCEEDED(voice->Speak(w.c_str(), SPF_ASYNC | SPF_IS_NOT_XML, nullptr))) in_flight++;
                else pending_--;
            } else if (c.kind == Cmd::Stop) {
                voice->Speak(nullptr, SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
                in_flight = 0;
                pending_ = 0;
            }
        }
        if (quit) break;
        if (voice) {
            SPEVENT ev;
            ULONG n = 0;
            while (voice->GetEvents(1, &ev, &n) == S_OK && n == 1) {
                if (ev.eEventId == SPEI_VISEME) {
                    int cur = LOWORD(ev.lParam);
                    viseme_level_ = viseme_open(cur);
                    last_viseme_ = now_seconds();
                } else if (ev.eEventId == SPEI_END_INPUT_STREAM) {
                    if (in_flight > 0) in_flight--;
                    if (pending_ > 0) pending_--;
                }
                clear_event(ev);
            }
            SPVOICESTATUS st{};
            if (SUCCEEDED(voice->GetStatus(&st, nullptr)))
                speaking_ = st.dwRunningState == SPRS_IS_SPEAKING || in_flight > 0 || pending_ > 0;
            if (in_flight == 0 && pending_ <= 0 && st.dwRunningState != SPRS_IS_SPEAKING) {
                pending_ = 0;
                speaking_ = false;
            }
        } else {
            speaking_ = false;
        }
    }
    if (voice) {
        voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
        voice->Release();
    }
    CoUninitialize();
}

std::vector<VoiceInfo> Speaker::voices() {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::vector<VoiceInfo> out;
    each_token([&](const VoiceInfo& v, ISpObjectToken*) {
        bool dup = false;
        for (auto& o : out) dup = dup || o.name == v.name;
        if (!dup) out.push_back(v);
        return false;
    });
    if (SUCCEEDED(co)) CoUninitialize();
    return out;
}

double Speaker::synth_to_wav(const std::string& text, const std::string& path, const std::string& lang,
                             const std::string& hint, int rate) {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    double secs = -1;
    ISpVoice* voice = nullptr;
    ISpStream* stream = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(SpVoice), nullptr, CLSCTX_ALL, __uuidof(ISpVoice), (void**)&voice)) &&
        SUCCEEDED(CoCreateInstance(__uuidof(SpStream), nullptr, CLSCTX_ALL, __uuidof(ISpStream), (void**)&stream))) {
        ISpObjectToken* t = find_token(lang, hint, nullptr);
        if (t) {
            voice->SetVoice(t);
            t->Release();
        }
        voice->SetRate(rate);
        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = 1;
        wf.nSamplesPerSec = 22050;
        wf.wBitsPerSample = 16;
        wf.nBlockAlign = 2;
        wf.nAvgBytesPerSec = 44100;
        if (SUCCEEDED(stream->BindToFile(widen(path).c_str(), SPFM_CREATE_ALWAYS, &kWaveFormatEx, &wf, 0)) &&
            SUCCEEDED(voice->SetOutput(stream, TRUE))) {
            voice->Speak(widen(speakable(text)).c_str(), SPF_DEFAULT | SPF_IS_NOT_XML, nullptr);
            voice->WaitUntilDone(INFINITE);
            stream->Close();
            int64_t sz = file_size(path);
            if (sz > 44) secs = (sz - 44) / 44100.0;
        }
    }
    if (stream) stream->Release();
    if (voice) voice->Release();
    if (SUCCEEDED(co)) CoUninitialize();
    return secs;
}

std::string Speaker::speakable(const std::string& text) {
    std::wstring w = widen(text), out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); i++) {
        wchar_t c = w[i];
        // links
        if ((c == L'h' && w.compare(i, 7, L"http://") == 0) || (c == L'h' && w.compare(i, 8, L"https://") == 0)) {
            while (i < w.size() && !iswspace(w[i])) i++;
            continue;
        }
        if (c == L'*' || c == L'#' || c == L'`' || c == L'_' || c == L'|' || c == L'>' || c == L'<') continue;
        if (c == L'\n' || c == L'\r' || c == L'\t') {
            if (!out.empty() && out.back() != L' ') {
                if (out.back() != L'.' && out.back() != L':' && out.back() != L',' && c == L'\n') out += L'.';
                out += L' ';
            }
            continue;
        }
        // emoji and pictographs (surrogate pairs and the symbol blocks)
        if ((c >= 0xD800 && c <= 0xDFFF) || (c >= 0x2600 && c <= 0x27BF) || c == 0xFE0F) continue;
        out += c;
    }
    std::string s = narrow(out);
    s = replace_all(s, " - ", ", ");
    while (contains(s, "  ")) s = replace_all(s, "  ", " ");
    while (contains(s, ". .")) s = replace_all(s, ". .", ".");
    return trim(s);
}

}  // namespace kayra

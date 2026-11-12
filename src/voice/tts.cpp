#include "voice/tts.h"

#include <windows.h>
#include <sapi.h>

#include <cmath>

#include "core/util.h"

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

Speaker::Speaker() = default;
Speaker::~Speaker() { shutdown(); }

void Speaker::start() {
    if (started_) return;
    started_ = true;
    th_ = std::thread([this] { run(); });
}

void Speaker::shutdown() {
    if (!started_) return;
    push({Cmd::Quit});
    if (th_.joinable()) th_.join();
    started_ = false;
}

void Speaker::push(Cmd c) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (c.kind == Cmd::Stop) q_.clear();
        q_.push_back(std::move(c));
    }
    cv_.notify_one();
}

void Speaker::configure(const std::string& lang, const std::string& hint, int rate, int volume) {
    Cmd c{Cmd::Configure};
    c.lang = lang;
    c.hint = hint;
    c.rate = rate;
    c.volume = volume;
    push(c);
}

void Speaker::speak(const std::string& text) {
    std::string t = speakable(text);
    if (t.empty()) return;
    pending_++;
    speaking_ = true;
    Cmd c{Cmd::Speak};
    c.text = t;
    push(c);
}

void Speaker::stop() {
    pending_ = 0;
    push({Cmd::Stop});
}

bool Speaker::speaking() const { return speaking_ || pending_ > 0; }

float Speaker::level() const {
    if (!speaking()) return 0.0f;
    double t = now_seconds();
    double since = t - last_viseme_.load();
    if (since < 0.35) return (float)(viseme_level_.load() * std::exp(-since * 6.0));
    // no viseme events from this voice: a lively synthetic envelope
    return (float)(0.45 + 0.25 * std::sin(t * 13.0) * std::sin(t * 3.1) + 0.12 * std::sin(t * 29.0));
}

std::string Speaker::voice_name() const {
    std::lock_guard<std::mutex> lk(mu_);
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

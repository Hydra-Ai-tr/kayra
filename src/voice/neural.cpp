#include "voice/neural.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <nlohmann/json.hpp>

#include "core/http.h"
#include "core/util.h"

namespace kayra {

// ------------------------------------------------------------------ voices

const std::vector<NeuralVoice>& piper_voices() {
    static const std::vector<NeuralVoice> v = {
        {"tr_TR-fahrettin-medium", "tr", "Fahrettin"},
        {"tr_TR-fettah-medium", "tr", "Fettah"},
        {"tr_TR-dfki-medium", "tr", "DFKI"},
        {"en_GB-alan-medium", "en", "Alan (British)"},
        {"en_GB-northern_english_male-medium", "en", "Northern English (British)"},
        {"en_GB-jenny_dioco-medium", "en", "Jenny (British)"},
        {"en_US-ryan-high", "en", "Ryan (American)"},
        {"en_US-lessac-high", "en", "Lessac (American)"},
    };
    return v;
}

const std::vector<NeuralVoice>& edge_voices() {
    static const std::vector<NeuralVoice> v = {
        {"tr-TR-AhmetNeural", "tr", "Ahmet"},
        {"tr-TR-EmelNeural", "tr", "Emel"},
        {"en-GB-RyanNeural", "en", "Ryan (British)"},
        {"en-GB-ThomasNeural", "en", "Thomas (British)"},
        {"en-GB-SoniaNeural", "en", "Sonia (British)"},
        {"en-US-AndrewNeural", "en", "Andrew (American)"},
        {"en-US-BrianNeural", "en", "Brian (American)"},
        {"en-US-AvaNeural", "en", "Ava (American)"},
    };
    return v;
}

std::string pick_neural_voice(const std::string& engine, const std::string& wanted, const std::string& lang) {
    const auto& list = engine == "edge" ? edge_voices() : piper_voices();
    for (auto& v : list)
        if (wanted == v.id && lang == v.lang) return v.id;
    for (auto& v : list)
        if (lang == v.lang) return v.id;
    return list.front().id;
}

std::string neural_label(const std::string& engine, const std::string& id) {
    for (auto& v : engine == "edge" ? edge_voices() : piper_voices())
        if (id == v.id) return v.label;
    return id;
}

// ------------------------------------------------------------------ audio

namespace {

uint32_t u32(const char* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
uint16_t u16(const char* p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

std::once_flag g_mf_once;

bool decode_audio(const std::string& path, Pcm& out) {
    std::call_once(g_mf_once, [] { MFStartup(MF_VERSION, MFSTARTUP_LITE); });
    IMFSourceReader* r = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(widen(path).c_str(), nullptr, &r))) return false;
    IMFMediaType* t = nullptr;
    MFCreateMediaType(&t);
    t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    t->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    HRESULT hr = r->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, t);
    t->Release();
    UINT32 ch = 1, sr = 24000;
    IMFMediaType* cur = nullptr;
    if (SUCCEEDED(hr) && SUCCEEDED(r->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &cur))) {
        cur->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
        cur->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
        cur->Release();
    }
    out.samples.clear();
    out.rate = (int)sr;
    while (SUCCEEDED(hr)) {
        DWORD flags = 0;
        IMFSample* s = nullptr;
        hr = r->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &s);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            if (s) s->Release();
            break;
        }
        if (!s) continue;
        IMFMediaBuffer* b = nullptr;
        if (SUCCEEDED(s->ConvertToContiguousBuffer(&b))) {
            BYTE* data = nullptr;
            DWORD len = 0;
            if (SUCCEEDED(b->Lock(&data, nullptr, &len))) {
                const int16_t* p = (const int16_t*)data;
                size_t frames = len / 2 / (ch ? ch : 1);
                for (size_t i = 0; i < frames; i++) {
                    int acc = 0;
                    for (UINT32 c = 0; c < ch; c++) acc += p[i * ch + c];
                    out.samples.push_back((int16_t)(acc / (int)(ch ? ch : 1)));
                }
                b->Unlock();
            }
            b->Release();
        }
        s->Release();
    }
    r->Release();
    return !out.samples.empty();
}

// A hidden child process with pipes to its stdin and stdout.
struct Child {
    HANDLE proc = nullptr, in_w = nullptr, out_r = nullptr;
    std::string buf;

    bool start(const std::wstring& cmdline, const std::wstring& cwd) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE in_r = nullptr, out_w = nullptr;
        if (!CreatePipe(&in_r, &in_w, &sa, 0)) return false;
        if (!CreatePipe(&out_r, &out_w, &sa, 0)) {
            CloseHandle(in_r);
            return false;
        }
        SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
        HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = in_r;
        si.hStdOutput = out_w;
        si.hStdError = nul;
        PROCESS_INFORMATION pi{};
        std::wstring cl = cmdline;
        BOOL ok = CreateProcessW(nullptr, &cl[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                 cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
        CloseHandle(in_r);
        CloseHandle(out_w);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        if (!ok) {
            close();
            return false;
        }
        CloseHandle(pi.hThread);
        proc = pi.hProcess;
        return true;
    }
    bool alive() const { return proc && WaitForSingleObject(proc, 0) == WAIT_TIMEOUT; }
    bool write(const std::string& s) {
        DWORD n = 0;
        return in_w && WriteFile(in_w, s.data(), (DWORD)s.size(), &n, nullptr) && n == s.size();
    }
    bool read_line(std::string& line, int timeout_ms) {
        double end = now_seconds() + timeout_ms / 1000.0;
        for (;;) {
            size_t nl = buf.find('\n');
            if (nl != std::string::npos) {
                line = trim(buf.substr(0, nl));
                buf.erase(0, nl + 1);
                return true;
            }
            DWORD avail = 0;
            if (!PeekNamedPipe(out_r, nullptr, 0, nullptr, &avail, nullptr)) return false;
            if (avail) {
                char tmp[4096];
                DWORD got = 0;
                if (!ReadFile(out_r, tmp, std::min<DWORD>(avail, sizeof tmp), &got, nullptr)) return false;
                buf.append(tmp, got);
                continue;
            }
            if (!alive() || now_seconds() > end) return false;
            Sleep(5);
        }
    }
    void close() {
        if (in_w) CloseHandle(in_w);
        if (proc) {
            if (WaitForSingleObject(proc, 300) == WAIT_TIMEOUT) TerminateProcess(proc, 0);
            CloseHandle(proc);
        }
        if (out_r) CloseHandle(out_r);
        proc = in_w = out_r = nullptr;
        buf.clear();
    }
    ~Child() { close(); }
};

std::wstring quote(const std::string& s) { return L"\"" + widen(s) + L"\""; }

std::string one_line(const std::string& text) {
    std::string s = text;
    for (auto& c : s)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    return trim(s);
}

// Runs a command hidden and waits; returns the exit code (-1 if it could not start or timed out).
int run_wait(const std::wstring& cmdline, int timeout_ms, std::string* output = nullptr) {
    Child c;
    if (!c.start(cmdline, L"")) return -1;
    c.write("");
    CloseHandle(c.in_w);
    c.in_w = nullptr;
    double end = now_seconds() + timeout_ms / 1000.0;
    std::string line;
    while (now_seconds() < end) {
        if (c.read_line(line, 200)) {
            if (output) *output += line + "\n";
            continue;
        }
        if (!c.alive()) break;
    }
    if (c.alive()) return -1;
    DWORD code = 1;
    GetExitCodeProcess(c.proc, &code);
    return (int)code;
}

std::string piper_dir() { return app_dir() + "\\piper"; }
std::string piper_exe() { return piper_dir() + "\\piper\\piper.exe"; }
std::string piper_model(const std::string& voice) { return piper_dir() + "\\voices\\" + voice + ".onnx"; }

}  // namespace

bool read_wav(const std::string& path, Pcm& out) {
    std::string d;
    if (!read_file(path, d) || d.size() < 44 || d.compare(0, 4, "RIFF") != 0 || d.compare(8, 4, "WAVE") != 0) return false;
    size_t p = 12;
    int ch = 1, bits = 16;
    out.samples.clear();
    while (p + 8 <= d.size()) {
        std::string id = d.substr(p, 4);
        uint32_t len = u32(&d[p + 4]);
        size_t body = p + 8;
        if (id == "fmt " && body + 16 <= d.size()) {
            ch = u16(&d[body + 2]);
            out.rate = (int)u32(&d[body + 4]);
            bits = u16(&d[body + 14]);
        } else if (id == "data") {
            if (bits != 16 || ch < 1) return false;
            size_t n = std::min<size_t>(len, d.size() - body) / 2 / ch;
            out.samples.resize(n);
            for (size_t i = 0; i < n; i++) {
                int acc = 0;
                for (int c = 0; c < ch; c++) acc += (int16_t)u16(&d[body + (i * ch + c) * 2]);
                out.samples[i] = (int16_t)(acc / ch);
            }
            return true;
        }
        p = body + len + (len & 1);
    }
    return false;
}

void resample(Pcm& p, int rate) {
    if (p.rate == rate || p.samples.empty()) {
        p.rate = rate;
        return;
    }
    size_t n = (size_t)((double)p.samples.size() * rate / p.rate);
    std::vector<int16_t> o(n);
    double step = (double)p.rate / rate;
    for (size_t i = 0; i < n; i++) {
        double x = i * step;
        size_t k = (size_t)x;
        double f = x - k;
        int a = p.samples[std::min(k, p.samples.size() - 1)];
        int b = p.samples[std::min(k + 1, p.samples.size() - 1)];
        o[i] = (int16_t)(a + (b - a) * f);
    }
    p.samples.swap(o);
    p.rate = rate;
}

// ------------------------------------------------------------------ Piper

bool piper_ready(const std::string& voice) {
    return file_exists(piper_exe()) && file_exists(piper_model(voice)) && file_exists(piper_model(voice) + ".json");
}

bool piper_install(const std::string& voice, std::atomic<float>* progress, const std::atomic<bool>* cancel,
                   std::string* error) {
    std::string dir = piper_dir();
    CreateDirectoryW(widen(dir).c_str(), nullptr);
    CreateDirectoryW(widen(dir + "\\voices").c_str(), nullptr);
    auto set = [&](float v) {
        if (progress) *progress = v;
    };
    if (!file_exists(piper_exe())) {
        std::string zip = dir + "\\piper.zip";
        bool ok = http_download("https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_windows_amd64.zip", zip,
                                [&](int64_t got, int64_t total) { set(total > 0 ? 0.25f * got / total : 0.05f); }, cancel, error);
        if (!ok) return false;
        // tar.exe ships with Windows 10 and reads zip files
        int code = run_wait(L"tar.exe -xf " + quote(zip) + L" -C " + quote(dir), 120000);
        DeleteFileW(widen(zip).c_str());
        if (code != 0 || !file_exists(piper_exe())) {
            if (error) *error = "could not unpack Piper";
            return false;
        }
    }
    // "tr_TR-fahrettin-medium" -> tr/tr_TR/fahrettin/medium/tr_TR-fahrettin-medium.onnx
    size_t a = voice.find('-'), b = voice.rfind('-');
    if (a == std::string::npos || b == a) {
        if (error) *error = "unknown voice " + voice;
        return false;
    }
    std::string locale = voice.substr(0, a), name = voice.substr(a + 1, b - a - 1), quality = voice.substr(b + 1);
    std::string url = "https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/" + locale.substr(0, 2) + "/" + locale +
                      "/" + name + "/" + quality + "/" + voice + ".onnx";
    std::string model = piper_model(voice);
    if (!file_exists(model + ".json") && !http_download(url + ".json", model + ".json", nullptr, cancel, error)) return false;
    if (!file_exists(model) &&
        !http_download(url, model, [&](int64_t got, int64_t total) { set(0.25f + (total > 0 ? 0.75f * got / total : 0.1f)); },
                       cancel, error))
        return false;
    set(1.0f);
    return true;
}

struct PiperEngine::Proc {
    Child child;
};

PiperEngine::PiperEngine() = default;
PiperEngine::~PiperEngine() { close(); }

void PiperEngine::close() { proc_.reset(); }

bool PiperEngine::synth(const std::string& text, const std::string& voice, double scale, Pcm& out, std::string* error) {
    if (!piper_ready(voice)) {
        if (error) *error = "piper voice not installed";
        return false;
    }
    if (!proc_ || !proc_->child.alive() || voice != voice_ || std::abs(scale - scale_) > 1e-3) {
        proc_.reset(new Proc);
        std::string outdir = piper_dir() + "\\out";
        CreateDirectoryW(widen(outdir).c_str(), nullptr);
        char ls[32];
        snprintf(ls, sizeof ls, "%.2f", scale);
        std::wstring cmd = quote(piper_exe()) + L" --model " + quote(piper_model(voice)) + L" --output_dir " + quote(outdir) +
                           L" --length_scale " + widen(ls) + L" --sentence_silence 0.12";
        if (!proc_->child.start(cmd, widen(piper_dir() + "\\piper"))) {
            proc_.reset();
            if (error) *error = "cannot start piper";
            return false;
        }
        voice_ = voice;
        scale_ = scale;
    }
    std::string line;
    if (!proc_->child.write(one_line(text) + "\n") || !proc_->child.read_line(line, 30000)) {
        proc_.reset();
        if (error) *error = "piper did not answer";
        return false;
    }
    bool ok = read_wav(line, out);
    DeleteFileW(widen(line).c_str());
    if (!ok && error) *error = "piper wrote no audio";
    return ok;
}

// ------------------------------------------------------------------ Edge

namespace {

const char* kEdgeHelper = R"PY(import sys, json, asyncio
import edge_tts

async def main():
    loop = asyncio.get_running_loop()
    while True:
        line = await loop.run_in_executor(None, sys.stdin.buffer.readline)
        if not line:
            break
        try:
            r = json.loads(line.decode("utf-8"))
            c = edge_tts.Communicate(r["text"], r["voice"], rate=r.get("rate", "+0%"))
            await c.save(r["out"])
            print("ok", flush=True)
        except Exception as e:
            print("err " + str(e).replace("\n", " "), flush=True)

asyncio.run(main())
)PY";

std::mutex g_py_mu;
std::string g_python;           // found interpreter, "" = none
std::atomic<int> g_edge_ok{-1};  // -1 unknown, 0 no, 1 yes

std::string find_python() {
    for (const wchar_t* exe : {L"python.exe", L"py.exe"}) {
        wchar_t buf[MAX_PATH];
        if (SearchPathW(nullptr, exe, nullptr, MAX_PATH, buf, nullptr) == 0) continue;
        std::string p = narrow(buf);
        if (contains(lower(p), "windowsapps")) continue;  // the Microsoft Store stub
        return p;
    }
    return "";
}

}  // namespace

bool edge_available(bool recheck) {
    if (!recheck && g_edge_ok >= 0) return g_edge_ok == 1;
    std::lock_guard<std::mutex> lk(g_py_mu);
    g_python = find_python();
    bool ok = !g_python.empty() && run_wait(quote(g_python) + L" -c \"import edge_tts\"", 20000) == 0;
    g_edge_ok = ok ? 1 : 0;
    return ok;
}

bool edge_install(std::string* log) {
    std::string py;
    {
        std::lock_guard<std::mutex> lk(g_py_mu);
        if (g_python.empty()) g_python = find_python();
        py = g_python;
    }
    if (py.empty()) {
        if (log) *log = "Python not found (python.org)";
        return false;
    }
    int code = run_wait(quote(py) + L" -m pip install --user --upgrade edge-tts", 180000, log);
    return code == 0 && edge_available(true);
}

struct EdgeEngine::Proc {
    Child child;
};

EdgeEngine::EdgeEngine() = default;
EdgeEngine::~EdgeEngine() { close(); }

void EdgeEngine::close() { proc_.reset(); }

bool EdgeEngine::synth(const std::string& text, const std::string& voice, int rate_percent, Pcm& out, std::string* error) {
    if (!edge_available()) {
        if (error) *error = "edge-tts is not installed";
        return false;
    }
    if (!proc_ || !proc_->child.alive()) {
        std::string helper = app_dir() + "\\edge_tts_helper.py";
        write_file(helper, kEdgeHelper);
        proc_.reset(new Proc);
        SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
        std::string py;
        {
            std::lock_guard<std::mutex> lk(g_py_mu);
            py = g_python;
        }
        if (!proc_->child.start(quote(py) + L" -u " + quote(helper), L"")) {
            proc_.reset();
            if (error) *error = "cannot start python";
            return false;
        }
    }
    static std::atomic<int> counter{0};
    std::string mp3 = app_dir() + "\\edge_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(counter++ % 4) + ".mp3";
    char rate[16];
    snprintf(rate, sizeof rate, "%+d%%", std::max(-50, std::min(50, rate_percent)));
    nlohmann::json req = {{"text", one_line(text)}, {"voice", voice}, {"rate", rate}, {"out", mp3}};
    std::string line;
    if (!proc_->child.write(req.dump() + "\n") || !proc_->child.read_line(line, 20000)) {
        proc_.reset();
        if (error) *error = "edge-tts did not answer";
        return false;
    }
    if (line != "ok") {
        if (error) *error = line;
        return false;
    }
    bool ok = decode_audio(mp3, out);
    DeleteFileW(widen(mp3).c_str());
    if (!ok && error) *error = "could not decode the audio";
    return ok;
}

}  // namespace kayra

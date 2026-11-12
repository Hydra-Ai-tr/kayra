#include "voice/mic.h"

#include <windows.h>
#include <mmsystem.h>

#include "core/util.h"

namespace kayra {

namespace {
constexpr int kBuffers = 10;
constexpr int kBufSamples = Mic::kFrame * 2;  // 40 ms per buffer
}  // namespace

Mic::~Mic() { stop(); }

std::vector<std::string> Mic::devices() {
    std::vector<std::string> out;
    UINT n = waveInGetNumDevs();
    for (UINT i = 0; i < n; i++) {
        WAVEINCAPSW caps{};
        if (waveInGetDevCapsW(i, &caps, sizeof caps) == MMSYSERR_NOERROR) out.push_back(narrow(caps.szPname));
        else out.push_back("Mic " + std::to_string(i));
    }
    return out;
}

bool Mic::start(int device, std::function<void(const int16_t*, size_t)> on_frame, std::string* error) {
    stop();
    on_frame_ = std::move(on_frame);
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 1;
    wf.nSamplesPerSec = kRate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 2;
    wf.nAvgBytesPerSec = kRate * 2;
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HWAVEIN h = nullptr;
    UINT dev = device < 0 ? WAVE_MAPPER : (UINT)device;
    MMRESULT r = waveInOpen(&h, dev, &wf, (DWORD_PTR)event_, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR) {
        if (error) {
            wchar_t msg[256];
            waveInGetErrorTextW(r, msg, 256);
            *error = narrow(msg);
        }
        CloseHandle(event_);
        event_ = nullptr;
        return false;
    }
    handle_ = h;
    bufs_.assign(kBuffers, std::vector<int16_t>(kBufSamples));
    headers_.assign(sizeof(WAVEHDR) * kBuffers, 0);
    auto* hdr = (WAVEHDR*)headers_.data();
    for (int i = 0; i < kBuffers; i++) {
        hdr[i].lpData = (LPSTR)bufs_[i].data();
        hdr[i].dwBufferLength = kBufSamples * 2;
        waveInPrepareHeader(h, &hdr[i], sizeof(WAVEHDR));
        waveInAddBuffer(h, &hdr[i], sizeof(WAVEHDR));
    }
    running_ = true;
    waveInStart(h);
    th_ = std::thread([this] { run(); });
    return true;
}

void Mic::run() {
    auto* hdr = (WAVEHDR*)headers_.data();
    auto h = (HWAVEIN)handle_;
    while (running_) {
        WaitForSingleObject((HANDLE)event_, 100);
        for (int i = 0; i < kBuffers && running_; i++) {
            if (!(hdr[i].dwFlags & WHDR_DONE)) continue;
            size_t n = hdr[i].dwBytesRecorded / 2;
            for (size_t off = 0; off + kFrame <= n; off += kFrame)
                if (on_frame_) on_frame_(bufs_[i].data() + off, kFrame);
            hdr[i].dwFlags &= ~WHDR_DONE;
            hdr[i].dwBytesRecorded = 0;
            if (running_) waveInAddBuffer(h, &hdr[i], sizeof(WAVEHDR));
        }
    }
}

void Mic::stop() {
    if (!handle_) return;
    running_ = false;
    if (event_) SetEvent((HANDLE)event_);
    if (th_.joinable()) th_.join();
    auto h = (HWAVEIN)handle_;
    waveInStop(h);
    waveInReset(h);
    auto* hdr = (WAVEHDR*)headers_.data();
    for (int i = 0; i < kBuffers; i++) waveInUnprepareHeader(h, &hdr[i], sizeof(WAVEHDR));
    waveInClose(h);
    handle_ = nullptr;
    if (event_) CloseHandle((HANDLE)event_);
    event_ = nullptr;
}

}  // namespace kayra

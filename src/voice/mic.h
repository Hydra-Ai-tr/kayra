// Microphone capture with waveIn: 16 kHz, mono, 16-bit, delivered in 20 ms frames.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace kayra {

class Mic {
public:
    static constexpr int kRate = 16000;
    static constexpr int kFrame = 320;  // 20 ms

    ~Mic();
    // device -1 = the default microphone
    bool start(int device, std::function<void(const int16_t*, size_t)> on_frame, std::string* error);
    void stop();
    bool running() const { return running_; }

    static std::vector<std::string> devices();

private:
    void run();
    void* handle_ = nullptr;
    void* event_ = nullptr;
    std::thread th_;
    std::atomic<bool> running_{false};
    std::function<void(const int16_t*, size_t)> on_frame_;
    std::vector<std::vector<int16_t>> bufs_;
    std::vector<uint8_t> headers_;
};

}  // namespace kayra

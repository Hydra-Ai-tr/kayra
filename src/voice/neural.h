// Neural voices: Piper (runs on this PC, downloaded on first use) and the
// Microsoft Edge voices through the edge-tts tool (online). Both give 16-bit
// mono PCM that the Speaker plays; the Windows voice is the fallback.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kayra {

struct NeuralVoice {
    const char* id;     // "tr_TR-fahrettin-medium", "tr-TR-AhmetNeural"
    const char* lang;   // "tr" / "en"
    const char* label;  // "Fahrettin"
};
const std::vector<NeuralVoice>& piper_voices();
const std::vector<NeuralVoice>& edge_voices();
// The voice to use: wanted when it suits lang, else the default for lang.
std::string pick_neural_voice(const std::string& engine, const std::string& wanted, const std::string& lang);
std::string neural_label(const std::string& engine, const std::string& id);

struct Pcm {
    std::vector<int16_t> samples;
    int rate = 24000;
};
bool read_wav(const std::string& path, Pcm& out);
void resample(Pcm& p, int rate);

// Piper ------------------------------------------------------------------
bool piper_ready(const std::string& voice);
// Downloads Piper and the voice into %APPDATA%\Kayra\piper; progress 0..1.
bool piper_install(const std::string& voice, std::atomic<float>* progress, const std::atomic<bool>* cancel,
                   std::string* error);

class PiperEngine {
public:
    PiperEngine();
    ~PiperEngine();
    bool synth(const std::string& text, const std::string& voice, double length_scale, Pcm& out, std::string* error);
    void close();

private:
    struct Proc;
    std::unique_ptr<Proc> proc_;
    std::string voice_;
    double scale_ = 0;
};

// Edge -------------------------------------------------------------------
// True when Python with the edge_tts package is installed (checked once, then cached).
bool edge_available(bool recheck = false);
bool edge_install(std::string* log);  // pip install edge-tts

class EdgeEngine {
public:
    EdgeEngine();
    ~EdgeEngine();
    // rate_percent: -50..50
    bool synth(const std::string& text, const std::string& voice, int rate_percent, Pcm& out, std::string* error);
    void close();

private:
    struct Proc;
    std::unique_ptr<Proc> proc_;
};

}  // namespace kayra

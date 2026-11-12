// Speech to text with whisper.cpp, fully on this computer. Models download
// once into %APPDATA%\Kayra\models.
#pragma once
#include <mutex>
#include <string>
#include <vector>

namespace kayra {

struct SttModel {
    const char* name;   // "base-q5_1"
    const char* label;  // shown in the settings
    int size_mb;
};

class Stt {
public:
    ~Stt();
    static bool available();  // built with whisper.cpp
    static const std::vector<SttModel>& models();
    static std::string model_path(const std::string& name);
    static std::string model_url(const std::string& name);

    bool load(const std::string& path, std::string* error);
    bool loaded() const;
    void unload();
    // 16 kHz mono samples in [-1, 1]; lang "tr", "en" or "auto"
    std::string transcribe(const std::vector<float>& pcm, const std::string& lang, const std::string& prompt);
    double last_seconds() const { return last_seconds_; }

private:
    mutable std::mutex mu_;
    void* ctx_ = nullptr;
    double last_seconds_ = 0;
};

// Whisper's well-known inventions on silence or noise ("Altyazı M.K.", "[Müzik]").
bool is_hallucination(const std::string& text);

}  // namespace kayra

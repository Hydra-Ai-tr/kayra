// Client for OpenAI-compatible chat APIs (OpenAI, Gemini, Groq, OpenRouter,
// Ollama, LM Studio, Kor Studio). Streams the answer and collects tool calls.
#pragma once
#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

#include "core/config.h"

namespace kayra {

struct LlmResult {
    bool ok = false;
    std::string content;
    nlohmann::json tool_calls = nlohmann::json::array();  // [{id, type, function{name, arguments}}]
    std::string error;
};

class LlmClient {
public:
    explicit LlmClient(const Config& cfg);
    bool configured() const;
    LlmResult chat(const nlohmann::json& messages, const nlohmann::json& tools,
                   const std::function<void(const std::string&)>& on_text, const std::atomic<bool>* cancel = nullptr);
    // A tiny request to check address, key and model; info gets the reply or the error.
    bool test(std::string& info);

private:
    std::string base_, key_, model_, provider_;
    double temperature_;
    bool tools_supported_ = true;
    LlmResult request(const nlohmann::json& body, bool stream, const std::function<void(const std::string&)>& on_text,
                      const std::atomic<bool>* cancel);
};

}  // namespace kayra

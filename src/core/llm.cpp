#include "core/llm.h"

#include <map>

#include "core/http.h"
#include "core/util.h"

using json = nlohmann::json;

namespace kayra {

LlmClient::LlmClient(const Config& cfg)
    : base_(cfg.base_url), key_(cfg.api_key), model_(cfg.model), provider_(cfg.provider), temperature_(cfg.temperature) {
    while (!base_.empty() && base_.back() == '/') base_.pop_back();
    tools_supported_ = cfg.use_tools;
}

bool LlmClient::configured() const { return provider_ != "none" && !base_.empty() && !model_.empty(); }

static std::string error_text(const HttpResponse& r) {
    if (r.status == 0) return r.error.empty() ? "no connection" : r.error;
    json j = json::parse(r.body, nullptr, false);
    std::string msg;
    if (j.is_object() && j.contains("error")) {
        auto& e = j["error"];
        if (e.is_string()) msg = e.get<std::string>();
        else if (e.is_object()) msg = e.value("message", e.dump());
    } else if (j.is_array() && !j.empty() && j[0].is_object() && j[0].contains("error")) {
        msg = j[0]["error"].value("message", "");
    }
    if (msg.empty()) msg = r.body.substr(0, 200);
    return "HTTP " + std::to_string(r.status) + ": " + msg;
}

LlmResult LlmClient::request(const json& body, bool stream, const std::function<void(const std::string&)>& on_text,
                             const std::atomic<bool>* cancel) {
    LlmResult res;
    std::vector<std::string> headers = {"Content-Type: application/json", "Accept: text/event-stream, application/json"};
    if (!key_.empty()) headers.push_back("Authorization: Bearer " + key_);
    if (provider_ == "openrouter") {
        headers.push_back("HTTP-Referer: https://github.com/Hydra-Ai-tr/kayra");
        headers.push_back("X-Title: Kayra");
    }
    std::string buf, raw;
    std::map<int, json> calls;
    bool saw_sse = false, done = false;
    auto handle_line = [&](const std::string& line) {
        if (!starts_with(line, "data:")) return;
        saw_sse = true;
        std::string data = trim(line.substr(5));
        if (data == "[DONE]") {
            done = true;
            return;
        }
        json j = json::parse(data, nullptr, false);
        if (!j.is_object()) return;
        if (j.contains("error")) {
            res.error = j["error"].is_object() ? j["error"].value("message", "error") : j["error"].dump();
            return;
        }
        if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) return;
        auto& ch = j["choices"][0];
        json delta = ch.contains("delta") ? ch["delta"] : ch.value("message", json::object());
        if (delta.contains("content") && delta["content"].is_string()) {
            std::string t = delta["content"].get<std::string>();
            res.content += t;
            if (on_text && !t.empty()) on_text(t);
        }
        if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
            for (auto& tc : delta["tool_calls"]) {
                int idx = tc.value("index", (int)calls.size());
                json& c = calls[idx];
                if (c.is_null()) c = {{"id", ""}, {"type", "function"}, {"function", {{"name", ""}, {"arguments", ""}}}};
                if (tc.contains("id") && tc["id"].is_string() && !tc["id"].get<std::string>().empty()) c["id"] = tc["id"];
                if (tc.contains("function")) {
                    auto& f = tc["function"];
                    if (f.contains("name") && f["name"].is_string())
                        c["function"]["name"] = c["function"]["name"].get<std::string>() + f["name"].get<std::string>();
                    if (f.contains("arguments")) {
                        std::string a = f["arguments"].is_string() ? f["arguments"].get<std::string>() : f["arguments"].dump();
                        c["function"]["arguments"] = c["function"]["arguments"].get<std::string>() + a;
                    }
                }
            }
        }
    };
    HttpResponse r = http_request(
        "POST", base_ + "/chat/completions", body.dump(), headers, 120000,
        [&](const char* p, size_t n) {
            raw.append(p, n);
            buf.append(p, n);
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, nl);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                buf.erase(0, nl + 1);
                handle_line(line);
            }
            return !done;
        },
        cancel);
    if (!buf.empty()) handle_line(trim(buf));
    if (!r.ok()) {
        res.error = error_text(r);
        return res;
    }
    if (!saw_sse) {
        // the server ignored "stream": a plain JSON answer
        json j = json::parse(raw, nullptr, false);
        if (!j.is_object() || !j.contains("choices") || j["choices"].empty()) {
            res.error = "unexpected reply: " + raw.substr(0, 200);
            return res;
        }
        auto& msg = j["choices"][0]["message"];
        if (msg.contains("content") && msg["content"].is_string()) {
            res.content = msg["content"].get<std::string>();
            if (on_text && !res.content.empty()) on_text(res.content);
        }
        if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
            int i = 0;
            for (auto& tc : msg["tool_calls"]) {
                json c = tc;
                if (c.contains("function") && c["function"].contains("arguments") && !c["function"]["arguments"].is_string())
                    c["function"]["arguments"] = c["function"]["arguments"].dump();
                calls[i++] = c;
            }
        }
    }
    (void)stream;
    for (auto& [i, c] : calls) {
        if (c["id"].get<std::string>().empty()) c["id"] = "call_" + std::to_string(i);
        res.tool_calls.push_back(c);
    }
    res.ok = res.error.empty();
    return res;
}

LlmResult LlmClient::chat(const json& messages, const json& tools, const std::function<void(const std::string&)>& on_text,
                          const std::atomic<bool>* cancel) {
    json body = {{"model", model_}, {"messages", messages}, {"stream", true}, {"temperature", temperature_}};
    bool with_tools = tools_supported_ && tools.is_array() && !tools.empty();
    if (with_tools) body["tools"] = tools;
    LlmResult r = request(body, true, on_text, cancel);
    if (!r.ok && with_tools && r.content.empty() && contains(lower(r.error), "tool")) {
        // the model has no tool support: talk without tools from now on
        log("LLM rejected tools, retrying without: " + r.error);
        tools_supported_ = false;
        body.erase("tools");
        r = request(body, true, on_text, cancel);
    }
    return r;
}

bool LlmClient::test(std::string& info) {
    json msgs = json::array({{{"role", "user"}, {"content", "Reply with one word: ready"}}});
    json body = {{"model", model_}, {"messages", msgs}, {"stream", false}, {"max_tokens", 64}};
    LlmResult r = request(body, false, nullptr, nullptr);
    if (!r.ok) {
        info = r.error;
        return false;
    }
    std::string c = r.content;
    size_t a = c.find("<think>");
    if (a != std::string::npos) {
        size_t b = c.find("</think>", a);
        c = c.substr(0, a) + (b == std::string::npos ? "" : c.substr(b + 8));
    }
    info = model_ + ": " + (trim(c).empty() ? "ok" : trim(c));
    return true;
}

}  // namespace kayra

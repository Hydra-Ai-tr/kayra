// Kayra's abilities. The built-in command parser and the LLM brain call the
// same tools; every tool returns a sentence to say and data for the model.
#pragma once
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

#include "core/config.h"
#include "core/memory.h"

namespace kayra {

struct ToolContext {
    const Config* cfg = nullptr;
    Memory* mem = nullptr;
    std::function<void(double seconds, const std::string& label)> add_timer;
    std::function<int()> cancel_timers;
    // Shutdown and friends wait for a spoken "yes".
    std::function<void(const std::string& action)> request_confirm;
    bool dry_run = false;  // demo recording: report, but do not touch the PC
};

struct ToolResult {
    bool ok = true;
    std::string say;            // a sentence in the user's language
    nlohmann::json data;        // details for the model
};

nlohmann::json tool_schemas(const Config& cfg);
ToolResult run_tool(const std::string& name, const nlohmann::json& args, ToolContext& ctx);

// Pieces the HUD uses directly.
struct Weather {
    bool ok = false;
    std::string place;
    double temp = 0, feels = 0, wind = 0, tmax = 0, tmin = 0;
    int humidity = 0, code = 0, rain_chance = -1;
    std::string error;
};
Weather fetch_weather(const std::string& city, bool turkish);
std::string weather_text(int code, bool turkish);
std::string wikipedia_summary(const std::string& topic, bool turkish, std::string* title = nullptr);

}  // namespace kayra

#include "core/tools.h"

#include <cmath>
#include <ctime>

#include "core/apps.h"
#include "core/calc.h"
#include "core/http.h"
#include "core/i18n.h"
#include "core/system.h"
#include "core/util.h"

using json = nlohmann::json;

namespace kayra {

namespace {

json fn(const char* name, const char* desc, json props = json::object(), json required = json::array()) {
    return {{"type", "function"},
            {"function",
             {{"name", name},
              {"description", desc},
              {"parameters", {{"type", "object"}, {"properties", props}, {"required", required}}}}}};
}

std::string str_arg(const json& a, const char* k, const std::string& def = "") {
    auto it = a.find(k);
    if (it == a.end() || it->is_null()) return def;
    if (it->is_string()) return it->get<std::string>();
    return it->dump();
}

double num_arg(const json& a, const char* k, double def) {
    auto it = a.find(k);
    if (it == a.end()) return def;
    if (it->is_number()) return it->get<double>();
    if (it->is_string()) {
        try {
            return std::stod(it->get<std::string>());
        } catch (...) {
        }
    }
    return def;
}

ToolResult fail(const std::string& say, const std::string& why = "") {
    ToolResult r;
    r.ok = false;
    r.say = say;
    r.data = {{"ok", false}, {"error", why.empty() ? say : why}};
    return r;
}

ToolResult done(const std::string& say, json data = json::object()) {
    ToolResult r;
    r.say = say;
    data["ok"] = true;
    data["result"] = say;
    r.data = data;
    return r;
}

std::string date_text(const std::tm& tm, bool turkish) {
    if (turkish)
        return std::to_string(tm.tm_mday) + " " + month_name(tm.tm_mon) + " " + std::to_string(tm.tm_year + 1900) +
               ", " + weekday_name(tm.tm_wday);
    return weekday_name(tm.tm_wday) + ", " + month_name(tm.tm_mon) + " " + std::to_string(tm.tm_mday) + ", " +
           std::to_string(tm.tm_year + 1900);
}

// ------------------------------------------------------------------ tools

ToolResult t_datetime(ToolContext&) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char hm[8];
    std::strftime(hm, sizeof hm, "%H:%M", &tm);
    std::string date = date_text(tm, is_turkish());
    return done(F("time_and_date", {hm, date}), {{"time", hm}, {"date", date}});
}

ToolResult t_open_app(const json& a, ToolContext& ctx) {
    std::string name = trim(str_arg(a, "name"));
    if (name.empty()) return fail(T("open_what"));
    OpenTarget t;
    if (!app_index().resolve(name, ctx.cfg->apps, t)) return fail(F("app_not_found", {name}), "not found: " + name);
    std::string disp = t.display;
    if (!ctx.dry_run && !app_index().open(t)) return fail(F("open_failed", {disp}));
    std::string key = t.kind == "folder" ? "opening_folder" : t.kind == "site" ? "opening_site" : "opening_app";
    return done(F(key.c_str(), {disp}), {{"opened", disp}, {"target", t.target}});
}

ToolResult t_open_url(const json& a, ToolContext& ctx) {
    std::string url = trim(str_arg(a, "url"));
    if (url.empty()) return fail(T("open_what"));
    if (!starts_with(url, "http://") && !starts_with(url, "https://")) url = "https://" + url;
    if (!ctx.dry_run && !shell_open(url)) return fail(F("open_failed", {url}));
    std::string title = trim(str_arg(a, "title"));
    return done(title.empty() ? T("opening_link") : F("opening_site", {title}), {{"url", url}});
}

ToolResult t_search(const json& a, ToolContext& ctx) {
    std::string q = trim(str_arg(a, "query"));
    std::string site = str_arg(a, "site", "google");
    if (q.empty()) return fail(T("search_what"));
    std::string url, where;
    if (site == "youtube") {
        url = "https://www.youtube.com/results?search_query=" + url_encode(q);
        where = "YouTube";
    } else if (site == "maps") {
        url = "https://www.google.com/maps/search/" + url_encode(q);
        where = is_turkish() ? "Haritalar" : "Maps";
    } else if (site == "wikipedia") {
        url = std::string("https://") + (is_turkish() ? "tr" : "en") +
              ".wikipedia.org/w/index.php?search=" + url_encode(q);
        where = is_turkish() ? "Vikipedi" : "Wikipedia";
    } else {
        url = "https://www.google.com/search?q=" + url_encode(q);
        where = "Google";
    }
    if (!ctx.dry_run && !shell_open(url)) return fail(F("open_failed", {url}));
    std::string loc = site == "youtube" ? "YouTube'da" : site == "maps" ? "Haritalar'da" : site == "wikipedia" ? "Vikipedi'de" : "Google'da";
    return done(is_turkish() ? F("searching", {loc, q}) : F("searching", {where, q}), {{"url", url}});
}

ToolResult t_weather(const json& a, ToolContext& ctx) {
    std::string city = trim(str_arg(a, "city"));
    if (city.empty()) city = ctx.cfg->city;
    Weather w = fetch_weather(city, is_turkish());
    if (!w.ok) {
        log("weather: " + w.error);
        return fail(T("weather_failed"), w.error);
    }
    std::string desc = weather_text(w.code, is_turkish());
    bool tr = is_turkish();
    std::string say = F("weather_now", {tr ? tr_locative(w.place) : w.place, format_number(std::round(w.temp), 0, tr), desc});
    say += " " + F("weather_range", {format_number(std::round(w.tmax), 0, tr), format_number(std::round(w.tmin), 0, tr)});
    if (w.rain_chance >= 30) say += " " + F("weather_rain", {std::to_string(w.rain_chance)});
    return done(say, {{"place", w.place},
                      {"temperature_c", w.temp},
                      {"feels_like_c", w.feels},
                      {"humidity", w.humidity},
                      {"wind_kmh", w.wind},
                      {"max_c", w.tmax},
                      {"min_c", w.tmin},
                      {"rain_chance", w.rain_chance},
                      {"sky", desc}});
}

ToolResult t_wikipedia(const json& a, ToolContext&) {
    std::string topic = trim(str_arg(a, "topic"));
    if (topic.empty()) return fail(T("search_what"));
    std::string title;
    std::string s = wikipedia_summary(topic, is_turkish(), &title);
    if (s.empty()) return fail(F("wiki_not_found", {topic}));
    return done(s, {{"title", title}});
}

ToolResult t_system(ToolContext&) {
    SysStats s = system_stats();
    bool tr = is_turkish();
    std::string say = F("sys_cpu_ram", {format_number(std::round(s.cpu), 0, tr), format_number(std::round(s.ram), 0, tr),
                                        format_number(s.ram_used_gb, 1, tr), format_number(s.ram_total_gb, 0, tr)});
    if (s.battery >= 0) say += " " + F(s.charging ? "sys_battery_charging" : "sys_battery", {std::to_string(s.battery)});
    say += " " + F("sys_disk", {format_number(std::round(s.disk_free_gb), 0, tr)});
    return done(say, {{"cpu_percent", s.cpu},
                      {"ram_percent", s.ram},
                      {"ram_used_gb", s.ram_used_gb},
                      {"ram_total_gb", s.ram_total_gb},
                      {"battery_percent", s.battery},
                      {"charging", s.charging},
                      {"disk_free_gb", s.disk_free_gb},
                      {"uptime_hours", s.uptime_s / 3600.0},
                      {"cpu", cpu_name()}});
}

ToolResult t_volume(const json& a, ToolContext& ctx) {
    std::string action = str_arg(a, "action", "set");
    int level = (int)num_arg(a, "level", -1);
    int cur = ctx.dry_run ? 40 : volume_get();
    if (cur < 0) return fail(T("volume_failed"));
    if (action == "mute") {
        if (!ctx.dry_run) volume_mute(true);
        return done(T("volume_muted"));
    }
    if (action == "unmute") {
        if (!ctx.dry_run) volume_mute(false);
        return done(F("volume_now", {std::to_string(cur)}));
    }
    int target = cur;
    if (action == "up") target = cur + (level > 0 ? level : 10);
    else if (action == "down") target = cur - (level > 0 ? level : 10);
    else if (action == "max") target = 100;
    else if (level >= 0) target = level;
    else return done(F("volume_now", {std::to_string(cur)}), {{"level", cur}});
    target = std::max(0, std::min(100, target));
    if (!ctx.dry_run && !volume_set(target)) return fail(T("volume_failed"));
    return done(F("volume_now", {std::to_string(target)}), {{"level", target}});
}

ToolResult t_media(const json& a, ToolContext& ctx) {
    std::string action = str_arg(a, "action", "play_pause");
    MediaKey k = MediaKey::PlayPause;
    const char* say = "media_toggled";
    if (action == "next") k = MediaKey::Next, say = "media_next";
    else if (action == "previous") k = MediaKey::Previous, say = "media_previous";
    else if (action == "stop") k = MediaKey::Stop, say = "media_stopped";
    else if (action == "pause") say = "media_paused";
    else if (action == "play") say = "media_playing";
    if (!ctx.dry_run) media_key(k);
    return done(T(say));
}

ToolResult t_timer(const json& a, ToolContext& ctx) {
    double secs = num_arg(a, "seconds", 0);
    std::string label = trim(str_arg(a, "label"));
    if (secs <= 0) return fail(T("timer_how_long"));
    if (secs > 7 * 24 * 3600) return fail(T("timer_too_long"));
    if (ctx.add_timer) ctx.add_timer(secs, label);
    std::string d = duration_text(secs);
    return done(label.empty() ? F("timer_set", {d}) : F("reminder_set", {d, label}), {{"seconds", secs}});
}

ToolResult t_cancel_timers(ToolContext& ctx) {
    int n = ctx.cancel_timers ? ctx.cancel_timers() : 0;
    return done(n ? F("timers_cancelled", {std::to_string(n)}) : T("no_timers"));
}

ToolResult t_remember(const json& a, ToolContext& ctx) {
    std::string fact = trim(str_arg(a, "fact"));
    if (fact.empty()) return fail(T("remember_what"));
    ctx.mem->remember(fact);
    return done(T("remembered"));
}

ToolResult t_forget(const json& a, ToolContext& ctx) {
    std::string about = trim(str_arg(a, "about"));
    int n = ctx.mem->forget(about);
    return done(n ? F("forgot", {std::to_string(n)}) : T("forgot_none"));
}

ToolResult t_list_memory(ToolContext& ctx) {
    auto f = ctx.mem->facts();
    if (f.empty()) return done(T("memory_empty"), {{"facts", json::array()}});
    std::string s = T("memory_list");
    for (size_t i = 0; i < f.size() && i < 8; i++) s += (i ? "; " : " ") + f[i];
    s += ".";
    return done(s, {{"facts", f}});
}

ToolResult t_screenshot(ToolContext& ctx) {
    std::string path = ctx.dry_run ? known_folder("pictures") + "\\Kayra\\ekran.png" : take_screenshot();
    if (path.empty()) return fail(T("screenshot_failed"));
    return done(T("screenshot_saved"), {{"path", path}});
}

ToolResult t_lock(ToolContext& ctx) {
    if (!ctx.dry_run) lock_workstation();
    return done(T("locking"));
}

ToolResult t_power(const json& a, ToolContext& ctx) {
    std::string action = str_arg(a, "action");
    if (action == "cancel") {
        if (!ctx.dry_run) power_action("cancel", 0);
        return done(T("power_cancelled"));
    }
    if (action != "shutdown" && action != "restart" && action != "sleep") return fail(T("power_unknown"));
    if (ctx.request_confirm) ctx.request_confirm(action);
    return done(F("confirm_power", {T(("power_" + action).c_str())}),
                {{"needs_confirmation", true}, {"note", "Ask the user to say yes to confirm."}});
}

ToolResult t_clipboard(ToolContext&) {
    std::string c = clipboard_text();
    if (trim(c).empty()) return fail(T("clipboard_empty"));
    if (c.size() > 6000) c = c.substr(0, 6000);
    ToolResult r = done(T("clipboard_read"), {{"text", c}});
    return r;
}

ToolResult t_calculate(const json& a, ToolContext&) {
    std::string e = trim(str_arg(a, "expression"));
    double v = 0;
    std::string err;
    if (!calc_eval(e, v, &err)) {
        std::string expr;
        if (!(to_expression(fold(e), is_turkish(), expr) && calc_eval(expr, v, &err)))
            return fail(T("calc_failed"), err);
    }
    std::string n = format_number(v, 6, is_turkish());
    return done(F("calc_result", {n}), {{"value", v}});
}

ToolResult t_run(const json& a, ToolContext& ctx) {
    if (!ctx.cfg->allow_shell) return fail(T("shell_disabled"), "console commands are disabled in the settings");
    std::string cmd = trim(str_arg(a, "command"));
    if (cmd.empty()) return fail(T("open_what"));
    std::string out;
    if (ctx.dry_run) out = "(demo)";
    else if (!run_command(cmd, out)) return fail(T("command_failed"));
    return done(T("command_done"), {{"output", out}});
}

}  // namespace

json tool_schemas(const Config& cfg) {
    json t = json::array();
    t.push_back(fn("get_datetime", "Current local time and date."));
    t.push_back(fn("open_app",
                   "Open an installed application, a Windows tool, a folder (downloads, documents, desktop...) or a "
                   "well-known web site by name.",
                   {{"name", {{"type", "string"}, {"description", "e.g. 'Spotify', 'Discord', 'not defteri', 'indirilenler'"}}}},
                   {"name"}));
    t.push_back(fn("open_url", "Open a web address in the default browser.",
                   {{"url", {{"type", "string"}}}}, {"url"}));
    t.push_back(fn("web_search", "Search the web (Google), YouTube, Wikipedia or maps and show the results in the browser.",
                   {{"query", {{"type", "string"}}},
                    {"site", {{"type", "string"}, {"enum", {"google", "youtube", "wikipedia", "maps"}}}}},
                   {"query"}));
    t.push_back(fn("get_weather", "Current weather and today's forecast. Omit city for the user's location.",
                   {{"city", {{"type", "string"}}}}));
    t.push_back(fn("wikipedia", "Short encyclopedia summary of a person, place or concept, for factual questions.",
                   {{"topic", {{"type", "string"}}}}, {"topic"}));
    t.push_back(fn("system_status", "CPU, memory, battery and disk status of this computer."));
    t.push_back(fn("set_volume", "Change the speaker volume.",
                   {{"action", {{"type", "string"}, {"enum", {"set", "up", "down", "mute", "unmute", "max", "get"}}}},
                    {"level", {{"type", "integer"}, {"description", "0-100 for set, step for up/down"}}}},
                   {"action"}));
    t.push_back(fn("media_control", "Control music/video playback (any player).",
                   {{"action", {{"type", "string"}, {"enum", {"play_pause", "play", "pause", "next", "previous", "stop"}}}}},
                   {"action"}));
    t.push_back(fn("set_timer", "Start a timer or a reminder. Kayra speaks when it ends.",
                   {{"seconds", {{"type", "number"}}},
                    {"label", {{"type", "string"}, {"description", "what to remind, empty for a plain timer"}}}},
                   {"seconds"}));
    t.push_back(fn("cancel_timers", "Cancel all running timers and reminders."));
    t.push_back(fn("remember", "Save a fact about the user for later conversations.",
                   {{"fact", {{"type", "string"}}}}, {"fact"}));
    t.push_back(fn("forget", "Forget saved facts that mention the given words.", {{"about", {{"type", "string"}}}},
                   {"about"}));
    t.push_back(fn("list_memory", "List the facts Kayra remembers."));
    t.push_back(fn("take_screenshot", "Save a screenshot of all screens into Pictures\\Kayra."));
    t.push_back(fn("lock_computer", "Lock the computer."));
    t.push_back(fn("power", "Shut down, restart or put the computer to sleep (asks the user to confirm), or cancel a "
                            "pending shutdown.",
                   {{"action", {{"type", "string"}, {"enum", {"shutdown", "restart", "sleep", "cancel"}}}}}, {"action"}));
    t.push_back(fn("read_clipboard", "Read the text the user copied (to summarize, translate or explain it)."));
    t.push_back(fn("calculate", "Evaluate arithmetic exactly, e.g. '(12.5*4)^2/3', 'sqrt(2)'.",
                   {{"expression", {{"type", "string"}}}}, {"expression"}));
    if (cfg.allow_shell)
        t.push_back(fn("run_command", "Run a Windows console (cmd) command and return its output.",
                       {{"command", {{"type", "string"}}}}, {"command"}));
    return t;
}

ToolResult run_tool(const std::string& name, const json& args, ToolContext& ctx) {
    log("tool " + name + " " + args.dump());
    try {
        if (name == "get_datetime") return t_datetime(ctx);
        if (name == "open_app") return t_open_app(args, ctx);
        if (name == "open_url") return t_open_url(args, ctx);
        if (name == "web_search") return t_search(args, ctx);
        if (name == "get_weather") return t_weather(args, ctx);
        if (name == "wikipedia") return t_wikipedia(args, ctx);
        if (name == "system_status") return t_system(ctx);
        if (name == "set_volume") return t_volume(args, ctx);
        if (name == "media_control") return t_media(args, ctx);
        if (name == "set_timer") return t_timer(args, ctx);
        if (name == "cancel_timers") return t_cancel_timers(ctx);
        if (name == "remember") return t_remember(args, ctx);
        if (name == "forget") return t_forget(args, ctx);
        if (name == "list_memory") return t_list_memory(ctx);
        if (name == "take_screenshot") return t_screenshot(ctx);
        if (name == "lock_computer") return t_lock(ctx);
        if (name == "power") return t_power(args, ctx);
        if (name == "read_clipboard") return t_clipboard(ctx);
        if (name == "calculate") return t_calculate(args, ctx);
        if (name == "run_command") return t_run(args, ctx);
    } catch (const std::exception& e) {
        return fail(T("tool_error"), e.what());
    }
    return fail(T("tool_error"), "unknown tool " + name);
}

// ------------------------------------------------------------------ weather

std::string weather_text(int code, bool tr) {
    struct W {
        int lo, hi;
        const char *t, *e;
    };
    static const W table[] = {
        {0, 0, "açık", "clear"},
        {1, 1, "az bulutlu", "mainly clear"},
        {2, 2, "parçalı bulutlu", "partly cloudy"},
        {3, 3, "kapalı", "overcast"},
        {45, 48, "sisli", "foggy"},
        {51, 57, "çiseliyor", "drizzle"},
        {61, 61, "hafif yağmurlu", "light rain"},
        {63, 63, "yağmurlu", "rain"},
        {65, 67, "sağanak yağmurlu", "heavy rain"},
        {71, 77, "karlı", "snow"},
        {80, 82, "sağanak yağışlı", "rain showers"},
        {85, 86, "kar sağanaklı", "snow showers"},
        {95, 99, "gök gürültülü fırtınalı", "thunderstorms"},
    };
    for (auto& w : table)
        if (code >= w.lo && code <= w.hi) return tr ? w.t : w.e;
    return tr ? "değişken" : "changeable";
}

Weather fetch_weather(const std::string& city_in, bool tr) {
    Weather w;
    double lat = 0, lon = 0;
    bool have = false;
    std::string city = trim(city_in);
    if (!city.empty()) {
        // "İstanbul'da" -> try as said, then without the case ending
        std::vector<std::string> tries = {city};
        std::string base = city;
        size_t ap = base.find_first_of("'\xE2");
        if (ap != std::string::npos) tries.insert(tries.begin(), trim(base.substr(0, ap)));
        for (auto& c : name_candidates(fold(city))) tries.push_back(c);
        for (auto& c : tries) {
            HttpResponse r = http_get("https://geocoding-api.open-meteo.com/v1/search?count=1&format=json&language=" +
                                      std::string(tr ? "tr" : "en") + "&name=" + url_encode(c));
            if (!r.ok()) {
                w.error = r.error.empty() ? "geocoding HTTP " + std::to_string(r.status) : r.error;
                if (r.status == 0) return w;
                continue;
            }
            json j = json::parse(r.body, nullptr, false);
            if (j.is_object() && j.contains("results") && j["results"].is_array() && !j["results"].empty()) {
                auto& g = j["results"][0];
                lat = g.value("latitude", 0.0);
                lon = g.value("longitude", 0.0);
                w.place = g.value("name", c);
                have = true;
                break;
            }
        }
        if (!have) {
            w.error = "place not found: " + city;
            return w;
        }
    } else {
        HttpResponse r = http_get("http://ip-api.com/json/?fields=status,city,lat,lon&lang=" + std::string(tr ? "tr" : "en"));
        json j = json::parse(r.body, nullptr, false);
        if (!r.ok() || !j.is_object() || j.value("status", "") != "success") {
            w.error = r.error.empty() ? "location lookup failed" : r.error;
            return w;
        }
        lat = j.value("lat", 0.0);
        lon = j.value("lon", 0.0);
        w.place = j.value("city", "");
        // the local spelling ("Istanbul" -> "İstanbul")
        HttpResponse g = http_get("https://geocoding-api.open-meteo.com/v1/search?count=1&format=json&language=" +
                                  std::string(tr ? "tr" : "en") + "&name=" + url_encode(w.place));
        json gj = json::parse(g.body, nullptr, false);
        if (g.ok() && gj.is_object() && gj.contains("results") && gj["results"].is_array() && !gj["results"].empty()) {
            auto& r0 = gj["results"][0];
            if (std::fabs(r0.value("latitude", 0.0) - lat) < 0.6 && std::fabs(r0.value("longitude", 0.0) - lon) < 0.6)
                w.place = r0.value("name", w.place);
        }
    }
    char url[512];
    snprintf(url, sizeof url,
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m"
             "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max&timezone=auto&forecast_days=1",
             lat, lon);
    HttpResponse r = http_get(url);
    json j = json::parse(r.body, nullptr, false);
    if (!r.ok() || !j.is_object() || !j.contains("current")) {
        w.error = r.error.empty() ? "forecast HTTP " + std::to_string(r.status) : r.error;
        return w;
    }
    auto& c = j["current"];
    w.temp = c.value("temperature_2m", 0.0);
    w.feels = c.value("apparent_temperature", 0.0);
    w.humidity = (int)c.value("relative_humidity_2m", 0.0);
    w.code = c.value("weather_code", 0);
    w.wind = c.value("wind_speed_10m", 0.0);
    if (j.contains("daily")) {
        auto& d = j["daily"];
        auto first = [&](const char* k, double def) {
            if (d.contains(k) && d[k].is_array() && !d[k].empty() && d[k][0].is_number()) return d[k][0].get<double>();
            return def;
        };
        w.tmax = first("temperature_2m_max", w.temp);
        w.tmin = first("temperature_2m_min", w.temp);
        w.rain_chance = (int)first("precipitation_probability_max", -1);
    }
    w.ok = true;
    return w;
}

// ------------------------------------------------------------------ wikipedia

std::string wikipedia_summary(const std::string& topic, bool tr, std::string* title_out) {
    std::string lang = tr ? "tr" : "en";
    HttpResponse s = http_get("https://" + lang + ".wikipedia.org/w/api.php?action=opensearch&limit=1&namespace=0&format=json&search=" +
                              url_encode(topic));
    json j = json::parse(s.body, nullptr, false);
    std::string title;
    if (s.ok() && j.is_array() && j.size() > 1 && j[1].is_array() && !j[1].empty()) title = j[1][0].get<std::string>();
    if (title.empty()) return "";
    if (title_out) *title_out = title;
    HttpResponse r = http_get("https://" + lang + ".wikipedia.org/api/rest_v1/page/summary/" +
                              url_encode(replace_all(title, " ", "_")));
    json p = json::parse(r.body, nullptr, false);
    if (!r.ok() || !p.is_object()) return "";
    std::string text = p.value("extract", "");
    // drop parenthesised birth dates and pronunciations, keep two sentences
    std::string clean;
    int depth = 0;
    for (char ch : text) {
        if (ch == '(') depth++;
        else if (ch == ')' && depth > 0) depth--;
        else if (depth == 0) clean += ch;
    }
    clean = replace_all(clean, "  ", " ");
    clean = replace_all(clean, " ,", ",");
    std::string out;
    int sentences = 0;
    for (size_t i = 0; i < clean.size(); i++) {
        out += clean[i];
        if (clean[i] == '.' && (i + 1 == clean.size() || clean[i + 1] == ' ') && out.size() > 40) {
            // "M.Ö." and initials: a sentence ends only after a lowercase letter or a digit
            char prev = i > 0 ? clean[i - 1] : 0;
            if (i > 1 && clean[i - 2] != ' ' && (islower((unsigned char)prev) || isdigit((unsigned char)prev) || (unsigned char)prev >= 0x80)) {
                if (++sentences == 2) break;
            }
        }
        if (out.size() > 420 && clean[i] == ' ') {
            out = trim(out) + "...";
            break;
        }
    }
    return trim(out);
}

}  // namespace kayra

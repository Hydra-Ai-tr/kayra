#include "core/assistant.h"

#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>

#include <ctime>

#include "core/i18n.h"
#include "core/intent.h"
#include "core/system.h"
#include "core/util.h"

using json = nlohmann::json;

namespace kayra {

namespace {

std::string now_hm() { return clock_text(false); }

std::string today_text() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    if (is_turkish())
        return std::to_string(tm.tm_mday) + " " + month_name(tm.tm_mon) + " " + std::to_string(tm.tm_year + 1900) +
               ", " + weekday_name(tm.tm_wday);
    return weekday_name(tm.tm_wday) + ", " + month_name(tm.tm_mon) + " " + std::to_string(tm.tm_mday) + ", " +
           std::to_string(tm.tm_year + 1900);
}

// Hides <think>...</think> blocks of reasoning models.
std::string visible_text(const std::string& s) {
    std::string out;
    size_t i = 0;
    for (;;) {
        size_t a = s.find("<think>", i);
        if (a == std::string::npos) {
            out += s.substr(i);
            break;
        }
        out += s.substr(i, a - i);
        size_t b = s.find("</think>", a);
        if (b == std::string::npos) break;
        i = b + 8;
    }
    return out;
}

// End of the last complete sentence at or after `from`, or npos.
size_t sentence_end(const std::string& s, size_t from) {
    size_t best = std::string::npos;
    for (size_t i = from; i < s.size(); i++) {
        char c = s[i];
        if (c == '\n') {
            best = i + 1;
        } else if ((c == '.' || c == '!' || c == '?' || c == ':' || c == ';') && i + 1 < s.size() &&
                   (s[i + 1] == ' ' || s[i + 1] == '\n')) {
            if (i - from >= 12) best = i + 1;
        }
    }
    return best;
}

}  // namespace

Assistant::Assistant(const Config& cfg, Speaker* tts) : tts_(tts) {
    mem_.load();
    set_config(cfg);
}

Assistant::~Assistant() { stop(); }

void Assistant::set_config(const Config& c) {
    auto copy = std::make_shared<const Config>(c);
    set_language(c.turkish());
    set_address(c.address());
    speak_replies_ = c.speak;
    {
        std::lock_guard<std::mutex> lk(cfg_mu_);
        cfg_ = copy;
    }
    std::lock_guard<std::mutex> lk(llm_mu_);
    llm_ = std::make_shared<LlmClient>(c);
}

Assistant::Cfg Assistant::cfg() const {
    std::lock_guard<std::mutex> lk(cfg_mu_);
    return cfg_;
}

Config Assistant::config() const { return *cfg(); }

bool Assistant::brain_connected() const {
    std::lock_guard<std::mutex> lk(llm_mu_);
    return llm_ && llm_->configured();
}

void Assistant::start() {
    if (worker_th_.joinable()) return;
    quit_ = false;
    worker_th_ = std::thread([this] { worker(); });
    timer_th_ = std::thread([this] { timer_loop(); });
}

void Assistant::stop() {
    quit_ = true;
    cancel_ = true;
    q_cv_.notify_all();
    if (worker_th_.joinable()) worker_th_.join();
    if (timer_th_.joinable()) timer_th_.join();
}

void Assistant::submit(const std::string& text) {
    if (trim(text).empty()) return;
    {
        std::lock_guard<std::mutex> lk(q_mu_);
        queue_.push_back(text);
    }
    q_cv_.notify_one();
}

std::string Assistant::ask(const std::string& text) { return process(text); }

void Assistant::worker() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (!quit_) {
        std::string text;
        {
            std::unique_lock<std::mutex> lk(q_mu_);
            q_cv_.wait(lk, [&] { return quit_ || !queue_.empty(); });
            if (quit_) break;
            text = queue_.front();
            queue_.pop_front();
        }
        cancel_ = false;
        try {
            process(text);
        } catch (const std::exception& e) {
            log(std::string("assistant: ") + e.what());
            thinking_ = false;
        }
    }
    CoUninitialize();
}

size_t Assistant::add_entry(bool user, const std::string& text, bool streaming, bool notice) {
    std::lock_guard<std::mutex> lk(log_mu_);
    ChatEntry e;
    e.user = user;
    e.text = text;
    e.time = now_hm();
    e.streaming = streaming;
    e.notice = notice;
    log_.push_back(e);
    if (log_.size() > 300) log_.erase(log_.begin(), log_.begin() + 100);
    version_++;
    return log_.size() - 1;
}

void Assistant::update_entry(size_t idx, const std::string& text, bool streaming, const std::string& tool) {
    std::lock_guard<std::mutex> lk(log_mu_);
    if (idx >= log_.size()) return;
    log_[idx].text = text;
    log_[idx].streaming = streaming;
    if (!tool.empty()) log_[idx].tool = tool;
    version_++;
}

std::vector<ChatEntry> Assistant::log_entries() const {
    std::lock_guard<std::mutex> lk(log_mu_);
    return log_;
}

void Assistant::clear_log() {
    std::lock_guard<std::mutex> lk(log_mu_);
    log_.clear();
    version_++;
}

std::string Assistant::last_tool() const {
    std::lock_guard<std::mutex> lk(log_mu_);
    return last_tool_;
}

std::vector<TimerInfo> Assistant::timers() const {
    std::lock_guard<std::mutex> lk(timer_mu_);
    return timers_;
}

void Assistant::speak_sentence(const std::string& s) {
    if (tts_ && speech_on_ && speak_replies_ && !trim(s).empty()) tts_->speak(s);
}

void Assistant::say(const std::string& text, bool log_it) {
    if (log_it) add_entry(false, text);
    speak_sentence(text);
}

void Assistant::greet() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    const char* key = tm.tm_hour >= 5 && tm.tm_hour < 12    ? "greet_morning"
                      : tm.tm_hour >= 12 && tm.tm_hour < 18 ? "greet_day"
                      : tm.tm_hour >= 18 && tm.tm_hour < 23 ? "greet_evening"
                                                            : "greet_night";
    say(T(key) + " " + T("online"));
}

ToolContext Assistant::tool_context(const Cfg& cfg) {
    ToolContext ctx;
    ctx.cfg = cfg.get();
    ctx.mem = &mem_;
    ctx.dry_run = dry_run_;
    ctx.add_timer = [this](double secs, const std::string& label) {
        std::lock_guard<std::mutex> lk(timer_mu_);
        timers_.push_back({next_timer_++, label, now_seconds() + secs, secs});
    };
    ctx.cancel_timers = [this]() {
        std::lock_guard<std::mutex> lk(timer_mu_);
        int n = (int)timers_.size();
        timers_.clear();
        return n;
    };
    ctx.request_confirm = [this](const std::string& tool, std::function<std::string()> on_yes) {
        pending_tool_ = tool;
        pending_action_ = std::move(on_yes);
    };
    return ctx;
}

void Assistant::timer_loop() {
    while (!quit_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::vector<TimerInfo> fired;
        {
            std::lock_guard<std::mutex> lk(timer_mu_);
            double now = now_seconds();
            for (auto it = timers_.begin(); it != timers_.end();) {
                if (it->due <= now) {
                    fired.push_back(*it);
                    it = timers_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& t : fired) {
            std::string text = t.label.empty() ? T("timer_done") : F("reminder_done", {t.label});
            add_entry(false, text, false, true);
            PlaySoundW(L"SystemNotification", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            speak_sentence(text);
            if (on_notice) on_notice(text);
        }
    }
}

std::string Assistant::system_prompt(const Config& c) const {
    std::string p;
    auto facts = mem_.facts();
    if (c.turkish()) {
        p = "Sen Kayra'sın: Tony Stark'ın J.A.R.V.I.S.'i tarzında, kullanıcının Windows bilgisayarında çalışan kişisel "
            "yapay zekâ asistanısın. Hydra-Ai-tr tarafından geliştirilen açık kaynak bir projesin. Kullanıcıya \"" +
            c.address() +
            "\" diye hitap et. Zeki, sakin, kibar ve hafif esprili konuş. Yanıtların sesli okunacak: kısa tut (genellikle "
            "1-3 cümle), markdown, madde işareti, emoji ve kod bloğu kullanma. Bilgisayarda bir şey yapman gerektiğinde "
            "araçlarını kullan ve sonucu kendi cümlelerinle kısaca aktar. Güncel ya da emin olmadığın bilgiler için "
            "search_internet veya wikipedia aracını kullan; bilgi uydurma. Her zaman Türkçe yanıt ver.\n";
        if (c.allow_files)
            p += "Kullanıcının dosyalarına erişimin var: klasörleri listeleyebilir, dosya bulabilir, okuyabilir, yazabilir, "
                 "taşıyabilir ve açabilirsin. Silmeden ya da üzerine yazmadan önce araç kullanıcıya sorar.\n";
        if (c.allow_web)
            p += "İnternete erişimin var: search_internet ile arama sonuçlarını, get_news ile haberleri, read_webpage ile "
                 "sayfaların metnini okuyabilirsin. Sonuçları kendi cümlelerinle özetle.\n";
        p += "Şu an: " + today_text() + ", saat " + now_hm() + ".\n";
        if (!c.user_name.empty()) p += "Kullanıcının adı: " + c.user_name + ".\n";
        if (!c.city.empty()) p += "Kullanıcının şehri: " + c.city + ".\n";
        if (!facts.empty()) {
            p += "Kullanıcı hakkında hatırladıkların:\n";
            for (auto& x : facts) p += "- " + x + "\n";
        }
    } else {
        p = "You are Kayra: a personal AI assistant in the style of Tony Stark's J.A.R.V.I.S., running on the user's "
            "Windows computer. You are an open-source project by Hydra-Ai-tr. Address the user as \"" +
            c.address() +
            "\". Be smart, calm, polite and lightly witty. Your replies are spoken aloud: keep them short (usually 1-3 "
            "sentences) and do not use markdown, bullet points, emoji or code blocks. Use your tools to act on the "
            "computer and report the result briefly in your own words. For current facts or anything you are unsure "
            "of, use the search_internet or wikipedia tool; never make things up. Always answer in English.\n";
        if (c.allow_files)
            p += "You have access to the user's files: list folders, find, read, write, move and open files. The tools ask "
                 "the user before deleting or overwriting.\n";
        if (c.allow_web)
            p += "You have internet access: search_internet for results, get_news for headlines, read_webpage for a "
                 "page's text. Summarize in your own words.\n";
        p += "Now: " + today_text() + ", " + now_hm() + ".\n";
        if (!c.user_name.empty()) p += "User's name: " + c.user_name + ".\n";
        if (!c.city.empty()) p += "User's city: " + c.city + ".\n";
        if (!facts.empty()) {
            p += "What you remember about the user:\n";
            for (auto& x : facts) p += "- " + x + "\n";
        }
    }
    return p;
}

std::string Assistant::run_intent(const Cfg& cfg, const Intent& it, std::string* tool_used) {
    if (!it.tool.empty()) {
        ToolContext ctx = tool_context(cfg);
        ToolResult r = run_tool(it.tool, it.args, ctx);
        if (tool_used) *tool_used = it.tool;
        return r.say;
    }
    if (!it.reply_key.empty()) {
        std::string s = T(it.reply_key.c_str());
        for (size_t i = 0; i < it.reply_args.size(); i++) s = replace_all(s, "{" + std::to_string(i) + "}", it.reply_args[i]);
        return s;
    }
    const std::string& s = it.special;
    if (s == "time") return F("time_now", {now_hm()});
    if (s == "date") return F("date_today", {today_text()});
    if (s == "get_name") return cfg->user_name.empty() ? T("dont_know_name") : F("your_name_is", {cfg->user_name});
    if (s == "set_name" || s == "set_title") {
        std::string value = trim(it.args.value(s == "set_name" ? "name" : "title", ""));
        if (value.empty()) return T(s == "set_name" ? "dont_know_name" : "ok");
        Config next = *cfg;
        if (s == "set_name") next.user_name = capitalize(value);
        else next.title = value;
        if (!dry_run_) next.save();
        set_config(next);
        if (on_config_changed) on_config_changed(next);
        return s == "set_name" ? F("user_name_set", {next.user_name}) : F("title_set", {value});
    }
    if (s == "clipboard_say") {
        std::string c = trim(clipboard_text());
        if (c.empty()) return T("clipboard_empty");
        if (c.size() > 400) c = c.substr(0, 400) + "...";
        return F("clipboard_says", {c});
    }
    if (s == "yes" || s == "no") return T("ok");
    return T("not_understood");
}

std::string Assistant::run_llm(const Cfg& cfg, const std::string& text, size_t entry, bool* failed) {
    *failed = false;
    std::shared_ptr<LlmClient> client;
    {
        std::lock_guard<std::mutex> lk(llm_mu_);
        client = llm_;
    }
    json messages = json::array();
    messages.push_back({{"role", "system"}, {"content", system_prompt(*cfg)}});
    auto hist = mem_.recent(13);
    if (!hist.empty()) hist.pop_back();  // the current question, added below
    for (auto& h : hist)
        if (!h.text.empty()) messages.push_back({{"role", h.role == "user" ? "user" : "assistant"}, {"content", h.text}});
    messages.push_back({{"role", "user"}, {"content", text}});
    json tools = cfg->use_tools ? tool_schemas(*cfg) : json::array();
    ToolContext ctx = tool_context(cfg);

    std::string shown, used_tool, error;
    bool any_text = false;
    for (int round = 0; round < 8; round++) {
        std::string round_text;
        size_t spoken = 0;
        std::string prefix = shown.empty() ? "" : shown + " ";
        auto on_text = [&](const std::string& delta) {
            round_text += delta;
            std::string vis = visible_text(round_text);
            std::string lead = trim(vis);
            if (!lead.empty()) {
                any_text = true;
                update_entry(entry, prefix + lead, true, used_tool);
            }
            size_t end = sentence_end(vis, spoken);
            if (end != std::string::npos && end > spoken) {
                speak_sentence(vis.substr(spoken, end - spoken));
                spoken = end;
            }
        };
        LlmResult r = client->chat(messages, tools, on_text, &cancel_);
        std::string vis = visible_text(r.content);
        if (spoken < vis.size()) speak_sentence(vis.substr(spoken));
        if (!trim(vis).empty()) shown = trim(prefix + trim(vis));
        if (!r.ok) {
            error = r.error;
            log("LLM error: " + error);
            if (!any_text) *failed = true;
            break;
        }
        if (r.tool_calls.empty()) break;
        messages.push_back({{"role", "assistant"}, {"content", r.content}, {"tool_calls", r.tool_calls}});
        for (auto& call : r.tool_calls) {
            std::string name = call["function"].value("name", "");
            std::string args_s = call["function"].value("arguments", "{}");
            json args = json::parse(args_s.empty() ? "{}" : args_s, nullptr, false);
            if (!args.is_object()) args = json::object();
            ToolResult tr = run_tool(name, args, ctx);
            used_tool = name;
            {
                std::lock_guard<std::mutex> lk(log_mu_);
                last_tool_ = name;
            }
            update_entry(entry, shown.empty() ? "…" : shown, true, used_tool);
            json content = tr.data.is_null() ? json::object() : tr.data;
            messages.push_back({{"role", "tool"}, {"tool_call_id", call.value("id", "")}, {"name", name}, {"content", content.dump()}});
        }
    }
    if (starts_with(error, "HTTP 429")) error = T("brain_quota");
    else if (starts_with(error, "HTTP 503") || starts_with(error, "HTTP 500")) error = T("brain_busy");
    else if (starts_with(error, "HTTP 401") || starts_with(error, "HTTP 403")) error = T("brain_key");
    else if (starts_with(error, "HTTP 404")) error = T("brain_model");
    if (*failed) return F("brain_error", {error});
    if (shown.empty()) shown = error.empty() ? T("ok") : F("brain_error", {error});
    update_entry(entry, shown, false, used_tool);
    return shown;
}

std::string Assistant::process(const std::string& raw) {
    std::string text = trim(raw);
    Cfg c = cfg();
    bool had_wake = false;
    std::string t = strip_wake_word(text, c->wake_words, &had_wake);
    if (!text.empty()) add_entry(true, text);
    if (fold(t).empty()) {
        std::string r = T("wake_ack");
        add_entry(false, r);
        speak_sentence(r);
        if (on_reply_done) on_reply_done();
        return r;
    }
    mem_.add_turn("user", t);
    thinking_ = true;
    if (tts_) tts_->stop();

    Intent it;
    bool matched = parse_intent(t, it);
    std::string reply, tool_used;
    bool done = false;

    if (pending_action_) {
        auto action = std::move(pending_action_);
        pending_action_ = nullptr;
        if (matched && it.special == "yes") {
            reply = action();
            tool_used = pending_tool_;
            done = true;
        } else if (matched && it.special == "no") {
            reply = T("confirm_cancelled");
            done = true;
        }
    }
    if (!done && matched && it.special == "stop") {
        thinking_ = false;
        if (tts_) tts_->stop();
        return "";
    }
    if (!done) {
        bool brain = brain_connected();
        bool yes_no = matched && (it.special == "yes" || it.special == "no");
        if (matched && !(brain && yes_no) && (!brain || it.confidence >= 0.9)) {
            reply = run_intent(c, it, &tool_used);
        } else if (brain) {
            size_t entry = add_entry(false, "…", true);
            bool failed = false;
            reply = run_llm(c, t, entry, &failed);
            if (failed) {
                if (matched) reply = run_intent(c, it, &tool_used);
                update_entry(entry, reply, false, tool_used);
                speak_sentence(reply);
            }
            thinking_ = false;
            mem_.add_turn("assistant", reply);
            if (on_reply_done) on_reply_done();
            return reply;
        } else {
            reply = T("not_understood");
        }
    }
    {
        std::lock_guard<std::mutex> lk(log_mu_);
        if (!tool_used.empty()) last_tool_ = tool_used;
    }
    size_t idx = add_entry(false, reply);
    if (!tool_used.empty()) update_entry(idx, reply, false, tool_used);
    thinking_ = false;
    speak_sentence(reply);
    mem_.add_turn("assistant", reply);
    if (on_reply_done) on_reply_done();
    return reply;
}

}  // namespace kayra

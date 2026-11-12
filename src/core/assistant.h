// The assistant: routes what the user says to the built-in commands or the LLM
// brain, runs tools, speaks the answer sentence by sentence, keeps timers.
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/config.h"
#include "core/llm.h"
#include "core/memory.h"
#include "core/tools.h"
#include "voice/tts.h"

namespace kayra {

struct ChatEntry {
    bool user = false;
    std::string text;
    std::string time;
    std::string tool;        // tool used for this reply ("open_app", ...)
    bool streaming = false;
    bool notice = false;     // timer alarms and system notes
};

struct TimerInfo {
    int id;
    std::string label;
    double due;      // now_seconds() based
    double total;
};

class Assistant {
public:
    Assistant(const Config& cfg, Speaker* tts);
    ~Assistant();

    void start();
    void stop();

    void submit(const std::string& text);          // async (UI)
    std::string ask(const std::string& text);      // blocking (CLI, demo recording)
    void say(const std::string& text, bool log_it = true);  // Kayra speaks unprompted
    void greet();
    void set_config(const Config& cfg);             // after the settings change
    Config config() const;

    bool thinking() const { return thinking_; }
    std::vector<ChatEntry> log_entries() const;
    size_t log_version() const { return version_; }
    void clear_log();
    std::vector<TimerInfo> timers() const;
    std::string last_tool() const;
    Memory& memory() { return mem_; }

    void set_speech(bool on) { speech_on_ = on; }   // demo recording speaks on its own
    void set_dry_run(bool on) {
        dry_run_ = on;
        mem_.set_persistent(!on);
    }
    bool brain_connected() const;

    std::function<void()> on_reply_done;           // e.g. open the follow-up window
    std::function<void(const std::string&)> on_notice;
    std::function<void(const Config&)> on_config_changed;  // the user changed name/title by voice

private:
    using Cfg = std::shared_ptr<const Config>;
    Cfg cfg() const;
    std::string process(const std::string& text);
    std::string run_llm(const Cfg& cfg, const std::string& text, size_t entry, bool* failed);
    std::string run_intent(const Cfg& cfg, const struct Intent& it, std::string* tool_used);
    std::string system_prompt(const Config& cfg) const;
    ToolContext tool_context(const Cfg& cfg);
    size_t add_entry(bool user, const std::string& text, bool streaming = false, bool notice = false);
    void update_entry(size_t idx, const std::string& text, bool streaming, const std::string& tool = "");
    void speak_sentence(const std::string& s);
    void timer_loop();
    void worker();

    mutable std::mutex cfg_mu_;
    Cfg cfg_;
    std::atomic<bool> speak_replies_{true};
    Speaker* tts_;
    Memory mem_;
    std::shared_ptr<LlmClient> llm_;
    mutable std::mutex llm_mu_;

    std::thread worker_th_, timer_th_;
    std::mutex q_mu_;
    std::condition_variable q_cv_;
    std::deque<std::string> queue_;
    std::atomic<bool> quit_{false}, thinking_{false}, speech_on_{true}, dry_run_{false};
    std::atomic<bool> cancel_{false};

    mutable std::mutex log_mu_;
    std::vector<ChatEntry> log_;
    std::atomic<size_t> version_{0};
    std::string last_tool_;

    mutable std::mutex timer_mu_;
    std::vector<TimerInfo> timers_;
    int next_timer_ = 1;

    // an action waiting for a spoken "yes" (shutdown, delete, overwrite)
    std::function<std::string()> pending_action_;
    std::string pending_tool_;
};

}  // namespace kayra

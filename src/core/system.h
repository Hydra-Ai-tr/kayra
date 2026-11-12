// Windows system access: telemetry, audio volume, media keys, screen capture,
// clipboard, power and console commands.
#pragma once
#include <cstdint>
#include <string>

namespace kayra {

struct SysStats {
    double cpu = 0;            // 0..100 (since the previous call)
    double ram_used_gb = 0, ram_total_gb = 0;
    double ram = 0;            // 0..100
    int battery = -1;          // percent, -1 = no battery
    bool charging = false;
    double disk_free_gb = 0, disk_total_gb = 0;
    uint64_t uptime_s = 0;
    int processes = 0;
};
SysStats system_stats();
std::string cpu_name();
std::string computer_name();

// Master volume of the default output device, 0..100; -1 on failure.
int volume_get();
bool volume_set(int percent);
bool volume_mute(bool mute);
bool volume_is_muted();

enum class MediaKey { PlayPause, Next, Previous, Stop };
void media_key(MediaKey k);

// PNG of all monitors into Pictures\Kayra; returns the path or "".
std::string take_screenshot();

bool lock_workstation();
// action: "shutdown", "restart", "sleep", "cancel"
bool power_action(const std::string& action, int delay_s);

std::string clipboard_text();
bool set_clipboard_text(const std::string& text);

// Runs "cmd /c command" hidden, returns the output (trimmed to max_bytes).
bool run_command(const std::string& command, std::string& output, int timeout_ms = 20000, size_t max_bytes = 4000);

bool shell_open(const std::string& target, const std::string& args = "");

bool set_start_with_windows(bool on);

}  // namespace kayra

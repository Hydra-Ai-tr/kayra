// kayra-cli: Kayra in the console, plus tools for testing and scripting.
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "core/apps.h"
#include "core/assistant.h"
#include "core/calc.h"
#include "core/config.h"
#include "core/http.h"
#include "core/i18n.h"
#include "core/intent.h"
#include "core/llm.h"
#include "core/system.h"
#include "core/tools.h"
#include "core/util.h"
#include "voice/listener.h"
#include "voice/stt.h"
#include "voice/tts.h"

using namespace kayra;
using json = nlohmann::json;

namespace {

void out(const std::string& s) {
    fwrite(s.data(), 1, s.size(), stdout);
    fflush(stdout);
}
void outln(const std::string& s) { out(s + "\n"); }

bool read_line(std::string& line) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(in, &mode)) {
        std::wstring w;
        wchar_t buf[512];
        DWORD n = 0;
        for (;;) {
            if (!ReadConsoleW(in, buf, 512, &n, nullptr) || n == 0) return false;
            w.append(buf, n);
            if (!w.empty() && w.back() == L'\n') break;
        }
        while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r')) w.pop_back();
        line = narrow(w);
        return true;
    }
    if (!std::getline(std::cin, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

std::vector<std::string> utf8_args() {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> a;
    for (int i = 0; i < argc; i++) a.push_back(narrow(wargv[i]));
    LocalFree(wargv);
    return a;
}

std::string opt(std::vector<std::string>& a, const std::string& name, const std::string& def = "") {
    for (size_t i = 0; i + 1 < a.size(); i++) {
        if (a[i] == name) {
            std::string v = a[i + 1];
            a.erase(a.begin() + (long)i, a.begin() + (long)i + 2);
            return v;
        }
    }
    return def;
}

bool flag(std::vector<std::string>& a, const std::string& name) {
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] == name) {
            a.erase(a.begin() + (long)i);
            return true;
        }
    }
    return false;
}

// 16-bit PCM WAV -> 16 kHz mono floats
bool read_wav(const std::string& path, std::vector<float>& pcm, std::string& err) {
    std::string d;
    if (!read_file(path, d) || d.size() < 44 || d.compare(0, 4, "RIFF") || d.compare(8, 4, "WAVE")) {
        err = "not a WAV file";
        return false;
    }
    int channels = 1, rate = 16000, bits = 16;
    size_t pos = 12, data_at = 0, data_len = 0;
    while (pos + 8 <= d.size()) {
        std::string id = d.substr(pos, 4);
        uint32_t len = *(const uint32_t*)(d.data() + pos + 4);
        if (id == "fmt ") {
            channels = *(const uint16_t*)(d.data() + pos + 10);
            rate = (int)*(const uint32_t*)(d.data() + pos + 12);
            bits = *(const uint16_t*)(d.data() + pos + 22);
        } else if (id == "data") {
            data_at = pos + 8;
            data_len = std::min<size_t>(len, d.size() - data_at);
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (!data_at || bits != 16) {
        err = "need 16-bit PCM";
        return false;
    }
    size_t frames = data_len / (2 * channels);
    std::vector<float> mono(frames);
    const int16_t* s = (const int16_t*)(d.data() + data_at);
    for (size_t i = 0; i < frames; i++) {
        float v = 0;
        for (int c = 0; c < channels; c++) v += s[i * channels + c] / 32768.0f;
        mono[i] = v / channels;
    }
    if (rate == 16000) {
        pcm = mono;
        return true;
    }
    size_t n = (size_t)((double)frames * 16000 / rate);
    pcm.resize(n);
    for (size_t i = 0; i < n; i++) {
        double x = (double)i * rate / 16000;
        size_t k = (size_t)x;
        double f = x - k;
        float a = mono[std::min(k, frames - 1)], b = mono[std::min(k + 1, frames - 1)];
        pcm[i] = (float)(a + (b - a) * f);
    }
    return true;
}

// ------------------------------------------------------------------ selftest

int g_fail = 0, g_pass = 0;

void check(bool ok, const std::string& what) {
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
        outln("  FAIL  " + what);
    }
}

void expect_intent(const std::string& text, const std::string& tool_or_special, const json& args = json::object()) {
    Intent it;
    bool ok = parse_intent(text, it);
    std::string got = ok ? (!it.tool.empty() ? it.tool : !it.special.empty() ? it.special : it.reply_key) : "(none)";
    bool good = ok && got == tool_or_special;
    for (auto& [k, v] : args.items()) {
        if (!it.args.contains(k)) {
            good = false;
            continue;
        }
        if (v.is_string()) good = good && fold(it.args[k].get<std::string>()) == fold(v.get<std::string>());
        else if (v.is_number()) good = good && it.args[k].is_number() && std::fabs(it.args[k].get<double>() - v.get<double>()) < 1e-6;
        else good = good && it.args[k] == v;
    }
    check(good, "intent \"" + text + "\" -> " + got + " " + it.args.dump() + " (want " + tool_or_special + " " + args.dump() + ")");
}

void expect_calc(const std::string& text, bool tr, double want) {
    std::string e;
    double v = 0;
    bool ok = to_expression(fold(text), tr, e) && calc_eval(e, v);
    check(ok && std::fabs(v - want) < 1e-6, "calc \"" + text + "\" = " + (ok ? format_number(v, 6, false) : "fail") + " [" + e + "]");
}

int selftest() {
    outln("Kayra selftest");
    // text
    check(fold("İSTANBUL'da Şehir, Çağrı!") == "istanbulda sehir cagri", "fold: " + fold("İSTANBUL'da Şehir, Çağrı!"));
    check(lower("IŞIK İzmir") == "ışık izmir", "lower: " + lower("IŞIK İzmir"));
    check(tr_locative("İstanbul") == "İstanbul'da", "locative istanbul " + tr_locative("İstanbul"));
    check(tr_locative("Paris") == "Paris'te", "locative paris " + tr_locative("Paris"));
    check(tr_locative("Berlin") == "Berlin'de", "locative berlin");
    check(tr_locative("Kars") == "Kars'ta", "locative kars " + tr_locative("Kars"));
    check(tr_locative("Ankara") == "Ankara'da", "locative ankara");
    check(capitalize("istanbul") == "İstanbul", "capitalize");
    check(format_number(3.5, 2, true) == "3,5", "format tr");
    check(similarity("kaira", "kayra") >= 0.75, "similarity");
    // numbers and math
    auto nw = numbers_from_words(split_words("yuz yirmi bes dakika"));
    check(nw.size() == 2 && nw[0] == "125", "number words: " + (nw.empty() ? "" : nw[0]));
    check(numbers_from_words(split_words("bir sarki ac"))[0] == "bir", "'bir' as article");
    check(numbers_from_words(split_words("turn on the light"))[1] == "on", "'on' in English");
    set_language(true);
    expect_calc("125 çarpı 4 kaç eder", true, 500);
    expect_calc("240'ın yüzde 15'i kaç", true, 36);
    expect_calc("kök 144 artı 3 üzeri 2", true, 21);
    expect_calc("15 percent of 240", false, 36);
    expect_calc("what is the square root of 144 plus 3 squared", false, 21);
    expect_calc("5'in karesi kaç", true, 25);
    expect_calc("yüz yirmi beş artı beş", true, 130);
    expect_calc("3,5 çarpı 2", true, 7);
    expect_calc("karekök 16", true, 4);
    expect_calc("2 üzeri 10", true, 1024);
    expect_calc("(3+4)*2", true, 14);
    expect_calc("100 bölü 8", true, 12.5);
    expect_calc("what is 12 times 12", false, 144);
    expect_calc("15 minus 20", false, -5);
    {
        std::string e;
        check(!to_expression(fold("saat kaç"), true, e), "not math: saat kac");
        check(!to_expression(fold("bir şarkı aç"), true, e), "not math: bir sarki ac");
        double v;
        check(!calc_eval("1/0", v), "division by zero is an error");
    }
    // wake word
    {
        bool had = false;
        check(strip_wake_word("Hey Kayra, saat kaç?", {"kayra"}, &had) == "saat kaç?" && had, "wake: hey kayra");
        check(strip_wake_word("Kaira müziği durdur", {"kayra"}, &had) == "müziği durdur" && had, "wake: kaira");
        check(strip_wake_word("Kayra.", {"kayra"}, &had).empty() && had, "wake only");
        strip_wake_word("saat kaç", {"kayra"}, &had);
        check(!had, "no wake word");
        check(mentions_wake_word("saat kaç Kayra", {"kayra"}), "wake at the end");
    }
    check(is_hallucination("Altyazı M.K."), "hallucination altyazi");
    check(is_hallucination("[Müzik]"), "hallucination [muzik]");
    check(!is_hallucination("saat kaç"), "not hallucination");
    {
        auto c = name_candidates("discordu");
        check(std::find(c.begin(), c.end(), "discord") != c.end(), "candidates discordu");
        OpenTarget t;
        check(app_index().resolve("indirilenler klasörünü", {}, t) && t.kind == "folder", "resolve downloads folder");
        check(app_index().resolve("not defterini", {}, t) && t.target == "notepad.exe", "resolve not defterini");
        check(app_index().resolve("youtube", {}, t) && t.kind == "site", "resolve youtube");
        check(app_index().resolve("hepsiburada com", {}, t) && t.target == "https://hepsiburada.com", "resolve domain");
    }
    // Turkish intents
    expect_intent("saat kaç", "time");
    expect_intent("Ankara'da yarın yağmur yağacak mı", "get_weather", {{"city", "Ankara"}});
    expect_intent("şunu hatırla: kahvemi şekersiz içerim", "remember");
    expect_intent("Tarkan'ın Şımarık şarkısını aç", "web_search", {{"site", "youtube"}});
    expect_intent("bana bir şarkı aç", "open_url");
    expect_intent("kim yaptı seni", "who_made_you");
    expect_intent("Kayra saati söyler misin", "time");
    expect_intent("bugün ayın kaçı", "date");
    expect_intent("Spotify'ı aç", "open_app", {{"name", "Spotify"}});
    expect_intent("not defterini açar mısın", "open_app");
    expect_intent("indirilenler klasörünü aç", "open_app");
    expect_intent("YouTube'da lofi müzik aç", "web_search", {{"site", "youtube"}, {"query", "lofi müzik"}});
    expect_intent("Google'da C++ dersleri ara", "web_search", {{"site", "google"}, {"query", "C++ dersleri"}});
    expect_intent("kedi videoları ara", "web_search", {{"query", "kedi videoları"}});
    expect_intent("İstanbul'da hava nasıl", "get_weather", {{"city", "İstanbul"}});
    expect_intent("hava durumu", "get_weather", {{"city", ""}});
    expect_intent("bugün hava nasıl olacak", "get_weather", {{"city", ""}});
    expect_intent("sesi yüzde 30 yap", "set_volume", {{"action", "set"}, {"level", 30}});
    expect_intent("sesi biraz kıs", "set_volume", {{"action", "down"}});
    expect_intent("sesi aç", "set_volume", {{"action", "up"}});
    expect_intent("sessize al", "set_volume", {{"action", "mute"}});
    expect_intent("sonraki şarkı", "media_control", {{"action", "next"}});
    expect_intent("müziği durdur", "media_control", {{"action", "pause"}});
    expect_intent("10 dakika sonra bana çayı hatırlat", "set_timer", {{"seconds", 600}, {"label", "çayı"}});
    expect_intent("5 dakikalık zamanlayıcı kur", "set_timer", {{"seconds", 300}});
    expect_intent("yarım saat sonra toplantıyı hatırlat", "set_timer", {{"seconds", 1800}, {"label", "toplantıyı"}});
    expect_intent("bir saat otuz dakika sonra ilacımı hatırlat", "set_timer", {{"seconds", 5400}});
    expect_intent("zamanlayıcıyı iptal et", "cancel_timers");
    expect_intent("bilgisayarı kilitle", "lock_computer");
    expect_intent("ekran görüntüsü al", "take_screenshot");
    expect_intent("bilgisayarı kapat", "power", {{"action", "shutdown"}});
    expect_intent("kapatmayı iptal et", "power", {{"action", "cancel"}});
    expect_intent("sistem durumu nasıl", "system_status");
    expect_intent("pil durumu", "system_status");
    expect_intent("125 çarpı 4 kaç eder", "calculate");
    expect_intent("Atatürk kimdir", "wikipedia", {{"topic", "Atatürk"}});
    expect_intent("kara delik nedir", "wikipedia", {{"topic", "kara delik"}});
    expect_intent("benim adım Ahmet", "set_name", {{"name", "Ahmet"}});
    expect_intent("bana patron de", "set_title", {{"title", "patron"}});
    expect_intent("hatırla ki kahvemi şekersiz içerim", "remember", {{"fact", "kahvemi şekersiz içerim"}});
    expect_intent("neleri hatırlıyorsun", "list_memory");
    expect_intent("merhaba", "hello");
    expect_intent("nasılsın", "how_are_you");
    expect_intent("teşekkürler Kayra", "thanks");
    expect_intent("sus", "stop");
    expect_intent("evet", "yes");
    expect_intent("panodakini oku", "clipboard_say");
    // English
    set_language(false);
    expect_intent("what time is it", "time");
    expect_intent("open notepad", "open_app", {{"name", "notepad"}});
    expect_intent("set a timer for 5 minutes", "set_timer", {{"seconds", 300}});
    expect_intent("remind me to call mom in 10 minutes", "set_timer", {{"seconds", 600}, {"label", "call mom"}});
    expect_intent("what's the weather in London", "get_weather", {{"city", "London"}});
    expect_intent("what's the weather like", "get_weather", {{"city", ""}});
    expect_intent("turn the volume up", "set_volume", {{"action", "up"}});
    expect_intent("set volume to 40", "set_volume", {{"action", "set"}, {"level", 40}});
    expect_intent("play lofi beats on youtube", "web_search", {{"site", "youtube"}, {"query", "lofi beats"}});
    expect_intent("who is Alan Turing", "wikipedia", {{"topic", "Alan Turing"}});
    expect_intent("what is 7 times 6", "calculate");
    expect_intent("lock the computer", "lock_computer");
    expect_intent("next song", "media_control", {{"action", "next"}});
    set_language(true);
    // text for speech
    check(Speaker::speakable("**Merhaba** 😀 bak: https://x.com burası") == "Merhaba bak: burası",
          "speakable: " + Speaker::speakable("**Merhaba** 😀 bak: https://x.com burası"));
    check(duration_text(5400) == "1 saat 30 dakika", "duration: " + duration_text(5400));
    // every reply key exists in both languages
    for (const char* k : {"time_now", "date_today", "timer_set", "reminder_set", "opening_app", "weather_now", "not_understood",
                          "brain_error", "wake_ack", "ok", "title_set", "power_go_shutdown", "confirm_power", "ui_listening"})
        check(has_key(k), std::string("text key ") + k);
    // tool schemas are valid
    Config cfg;
    json schemas = tool_schemas(cfg);
    check(schemas.is_array() && schemas.size() >= 18, "tool schemas: " + std::to_string(schemas.size()));
    outln(std::to_string(g_pass) + " passed, " + std::to_string(g_fail) + " failed");
    return g_fail ? 1 : 0;
}

void usage() {
    outln(std::string("Kayra ") + KAYRA_VERSION + " - kişisel yapay zekâ asistanı / personal AI assistant\n"
          "\n"
          "  kayra-cli                       sohbet / chat (type, Enter; 'q' quits)\n"
          "  kayra-cli ask \"saat kaç\"         one question, prints the reply\n"
          "  kayra-cli say \"metin\" [--wav f]  speak, or write a WAV\n"
          "  kayra-cli voices                 Windows voices\n"
          "  kayra-cli listen                 microphone -> commands (Ctrl+C stops)\n"
          "  kayra-cli transcribe file.wav    speech to text\n"
          "  kayra-cli model [name]           download a speech model\n"
          "  kayra-cli apps [name]            installed apps / what a name opens\n"
          "  kayra-cli intent \"text\"          how the built-in parser reads a sentence\n"
          "  kayra-cli brain                  test the LLM connection\n"
          "  kayra-cli selftest\n"
          "\n"
          "  --lang tr|en   --voice   (speak replies in chat/ask)   --dry-run (no side effects)");
}

}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::vector<std::string> a = utf8_args();
    a.erase(a.begin());
    Config cfg;
    cfg.load();
    std::string lang = opt(a, "--lang");
    if (!lang.empty()) cfg.language = lang == "en" ? "en" : "tr";
    bool voice = flag(a, "--voice");
    bool dry = flag(a, "--dry-run");
    set_language(cfg.turkish());
    set_address(cfg.address());
    std::string cmd = a.empty() ? "chat" : a[0];
    if (!a.empty()) a.erase(a.begin());
    auto rest = [&] {
        std::string s;
        for (auto& x : a) s += (s.empty() ? "" : " ") + x;
        return s;
    };

    if (cmd == "-h" || cmd == "--help" || cmd == "help") {
        usage();
        return 0;
    }
    if (cmd == "selftest") return selftest();
    if (cmd == "--version" || cmd == "version") {
        outln(std::string("Kayra ") + KAYRA_VERSION);
        return 0;
    }
    if (cmd == "intent") {
        Intent it;
        if (!parse_intent(strip_wake_word(rest(), cfg.wake_words, nullptr), it)) {
            outln("(no built-in match: goes to the brain)");
            return 0;
        }
        json j = {{"tool", it.tool}, {"args", it.args}, {"reply", it.reply_key}, {"special", it.special}, {"confidence", it.confidence}};
        outln(j.dump(2));
        return 0;
    }
    if (cmd == "get") {
        HttpResponse r = http_get(rest());
        outln("HTTP " + std::to_string(r.status) + (r.error.empty() ? "" : "  error: " + r.error));
        outln(r.body.substr(0, 600));
        return r.ok() && r.error.empty() ? 0 : 1;
    }
    if (cmd == "voices") {
        for (auto& v : Speaker::voices()) outln("[" + v.lang + "] " + v.name);
        return 0;
    }
    if (cmd == "say") {
        std::string wav = opt(a, "--wav");
        std::string text = rest();
        if (!wav.empty()) {
            double s = Speaker::synth_to_wav(text, wav, cfg.turkish() ? "tr" : "en", cfg.voice, cfg.voice_rate);
            if (s < 0) {
                outln("synthesis failed");
                return 1;
            }
            outln(wav + " " + format_number(s, 2, false) + " s");
            return 0;
        }
        Speaker sp;
        sp.start();
        sp.configure(cfg.turkish() ? "tr" : "en", cfg.voice, cfg.voice_rate, cfg.voice_volume);
        sp.speak(text);
        Sleep(300);
        while (sp.speaking()) Sleep(50);
        outln(sp.voice_name());
        return 0;
    }
    if (cmd == "apps") {
        app_index().build();
        if (a.empty()) {
            outln(std::to_string(app_index().size()) + " entries");
            return 0;
        }
        OpenTarget t;
        if (app_index().resolve(rest(), cfg.apps, t)) outln(t.display + "  ->  " + t.target + "  (" + t.kind + ")");
        else outln("not found");
        return 0;
    }
    if (cmd == "model") {
        std::string name = a.empty() ? cfg.stt_model : a[0];
        std::string path = Stt::model_path(name);
        if (file_exists(path)) {
            outln("already here: " + path);
            return 0;
        }
        std::string err;
        int last = -1;
        bool ok = http_download(
            Stt::model_url(name), path,
            [&](int64_t got, int64_t total) {
                int pct = total > 0 ? (int)(got * 100 / total) : -1;
                if (pct != last) {
                    last = pct;
                    out("\r" + std::to_string(pct) + "%  " + std::to_string(got / 1048576) + " MB   ");
                }
            },
            nullptr, &err);
        outln(ok ? "\ndone: " + path : "\nfailed: " + err);
        return ok ? 0 : 1;
    }
    if (cmd == "transcribe") {
        std::vector<float> pcm;
        std::string err;
        if (a.empty() || !read_wav(a[0], pcm, err)) {
            outln("transcribe: " + (a.empty() ? std::string("give a WAV file") : err));
            return 1;
        }
        std::string name = opt(a, "--model", cfg.stt_model);
        Stt stt;
        if (!stt.load(Stt::model_path(name), &err)) {
            outln("model: " + err + " (run: kayra-cli model " + name + ")");
            return 1;
        }
        std::string text = stt.transcribe(pcm, cfg.turkish() ? "tr" : "en", "Kayra");
        outln(text);
        outln("(" + format_number(stt.last_seconds(), 2, false) + " s for " + format_number(pcm.size() / 16000.0, 1, false) + " s of audio)");
        return 0;
    }
    if (cmd == "brain") {
        LlmClient c(cfg);
        if (!c.configured()) {
            outln("no brain configured (provider: " + cfg.provider + ")");
            return 1;
        }
        std::string info;
        bool ok = c.test(info);
        outln((ok ? "OK: " : "FAILED: ") + info);
        return ok ? 0 : 1;
    }

    Speaker sp;
    if (voice) {
        sp.start();
        sp.configure(cfg.turkish() ? "tr" : "en", cfg.voice, cfg.voice_rate, cfg.voice_volume);
    }
    std::thread([] { app_index().build(); }).detach();
    Assistant as(cfg, voice ? &sp : nullptr);
    as.set_dry_run(dry);
    auto wait_voice = [&] {
        if (!voice) return;
        Sleep(200);
        while (sp.speaking()) Sleep(50);
    };

    if (cmd == "ask") {
        while (!app_index().ready()) Sleep(20);
        outln(as.ask(rest()));
        wait_voice();
        return 0;
    }
    if (cmd == "listen") {
        Listener ls(cfg);
        as.start();
        ls.on_command = [&](const std::string& c) {
            outln("> " + c);
            outln(as.ask(c));
            ls.follow_up(cfg.follow_up_seconds);
        };
        ls.on_wake = [&] { outln(as.ask("")); };
        ls.on_heard = [&](const std::string& h) { outln("  (" + h + ")"); };
        ls.start();
        Listener::Phase last = Listener::Phase::Off;
        for (;;) {
            if (voice) ls.set_suppressed(sp.speaking());
            Listener::Phase p = ls.phase();
            if (p != last) {
                static const char* names[] = {"off", "downloading", "loading", "listening", "hearing", "transcribing", "error"};
                outln(std::string("[") + names[(int)p] + "]" + (p == Listener::Phase::Error ? " " + ls.error() : ""));
                last = p;
                if (p == Listener::Phase::Error) return 1;
            }
            Sleep(50);
        }
    }
    // chat
    outln(std::string("Kayra ") + KAYRA_VERSION + (as.brain_connected() ? "  ·  " + cfg.provider + " / " + cfg.model : "") +
          "   ('q' = çıkış / quit)");
    std::string line;
    for (;;) {
        out("\n> ");
        if (!read_line(line)) break;
        line = trim(line);
        if (line == "q" || line == "quit" || line == "exit" || line == "çık") break;
        if (line.empty()) continue;
        outln(as.ask(line));
        wait_voice();
    }
    return 0;
}

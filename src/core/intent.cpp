#include "core/intent.h"

#include <map>
#include <regex>

#include "core/access.h"
#include "core/apps.h"
#include "core/calc.h"
#include "core/i18n.h"
#include "core/util.h"

using json = nlohmann::json;

namespace kayra {

namespace {

// The text as original words and folded words, so a match on the folded text
// can give back the words as the user wrote them ("İstanbul'da").
struct Utter {
    std::vector<std::string> ow;
    std::vector<std::string> fw;
    std::vector<size_t> start;  // offset of fw[i] in f (npos when empty)
    std::string f;

    explicit Utter(const std::string& text) {
        for (auto& w : split_words(text)) {
            std::string o = w;
            // strip punctuation around the word, keep inner apostrophes
            auto edge = [](unsigned char c) {
                return c == ',' || c == '.' || c == '!' || c == '?' || c == ';' || c == ':' || c == '"' || c == '(' ||
                       c == ')';
            };
            while (!o.empty() && edge((unsigned char)o.back())) o.pop_back();
            while (!o.empty() && edge((unsigned char)o.front())) o.erase(o.begin());
            // curly quotes
            for (const char* q : {"\xE2\x80\x9C", "\xE2\x80\x9D"}) o = replace_all(o, q, "");
            std::string fo = fold(w);
            if (fo.empty() && o.empty()) continue;
            ow.push_back(o);
            fw.push_back(fo);
            if (fo.empty()) {
                start.push_back(std::string::npos);
            } else {
                if (!f.empty()) f += ' ';
                start.push_back(f.size());
                f += fo;
            }
        }
    }

    // original words covering [pos, pos+len) of f
    std::string orig(size_t pos, size_t len) const {
        std::string out;
        for (size_t i = 0; i < ow.size(); i++) {
            if (start[i] == std::string::npos) continue;
            size_t a = start[i], b = start[i] + fw[i].size();
            if (b > pos && a < pos + len) out += (out.empty() ? "" : " ") + ow[i];
        }
        return out;
    }
};

const std::regex& re(const std::string& p) {
    thread_local std::map<std::string, std::regex> cache;
    auto it = cache.find(p);
    if (it == cache.end()) it = cache.emplace(p, std::regex(p, std::regex::ECMAScript | std::regex::optimize)).first;
    return it->second;
}

bool rx(const std::string& f, const std::string& p, std::smatch& m) { return std::regex_search(f, m, re(p)); }
bool rx(const std::string& f, const std::string& p) {
    std::smatch m;
    return std::regex_search(f, m, re(p));
}

std::string group(const Utter& u, const std::smatch& m, int g) {
    if (!m[g].matched) return "";
    return u.orig((size_t)m.position(g), (size_t)m.length(g));
}

// Seconds in "1 saat 30 dakika", "5 minutes", "yarım saat", "an hour".
double find_duration(const std::vector<std::string>& w, std::vector<bool>* used = nullptr) {
    double total = 0;
    for (size_t i = 0; i < w.size(); i++) {
        double mult = 0;
        const std::string& u = w[i];
        if (u == "saniye" || u == "sn" || starts_with(u, "second") || u == "sec" || u == "secs" || starts_with(u, "saniyelik")) mult = 1;
        else if (u == "dakika" || u == "dk" || starts_with(u, "minute") || u == "min" || u == "mins" || starts_with(u, "dakikalik")) mult = 60;
        else if (u == "saat" || starts_with(u, "hour") || u == "saatlik") mult = 3600;
        else if (u == "gun" || starts_with(u, "day") || u == "gunluk") mult = 86400;
        if (!mult || i == 0) continue;
        const std::string& n = w[i - 1];
        double v = -1;
        if (n == "yarim" || n == "half") v = 0.5;
        else if (n == "a" || n == "an") v = 1;
        else {
            char* end = nullptr;
            std::string t = replace_all(n, ",", ".");
            double x = std::strtod(t.c_str(), &end);
            if (end && *end == 0) v = x;
        }
        if (v < 0) continue;
        total += v * mult;
        if (used) (*used)[i] = (*used)[i - 1] = true;
        // "half an hour"
        if (n == "an" && i >= 2 && w[i - 2] == "half") {
            total -= 0.5 * mult;
            if (used) (*used)[i - 2] = true;
        }
    }
    return total;
}

Intent tool(const char* name, json args = json::object(), double conf = 1.0) {
    Intent it;
    it.tool = name;
    it.args = std::move(args);
    it.confidence = conf;
    return it;
}

Intent reply(const char* key, double conf = 0.6) {
    Intent it;
    it.reply_key = key;
    it.confidence = conf;
    return it;
}

Intent special(const char* s, double conf = 1.0) {
    Intent it;
    it.special = s;
    it.confidence = conf;
    return it;
}

bool is_one_of(const std::string& f, std::initializer_list<const char*> list) {
    for (const char* x : list)
        if (f == x) return true;
    return false;
}

std::string strip_trailing(std::string s, std::initializer_list<const char*> words) {
    bool again = true;
    while (again) {
        again = false;
        for (const char* w : words) {
            std::string sw = std::string(" ") + w;
            if (ends_with(s, sw)) {
                s = s.substr(0, s.size() - sw.size());
                again = true;
            } else if (s == w) {
                s.clear();
            }
        }
    }
    return trim(s);
}

}  // namespace

std::string strip_wake_word(const std::string& text, const std::vector<std::string>& wake_words, bool* had) {
    if (had) *had = false;
    std::vector<std::string> words = split_words(text);
    size_t i = 0;
    auto is_wake = [&](const std::string& w) {
        std::string f = fold(w);
        for (auto& k : wake_words) {
            std::string kf = fold(k);
            if (f == kf) return true;
            if (kf.size() >= 4 && f.size() >= 4 && similarity(f, kf) >= 0.75) return true;
        }
        return false;
    };
    if (i < words.size() && is_one_of(fold(words[i]), {"hey", "hi", "ok", "okay", "hay", "ey", "hei", "selam", "merhaba"}) &&
        i + 1 < words.size() && is_wake(words[i + 1])) {
        i += 2;
    } else if (i < words.size() && is_wake(words[i])) {
        i += 1;
    } else {
        return text;
    }
    if (had) *had = true;
    std::string out;
    for (size_t k = i; k < words.size(); k++) out += (out.empty() ? "" : " ") + words[k];
    out = trim(out);
    while (!out.empty() && (out[0] == ',' || out[0] == '.' || out[0] == '!' || out[0] == '-')) out = trim(out.substr(1));
    return out;
}

bool mentions_wake_word(const std::string& text, const std::vector<std::string>& wake_words) {
    for (auto& w : split_words(fold(text)))
        for (auto& k : wake_words) {
            std::string kf = fold(k);
            if (w == kf || (kf.size() >= 4 && w.size() >= 4 && similarity(w, kf) >= 0.8)) return true;
        }
    return false;
}

bool parse_intent(const std::string& text_in, Intent& out) {
    Utter u(text_in);
    std::string f = u.f;
    if (f.empty()) return false;
    std::smatch m;
    // drop polite fillers at the end
    std::string g = strip_trailing(f, {"lutfen", "please", "kayra", "hemen", "simdi", "now"});
    if (g.empty()) g = f;

    // ---------------------------------------------------------------- control
    if (is_one_of(g, {"sus", "dur", "kes", "yeter", "sessiz ol", "tamam yeter", "sus artik", "kes artik", "stop",
                      "be quiet", "shut up", "enough", "stop talking", "quiet", "sessiz", "tamam sus"})) {
        out = special("stop");
        return true;
    }
    if (is_one_of(g, {"evet", "evet eminim", "eminim", "onayliyorum", "onayla", "tabii", "tabi", "olur", "yap", "evet yap",
                      "yes", "yeah", "yep", "confirm", "do it", "yes do it", "sure"})) {
        out = special("yes", 0.95);
        return true;
    }
    if (is_one_of(g, {"hayir", "vazgec", "vazgectim", "iptal", "iptal et", "hayir yapma", "yapma", "no", "nope", "cancel",
                      "never mind", "nevermind", "dont"})) {
        out = special("no", 0.95);
        return true;
    }

    // ---------------------------------------------------------------- power
    if (rx(g, "(kapatmayi|kapanmayi|kapanisi|yeniden baslatmayi) (iptal et|durdur|iptal)") ||
        rx(g, "(cancel|abort|stop) (the )?(shutdown|restart)")) {
        out = tool("power", {{"action", "cancel"}});
        return true;
    }

    // ---------------------------------------------------------------- timers
    if (rx(g, "(zamanlayici\\w*|alarm\\w*|hatirlatma\\w*|geri sayim\\w*) (iptal|durdur|sil|kapat|kaldir)") ||
        rx(g, "(cancel|stop|clear|delete) (the |all |my )?(timers?|reminders?|alarms?)")) {
        out = tool("cancel_timers");
        return true;
    }
    {
        std::vector<std::string> w = numbers_from_words(split_words(g));
        std::vector<bool> used(w.size(), false);
        double secs = find_duration(w, &used);
        bool remind = rx(g, "hatirlat|remind");
        bool timer = rx(g, "zamanlayici|alarm|geri sayim|sayac|timer|kur\\b|kurar|set a|countdown|sonra|icinde|^in\\b| in \\d");
        if (secs > 0 && (remind || timer)) {
            std::string label;
            if (remind) {
                static const char* drop[] = {"sonra", "icinde", "bana", "beni", "hatirlat", "hatirlatir", "hatirlatsana",
                                             "misin", "musun", "lutfen", "diye", "kayra", "zamanlayici", "kur", "remind",
                                             "me", "in", "please", "hatirlatma", "olarak", "sonrasinda", "yarim", "half",
                                             "bucuk", "saniye", "sn", "dakika", "dk", "saat", "gun", "second", "seconds",
                                             "minute", "minutes", "hour", "hours", "day", "days", "and", "ve"};
                auto is_unit = [](const std::string& x) {
                    return is_one_of(x, {"saniye", "sn", "dakika", "dk", "saat", "gun", "second", "seconds", "minute",
                                         "minutes", "hour", "hours", "day", "days"});
                };
                std::vector<std::string> keep;
                for (size_t i = 0; i < u.ow.size(); i++) {
                    const std::string& x = u.fw[i];
                    if (x.empty()) continue;
                    std::string next = i + 1 < u.fw.size() ? u.fw[i + 1] : "";
                    bool d = false;
                    for (const char* k : drop) d = d || x == k;
                    auto conv = numbers_from_words({x});
                    bool numeric = std::strtod(replace_all(x, ",", ".").c_str(), nullptr) != 0 || x == "0";
                    if (numeric || (conv.size() == 1 && conv[0] != x)) d = true;
                    if (is_one_of(x, {"bir", "on", "a", "an"}) && (is_unit(next) || numbers_from_words({next})[0] != next))
                        d = true;
                    if (keep.empty() && x == "to") d = true;
                    if (!d) keep.push_back(u.ow[i]);
                }
                for (auto& x : keep) label += (label.empty() ? "" : " ") + x;
            }
            out = tool("set_timer", {{"seconds", secs}, {"label", trim(label)}});
            return true;
        }
    }

    // ---------------------------------------------------------------- memory / identity
    if (rx(g, "^(benim )?adim (ne|neydi)$") || rx(g, "^(whats|what is) my name$") || rx(g, "^ben kimim$")) {
        out = special("get_name");
        return true;
    }
    if (rx(g, "^(benim )?(adim|ismim) (.+)$", m) || rx(g, "^my name is (.+)$", m)) {
        out = special("set_name");
        out.args = {{"name", group(u, m, (int)m.size() - 1)}};
        out.args["name"] = strip_trailing(out.args["name"].get<std::string>(), {});
        return true;
    }
    if (rx(g, "^(bana|beni) (.+) (diye )?(cagir|de|hitap et)$", m) || rx(g, "^call me (.+)$", m)) {
        out = special("set_title");
        int gi = m.size() > 4 ? 2 : 1;
        out.args = {{"title", group(u, m, gi)}};
        return true;
    }
    if (rx(g, "(neleri? hatirliyorsun|ne biliyorsun benim hakkimda|benim hakkimda ne biliyorsun|hatirladiklarin|hafizanda ne var|what do you (remember|know about me))")) {
        out = tool("list_memory");
        return true;
    }
    if (rx(g, "^((bunu|sunu|su) )?(hatirla|unutma|aklinda tut|not al|not et|kaydet) (ki )?(.+)$", m) ||
        rx(g, "^(remember|note|dont forget) (that )?(.+)$", m)) {
        out = tool("remember", {{"fact", group(u, m, (int)m.size() - 1)}});
        return true;
    }
    if (rx(g, "^(.+) (oldugunu |diye )?(unutma|hatirla|aklinda tut)$", m) && !is_one_of(m[1].str(), {"beni", "bunu", "sakin"})) {
        out = tool("remember", {{"fact", group(u, m, 1)}});
        return true;
    }
    if (rx(g, "^forget (about )?(.+)$", m)) {
        out = tool("forget", {{"about", group(u, m, 2)}});
        return true;
    }
    if (rx(g, "^(.+?) (hakkindakileri |ile ilgili )?(unut|hafizandan sil)$", m)) {
        out = tool("forget", {{"about", group(u, m, 1)}});
        return true;
    }

    // ---------------------------------------------------------------- power, lock, screenshot
    if (rx(g, "(bilgisayar\\w*|pc\\w*|sistem\\w*) (kapat|kapatir misin|kapatsana)$") ||
        rx(g, "(shut ?down|turn off|power off) (the |my )?(computer|pc|system)")) {
        out = tool("power", {{"action", "shutdown"}});
        return true;
    }
    if (rx(g, "(bilgisayar\\w*|pc\\w*|sistem\\w*) (yeniden baslat|resetle|restart)") ||
        rx(g, "(restart|reboot) (the |my )?(computer|pc|system)")) {
        out = tool("power", {{"action", "restart"}});
        return true;
    }
    if (rx(g, "(uyku moduna (al|gec|sok)|bilgisayari uyut)") || rx(g, "(put )?(the |my )?(computer|pc) to sleep|^sleep mode")) {
        out = tool("power", {{"action", "sleep"}});
        return true;
    }
    if (rx(g, "^((bilgisayar\\w*|ekran\\w*|pc\\w*) )?kilitle") || rx(g, "^lock( the| my)? (computer|pc|screen)")) {
        out = tool("lock_computer");
        return true;
    }
    if (rx(g, "ekran goruntusu|ekran resmi|screenshot|screen shot|ekrani (cek|kaydet|fotografla)")) {
        out = tool("take_screenshot");
        return true;
    }

    // ---------------------------------------------------------------- time and date
    if (rx(g, "saat (kac|ne|kaci)|saati (soyle|soyler)|^saat$|what time|the time|whats the time|current time")) {
        out = special("time");
        return true;
    }
    if (rx(g, "(bugun|bu gun) (ayin kaci|gunlerden ne|ne gunu|hangi gun|tarih)|tarih (ne|nedir)|bugunun tarihi|hangi gundeyiz|gunlerden ne|ayin kaci|what (day|date)|todays date|the date")) {
        out = special("date");
        return true;
    }

    // ---------------------------------------------------------------- volume
    if (rx(g, "ses\\w* (seviyesi )?(yuzde )?(\\d+)( e| a| ye| ya)? ?(yap|ayarla|olsun|getir|cek|indir|cikar|dusur)", m)) {
        out = tool("set_volume", {{"action", "set"}, {"level", std::stoi(m[3].str())}});
        return true;
    }
    if (rx(g, "(set )?(the )?volume (to |at )?(\\d+)", m)) {
        out = tool("set_volume", {{"action", "set"}, {"level", std::stoi(m[4].str())}});
        return true;
    }
    if (rx(g, "sessize al|sesi kapat|sesi tamamen kapat|^mute|mute (the )?(sound|volume|audio)")) {
        out = tool("set_volume", {{"action", "mute"}});
        return true;
    }
    if (rx(g, "sesi geri ac|sesi tekrar ac|unmute")) {
        out = tool("set_volume", {{"action", "unmute"}});
        return true;
    }
    if (rx(g, "ses\\w* (biraz )?(daha )?(ac|yukselt|artir|arttir|kok|fazlalastir)|daha sesli|sesi sonuna kadar|volume up|louder|turn (it |the volume )?up")) {
        out = tool("set_volume", {{"action", rx(g, "sonuna kadar|full|max") ? "max" : "up"}});
        return true;
    }
    if (rx(g, "ses\\w* (biraz )?(daha )?(kis|azalt|dusur|alcalt)|daha sessiz|volume down|quieter|turn (it |the volume )?down")) {
        out = tool("set_volume", {{"action", "down"}});
        return true;
    }
    if (rx(g, "ses seviyesi (kac|ne)|ses kac|what( is|s) the volume")) {
        out = tool("set_volume", {{"action", "get"}});
        return true;
    }

    // ---------------------------------------------------------------- media
    if (rx(g, "(sonraki|siradaki|bir sonraki) (sarki|parca|sarkiya|parcaya)|sarkiyi (gec|atla)|^(gec|atla)$|next (song|track)|^skip")) {
        out = tool("media_control", {{"action", "next"}});
        return true;
    }
    if (rx(g, "(onceki|bir onceki) (sarki|parca|sarkiya|parcaya)|previous (song|track)|go back a (song|track)")) {
        out = tool("media_control", {{"action", "previous"}});
        return true;
    }
    if (rx(g, "(muzig|sarkiy|videoy|parcay)\\w* (durdur|duraklat)|^(duraklat|durdur)$|^pause|pause (the )?(music|song|video)")) {
        out = tool("media_control", {{"action", "pause"}});
        return true;
    }
    // "Tarkan'ın Şımarık şarkısını aç" -> YouTube; "bir şarkı aç" -> YouTube Music
    if (rx(g, "^(.+?) (sarkisini|sarkisi|parcasini|sarkilarini|sarkilari|klibini) (ac|cal|oynat|acar misin|calar misin|dinlet)$", m) ||
        rx(g, "^play (?!.* on youtube$)(the song |the track )?(.+)$", m)) {
        std::string q = starts_with(g, "play ") ? group(u, m, 2) : group(u, m, 1);
        if (!is_one_of(fold(q), {"bir", "bana bir", "guzel bir", "bana guzel bir", "some", "music", "some music",
                                 "the music", "a song", "some songs", "something"})) {
            out = tool("web_search", {{"query", q}, {"site", "youtube"}});
            return true;
        }
    }
    if (rx(g, "^((bana )?(guzel )?bir )?(sarki|muzik|parca) (ac|cal|acar misin|calar misin|calsana|acsana)$|^play (some )?music$")) {
        out = tool("open_url", {{"url", "https://music.youtube.com"}, {"title", "YouTube Music"}});
        return true;
    }
    if (rx(g, "(muzig|sarkiy|videoy|parcay)\\w* (devam ettir|devam et|baslat|oynat|cal|ac)|^muzik (baslat)|^(devam et|oynat)$|^(play|resume)( the)? (music|song)?$|^resume")) {
        out = tool("media_control", {{"action", "play"}});
        return true;
    }

    // ---------------------------------------------------------------- weather
    if (rx(g, "\\bhava\\b|havalar|kac derece|sicaklik|yagmur yag|kar yag|weather|temperature|how (hot|cold)|going to rain|will it rain")) {
        std::string city;
        if (rx(g, "^(.+?) (icin )?(hava|havalar|kac derece|sicaklik)", m)) {
            std::string c = group(u, m, 1);
            std::string cf = fold(c);
            static const char* nope[] = {"bugun", "yarin", "su an", "simdi", "disarida", "disari", "disarisi", "bugunku",
                                         "bu gun", "nasil", "bugun disarida", "su anki"};
            bool skip = false;
            for (const char* n : nope) skip = skip || cf == n;
            if (!skip) {
                // drop leading time words: "bugün Ankara'da"
                std::vector<std::string> cw = split_words(c);
                while (!cw.empty() && is_one_of(fold(cw.front()), {"bugun", "yarin", "simdi", "su", "an", "acaba"}))
                    cw.erase(cw.begin());
                city.clear();
                for (auto& x : cw) city += (city.empty() ? "" : " ") + x;
            }
        }
        std::smatch m2;
        if (rx(g, "(weather|temperature|how hot is it|how cold is it|rain) (like )?(in|at|for) (.+)$", m2)) {
            city = group(u, m2, 4);
        } else if (rx(g, "^(.+) weather", m2)) {
            std::string pre = m2[1].str();
            if (!rx(pre, "\\b(the|whats|what|how|hows|todays|is)\\b")) city = group(u, m2, 1);
            else if (rx(g, "weather", m2) && !rx(g, "\\bhava\\b")) city.clear();
        }
        if (city.empty()) {
            // "Ankara'da yarın yağmur yağacak mı": a place in the locative anywhere in the sentence
            static const std::regex loc("^(.+)(?:'|\xE2\x80\x99)(da|de|ta|te|daki|deki|taki|teki)$");
            for (auto& w : split_words(text_in)) {
                std::smatch lm;
                std::string x = w;
                while (!x.empty() && std::ispunct((unsigned char)x.back())) x.pop_back();
                if (std::regex_match(x, lm, loc)) {
                    city = lm[1].str();
                    break;
                }
            }
        }
        // cut the Turkish case ending after an apostrophe: "İstanbul'da" -> "İstanbul"
        size_t ap = city.find('\'');
        if (ap == std::string::npos) ap = city.find("\xE2\x80\x99");
        if (ap != std::string::npos) city = city.substr(0, ap);
        out = tool("get_weather", {{"city", trim(city)}});
        return true;
    }

    // ---------------------------------------------------------------- system
    if (rx(g, "sistem durumu|sistem raporu|bilgisayar\\w* (durumu|nasil)|islemci (kullanimi|durumu|yuzde)|ram (kullanimi|durumu)|bellek (kullanimi|durumu)|(pil|sarj|batarya)\\w* (durumu|kac|yuzde|ne kadar|seviyesi)|^(pil|sarj|batarya)$|disk (durumu|alani)|system (status|report)|battery|cpu (usage|load)|memory usage|diagnostics")) {
        out = tool("system_status");
        return true;
    }

    // ---------------------------------------------------------------- clipboard
    if (rx(g, "pano\\w* (oku|ne var)|kopyaladigim\\w* (oku|ne)|read (my |the )?clipboard|whats on (my |the )?clipboard")) {
        out = special("clipboard_say");
        return true;
    }

    // ---------------------------------------------------------------- news
    if (rx(g, "^(bana )?(son |guncel |bugunun |gunun )?(dakika )?haber\\w*( neler| ne var| var mi| oku| okur musun| okusana| soyle| ver)?$") ||
        rx(g, "^(gundemde ne var|gundem ne|neler oluyor|dunyada neler oluyor)$") ||
        rx(g, "^((whats|what is) (in )?the news|(read |tell me )?(me )?(the )?(latest |todays )?(news|headlines))$")) {
        out = tool("get_news");
        return true;
    }
    if (rx(g, "^(.+?) (ile ilgili|hakkinda|hakkindaki) (son )?haber\\w*( neler| ne var| var mi| oku| soyle)?$", m) ||
        rx(g, "^(?:son |guncel )?(.+?) haberleri( neler| ne| oku)?$", m) || rx(g, "^(?:the )?(?:latest )?news (?:about|on) (.+)$", m)) {
        std::string topic = group(u, m, 1);
        if (!fold(topic).empty()) {
            out = tool("get_news", {{"topic", topic}});
            return true;
        }
    }

    // ---------------------------------------------------------------- files
    if (rx(g, "^(.+?) (klasorunde |klasorumde |klasorunun icinde )?(ne var|neler var|hangi dosyalar var|neler duruyor)$", m) ||
        rx(g, "^(.+?) (klasorunu |klasorumu |dosyalarini )?listele$", m) ||
        rx(g, "^(?:whats|what is) (?:on|in) (?:my |the )?(.+?)(?: folder)?$", m) ||
        rx(g, "^(?:list|show) (?:me )?(?:the )?(?:files (?:on|in) )?(?:my |the )?(.+?)(?: folder)?$", m)) {
        std::string what = fold(m[1].str());
        std::string id = folder_alias(what);
        if (!id.empty()) {
            out = tool("list_folder", {{"path", id}});
            return true;
        }
    }
    if (rx(g, "^(.+?) (?:adli |isimli )?(?:dosya\\w*|belge\\w*|klasor\\w*) (?:bul|ara|nerede|nerde|arar misin|bulur musun|bulabilir misin)$", m) ||
        rx(g, "^(?:find|search for|look for|locate) (?:the |my )?(?:file|files|document|folder) (?:named |called )?(.+)$", m) ||
        rx(g, "^where is (?:the |my )?(?:file )?(.+ (?:txt|pdf|docx?|xlsx?|pptx?|png|jpe?g|mp3|mp4|zip|csv))$", m)) {
        std::string q = group(u, m, 1);
        if (!fold(q).empty()) {
            out = tool("find_files", {{"query", q}});
            return true;
        }
    }
    if (rx(g, "^(.+?) (?:dosyasini|belgesini|dosyasindakileri|dosyasinin icerigini) (?:oku|okur musun|okusana|oku bana)$", m) ||
        rx(g, "^(.+?) (?:dosyasinda|belgesinde) (?:ne yaziyor|ne var|neler yaziyor)$", m) ||
        rx(g, "^read (?:me )?(?:the |my )?file (.+)$", m) ||
        rx(g, "^read (?:me )?(?:the |my )?(.+ (?:txt|md|csv|json|log))$", m)) {
        std::string q = group(u, m, 1);
        if (!fold(q).empty()) {
            out = tool("read_file", {{"path", q}});
            return true;
        }
    }

    // ---------------------------------------------------------------- web research
    if (rx(g, "^(?:internette|internetten|webde) (.+?) (?:hakkinda )?(?:arastir|ara ve oku|ne diyor|ne yaziyor|bilgi bul)$", m) ||
        rx(g, "^(.+?) (?:hakkinda |ile ilgili )?(?:arastir|arastirma yap|internette arastir|internetten arastir|internetten bak|internete bak)$", m) ||
        rx(g, "^(?:research|look up online|search online for|check online|find online) (.+)$", m)) {
        std::string q = group(u, m, 1);
        if (!fold(q).empty()) {
            out = tool("search_internet", {{"query", q}});
            return true;
        }
    }

    // ---------------------------------------------------------------- search
    if (rx(g, "^youtube\\w* (.+?) (ac|ara|oynat|cal|bul|izle|izlet|acar misin|arar misin)$", m) ||
        rx(g, "^(.+?) youtube\\w* (ac|ara|oynat|cal|bul|izle|acar misin|arar misin)$", m) ||
        rx(g, "^(play|search|find|watch) (.+) on youtube$", m) || rx(g, "^youtube (.+)$", m)) {
        std::string q;
        if (starts_with(g, "youtube") && m.size() >= 2) q = group(u, m, 1);
        else if (rx(g, "^(play|search|find|watch) (.+) on youtube$", m)) q = group(u, m, 2);
        else q = group(u, m, 1);
        if (!fold(q).empty()) {
            out = tool("web_search", {{"query", q}, {"site", "youtube"}});
            return true;
        }
    }
    if (rx(g, "^(.+?) (haritada|haritalarda) (goster|bul|ac)$", m) || rx(g, "^(.+?) (nerede|nerde)$", m) ||
        rx(g, "^show (.+) on (the )?map$", m) || rx(g, "^(directions|navigate) to (.+)$", m)) {
        std::string q = rx(g, "^(directions|navigate) to (.+)$", m) ? group(u, m, 2) : group(u, m, 1);
        if (!q.empty()) {
            out = tool("web_search", {{"query", q}, {"site", "maps"}}, 0.9);
            return true;
        }
    }
    if (rx(g, "^(google\\w* |internette |webde |internetten )(.+?) (ara|arat|bul|arar misin|aratir misin)$", m) ||
        rx(g, "^(.+?) (diye )?(ara|arat|googlela|arar misin|aratir misin|internette ara|googleda ara)$", m) ||
        rx(g, "^(search|google|look up|search the web for|search for) (.+)$", m)) {
        std::string q;
        if (rx(g, "^(search|google|look up|search the web for|search for) (.+)$", m)) q = group(u, m, 2);
        else if (rx(g, "^(google\\w* |internette |webde |internetten )(.+?) (ara|arat|bul|arar misin|aratir misin)$", m)) q = group(u, m, 2);
        else {
            rx(g, "^(.+?) (diye )?(ara|arat|googlela|arar misin|aratir misin|internette ara|googleda ara)$", m);
            q = group(u, m, 1);
            std::vector<std::string> qw = split_words(q);
            while (!qw.empty() && is_one_of(fold(qw.back()), {"internette", "googleda", "webde", "google", "diye"})) qw.pop_back();
            while (!qw.empty() && is_one_of(fold(qw.front()), {"internette", "googleda", "webde", "google"})) qw.erase(qw.begin());
            q.clear();
            for (auto& x : qw) q += (q.empty() ? "" : " ") + x;
        }
        // "beni ara" / "annemi ara" means a phone call, not a search
        if (!q.empty() && !rx(fold(q), "^(beni|annemi|babami|onu|bizi)$")) {
            out = tool("web_search", {{"query", q}, {"site", "google"}}, 0.9);
            return true;
        }
    }

    // ---------------------------------------------------------------- open
    {
        std::string name;
        if (rx(g, "^(open|launch|start|run|go to|show me|bring up) (.+)$", m)) name = group(u, m, 2);
        else if (rx(g, "^(.+?) (sitesine|adresine|klasorune|sayfasina) (git|gir|gidelim)$", m)) name = group(u, m, 1);
        else if (rx(g, "^(.+?) (ac|acar misin|acsana|acabilir misin|baslat|calistir|ac bakalim|aciver)$", m)) name = group(u, m, 1);
        if (!fold(name).empty()) {
            OpenTarget t;
            std::string clean = name;
            // "Discord'u" -> "Discord"
            size_t ap = clean.find('\'');
            if (ap == std::string::npos) ap = clean.find("\xE2\x80\x99");
            if (ap != std::string::npos && clean.find(' ', ap) == std::string::npos) clean = clean.substr(0, ap);
            bool known = app_index().resolve(clean, {}, t) || app_index().resolve(name, {}, t);
            out = tool("open_app", {{"name", clean}}, known ? 1.0 : 0.5);
            return true;
        }
    }

    // ---------------------------------------------------------------- math
    {
        std::string expr;
        if (to_expression(g, is_turkish(), expr)) {
            out = tool("calculate", {{"expression", expr}});
            return true;
        }
    }

    // ---------------------------------------------------------------- small talk
    if (rx(g, "^(gunaydin)")) {
        out = reply("greet_morning");
        return true;
    }
    if (rx(g, "^(iyi aksamlar)")) {
        out = reply("greet_evening");
        return true;
    }
    if (rx(g, "^(iyi geceler|good night)")) {
        out = reply("goodbye");
        return true;
    }
    if (rx(g, "(nasilsin|naber|ne haber|nasil gidiyor|ne var ne yok|how are you|hows it going|how are things)")) {
        out = reply("how_are_you");
        return true;
    }
    if (rx(g, "(tesekkur|sag ol|sagol|eyvallah|sagolasin|thanks|thank you|cheers)")) {
        out = reply("thanks");
        return true;
    }
    if (rx(g, "(seni kim (yapti|yaratti|gelistirdi|yazdi|kodladi)|kim (yapti|yaratti|gelistirdi|yazdi|kodladi) seni|"
              "yaratici\\w*|gelistiricin\\w*|who (made|built|created|wrote|developed) you)")) {
        out = reply("who_made_you");
        return true;
    }
    if (rx(g, "(sen kimsin|^kimsin|adin ne|ismin ne|kendini tanit|sen nesin|who are you|whats your name|what are you|introduce yourself)")) {
        out = reply("who_are_you");
        return true;
    }
    if (rx(g, "(neler yapabilirsin|ne yapabilirsin|ne ise yararsin|yeteneklerin|^yardim|komutlar\\w*|what can you do|^help)")) {
        out = reply("help");
        return true;
    }
    if (rx(g, "(fikra|saka yap|espri|guldur beni|tell me a joke|make me laugh|^joke)")) {
        out = reply("joke", 0.5);
        return true;
    }
    if (rx(g, "^(iyiyim|ben de iyiyim|ben iyiyim|harika|super|cok iyiyim|im (fine|good|great)|fine|good|great)")) {
        out = reply("glad");
        return true;
    }
    if (rx(g, "^(gorusuruz|hosca kal|bay bay|bye|goodbye|see you)")) {
        out = reply("goodbye");
        return true;
    }
    if (rx(g, "^(merhaba|selam|selamlar|hey|hello|hi|alo|selamun aleykum|sa)$")) {
        out = reply("hello");
        return true;
    }

    // ---------------------------------------------------------------- knowledge
    if (rx(g, "^(.+?) (kimdir|kim|nedir|ne demek|ne demektir|nerededir)$", m) ||
        rx(g, "^(.+?) hakkinda (bilgi ver|bilgi|ne biliyorsun|anlat)$", m) ||
        rx(g, "^(who is|who was|what is|what are|whats|tell me about) (a |an |the )?(.+)$", m)) {
        std::string topic;
        if (rx(g, "^(who is|who was|what is|what are|whats|tell me about) (a |an |the )?(.+)$", m)) topic = group(u, m, 3);
        else if (rx(g, "^(.+?) (kimdir|kim|nedir|ne demek|ne demektir|nerededir)$", m)) topic = group(u, m, 1);
        else if (rx(g, "^(.+?) hakkinda (bilgi ver|bilgi|ne biliyorsun|anlat)$", m)) topic = group(u, m, 1);
        // "Atatürk'ün" -> "Atatürk"
        size_t ap = topic.find('\'');
        if (ap != std::string::npos) topic = topic.substr(0, ap);
        if (!fold(topic).empty()) {
            out = tool("wikipedia", {{"topic", trim(topic)}}, 0.5);
            return true;
        }
    }
    return false;
}

}  // namespace kayra

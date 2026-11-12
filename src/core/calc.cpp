#include "core/calc.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <map>
#include <stdexcept>

#include "core/util.h"

namespace kayra {

namespace {

const std::map<std::string, double>& units() {
    static const std::map<std::string, double> m = {
        {"sifir", 0}, {"bir", 1}, {"iki", 2}, {"uc", 3}, {"dort", 4}, {"bes", 5}, {"alti", 6}, {"yedi", 7},
        {"sekiz", 8}, {"dokuz", 9}, {"on", 10}, {"yirmi", 20}, {"otuz", 30}, {"kirk", 40}, {"elli", 50},
        {"altmis", 60}, {"yetmis", 70}, {"seksen", 80}, {"doksan", 90},
        {"zero", 0}, {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5}, {"six", 6}, {"seven", 7},
        {"eight", 8}, {"nine", 9}, {"ten", 10}, {"eleven", 11}, {"twelve", 12}, {"thirteen", 13}, {"fourteen", 14},
        {"fifteen", 15}, {"sixteen", 16}, {"seventeen", 17}, {"eighteen", 18}, {"nineteen", 19}, {"twenty", 20},
        {"thirty", 30}, {"forty", 40}, {"fifty", 50}, {"sixty", 60}, {"seventy", 70}, {"eighty", 80}, {"ninety", 90},
    };
    return m;
}

const std::map<std::string, double>& scales() {
    static const std::map<std::string, double> m = {
        {"yuz", 100}, {"bin", 1000}, {"milyon", 1e6}, {"milyar", 1e9},
        {"hundred", 100}, {"thousand", 1000}, {"million", 1e6}, {"billion", 1e9},
    };
    return m;
}

bool is_number(const std::string& w) {
    if (w.empty()) return false;
    char* end = nullptr;
    std::string t = replace_all(w, ",", ".");
    std::strtod(t.c_str(), &end);
    return end && *end == 0;
}

}  // namespace

std::vector<std::string> numbers_from_words(const std::vector<std::string>& words) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < words.size()) {
        const std::string& w = words[i];
        bool unit = units().count(w) > 0, scale = scales().count(w) > 0;
        // "bir" alone is usually the article "a" ("bir şarkı aç"); only count it before a number or a measure
        static const char* measures[] = {"saniye", "sn", "dakika", "dk", "saat", "gun", "hafta", "bucuk", "kere", "kez", "tane", "derece"};
        bool before_measure = false;
        if (i + 1 < words.size())
            for (const char* m : measures) before_measure = before_measure || words[i + 1] == m;
        if (w == "bir" && !before_measure &&
            !(i + 1 < words.size() && (scales().count(words[i + 1]) || units().count(words[i + 1])))) {
            out.push_back(w);
            i++;
            continue;
        }
        if (w == "on") {
            // "on" is also English ("turn on"): a number only next to other number or unit words
            static const char* near[] = {"bucuk", "saniye", "dakika", "saat", "gun", "kere", "carpi", "arti", "eksi",
                                         "bolu", "uzeri", "derece", "tane", "yil", "hafta", "ay"};
            bool num = false;
            if (i + 1 < words.size()) {
                const std::string& n = words[i + 1];
                num = units().count(n) || scales().count(n) || is_number(n);
                for (const char* x : near) num = num || n == x;
            }
            if (i > 0) {
                const std::string& b = words[i - 1];
                for (const char* x : near) num = num || b == x;
                num = num || is_number(b);
            }
            unit = num;
        }
        if (!unit && !scale) {
            out.push_back(w);
            i++;
            continue;
        }
        double total = 0, group = 0;
        bool any = false;
        while (i < words.size()) {
            const std::string& x = words[i];
            if (units().count(x)) {
                group += units().at(x);
            } else if (scales().count(x)) {
                double s = scales().at(x);
                if (group == 0) group = 1;
                if (s >= 1000) {
                    total += group * s;
                    group = 0;
                } else {
                    group *= s;
                }
            } else if (x == "and" && any) {
                // "one hundred and five"
            } else {
                break;
            }
            any = true;
            i++;
        }
        total += group;
        if (i < words.size() && (words[i] == "bucuk" || words[i] == "half")) {
            total += 0.5;
            i++;
        }
        out.push_back(format_number(total, 6, false));
    }
    // "5 bucuk" with digits
    std::vector<std::string> merged;
    for (size_t k = 0; k < out.size(); k++) {
        if ((out[k] == "bucuk" || out[k] == "half") && !merged.empty() && is_number(merged.back())) {
            merged.back() = format_number(std::atof(replace_all(merged.back(), ",", ".").c_str()) + 0.5, 6, false);
        } else {
            merged.push_back(out[k]);
        }
    }
    return merged;
}

bool to_expression(const std::string& folded, bool turkish, std::string& expr) {
    std::vector<std::string> words = numbers_from_words(split_words(folded));
    // "240in", "15i": a number with its Turkish case ending (the apostrophe was folded away)
    for (auto& w : words) {
        size_t d = 0;
        while (d < w.size() && (std::isdigit((unsigned char)w[d]) || w[d] == '.' || w[d] == ',')) d++;
        if (d > 0 && d < w.size() && w.size() - d <= 4 && std::isdigit((unsigned char)w[0])) {
            bool letters = true;
            for (size_t i = d; i < w.size(); i++) letters = letters && std::isalpha((unsigned char)w[i]);
            if (letters) w = w.substr(0, d);
        }
    }
    static const std::map<std::string, std::string> ops = {
        {"arti", "+"}, {"topla", "+"}, {"plus", "+"}, {"eksi", "-"}, {"minus", "-"}, {"carpi", "*"},
        {"kere", "*"}, {"times", "*"}, {"x", "*"}, {"bolu", "/"}, {"over", "/"}, {"uzeri", "^"}, {"ussu", "^"},
        {"karekok", "sqrt"}, {"kokuu", "sqrt"}, {"kok", "sqrt"}, {"koku", "sqrt"}, {"sqrt", "sqrt"}, {"mod", "%%"}, {"modu", "%%"},
        {"sin", "sin"}, {"sinus", "sin"}, {"sine", "sin"}, {"cos", "cos"}, {"kosinus", "cos"}, {"cosine", "cos"},
        {"tan", "tan"}, {"tanjant", "tan"}, {"log", "log"}, {"logaritma", "log"}, {"ln", "ln"}, {"pi", "pi"},
    };
    static const std::vector<std::string> filler = {
        "kac", "eder", "yapar", "nedir", "ne", "hesapla", "hesaplar", "misin", "musun", "bana", "kayra", "acaba",
        "sonuc", "sonucu", "is", "what", "whats", "calculate", "compute", "equals", "equal", "the", "of", "by",
        "to", "power", "multiplied", "divided", "square", "root", "please", "how", "much", "tell", "me", "lutfen",
        "esittir", "esit", "kaca", "nin", "nun", "in", "un", "si", "su", "i", "u", "a", "e", "ile", "and", "degeri",
        "derece", "degrees",
    };
    std::string e;
    int numbers = 0, operators = 0;
    for (size_t k = 0; k < words.size(); k++) {
        std::string w = words[k];
        if ((w == "square" && k + 1 < words.size() && words[k + 1] == "root") ||
            (w == "kare" && k + 1 < words.size() && (words[k + 1] == "kok" || words[k + 1] == "koku"))) {
            e += " sqrt ";
            operators++;
            k++;
            continue;
        }
        if (w == "squared" || w == "karesi" || w == "cubed" || w == "kupu") {
            e += (w == "squared" || w == "karesi") ? " ^ 2 " : " ^ 3 ";
            operators++;
            numbers++;
            continue;
        }
        if (w == "divided" || w == "multiplied") {
            e += w == "divided" ? " / " : " * ";
            operators++;
            continue;
        }
        if (w == "power" || (w == "to" && k + 2 < words.size() && words[k + 2] == "power")) {
            if (w == "to") k += 2;
            e += " ^ ";
            operators++;
            continue;
        }
        if (w == "yuzde" || w == "percent" || w == "%") {
            if ((w == "yuzde" || w == "%") && k + 1 < words.size() && is_number(words[k + 1])) {
                std::string n = words[++k];
                e += " (" + replace_all(n, ",", ".") + "/100) ";
                numbers++;
                // "yüzde 20'si" -> times what follows / precedes
                if (k + 1 < words.size() && (words[k + 1] == "si" || words[k + 1] == "u")) k++;
                operators++;
                e += " * ";
                continue;
            }
            e += " /100 ";
            operators++;
            if (k + 1 < words.size() && words[k + 1] == "of") {
                e += " * ";
                k++;
            }
            continue;
        }
        if (is_number(w)) {
            std::string n = w;
            if (turkish) {
                // "1.250,5" -> "1250.5"
                if (contains(n, ",")) {
                    n = replace_all(n, ".", "");
                    n = replace_all(n, ",", ".");
                } else {
                    size_t dot = n.find('.');
                    if (dot != std::string::npos && n.size() - dot - 1 == 3) n = replace_all(n, ".", "");
                }
            } else {
                n = replace_all(n, ",", "");
            }
            e += " " + n + " ";
            numbers++;
            continue;
        }
        auto it = ops.find(w);
        if (it != ops.end()) {
            e += " " + (it->second == "%%" ? std::string("%") : it->second) + " ";
            if (it->second != "pi") operators++;
            else numbers++;
            continue;
        }
        if (w == "+" || w == "-" || w == "*" || w == "/" || w == "^" || w == "(" || w == ")") {
            e += " " + w + " ";
            if (w != "(" && w != ")") operators++;
            continue;
        }
        bool skip = false;
        for (auto& f : filler)
            if (w == f) skip = true;
        if (skip || w == "=") continue;
        return false;  // an unknown word: not arithmetic
    }
    // "yüzde 20 * " at the end ("150 nin yüzde 20 si") -> drop the trailing operator
    std::string t = trim(e);
    while (!t.empty() && (t.back() == '*' || t.back() == ' ')) t.pop_back();
    if (numbers == 0 || (operators == 0 && numbers < 2)) return false;
    if (operators == 0) return false;
    expr = t;
    return true;
}

namespace {

struct Parser {
    const std::string& s;
    size_t p = 0;
    explicit Parser(const std::string& x) : s(x) {}

    void ws() {
        while (p < s.size() && isspace((unsigned char)s[p])) p++;
    }
    bool eat(char c) {
        ws();
        if (p < s.size() && s[p] == c) {
            p++;
            return true;
        }
        return false;
    }
    bool word(const char* w) {
        ws();
        size_t n = strlen(w);
        if (s.compare(p, n, w) == 0 && (p + n >= s.size() || !isalpha((unsigned char)s[p + n]))) {
            p += n;
            return true;
        }
        return false;
    }
    double expr() {
        double v = term();
        for (;;) {
            if (eat('+')) v += term();
            else if (eat('-')) v -= term();
            else return v;
        }
    }
    double term() {
        double v = factor();
        for (;;) {
            if (eat('*')) v *= factor();
            else if (eat('/')) {
                double d = factor();
                if (d == 0) throw std::runtime_error("division by zero");
                v /= d;
            } else if (eat('%')) {
                double d = factor();
                if (d == 0) throw std::runtime_error("division by zero");
                v = std::fmod(v, d);
            } else {
                // implicit multiplication: "2(3+4)", "2 pi"
                ws();
                if (p < s.size() && (s[p] == '(' || isalpha((unsigned char)s[p]))) v *= factor();
                else return v;
            }
        }
    }
    double factor() {
        double b = unary();
        if (eat('^')) return std::pow(b, factor());
        return b;
    }
    double unary() {
        if (eat('-')) return -unary();
        if (eat('+')) return unary();
        return primary();
    }
    double primary() {
        ws();
        if (eat('(')) {
            double v = expr();
            if (!eat(')')) throw std::runtime_error("missing )");
            return v;
        }
        if (word("pi")) return 3.14159265358979323846;
        static const char* fns[] = {"sqrt", "sin", "cos", "tan", "log", "ln", "abs"};
        for (const char* f : fns) {
            if (word(f)) {
                double a = factor();
                std::string n = f;
                const double deg = 3.14159265358979323846 / 180.0;
                if (n == "sqrt") {
                    if (a < 0) throw std::runtime_error("negative root");
                    return std::sqrt(a);
                }
                if (n == "sin") return std::sin(a * deg);
                if (n == "cos") return std::cos(a * deg);
                if (n == "tan") return std::tan(a * deg);
                if (n == "log") return std::log10(a);
                if (n == "ln") return std::log(a);
                return std::fabs(a);
            }
        }
        size_t start = p;
        while (p < s.size() && (isdigit((unsigned char)s[p]) || s[p] == '.')) p++;
        if (start == p) throw std::runtime_error("number expected");
        return std::atof(s.substr(start, p - start).c_str());
    }
};

}  // namespace

bool calc_eval(const std::string& expr, double& out, std::string* error) {
    try {
        Parser ps(expr);
        out = ps.expr();
        ps.ws();
        if (ps.p != expr.size()) throw std::runtime_error("unexpected '" + expr.substr(ps.p, 1) + "'");
        if (std::isnan(out) || std::isinf(out)) throw std::runtime_error("undefined");
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

}  // namespace kayra

// Small helpers shared by every part of Kayra: UTF-8 <-> UTF-16, Turkish-aware
// text folding, paths under %APPDATA%\Kayra and a log file.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace kayra {

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);

// Lower case that knows Turkish dotted/dotless i (I -> ı, İ -> i).
std::string lower(const std::string& s);
// Lower case and Turkish letters folded to ASCII (ç->c, ğ->g, ı->i, ö->o, ş->s, ü->u),
// punctuation turned into spaces, whitespace collapsed. Used for matching.
std::string fold(const std::string& s);
std::string trim(const std::string& s);
std::vector<std::string> split_words(const std::string& s);
bool starts_with(const std::string& s, const std::string& p);
bool ends_with(const std::string& s, const std::string& p);
bool contains(const std::string& s, const std::string& p);
std::string replace_all(std::string s, const std::string& from, const std::string& to);
// Uppercase first letter (UTF-8 aware for Turkish letters).
std::string capitalize(const std::string& s);
// Similarity 0..1 from Levenshtein distance on bytes.
double similarity(const std::string& a, const std::string& b);
std::string url_encode(const std::string& s);
// Formats a number with the decimal separator of the language ("3,5" in Turkish).
std::string format_number(double v, int max_decimals, bool turkish);

std::string app_dir();                 // %APPDATA%\Kayra (created on demand)
std::string models_dir();              // %APPDATA%\Kayra\models
std::string exe_dir();
std::string known_folder(const std::string& which);  // desktop, downloads, documents, pictures, music, videos
bool file_exists(const std::string& path);
bool read_file(const std::string& path, std::string& out);
bool write_file(const std::string& path, const std::string& data);
int64_t file_size(const std::string& path);

void log(const std::string& line);
double now_seconds();                  // monotonic
std::string clock_text(bool seconds);  // "14:05"
}  // namespace kayra

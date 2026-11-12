// Turkish / English text for replies and the HUD.
// "{sir}" in a text becomes the user's form of address, "{Sir}" the same capitalized.
#pragma once
#include <initializer_list>
#include <string>

namespace kayra {

void set_language(bool turkish);
bool is_turkish();
void set_address(const std::string& address);

std::string T(const char* key);
std::string F(const char* key, std::initializer_list<std::string> args);
// For ImGui: a stable pointer (cached per key and language).
const char* U(const char* key);

std::string weekday_name(int wday);  // 0 = Sunday
std::string month_name(int mon);     // 0 = January
std::string duration_text(double seconds);
// "İstanbul" -> "İstanbul'da", "Paris" -> "Paris'te"
std::string tr_locative(const std::string& place);
bool has_key(const char* key);

}  // namespace kayra

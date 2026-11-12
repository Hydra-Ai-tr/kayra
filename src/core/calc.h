// Safe arithmetic for spoken questions ("yüz yirmi beş çarpı dört", "sqrt(2)*10").
#pragma once
#include <string>
#include <vector>

namespace kayra {

// Folded words with number words merged into digits:
// {"yuz","yirmi","bes","dakika"} -> {"125","dakika"}; "bucuk"/"half" add 0.5.
std::vector<std::string> numbers_from_words(const std::vector<std::string>& words);

// Turns folded text into an expression ("5 carpi 3" -> "5*3"); returns false
// when the text does not look like arithmetic.
bool to_expression(const std::string& folded, bool turkish, std::string& expr);

bool calc_eval(const std::string& expr, double& out, std::string* error = nullptr);

}  // namespace kayra

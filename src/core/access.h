// Files and the web for the tools: user paths, folder listings, file search,
// text out of files and pages, search results and news headlines.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace kayra {

struct FileEntry {
    std::string name;
    std::string path;
    bool dir = false;
    int64_t size = 0;
    int64_t modified = 0;  // unix seconds
};

// "masaüstü\not.txt", "downloads", "~\x", "%USERPROFILE%\y", "C:\z" -> a full path.
// Relative paths start at the user's home folder.
std::string resolve_path(const std::string& path);
// "Masaüstü" / "Desktop" for the known folders, else the last part of the path.
std::string place_name(const std::string& path);
// Known folder named in a folded phrase ("masaustu", "indirilenler", "downloads"...), or "".
std::string folder_alias(const std::string& folded);

bool list_folder(const std::string& path, std::vector<FileEntry>& out, std::string* error);
// Name search (all words of query, or a wildcard such as "*.pdf") under root, or under the
// user's folders when root is empty. Newest first, exact names before partial ones.
std::vector<FileEntry> find_files(const std::string& query, const std::string& root, size_t max_results = 30,
                                  double max_seconds = 4.0);
// Text of a text file (UTF-8 or UTF-16); fails for binary files.
bool read_text_file(const std::string& path, std::string& out, size_t max_bytes, std::string* error);
bool recycle_path(const std::string& path);  // to the Recycle Bin
std::string size_text(int64_t bytes, bool turkish);

struct WebResult {
    std::string title, url, snippet, source;
};
std::vector<WebResult> search_web(const std::string& query, bool turkish, std::string* error);
std::vector<WebResult> news_headlines(const std::string& topic, bool turkish, std::string* error);
bool read_webpage(const std::string& url, std::string& title, std::string& text, std::string* error);
std::string html_to_text(const std::string& html, std::string* title = nullptr);

}  // namespace kayra

#include "companion/localization.h"
#include <windows.h>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>

namespace {
// Small strict JSON parser for flat {"key":"value"} translation files.
// UTF-8 bytes are retained; \uXXXX escapes (including surrogate pairs) are decoded.
void append_utf8(std::string& out, unsigned cp) {
    if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7ff) { out.push_back(static_cast<char>(0xc0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 63))); }
    else if (cp <= 0xffff) { out.push_back(static_cast<char>(0xe0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (cp & 63))); }
    else { out.push_back(static_cast<char>(0xf0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (cp & 63))); }
}
struct Parser {
    std::string_view s;
    size_t i = 0;
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    bool take(char c) { ws(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    unsigned hex4() {
        if (s.size() - i < 4) throw std::runtime_error("Truncated unicode escape");
        unsigned n = 0;
        for (int k=0;k<4;++k) {
            char c=s[i++]; n <<= 4;
            if (c >= '0' && c <= '9') n |= c-'0';
            else if (c >= 'a' && c <= 'f') n |= c-'a'+10;
            else if (c >= 'A' && c <= 'F') n |= c-'A'+10;
            else throw std::runtime_error("Invalid unicode escape");
        }
        return n;
    }
    std::string str() {
        ws(); if (i >= s.size() || s[i++] != '"') throw std::runtime_error("Expected string");
        std::string out;
        while (i < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[i++]);
            if (c == '"') return out;
            if (c < 0x20) throw std::runtime_error("Control character in JSON string");
            if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
            if (i == s.size()) throw std::runtime_error("Incomplete escape");
            char e=s[i++];
            switch (e) {
                case '"': case '\\': case '/': out.push_back(e); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'u': {
                    unsigned cp=hex4();
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (s.size()-i < 2 || s.substr(i,2) != "\\u") throw std::runtime_error("Missing low surrogate");
                        i+=2; unsigned lo=hex4();
                        if (lo < 0xdc00 || lo > 0xdfff) throw std::runtime_error("Invalid low surrogate");
                        cp=0x10000 + ((cp-0xd800)<<10) + (lo-0xdc00);
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) throw std::runtime_error("Unexpected low surrogate");
                    append_utf8(out,cp); break;
                }
                default: throw std::runtime_error("Invalid escape");
            }
        }
        throw std::runtime_error("Unterminated string");
    }
    std::unordered_map<std::string,std::string> object() {
        std::unordered_map<std::string,std::string> result;
        if (!take('{')) throw std::runtime_error("Expected object");
        if (take('}')) return result;
        do {
            auto key=str(); if (!take(':')) throw std::runtime_error("Expected colon");
            auto value=str();
            if (!result.emplace(std::move(key),std::move(value)).second) throw std::runtime_error("Duplicate translation key");
            if (take('}')) { ws(); if (i != s.size()) throw std::runtime_error("Trailing content"); return result; }
            if (!take(',')) throw std::runtime_error("Expected comma");
        } while (true);
    }
};
std::unordered_map<std::string,std::string> load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open locale file");
    std::string content((std::istreambuf_iterator<char>(file)), {});
    if (content.size() >= 3 && content.substr(0,3) == "\xef\xbb\xbf") content.erase(0,3);
    return Parser{content}.object();
}
}
namespace companion {
Localization::Localization(std::filesystem::path locales_directory) : directory_(std::move(locales_directory)) {
    fallback_ = load(directory_ / "en-US.json");
    active_ = fallback_;
}
bool Localization::set_language(const std::string& requested) {
    if (requested == "en-US") { active_=fallback_; language_=requested; return true; }
    // Prevent path traversal; locales are only selected by a safe language tag.
    if (requested.empty() || requested.size()>32 || requested.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") != std::string::npos) return false;
    try { auto next=load(directory_ / (requested + ".json")); active_=std::move(next); language_=requested; return true; }
    catch (...) { return false; }
}
std::string Localization::get(const std::string& key) const {
    if (auto it=active_.find(key); it!=active_.end()) return it->second;
    if (auto it=fallback_.find(key); it!=fallback_.end()) return it->second;
    return "["+key+"]";
}
std::string Localization::system_language() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (!GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH)) return "en-US";
    int size=WideCharToMultiByte(CP_UTF8,0,name,-1,nullptr,0,nullptr,nullptr);
    if (size<=1) return "en-US";
    std::string value(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8,0,name,-1,value.data(),size,nullptr,nullptr);
    value.pop_back(); return value;
}
}

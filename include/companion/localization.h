#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>

namespace companion {
class Localization {
public:
    explicit Localization(std::filesystem::path locales_directory);
    bool set_language(const std::string& language);
    const std::string& language() const noexcept { return language_; }
    std::string get(const std::string& key) const;
    static std::string system_language();
private:
    std::filesystem::path directory_;
    std::string language_ = "en-US";
    std::unordered_map<std::string, std::string> fallback_;
    std::unordered_map<std::string, std::string> active_;
};
}

#include "companion/localization.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int assertions = 0;
void expect(bool condition, const char* description) {
    ++assertions;
    if (!condition) throw std::runtime_error(description);
}
void write(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not create test locale");
    file << content;
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: EmbervaleCompanionTests <locales_directory>");
        const std::filesystem::path locales = argv[1];
        companion::Localization i18n(locales);
        expect(i18n.language() == "en-US", "English should be default");
        expect(i18n.get("settings.title") == "Settings", "English text");
        expect(i18n.set_language("pt-BR"), "Portuguese should load");
        expect(i18n.get("settings.title") == "Configurações", "UTF-8 accents");
        expect(i18n.get("action.save") == "Salvar alterações", "Portuguese save text");
        expect(i18n.get("missing.key") == "[missing.key]", "Missing key marker");
        expect(!i18n.set_language("../../windows"), "Reject unsafe locale names");
        expect(i18n.language() == "pt-BR", "Invalid locale should not change language");
        expect(!i18n.set_language("zz-ZZ"), "Reject unavailable locale");
        expect(i18n.get("settings.title") == "Configurações", "Keep active locale after failure");
        expect(i18n.set_language("en-US"), "Switch back to English");
        expect(i18n.get("settings.title") == "Settings", "English restored");
        const auto scratch = std::filesystem::temp_directory_path() / "embervale_companion_localization_test";
        std::filesystem::create_directories(scratch);
        struct Cleanup { std::filesystem::path p; ~Cleanup(){std::error_code ec; std::filesystem::remove_all(p,ec);} } cleanup{scratch};
        write(scratch / "en-US.json", R"({"a":"Fallback","accent":"English"})");
        write(scratch / "xx-XX.json", R"({"accent":"\u00c1rvore"})");
        companion::Localization partial(scratch);
        expect(partial.set_language("xx-XX"), "Partial locale loads");
        expect(partial.get("a") == "Fallback", "Missing translation falls back to English");
        expect(partial.get("accent") == "Árvore", "Unicode escape decoding");
        write(scratch / "broken.json", R"({"bad":true})");
        expect(!partial.set_language("broken"), "Malformed translation rejected");
        expect(partial.get("accent") == "Árvore", "Malformed translation preserves locale");
        std::cout << "PASS: " << assertions << " localization assertions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

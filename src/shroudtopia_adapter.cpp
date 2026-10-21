// This translation unit must be compiled against the exact Shroudtopia SDK header.
// Do not duplicate the loader's C++ ABI declarations locally.
#include "shroudtopia.h"
#include <fstream>
#include <mutex>
#include <string>
#include <filesystem>
#include <windows.h>

namespace {
std::mutex log_mutex;
void log_event(const char* event) noexcept {
    try {
        std::lock_guard<std::mutex> guard(log_mutex);
        wchar_t path[MAX_PATH] = {};
        const DWORD count = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (!count || count >= MAX_PATH) return;
        auto log = std::filesystem::path(path).parent_path() / "embervale-companion.log";
        std::ofstream out(log, std::ios::app);
        if (out) out << "[Embervale Companion v0.6] " << event << '\n';
        OutputDebugStringA(event);
    } catch (...) {}
}

class CompanionMod final : public Mod {
public:
    ModMetaData GetMetaData() override {
        return {"Embervale Companion", "Localization and in-game map project (loader diagnostic)",
                "0.6.0", "Embervale Companion contributors", "", true, false};
    }
    void Load(ModContext*) override { log_event("Load"); }
    void Unload(ModContext*) override { log_event("Unload"); }
    void Activate(ModContext*) override { log_event("Activate"); }
    void Deactivate(ModContext*) override { log_event("Deactivate"); }
    void Update(ModContext*) override {
        if (!logged_update_) { logged_update_ = true; log_event("First Update"); }
    }
private:
    bool logged_update_ = false;
};
} // namespace

extern "C" __declspec(dllexport) Mod* CreateModInstance() {
    log_event("CreateModInstance");
    return new CompanionMod();
}

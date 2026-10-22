#pragma once

// C4.1 is diagnostic only. Key observations are NOT proof that the native map
// is open; manual labels (F7/F8) supply ground truth for the experiment.
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace embervale {
namespace c4 {

inline void observe(void (*logger)(const char*) noexcept,
                    std::uint64_t present_number) noexcept {
    static const bool enabled = []() noexcept {
        char value[8] = {};
        const DWORD size = GetEnvironmentVariableA("EMBERVALE_C4_OBSERVE", value, sizeof(value));
        return size == 1 && value[0] == '1';
    }();
    if (!enabled || !logger) return;

    // vkQueuePresentKHR can be called from multiple threads. Skip concurrent
    // observations rather than blocking the game's presentation path.
    static std::atomic_flag busy = ATOMIC_FLAG_INIT;
    if (busy.test_and_set(std::memory_order_acquire)) return;
    struct Unlock {
        std::atomic_flag& flag;
        ~Unlock() noexcept { flag.clear(std::memory_order_release); }
    } unlock{busy};

    struct Key {
        int code;
        const char* name;
    };
    static const Key keys[] = {
        {'M', "M"}, {VK_ESCAPE, "ESC"},
        {'I', "I"}, {'B', "B"}, {'N', "N"},
        {VK_F7, "F7"}, {VK_F8, "F8"}
    };
    static bool down[sizeof(keys) / sizeof(keys[0])] = {};
    static bool initialized = false;
    static bool last_focus = false;

    DWORD foreground_pid = 0;
    const HWND foreground = GetForegroundWindow();
    if (foreground) GetWindowThreadProcessId(foreground, &foreground_pid);
    const bool focused = foreground_pid == GetCurrentProcessId();

    char message[192];
    if (!initialized || focused != last_focus) {
        std::snprintf(message, sizeof(message),
                      "C4.1 focus=%u present=%llu ms=%llu",
                      focused ? 1u : 0u,
                      static_cast<unsigned long long>(present_number),
                      static_cast<unsigned long long>(GetTickCount64()));
        logger(message);
        initialized = true;
        last_focus = focused;
    }

    if (!focused) {
        for (auto& was_down : down) was_down = false;
        return;
    }

    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const bool pressed = (GetAsyncKeyState(keys[i].code) & 0x8000) != 0;
        if (pressed && !down[i]) {
            const char* kind = "input";
            if (keys[i].code == VK_F7) kind = "MANUAL_MAP_VISIBLE";
            if (keys[i].code == VK_F8) kind = "MANUAL_MAP_HIDDEN";
            std::snprintf(message, sizeof(message),
                          "C4.1 %s=%s present=%llu ms=%llu",
                          kind, keys[i].name,
                          static_cast<unsigned long long>(present_number),
                          static_cast<unsigned long long>(GetTickCount64()));
            logger(message);
        }
        down[i] = pressed;
    }
}

} // namespace c4
} // namespace embervale

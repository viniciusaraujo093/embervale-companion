#pragma once
// C4-S8: opt-in function-entry detour for the Vulkan present pointer found at
// dispatch table +0x590. This is an experiment, not shipping mod code.
// Unlike S7, it does NOT edit the dispatch table. It patches target code via
// MinHook, which may belong to the Steam overlay. Never hot-reload this mod.
// No Vulkan GPU work. Forward VkQueuePresentKHR unmodified.
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

namespace embervale { namespace c4 { namespace direct {
using Logger = void (*)(const std::string&) noexcept;
using PresentFn = std::int32_t (WINAPI*)(void*, const void*);

inline std::atomic<std::uintptr_t> trampoline{0};
inline std::atomic<std::uintptr_t> hookedTarget{0};
inline std::atomic<std::uint64_t> presentCalls{0};
inline std::atomic<std::uint32_t> activeCalls{0};
inline std::atomic<bool> attempted{false};
inline std::atomic<bool> enabled{false};
inline std::atomic<ULONGLONG> reportTick{0};
inline bool ownsMinHook = false;

inline std::int32_t WINAPI observed_present(void* queue, const void* info) noexcept {
    activeCalls.fetch_add(1, std::memory_order_acq_rel);
    presentCalls.fetch_add(1, std::memory_order_relaxed);
    const auto next = reinterpret_cast<PresentFn>(trampoline.load(std::memory_order_acquire));
    // Trampoline must be published before MH_EnableHook. This fallback avoids
    // dereferencing null if an unexpected life-cycle bug occurs.
    const auto result = next ? next(queue, info) : static_cast<std::int32_t>(-1);
    activeCalls.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

inline void log(Logger logger, const char* status, std::uintptr_t target,
                int detail = 0, const char* module = "") noexcept {
    if (!logger) return;
    char message[420]{};
    std::snprintf(message, sizeof(message),
        "C4-S8 status=%s present_calls=%llu target=0x%llx detail=%d module=%s "
        "hook=direct_detour gpu_work=0",
        status, static_cast<unsigned long long>(presentCalls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(target), detail, module);
    try { logger(std::string(message)); } catch (...) {}
}

inline bool valid_target(std::uintptr_t address, char* module, std::size_t capacity) noexcept {
    if (!address || !module || capacity < 2) return false;
    module[0] = '\0';
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) != sizeof(mbi) ||
        mbi.State != MEM_COMMIT || mbi.Type != MEM_IMAGE || (mbi.Protect & PAGE_GUARD)) return false;
    const DWORD p = mbi.Protect & 0xff;
    if (p != PAGE_EXECUTE && p != PAGE_EXECUTE_READ &&
        p != PAGE_EXECUTE_READWRITE && p != PAGE_EXECUTE_WRITECOPY) return false;

    HMODULE owner = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(address), &owner) || !owner) return false;
    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(owner, path, MAX_PATH)) return false;
    const char* last = path;
    for (const char* it = path; *it; ++it) if (*it == '\\' || *it == '/') last = it + 1;
    std::snprintf(module, capacity, "%s", last);
    return true;
}

inline bool read_pointer(std::uintptr_t address, std::uintptr_t& value) noexcept {
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                             &value, sizeof(value), &copied) && copied == sizeof(value);
}

inline void maybe_install(std::uintptr_t table, std::uintptr_t target, Logger logger) noexcept {
    // Only attempt on a well-identified legacy Vulkan dispatch table.
    if (enabled.load(std::memory_order_acquire) || !table || !target ||
        (table & 7u) || table > UINTPTR_MAX - 0x590 - sizeof(void*)) return;
    const auto loader = GetModuleHandleW(L"vulkan-1.dll");
    if (!loader) return;
    const auto gipa = GetProcAddress(loader, "vkGetInstanceProcAddr");
    const auto createInstance = GetProcAddress(loader, "vkCreateInstance");
    const auto gdpa = GetProcAddress(loader, "vkGetDeviceProcAddr");
    if (!gipa || !createInstance || !gdpa) return;

    std::uintptr_t value=0;
    if (!read_pointer(table + 0x10, value) || value != reinterpret_cast<std::uintptr_t>(loader) ||
        !read_pointer(table + 0x18, value) || value != reinterpret_cast<std::uintptr_t>(gipa) ||
        !read_pointer(table + 0x20, value) || value != reinterpret_cast<std::uintptr_t>(createInstance) ||
        !read_pointer(table + 0x78, value) || value != reinterpret_cast<std::uintptr_t>(gdpa) ||
        !read_pointer(table + 0x590, value) || value != target) return;

    char module[128]{};
    if (!valid_target(target, module, sizeof(module))) return;
    bool expected = false;
    if (!attempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;

    log(logger, "candidate_validated", target, 0, module);
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        log(logger, "minhook_init_failed", target, static_cast<int>(init), module);
        return;
    }
    ownsMinHook = init == MH_OK;
    void* original = nullptr;
    const MH_STATUS create = MH_CreateHook(reinterpret_cast<void*>(target),
                                            reinterpret_cast<void*>(&observed_present), &original);
    if (create != MH_OK || !original) {
        log(logger, "minhook_create_failed", target, static_cast<int>(create), module);
        if (ownsMinHook) { MH_Uninitialize(); ownsMinHook = false; }
        return;
    }
    trampoline.store(reinterpret_cast<std::uintptr_t>(original), std::memory_order_release);
    const MH_STATUS activation = MH_EnableHook(reinterpret_cast<void*>(target));
    if (activation != MH_OK) {
        log(logger, "minhook_enable_failed", target, static_cast<int>(activation), module);
        MH_RemoveHook(reinterpret_cast<void*>(target));
        trampoline.store(0, std::memory_order_release);
        if (ownsMinHook) { MH_Uninitialize(); ownsMinHook = false; }
        return;
    }
    hookedTarget.store(target, std::memory_order_release);
    enabled.store(true, std::memory_order_release);
    reportTick.store(GetTickCount64(), std::memory_order_release);
    log(logger, "detour_installed", target, 0, module);
}

inline void report(Logger logger) noexcept {
    if (!enabled.load(std::memory_order_acquire)) return;
    const auto now = GetTickCount64();
    auto last = reportTick.load(std::memory_order_relaxed);
    if (now - last < 5000 ||
        !reportTick.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
    log(logger, "observing", hookedTarget.load(std::memory_order_acquire));
}

inline void stop(Logger logger) noexcept {
    if (!enabled.exchange(false, std::memory_order_acq_rel)) return;
    const auto target = hookedTarget.load(std::memory_order_acquire);
    if (!target) return;
    const MH_STATUS disabled = MH_DisableHook(reinterpret_cast<void*>(target));
    if (disabled != MH_OK && disabled != MH_ERROR_DISABLED) {
        log(logger, "disable_failed_do_not_hot_unload", target, static_cast<int>(disabled));
        return;
    }
    // Best effort only. Shroudtopia hot-unload is NOT supported for this experiment.
    for (unsigned i = 0; i < 1500 && activeCalls.load(std::memory_order_acquire); ++i) Sleep(1);
    if (activeCalls.load(std::memory_order_acquire)) {
        log(logger, "callbacks_in_flight_do_not_hot_unload", target);
        return;
    }
    const MH_STATUS removed = MH_RemoveHook(reinterpret_cast<void*>(target));
    log(logger, removed == MH_OK ? "detour_removed" : "remove_failed", target,
        static_cast<int>(removed));
    if (removed == MH_OK && ownsMinHook) {
        MH_Uninitialize();
        ownsMinHook = false;
    }
}

}}} // namespace embervale::c4::direct
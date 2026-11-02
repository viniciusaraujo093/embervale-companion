// Experimental passive Vulkan instance function-resolution probe for Shroudtopia.
#include "shroudtopia.h"
#ifdef EMBERVALE_C4_S18_PUBLIC_API
#include "embervale_core_runtime.h"
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>
#ifdef EMBERVALE_C4_S20_LEAN_STARTUP
#include "c4_s20_startup_metrics.h"
#endif
#ifdef EMBERVALE_C4_SHROUDTOPIA_PROBE
#include "c4_shroudtopia_probe.h"
#ifdef EMBERVALE_C4_S8_DIRECT_PRESENT_DETOUR
#include "c4_s8_direct_present_detour.h"
#endif
#ifdef EMBERVALE_C4_S7_PASSIVE_PRESENT_HOOK
#include "c4_s7_present_observer.h"
#ifdef EMBERVALE_C4_S11_SWAPCHAIN_OBSERVER
#include "c4_s11_swapchain_observer.h"
#ifdef EMBERVALE_C4_S12_ACQUIRE_OBSERVER
#include "c4_s12_acquire_observer.h"
#ifdef EMBERVALE_C4_S13_NATIVE_CONTEXT
#include "c4_s13_native_context.h"
#ifdef EMBERVALE_C4_S16_NATIVE_FIRST
#include "c4_s16_native_first.h"
#endif
#ifdef EMBERVALE_C4_S14_ORANGE_SQUARE
#if defined(EMBERVALE_C4_S21_CORE_MODE)
#include "c4_s21_core_presentation.h"
#elif defined(EMBERVALE_C4_S17_RECLAIMING_SQUARE)
#include "c4_s17_reclaiming_square.h"
#elif defined(EMBERVALE_C4_S15_RESILIENT_SQUARE)
#include "c4_s15_resilient_square.h"
#else
#include "c4_s14_orange_square.h"
#endif
#endif
#endif
#endif
#endif
#endif
#include "c4_shroudtopia_vulkan_table_probe.h"
#endif

#ifdef EMBERVALE_EXPERIMENTAL_VULKAN_HOOK
#include <MinHook.h>
#endif

namespace {
std::mutex log_mutex;

void log_event(const std::string& event) noexcept {
    try {
        std::lock_guard<std::mutex> lock(log_mutex);
        wchar_t path[MAX_PATH] = {};
        const DWORD count = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (!count || count >= MAX_PATH) return;
        const auto filename = std::filesystem::path(path).parent_path() /
                              "embervale-companion.log";
        std::ofstream out(filename, std::ios::app);
        if (out) out << "[Embervale Companion v0.7] " << event << '\n';
        OutputDebugStringA(event.c_str());
    } catch (...) {}
}

void probe_vulkan(const char* stage) noexcept {
    const HMODULE module = GetModuleHandleW(L"vulkan-1.dll");
    log_event(std::string(stage) +
              (module ? ": vulkan-1.dll loaded" : ": vulkan-1.dll not loaded"));
    if (!module) return;
    log_event(std::string(stage) +
              (GetProcAddress(module, "vkGetInstanceProcAddr")
                   ? ": vkGetInstanceProcAddr export available"
                   : ": vkGetInstanceProcAddr export unavailable"));
}

#ifdef EMBERVALE_EXPERIMENTAL_VULKAN_HOOK
// Vulkan.s dispatchable VkInstance is an opaque pointer. PFN_vkVoidFunction
// is a pointer to a function using VKAPI_CALL (WINAPI on Windows).
struct VkInstance_T;
using VkInstance = VkInstance_T*;
using VulkanVoidFunction = void (WINAPI*)();
using GetInstanceProcAddrFunction = VulkanVoidFunction (WINAPI*)(VkInstance, const char*);

std::atomic<std::uint64_t> resolution_calls{0};
std::atomic<std::uint64_t> present_lookups{0};
std::atomic<std::uint64_t> present_nonnull{0};
std::atomic<std::uint64_t> device_proc_lookups{0};
std::atomic<std::uint64_t> create_device_lookups{0};
std::atomic<std::uintptr_t> last_present_address{0};
GetInstanceProcAddrFunction original_get_instance_proc_addr = nullptr;
void* hook_target = nullptr;
bool minhook_initialized = false;
bool hook_created = false;
bool hook_enabled = false;

VulkanVoidFunction WINAPI hooked_get_instance_proc_addr(VkInstance instance, const char* name) {
    // Do not log, allocate, or modify the returned function pointer here.
    resolution_calls.fetch_add(1, std::memory_order_relaxed);
    const bool is_present = name && lstrcmpA(name, "vkQueuePresentKHR") == 0;
    if (is_present) present_lookups.fetch_add(1, std::memory_order_relaxed);
    if (name && lstrcmpA(name, "vkGetDeviceProcAddr") == 0) device_proc_lookups.fetch_add(1, std::memory_order_relaxed);
    if (name && lstrcmpA(name, "vkCreateDevice") == 0) create_device_lookups.fetch_add(1, std::memory_order_relaxed);

    const VulkanVoidFunction result = original_get_instance_proc_addr(instance, name);
    if (is_present && result) {
        present_nonnull.fetch_add(1, std::memory_order_relaxed);
        last_present_address.store(reinterpret_cast<std::uintptr_t>(result),
                                   std::memory_order_relaxed);
    }
    return result;
}

bool install_resolution_hook() {
    if (hook_enabled) return true;
    const HMODULE vulkan = GetModuleHandleW(L"vulkan-1.dll");
    if (!vulkan) {
        log_event("Hook: Vulkan loader not loaded");
        return false;
    }
    const FARPROC proc = GetProcAddress(vulkan, "vkGetInstanceProcAddr");
    if (!proc) {
        log_event("Hook: vkGetInstanceProcAddr export unavailable");
        return false;
    }

    if (!minhook_initialized) {
        const MH_STATUS status = MH_Initialize();
        if (status != MH_OK) {
            log_event("Hook: MH_Initialize failed: " + std::to_string(status));
            return false;
        }
        minhook_initialized = true;
    }

    hook_target = reinterpret_cast<void*>(proc);
    const MH_STATUS create_status = MH_CreateHook(
        hook_target,
        reinterpret_cast<void*>(&hooked_get_instance_proc_addr),
        reinterpret_cast<void**>(&original_get_instance_proc_addr));
    if (create_status != MH_OK) {
        log_event("Hook: MH_CreateHook failed: " + std::to_string(create_status));
        return false;
    }
    hook_created = true;

    const MH_STATUS enable_status = MH_EnableHook(hook_target);
    if (enable_status != MH_OK) {
        log_event("Hook: MH_EnableHook failed: " + std::to_string(enable_status));
        return false;
    }
    hook_enabled = true;
    log_event("Hook: vkGetInstanceProcAddr export hook enabled");
    return true;
}

// Only unload when no threads can be executing the hooked function.
void stop_resolution_hook() {
    if (hook_enabled) {
        const MH_STATUS status = MH_DisableHook(hook_target);
        if (status != MH_OK) {
            log_event("Hook: MH_DisableHook failed: " + std::to_string(status));
            return;
        }
        hook_enabled = false;
    }
    if (hook_created) {
        const MH_STATUS status = MH_RemoveHook(hook_target);
        if (status != MH_OK) {
            log_event("Hook: MH_RemoveHook failed: " + std::to_string(status));
            return;
        }
        hook_created = false;
        hook_target = nullptr;
        original_get_instance_proc_addr = nullptr;
    }
    if (minhook_initialized) {
        const MH_STATUS status = MH_Uninitialize();
        if (status != MH_OK) {
            log_event("Hook: MH_Uninitialize failed: " + std::to_string(status));
            return;
        }
        minhook_initialized = false;
    }
    log_event("Hook: stopped");
}

void report_counters(const char* prefix) {
    log_event(std::string(prefix) +
              " instance-proc calls = " + std::to_string(resolution_calls.load()) +
              ", vkGetDeviceProcAddr lookups = " + std::to_string(device_proc_lookups.load()) +
              ", vkCreateDevice lookups = " + std::to_string(create_device_lookups.load()) +
              ", vkQueuePresentKHR lookups = " + std::to_string(present_lookups.load()) +
              ", non-null results = " + std::to_string(present_nonnull.load()) +
              ", last address = " +
              std::to_string(last_present_address.load()));
}
#endif

class CompanionMod final : public Mod {
public:
    ModMetaData GetMetaData() override {
        return {"Embervale Companion",
                "Localization and in-game map project (Vulkan instance resolution probe)",
                "0.7.0", "Embervale Companion contributors", "", true, false};
    }

    void Load(ModContext* context) override {
        #ifdef EMBERVALE_C4_S18_PUBLIC_API
            embervale::core::set_loaded(true);
        #endif
        loaded = true;
#ifdef EMBERVALE_C4_S20_LEAN_STARTUP
        embervale::c4::startup20::load_start();
#endif
        log_event("Load");
        probe_vulkan("Load");
#ifdef EMBERVALE_C4_SHROUDTOPIA_PROBE
#ifndef EMBERVALE_C4_S20_LEAN_STARTUP
        if (context != nullptr && context->game.isClient)
            log_event(embervale::c4::inspect_render_present());
        else
            log_event("C4-S1 scan skipped (not client)");
#endif
#endif
#ifdef EMBERVALE_EXPERIMENTAL_VULKAN_HOOK
        log_event("Hook: attempting early installation in Load");
        install_resolution_hook();
#endif
#ifdef EMBERVALE_C4_S20_LEAN_STARTUP
        embervale::c4::startup20::load_end(log_event);
#endif
    }

    void Unload(ModContext*) override {
        #ifdef EMBERVALE_C4_S18_PUBLIC_API
            embervale::core::set_loaded(false);
        #endif
#ifdef EMBERVALE_C4_S8_DIRECT_PRESENT_DETOUR
        embervale::c4::direct::stop(log_event);
#endif
#ifdef EMBERVALE_C4_S11_SWAPCHAIN_OBSERVER
#ifdef EMBERVALE_C4_S12_ACQUIRE_OBSERVER
#ifdef EMBERVALE_C4_S13_NATIVE_CONTEXT
#ifdef EMBERVALE_C4_S14_ORANGE_SQUARE
        embervale::c4::square::stop(log_event);
#endif
        embervale::c4::nativeframe::stop(log_event);
#endif
        embervale::c4::acquire::stop(log_event);
#endif
        embervale::c4::swapchain::stop(log_event);
#endif
#ifdef EMBERVALE_C4_S7_PASSIVE_PRESENT_HOOK
        embervale::c4::present::stop(log_event);
#endif
#ifdef EMBERVALE_EXPERIMENTAL_VULKAN_HOOK
        stop_monitor();
        report_counters("Hook: final");
        stop_resolution_hook();
#endif
        active = false;
        loaded = false;
        log_event("Unload");
    }

    void Activate(ModContext*) override {
        #ifdef EMBERVALE_C4_S18_PUBLIC_API
            embervale::core::set_active(true);
        #endif
        if (active) return;
        active = true;
        logged_update_ = false;
        log_event("Activate");
        probe_vulkan("Activate");
#ifdef EMBERVALE_EXPERIMENTAL_VULKAN_HOOK
        start_monitor();
#else
        log_event("Hook experiment disabled at build time");
#endif
    }

    void Deactivate(ModContext*) override {
        #ifdef EMBERVALE_C4_S18_PUBLIC_API
            embervale::core::set_active(false);
        #endif
#ifdef EMBERVALE_C4_S8_DIRECT_PRESENT_DETOUR
        embervale::c4::direct::stop(log_event);
#endif
#ifdef EMBERVALE_C4_S11_SWAPCHAIN_OBSERVER
#ifdef EMBERVALE_C4_S12_ACQUIRE_OBSERVER
#ifdef EMBERVALE_C4_S13_NATIVE_CONTEXT
#ifdef EMBERVALE_C4_S14_ORANGE_SQUARE
        embervale::c4::square::stop(log_event);
#endif
        embervale::c4::nativeframe::stop(log_event);
#endif
        embervale::c4::acquire::stop(log_event);
#endif
        embervale::c4::swapchain::stop(log_event);
#endif
#ifdef EMBERVALE_C4_S7_PASSIVE_PRESENT_HOOK
        embervale::c4::present::stop(log_event);
#endif
        if (!active) return;
#ifdef EMBERVALE_EXPERIMENTAL_VULKAN_HOOK
        stop_monitor();
        report_counters("Hook: final");
        stop_resolution_hook();
#endif
        active = false;
        log_event("Deactivate");
    }

    void Update(ModContext*) override {
#ifdef EMBERVALE_C4_SHROUDTOPIA_PROBE
#ifndef EMBERVALE_C4_S16_NATIVE_FIRST
        if (active) embervale::c4::probe_vulkan_table_step(log_event);
#endif
#ifdef EMBERVALE_C4_S8_DIRECT_PRESENT_DETOUR
        if (active) embervale::c4::direct::report(log_event);
#endif
#ifdef EMBERVALE_C4_S7_PASSIVE_PRESENT_HOOK
#ifndef EMBERVALE_C4_S20_LEAN_STARTUP
        if (active) embervale::c4::present::report(log_event);
#endif
#ifdef EMBERVALE_C4_S11_SWAPCHAIN_OBSERVER
#ifndef EMBERVALE_C4_S20_LEAN_STARTUP
        if (active) embervale::c4::swapchain::report(log_event);
#endif
#ifdef EMBERVALE_C4_S12_ACQUIRE_OBSERVER
#ifndef EMBERVALE_C4_S20_LEAN_STARTUP
        if (active) embervale::c4::acquire::report(log_event);
#endif
#ifdef EMBERVALE_C4_S13_NATIVE_CONTEXT
        if (active) {
            embervale::c4::nativeframe::maybe_install(log_event);
#ifdef EMBERVALE_C4_S16_NATIVE_FIRST
            embervale::c4::nativefirst::step(log_event);
#endif
#ifndef EMBERVALE_C4_S20_LEAN_STARTUP
            embervale::c4::nativeframe::report(log_event);
#endif
#ifdef EMBERVALE_C4_S14_ORANGE_SQUARE
            embervale::c4::square::maybe_enable(log_event);
#ifdef EMBERVALE_C4_S20_LEAN_STARTUP
            embervale::c4::startup20::update(
                log_event,
                embervale::c4::present::installed.load(std::memory_order_acquire) &&
                embervale::c4::swapchain::installed.load(std::memory_order_acquire) &&
                embervale::c4::acquire::installed.load(std::memory_order_acquire),
                embervale::c4::square::drawSuccess.load(std::memory_order_relaxed));
#endif
            embervale::c4::square::report(log_event);
#endif
        }
#endif
#endif
#endif
#endif
#endif
        if (!active || logged_update_) return;
        logged_update_ = true;
        log_event("First Update");
        probe_vulkan("First Update");
    }

    ~CompanionMod() override {
#ifdef EMBERVALE_C4_S8_DIRECT_PRESENT_DETOUR
        embervale::c4::direct::stop(log_event);
#endif
#ifdef EMBERVALE_EXPERIMENTAL_VULKAN_HOOK
        stop_monitor();
        stop_resolution_hook();
#endif
    }

private:
#ifdef EMBERVALE_EXPERIMENTAL_VULKAN_HOOK
    void start_monitor() {
        try {
            stop_requested_.store(false);
            monitor_ = std::thread([this] {
                std::unique_lock<std::mutex> lock(monitor_mutex_);
                while (!monitor_cv_.wait_for(lock, std::chrono::seconds(5),
                    [this] { return stop_requested_.load(); })) {
                    lock.unlock();
                    report_counters("Hook: monitor");
                    lock.lock();
                }
            });
            log_event("Hook: monitor started (5-second interval)");
        } catch (...) {
            log_event("Hook: monitor thread creation failed");
        }
    }

    void stop_monitor() {
        stop_requested_.store(true);
        monitor_cv_.notify_all();
        if (monitor_.joinable()) monitor_.join();
    }

    std::atomic<bool> stop_requested_{true};
    std::mutex monitor_mutex_;
    std::condition_variable monitor_cv_;
    std::thread monitor_;
#endif
    bool logged_update_ = false;
};
} // namespace

extern "C" __declspec(dllexport) Mod* CreateModInstance() {
    log_event("CreateModInstance");
    return new CompanionMod();
}

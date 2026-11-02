#pragma once
// C4-S16: native-first initialization bridge for exact verified Enshrouded client.
// Invoked by Shroudtopia Update, not the render callback. No GPU commands here.
// Install sequence: native capture -> verified Vulkan present -> swapchain -> acquire.
// Rendering remains opt-in under the existing C4-S15 build flags.
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

namespace embervale { namespace c4 { namespace nativefirst {
using Logger = embervale::c4::present::Logger;
inline std::atomic<ULONGLONG> lastReport{0};
inline std::atomic<std::uint64_t> nativeTableAttempts{0};

inline void step(Logger log) noexcept {
    if (!embervale::c4::nativeframe::installed.load(std::memory_order_acquire)) return;

    const auto table = embervale::c4::nativeframe::observedTable.load(std::memory_order_acquire);
    const auto device = embervale::c4::nativeframe::observedDevice.load(std::memory_order_acquire);
    const auto sampledAt = embervale::c4::nativeframe::lastTick.load(std::memory_order_acquire);
    const auto now = GetTickCount64();
    const bool fresh = sampledAt != 0 && now >= sampledAt && now - sampledAt <= 2000;

    // Never feed the dispatch hooks an unproven, stale, or unrelated table.
    // Each hook revalidates the exact client, Vulkan module and function slot.
    if (fresh && table >= 0x10000 && device >= 0x10000 &&
        embervale::c4::nativeframe::contexts.load(std::memory_order_acquire) != 0) {
        if (!embervale::c4::present::installed.load(std::memory_order_acquire)) {
            nativeTableAttempts.fetch_add(1, std::memory_order_relaxed);
            embervale::c4::present::maybe_install(table, log);
        }
        if (embervale::c4::present::installed.load(std::memory_order_acquire) &&
            embervale::c4::present::tableSlot.load(std::memory_order_acquire) ==
                table + embervale::c4::present::kPresentSlotOffset) {
            if (!embervale::c4::swapchain::installed.load(std::memory_order_acquire))
                embervale::c4::swapchain::maybe_install(table, log);
            if (embervale::c4::swapchain::installed.load(std::memory_order_acquire) &&
                !embervale::c4::acquire::installed.load(std::memory_order_acquire))
                embervale::c4::acquire::maybe_install(table, log);
        }
    }

    // Emit a useful status even if no native callback ever arrives.
    if (!log) return;
    auto last = lastReport.load(std::memory_order_relaxed);
#ifdef EMBERVALE_C4_S20_LEAN_STARTUP
    constexpr ULONGLONG interval = 30000;
#else
    constexpr ULONGLONG interval = 5000;
#endif
    if (now - last < interval || !lastReport.compare_exchange_strong(last, now)) return;
    const bool present = embervale::c4::present::installed.load(std::memory_order_acquire);
    const bool swapchain = embervale::c4::swapchain::installed.load(std::memory_order_acquire);
    const bool acquire = embervale::c4::acquire::installed.load(std::memory_order_acquire);
    const char* status = !table ? "waiting_for_native_context" :
        !fresh ? "native_context_stale" :
        !present ? "waiting_for_verified_present_hook" :
        !swapchain ? "waiting_for_swapchain_hooks" :
        !acquire ? "waiting_for_acquire_hook" : "bridge_ready";
    char message[430]{};
    std::snprintf(message, sizeof(message),
        "C4-S16 status=%s native_entries=%llu native_valid=%llu table=0x%llx "
        "present=%u swapchain=%u acquire=%u attempts=%llu scan_enabled=0 bridge_gpu_work=0",
        status,
        static_cast<unsigned long long>(embervale::c4::nativeframe::entries.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(embervale::c4::nativeframe::contexts.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(table),
        present ? 1u : 0u, swapchain ? 1u : 0u, acquire ? 1u : 0u,
        static_cast<unsigned long long>(nativeTableAttempts.load(std::memory_order_relaxed)));
    try { log(std::string(message)); } catch (...) {}
}
}}} // embervale::c4::nativefirst

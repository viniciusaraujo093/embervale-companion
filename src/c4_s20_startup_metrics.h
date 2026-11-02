#pragma once
// C4-S20: very small startup telemetry. No game hooks or Vulkan calls.
// Invoked from Shroudtopia lifecycle, never from the Vulkan present callback.
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

namespace embervale { namespace c4 { namespace startup20 {
using Logger = void (*)(const std::string&) noexcept;
inline std::atomic<ULONGLONG> loadStarted{0};
inline std::atomic<ULONGLONG> loadFinished{0};
inline std::atomic<ULONGLONG> firstUpdate{0};
inline std::atomic<bool> reportedBridge{false};
inline std::atomic<bool> reportedDraw{false};
inline std::atomic<bool> reportedTimeout{false};

inline void load_start() noexcept {
    loadStarted.store(GetTickCount64(), std::memory_order_release);
}
inline void emit(Logger log, const char* status, ULONGLONG now,
                 std::uint64_t drawn, bool bridge) noexcept {
    if (!log) return;
    const auto start=loadStarted.load(std::memory_order_acquire);
    const auto loaded=loadFinished.load(std::memory_order_acquire);
    const auto updated=firstUpdate.load(std::memory_order_acquire);
    char output[280]{};
    std::snprintf(output, sizeof(output),
        "C4-S20 status=%s load_ms=%llu startup_ms=%llu first_update_ms=%llu "
        "bridge=%u drawn=%llu historical_signature_scan=0 incremental_memory_scan=0",
        status,
        static_cast<unsigned long long>(loaded >= start && start ? loaded-start : 0),
        static_cast<unsigned long long>(now >= start && start ? now-start : 0),
        static_cast<unsigned long long>(updated >= start && start ? updated-start : 0),
        bridge ? 1u : 0u, static_cast<unsigned long long>(drawn));
    try { log(std::string(output)); } catch (...) {}
}
inline void load_end(Logger log) noexcept {
    const auto now=GetTickCount64();
    loadFinished.store(now,std::memory_order_release);
    emit(log,"load_complete",now,0,false);
}
inline void update(Logger log, bool bridge, std::uint64_t drawn) noexcept {
    const auto now=GetTickCount64();
    ULONGLONG expected=0;
    firstUpdate.compare_exchange_strong(expected,now,std::memory_order_acq_rel);
    if (bridge && !reportedBridge.exchange(true,std::memory_order_acq_rel))
        emit(log,"bridge_ready",now,drawn,true);
    if (drawn != 0 && !reportedDraw.exchange(true,std::memory_order_acq_rel))
        emit(log,"first_draw_observed",now,drawn,bridge);
#if defined(EMBERVALE_C4_S21_CORE_MODE) && !defined(EMBERVALE_C4_S21_DEV_BANNER)
    constexpr bool requireDraw=false;
#else
    constexpr bool requireDraw=true;
#endif
    const auto started=loadStarted.load(std::memory_order_acquire);
    if (started && now >= started && now-started >= 30000 &&
        (!bridge || (requireDraw && drawn==0)) && !reportedTimeout.exchange(true,std::memory_order_acq_rel))
        emit(log,"startup_incomplete_after_30s",now,drawn,bridge);
}
}}} // namespace embervale::c4::startup20
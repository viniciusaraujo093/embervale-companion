#pragma once
// Embervale C4-S4: incremental, read-only Vulkan dispatch-table discovery.
// Experimental hypothesis based on elxokker/Enshrouded-minimap's legacy layout.
// No hooks, memory writes, Vulkan calls, driver calls or GPU submissions.
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>

namespace embervale { namespace c4 {

struct TableScanState {
    std::uintptr_t cursor = 0;
    std::uintptr_t maxAddress = 0;
    std::uint64_t attempted = 0;
    std::uint64_t readable = 0;
    std::uint64_t regions = 0;
    std::uint64_t failures = 0;
    std::uint64_t pointerPairs = 0;
    std::uint64_t plausibleLayouts = 0;
    std::uint64_t nextProgress = 128ull * 1024 * 1024;
    std::uintptr_t firstLayout = 0;
    bool started = false;
    bool ended = false;
    bool waitingLogged = false;
};

inline bool table_executable_pointer(std::uintptr_t p) noexcept {
    if (p < 65536) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<const void*>(p), &info, sizeof(info)) != sizeof(info))
        return false;
    const DWORD prot = info.Protect & 0xff;
    return info.State == MEM_COMMIT && !(info.Protect & PAGE_GUARD) &&
        (prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ ||
         prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY);
}

inline bool table_legacy_layout(std::uintptr_t candidate) noexcept {
    constexpr std::size_t span = 0x590 + sizeof(std::uintptr_t);
    std::array<unsigned char, span> raw{};
    SIZE_T copied = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(candidate),
                           raw.data(), raw.size(), &copied) || copied != raw.size()) return false;
    const auto ptr = [&](std::size_t offset) noexcept {
        std::uintptr_t result = 0;
        std::memcpy(&result, raw.data() + offset, sizeof(result));
        return result;
    };
    // The offsets below are only a hypothesis from an older game's layout.
    // A match does not establish that the table is safe to patch.
    return table_executable_pointer(ptr(0x20)) &&
           table_executable_pointer(ptr(0x78)) &&
           table_executable_pointer(ptr(0x570)) &&
           table_executable_pointer(ptr(0x580)) &&
           table_executable_pointer(ptr(0x588)) &&
           table_executable_pointer(ptr(0x590));
}

// C4-S6: report mapped-module ownership and adjacent Vulkan-table slots for
// candidates with five plausible executable entries. Diagnostic-only.
inline void table_module_name(std::uintptr_t address, char* out, std::size_t cap) noexcept {
    if (!out || !cap) return;
    out[0] = '\0';
    if (!table_executable_pointer(address)) {
        std::snprintf(out, cap, "none");
        return;
    }
    HMODULE module = nullptr;
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (GetModuleHandleExA(flags, reinterpret_cast<LPCSTR>(address), &module)) {
        char path[MAX_PATH]{};
        if (GetModuleFileNameA(module, path, MAX_PATH) > 0) {
            const char* backslash = std::strrchr(path, '\\');
            const char* slash = std::strrchr(path, '/');
            const char* base = path;
            if (backslash && (!slash || backslash > slash)) base = backslash + 1;
            else if (slash) base = slash + 1;
            std::snprintf(out, cap, "%s", base);
            return;
        }
    }
    MEMORY_BASIC_INFORMATION mbi{};
    const bool mapped = VirtualQuery(reinterpret_cast<const void*>(address), &mbi,
                                     sizeof(mbi)) == sizeof(mbi);
    std::snprintf(out, cap, "%s", mapped && mbi.Type == MEM_PRIVATE ?
                  "private_executable" : "module_unknown");
}

inline void table_log_candidate_detail(void (*logger)(const std::string&) noexcept,
                                       std::uintptr_t candidate,
                                       std::uint64_t index,
                                       const std::array<std::uintptr_t, 6>& slots,
                                       unsigned executableMask) noexcept {
    if (!logger || (executableMask & 0x3bu) != 0x3bu) return;
    char names[6][64]{};
    for (std::size_t i = 0; i < 6; ++i)
        table_module_name(slots[i], names[i], sizeof(names[i]));
    const auto vulkan = GetModuleHandleA("vulkan-1.dll");
    const auto expectedCreate = vulkan ? reinterpret_cast<std::uintptr_t>(
        GetProcAddress(vulkan, "vkCreateInstance")) : 0;
    const auto expectedGetDeviceProc = vulkan ? reinterpret_cast<std::uintptr_t>(
        GetProcAddress(vulkan, "vkGetDeviceProcAddr")) : 0;
    char message[720]{};
    std::snprintf(message, sizeof(message),
        "C4-S6 candidate=%llu table=0x%llx modules "
        "s20=%s s78=%s s570=%s s580=%s s588=%s s590=%s "
        "exports_create_instance_match=%u exports_device_proc_match=%u "
        "read_only=1 hooks=0 gpu_work=0",
        static_cast<unsigned long long>(index),
        static_cast<unsigned long long>(candidate),
        names[0], names[1], names[2], names[3], names[4], names[5],
        expectedCreate != 0 && slots[0] == expectedCreate ? 1u : 0u,
        expectedGetDeviceProc != 0 && slots[1] == expectedGetDeviceProc ? 1u : 0u);
    try { logger(std::string(message)); } catch (...) {}

    constexpr std::array<std::size_t, 10> nearOffsets{
        0x560, 0x568, 0x570, 0x578, 0x580,
        0x588, 0x590, 0x598, 0x5a0, 0x5a8
    };
    std::array<std::uintptr_t, 10> nearby{};
    unsigned readableMask = 0;
    unsigned execMask = 0;
    for (std::size_t i = 0; i < nearOffsets.size(); ++i) {
        SIZE_T copied = 0;
        const auto address = candidate + nearOffsets[i];
        if (address < candidate) continue;
        if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                              &nearby[i], sizeof(nearby[i]), &copied) &&
            copied == sizeof(nearby[i])) {
            readableMask |= 1u << i;
            if (table_executable_pointer(nearby[i])) execMask |= 1u << i;
        }
    }
    std::snprintf(message, sizeof(message),
        "C4-S6 neighbors candidate=%llu read=0x%x exec=0x%x "
        "s560=0x%llx s568=0x%llx s570=0x%llx s578=0x%llx s580=0x%llx "
        "s588=0x%llx s590=0x%llx s598=0x%llx s5a0=0x%llx s5a8=0x%llx",
        static_cast<unsigned long long>(index), readableMask, execMask,
        static_cast<unsigned long long>(nearby[0]),
        static_cast<unsigned long long>(nearby[1]),
        static_cast<unsigned long long>(nearby[2]),
        static_cast<unsigned long long>(nearby[3]),
        static_cast<unsigned long long>(nearby[4]),
        static_cast<unsigned long long>(nearby[5]),
        static_cast<unsigned long long>(nearby[6]),
        static_cast<unsigned long long>(nearby[7]),
        static_cast<unsigned long long>(nearby[8]),
        static_cast<unsigned long long>(nearby[9]));
    try { logger(std::string(message)); } catch (...) {}
#ifdef EMBERVALE_C4_S8_DIRECT_PRESENT_DETOUR
    if (expectedCreate != 0 && expectedGetDeviceProc != 0 &&
        slots[0] == expectedCreate && slots[1] == expectedGetDeviceProc)
        embervale::c4::direct::maybe_install(candidate, slots[5], logger);
#endif
#ifdef EMBERVALE_C4_S7_PASSIVE_PRESENT_HOOK
    if (expectedCreate != 0 && expectedGetDeviceProc != 0 &&
        slots[0] == expectedCreate && slots[1] == expectedGetDeviceProc)
        embervale::c4::present::maybe_install(candidate, logger);
#endif
#ifdef EMBERVALE_C4_S11_SWAPCHAIN_OBSERVER
    // Only attempt after the verified C4-S9 presenting table is installed.
    if (expectedCreate != 0 && expectedGetDeviceProc != 0 &&
        slots[0] == expectedCreate && slots[1] == expectedGetDeviceProc)
        embervale::c4::swapchain::maybe_install(candidate, logger);
#endif
#ifdef EMBERVALE_C4_S12_ACQUIRE_OBSERVER
    if (embervale::c4::swapchain::installed.load(std::memory_order_acquire))
        embervale::c4::acquire::maybe_install(candidate, logger);
#endif
}

// C4-S5: explain why each initial pointer-pair fails the historical layout.
// Each slot is read separately, so an inaccessible whole-table span is visible.
// Read-only; no hook installation, pointer writes or Vulkan calls.
inline void table_log_pair(void (*logger)(const std::string&) noexcept,
                           std::uintptr_t candidate,
                           std::uint64_t index) noexcept {
    if (!logger) return;
    constexpr std::array<std::size_t, 6> offsets{
        0x20, 0x78, 0x570, 0x580, 0x588, 0x590
    };
    std::array<std::uintptr_t, 6> slots{};
    unsigned readMask = 0;
    unsigned executableMask = 0;
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        SIZE_T copied = 0;
        const std::uintptr_t address = candidate + offsets[i];
        const bool read = ReadProcessMemory(GetCurrentProcess(),
            reinterpret_cast<const void*>(address), &slots[i],
            sizeof(slots[i]), &copied) != 0 && copied == sizeof(slots[i]);
        if (read) {
            readMask |= 1u << i;
            if (table_executable_pointer(slots[i])) executableMask |= 1u << i;
        }
    }
    char msg[550]{};
    std::snprintf(msg, sizeof(msg),
        "C4-S5 pair=%llu candidate=0x%llx read_mask=0x%02x exec_mask=0x%02x "
        "s20=0x%llx s78=0x%llx s570=0x%llx s580=0x%llx s588=0x%llx s590=0x%llx "
        "read_only=1 hooks=0 gpu_work=0",
        static_cast<unsigned long long>(index),
        static_cast<unsigned long long>(candidate),
        readMask, executableMask,
        static_cast<unsigned long long>(slots[0]),
        static_cast<unsigned long long>(slots[1]),
        static_cast<unsigned long long>(slots[2]),
        static_cast<unsigned long long>(slots[3]),
        static_cast<unsigned long long>(slots[4]),
        static_cast<unsigned long long>(slots[5]));
    try { logger(std::string(msg)); } catch (...) {}
    table_log_candidate_detail(logger, candidate, index, slots, executableMask);
}

inline void table_log(void (*logger)(const std::string&) noexcept,
                      const char* status, const TableScanState& s) noexcept {
    char buf[340]{};
    std::snprintf(buf, sizeof(buf),
       "C4-S4 status=%s read_only=1 hooks=0 gpu_work=0 "
       "attempted_mib=%llu readable_mib=%llu regions=%llu read_failures=%llu "
       "pointer_pairs=%llu legacy_layout_candidates=%llu first_table=0x%llx",
       status,
       static_cast<unsigned long long>(s.attempted / (1024 * 1024)),
       static_cast<unsigned long long>(s.readable / (1024 * 1024)),
       static_cast<unsigned long long>(s.regions),
       static_cast<unsigned long long>(s.failures),
       static_cast<unsigned long long>(s.pointerPairs),
       static_cast<unsigned long long>(s.plausibleLayouts),
       static_cast<unsigned long long>(s.firstLayout));
    logger(std::string(buf));
}

inline void probe_vulkan_table_step(void (*logger)(const std::string&) noexcept) noexcept {
    if (!logger) return;
    static std::atomic_flag inUse = ATOMIC_FLAG_INIT;
    if (inUse.test_and_set(std::memory_order_acquire)) return;
    struct Clear { std::atomic_flag& f; ~Clear() noexcept { f.clear(std::memory_order_release); } } clear{inUse};
    static TableScanState s{};
    if (s.ended) return;

    constexpr std::uint64_t perUpdate = 8ull * 1024 * 1024;
    constexpr std::uint64_t totalLimit = 1024ull * 1024 * 1024;
    constexpr std::size_t readChunk = 64 * 1024;
    constexpr std::uint64_t regionQueryLimit = 2048;
    const HMODULE vulkan = GetModuleHandleW(L"vulkan-1.dll");
    const FARPROC gipa = vulkan ? GetProcAddress(vulkan, "vkGetInstanceProcAddr") : nullptr;
    if (!vulkan || !gipa) {
        if (!s.waitingLogged) { logger("C4-S4 status=waiting_for_vulkan_loader read_only=1 hooks=0 gpu_work=0"); s.waitingLogged = true; }
        return;
    }
    if (!s.started) {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        s.cursor = reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
        s.maxAddress = reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);
        s.started = true;
        table_log(logger, "scan_started", s);
    }
    const std::uintptr_t module = reinterpret_cast<std::uintptr_t>(vulkan);
    const std::uintptr_t instanceProc = reinterpret_cast<std::uintptr_t>(gipa);
    std::array<unsigned char, readChunk> bytes{};
    std::uint64_t used = 0;
    std::uint64_t queries = 0;

    while (s.cursor < s.maxAddress && used < perUpdate &&
           s.attempted < totalLimit && queries < regionQueryLimit &&
           s.plausibleLayouts < 2) {
        MEMORY_BASIC_INFORMATION mbi{};
        ++queries;
        if (VirtualQuery(reinterpret_cast<const void*>(s.cursor), &mbi, sizeof(mbi)) != sizeof(mbi)) {
            s.ended = true;
            table_log(logger, "virtual_query_failed", s);
            return;
        }
        const auto regionStart = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        if (mbi.RegionSize == 0 || regionStart > UINTPTR_MAX - mbi.RegionSize) {
            s.ended = true;
            table_log(logger, "region_overflow", s);
            return;
        }
        const auto regionEnd = regionStart + mbi.RegionSize;
        if (regionEnd <= s.cursor) {
            s.ended = true;
            table_log(logger, "region_not_advancing", s);
            return;
        }
        ++s.regions;
        const DWORD p = mbi.Protect & 0xff;
        const bool readable = p == PAGE_READONLY || p == PAGE_READWRITE ||
                              p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READWRITE ||
                              p == PAGE_EXECUTE_WRITECOPY;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE ||
            (mbi.Protect & PAGE_GUARD) || !readable) {
            s.cursor = regionEnd;
            continue;
        }
        const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(
            std::min<std::uint64_t>(regionEnd - s.cursor, readChunk),
            std::min<std::uint64_t>(perUpdate - used, totalLimit - s.attempted)));
        if (count == 0) break;
        SIZE_T copied = 0;
        const bool ok = ReadProcessMemory(GetCurrentProcess(),
            reinterpret_cast<const void*>(s.cursor), bytes.data(), count, &copied) != 0 && copied == count;
        used += count;
        s.attempted += count;
        if (!ok) {
            ++s.failures;
            s.cursor += count;
            continue;
        }
        s.readable += count;
        // Look for [Vulkan module HMODULE, vkGetInstanceProcAddr export] at
        // offsets +0x10 and +0x18 in a candidate legacy dispatch table.
        const std::uintptr_t aligned = (s.cursor + 7) & ~std::uintptr_t(7);
        const std::size_t begin = static_cast<std::size_t>(aligned - s.cursor);
        for (std::size_t i = begin; i + 2 * sizeof(std::uintptr_t) <= count; i += 8) {
            std::uintptr_t left = 0, right = 0;
            std::memcpy(&left, bytes.data() + i, sizeof(left));
            if (left != module) continue;
            std::memcpy(&right, bytes.data() + i + 8, sizeof(right));
            if (right != instanceProc || s.cursor + i < 0x10) continue;
            ++s.pointerPairs;
            const std::uintptr_t candidate = s.cursor + i - 0x10;
            if (s.pointerPairs <= 12) table_log_pair(logger, candidate, s.pointerPairs);
            if (table_legacy_layout(candidate)) {
                ++s.plausibleLayouts;
                if (s.firstLayout == 0) s.firstLayout = candidate;
                table_log(logger, "legacy_layout_candidate", s);
                if (s.plausibleLayouts >= 2) break;
            }
        }
        // Revisit the last 16 bytes if another chunk is in the same region.
        s.cursor += count >= 32 && s.cursor + count < regionEnd ? count - 16 : count;
    }
    if (s.attempted >= s.nextProgress) {
        s.nextProgress += 128ull * 1024 * 1024;
        table_log(logger, "scanning", s);
    }
    if (s.cursor >= s.maxAddress || s.attempted >= totalLimit || s.plausibleLayouts >= 2) {
        s.ended = true;
        table_log(logger,
            s.plausibleLayouts >= 2 ? "multiple_candidates" :
            s.attempted >= totalLimit ? "scan_budget_reached" : "address_space_complete", s);
    }
}

}} // namespace embervale::c4
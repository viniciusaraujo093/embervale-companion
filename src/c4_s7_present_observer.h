#pragma once
// C4-S9: exact-client (SHA-256) gated, passive Vulkan vkQueuePresentKHR observer.
// Loaded by Shroudtopia; no Vulkan Loader Layer, no GPU commands.
// Replaces only the verified client layout's slot +0x5A0.
// Experimental only. No hot-unload support; callback module is pinned.
#include <windows.h>
#include <bcrypt.h>
#include <array>
#ifdef EMBERVALE_C4_S10_PRESENT_INFO
#include <vulkan/vulkan.h>
#endif
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#pragma comment(lib, "bcrypt.lib")

namespace embervale { namespace c4 { namespace present {
using Logger = void (*)(const std::string&) noexcept;
using QueuePresentFn = std::int32_t (WINAPI*)(void*, const void*);
#ifdef EMBERVALE_C4_S14_ORANGE_SQUARE
using RenderDispatchFn = std::int32_t (*)(void*,const void*,QueuePresentFn) noexcept;
inline std::atomic<RenderDispatchFn> renderDispatch{nullptr};
#endif
using DeviceProcFn = void* (WINAPI*)(void*, const char*);
inline constexpr std::uintptr_t kPresentSlotOffset = 0x5A0;

inline std::atomic<std::uintptr_t> original{0};
inline std::atomic<std::uintptr_t> tableSlot{0};
inline std::atomic<std::uint64_t> calls{0};
inline std::atomic<std::uint32_t> inFlight{0};
inline std::atomic<bool> installed{false};
inline std::atomic<ULONGLONG> lastReport{0};
inline std::atomic<bool> rejectedOnce{false};
#ifdef EMBERVALE_C4_S10_PRESENT_INFO
// Capture an occasional read-only snapshot. This must never issue Vulkan commands.
inline std::atomic<std::uint64_t> presentSamples{0};
inline std::atomic<std::uint64_t> presentSampleValid{0};
inline std::atomic<std::uint64_t> presentSampleInvalid{0};
inline std::atomic<std::uintptr_t> lastQueue{0};
inline std::atomic<std::uint64_t> lastSwapchain{0};
inline std::atomic<std::uint32_t> lastImageIndex{UINT32_MAX};
inline std::atomic<std::uint32_t> lastSwapchainCount{0};
inline std::atomic<std::uint32_t> lastWaitCount{0};

inline bool sample_copy(const void* address, void* result, SIZE_T bytes) noexcept {
    if (!address || !result || !bytes) return false;
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, result, bytes, &copied) && copied == bytes;
}

inline void capture_present_info(void* queue, const void* rawInfo) noexcept {
    presentSamples.fetch_add(1, std::memory_order_relaxed);
    VkPresentInfoKHR snapshot{};
    if (!sample_copy(rawInfo, &snapshot, sizeof(snapshot)) ||
        snapshot.sType != VK_STRUCTURE_TYPE_PRESENT_INFO_KHR ||
        snapshot.swapchainCount == 0 || snapshot.swapchainCount > 8 ||
        !snapshot.pSwapchains || !snapshot.pImageIndices ||
        snapshot.waitSemaphoreCount > 64 ||
        (snapshot.waitSemaphoreCount && !snapshot.pWaitSemaphores)) {
        presentSampleInvalid.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::uint32_t imageIndex = UINT32_MAX;
    if (!sample_copy(snapshot.pSwapchains, &swapchain, sizeof(swapchain)) ||
        !sample_copy(snapshot.pImageIndices, &imageIndex, sizeof(imageIndex)) ||
        imageIndex >= 256) {
        presentSampleInvalid.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    static_assert(sizeof(VkSwapchainKHR) <= sizeof(std::uint64_t), "Unexpected Vulkan handle size");
    std::uint64_t handleBits = 0;
    std::memcpy(&handleBits, &swapchain, sizeof(swapchain));
    lastQueue.store(reinterpret_cast<std::uintptr_t>(queue), std::memory_order_relaxed);
    lastSwapchain.store(handleBits, std::memory_order_relaxed);
    lastImageIndex.store(imageIndex, std::memory_order_relaxed);
    lastSwapchainCount.store(snapshot.swapchainCount, std::memory_order_relaxed);
    lastWaitCount.store(snapshot.waitSemaphoreCount, std::memory_order_relaxed);
    presentSampleValid.fetch_add(1, std::memory_order_release);
}
#endif

inline std::int32_t WINAPI observe_queue_present(void* queue, const void* info) noexcept {
    inFlight.fetch_add(1, std::memory_order_acq_rel);
    calls.fetch_add(1, std::memory_order_relaxed);
#ifdef EMBERVALE_C4_S10_PRESENT_INFO
    // Sample once every 256 presentations, not on every frame.
    if ((calls.load(std::memory_order_relaxed) & 255u) == 1u)
        capture_present_info(queue, info);
#endif
    auto next = reinterpret_cast<QueuePresentFn>(original.load(std::memory_order_acquire));
#ifdef EMBERVALE_C4_S14_ORANGE_SQUARE
    const auto render = renderDispatch.load(std::memory_order_acquire);
    const auto result = render ? render(queue,info,next) :
        (next ? next(queue,info) : static_cast<std::int32_t>(-1));
#else
    const auto result = next ? next(queue, info) : static_cast<std::int32_t>(-1);
#endif
    inFlight.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

inline bool verified_executable() noexcept {
    // Exact match to the Enshrouded client build documented by the maintained
    // Aerox912 minimap's Build26082781 profile (2026-10-21).
    static const bool verified = []() noexcept -> bool {
        try {
            wchar_t path[32768]{};
            const DWORD n = GetModuleFileNameW(nullptr, path, 32768);
            if (n == 0 || n >= 32768) return false;
            std::ifstream input(std::filesystem::path(path), std::ios::binary);
            if (!input) return false;
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
                return false;
            bool ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
            std::array<char, 65536> bytes{};
            while (ok && input) {
                input.read(bytes.data(), bytes.size());
                const auto nread = input.gcount();
                if (nread > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(bytes.data()),
                        static_cast<ULONG>(nread), 0) < 0) ok = false;
            }
            ok = ok && input.eof() && !input.bad();
            std::array<unsigned char, 32> digest{};
            if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
            if (hash) BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            static constexpr std::array<unsigned char, 32> expected = {
                0xD6,0x49,0x66,0xE2,0x3B,0x1A,0x45,0x8E,
                0xE6,0xF1,0x47,0xBC,0xBC,0xEA,0xC0,0xB5,
                0x77,0xFA,0x7E,0x39,0xA2,0x12,0xF3,0x71,
                0x28,0x7D,0x8C,0xC1,0x19,0x81,0x78,0x82
            };
            return ok && digest == expected;
        } catch (...) { return false; }
    }();
    return verified;
}

inline bool get_ptr(std::uintptr_t addr, std::uintptr_t& out) noexcept {
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(addr),
                             &out, sizeof(out), &copied) && copied == sizeof(out);
}

inline bool executable(std::uintptr_t address) noexcept {
    if (address < 0x10000) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &m, sizeof(m)) != sizeof(m)) return false;
    const DWORD p = m.Protect & 0xff;
    return m.State == MEM_COMMIT && !(m.Protect & PAGE_GUARD) &&
           (p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
            p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY);
}

inline void emit(Logger log, const char* status, std::uintptr_t slot = 0) noexcept {
    if (!log) return;
    char buf[320]{};
    std::snprintf(buf, sizeof(buf),
        "C4-S9 status=%s present_calls=%llu slot=0x%llx offset=0x5a0 verified_client=%d gpu_work=0",
        status, static_cast<unsigned long long>(calls.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(slot), verified_executable() ? 1 : 0);
    try { log(std::string(buf)); } catch (...) {}
}

inline void reject_once(Logger log, const char* reason) noexcept {
    if (!rejectedOnce.exchange(true, std::memory_order_acq_rel)) emit(log, reason);
}

inline void maybe_install(std::uintptr_t candidate, Logger log) noexcept {
    if (installed.load(std::memory_order_acquire)) return;
    if (!verified_executable()) { reject_once(log, "unsupported_executable_no_hook"); return; }
    if ((candidate & 7u) || candidate > UINTPTR_MAX - kPresentSlotOffset - sizeof(void*)) return;

    const HMODULE loader = GetModuleHandleW(L"vulkan-1.dll");
    if (!loader) return;
    const FARPROC gipa = GetProcAddress(loader, "vkGetInstanceProcAddr");
    const FARPROC create = GetProcAddress(loader, "vkCreateInstance");
    const FARPROC gdpa = GetProcAddress(loader, "vkGetDeviceProcAddr");
    if (!gipa || !create || !gdpa) return;

    std::uintptr_t mod = 0, inst = 0, ci = 0, dp = 0, device = 0;
    std::uintptr_t createSwapchain = 0, getImages = 0, acquire = 0, old = 0;
    if (!get_ptr(candidate + 0x10, mod) || !get_ptr(candidate + 0x18, inst) ||
        !get_ptr(candidate + 0x20, ci) || !get_ptr(candidate + 0x78, dp) ||
        !get_ptr(candidate + 0x08, device) ||
        !get_ptr(candidate + 0x580, createSwapchain) ||
        !get_ptr(candidate + 0x590, getImages) ||
        !get_ptr(candidate + 0x598, acquire) ||
        !get_ptr(candidate + kPresentSlotOffset, old)) return;
    if (mod != reinterpret_cast<std::uintptr_t>(loader) ||
        inst != reinterpret_cast<std::uintptr_t>(gipa) ||
        ci != reinterpret_cast<std::uintptr_t>(create) ||
        dp != reinterpret_cast<std::uintptr_t>(gdpa) || !device ||
        !executable(createSwapchain) || !executable(getImages) ||
        !executable(acquire) || !executable(old) ||
        old == reinterpret_cast<std::uintptr_t>(&observe_queue_present)) return;

    // VkDevice is a dispatchable handle. Require its dispatch word to be
    // readable before passing it to the loader's getDeviceProcAddr.
    std::uintptr_t deviceDispatch = 0;
    if (device < 0x10000 || !get_ptr(device, deviceDispatch) || deviceDispatch < 0x10000)
        return;

    // Require the actual current device resolver to identify this precise slot
    // as vkQueuePresentKHR. Do not guess from offsets alone.
    const auto resolve = reinterpret_cast<DeviceProcFn>(gdpa);
    const auto resolved = reinterpret_cast<std::uintptr_t>(resolve(reinterpret_cast<void*>(device), "vkQueuePresentKHR"));
    if (!resolved || resolved != old) {
        reject_once(log, "device_resolver_mismatch_no_hook");
        return;
    }

    const auto address = candidate + kPresentSlotOffset;
    MEMORY_BASIC_INFORMATION m{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &m, sizeof(m)) != sizeof(m)) return;
    const DWORD protection = m.Protect & 0xff;
    if (m.State != MEM_COMMIT || m.Type != MEM_PRIVATE || (m.Protect & PAGE_GUARD) ||
        (protection != PAGE_READWRITE && protection != PAGE_EXECUTE_READWRITE)) {
        reject_once(log, "slot_not_writable_no_hook");
        return;
    }

    // Pin callback code before publishing pointer: no hot-unload of this DLL.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&observe_queue_present), &pinned) || !pinned) {
        reject_once(log, "pin_failed_no_hook");
        return;
    }
    original.store(old, std::memory_order_release);
    const auto previous = InterlockedCompareExchangePointer(
        reinterpret_cast<PVOID volatile*>(address),
        reinterpret_cast<void*>(&observe_queue_present), reinterpret_cast<void*>(old));
    if (reinterpret_cast<std::uintptr_t>(previous) != old) {
        reject_once(log, "slot_changed_no_hook");
        return;
    }
    tableSlot.store(address, std::memory_order_release);
    installed.store(true, std::memory_order_release);
    lastReport.store(GetTickCount64(), std::memory_order_relaxed);
    emit(log, "hook_installed", address);
}

inline void report(Logger log) noexcept {
    if (!installed.load(std::memory_order_acquire)) return;
    const ULONGLONG now = GetTickCount64();
    auto last = lastReport.load(std::memory_order_relaxed);
    if (now - last < 5000 ||
        !lastReport.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
    const auto slot = tableSlot.load(std::memory_order_acquire);
    std::uintptr_t current = 0;
    if (!slot || !get_ptr(slot, current)) { emit(log, "slot_unreadable", slot); return; }
    emit(log, current == reinterpret_cast<std::uintptr_t>(&observe_queue_present)
                  ? "observing" : "slot_replaced", slot);
#ifdef EMBERVALE_C4_S10_PRESENT_INFO
    char diag[460]{};
    const auto valid = presentSampleValid.load(std::memory_order_acquire);
    std::snprintf(diag, sizeof(diag),
        "C4-S10 status=%s samples=%llu valid=%llu invalid=%llu "
        "queue=0x%llx swapchain=0x%llx image_index=%u swapchain_count=%u "
        "wait_semaphores=%u present_info_read_only=1 hook=S9 gpu_work=0",
        valid ? "present_info_captured" : "waiting_for_valid_present_info",
        static_cast<unsigned long long>(presentSamples.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(valid),
        static_cast<unsigned long long>(presentSampleInvalid.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(lastQueue.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(lastSwapchain.load(std::memory_order_relaxed)),
        lastImageIndex.load(std::memory_order_relaxed),
        lastSwapchainCount.load(std::memory_order_relaxed),
        lastWaitCount.load(std::memory_order_relaxed));
    try { log(std::string(diag)); } catch (...) {}
#endif
}

inline void stop(Logger log) noexcept {
    if (!installed.exchange(false, std::memory_order_acq_rel)) return;
    const auto slot = tableSlot.exchange(0, std::memory_order_acq_rel);
    const auto previous = original.load(std::memory_order_acquire);
    if (!slot || !previous) return;
    MEMORY_BASIC_INFORMATION m{};
    if (VirtualQuery(reinterpret_cast<const void*>(slot), &m, sizeof(m)) == sizeof(m) &&
        m.State == MEM_COMMIT && !(m.Protect & PAGE_GUARD) &&
        ((m.Protect & 0xff) == PAGE_READWRITE || (m.Protect & 0xff) == PAGE_EXECUTE_READWRITE)) {
        const auto found = InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(slot), reinterpret_cast<void*>(previous),
            reinterpret_cast<void*>(&observe_queue_present));
        emit(log, found == reinterpret_cast<void*>(&observe_queue_present)
            ? "hook_restored" : "restore_skipped_other_owner", slot);
    } else emit(log, "restore_skipped_unwritable", slot);
    // Best-effort draining is not proof of safe unloading; callback module is pinned.
    for (unsigned i = 0; i < 500 && inFlight.load(std::memory_order_acquire); ++i) Sleep(1);
}
}}} // namespace embervale::c4::present

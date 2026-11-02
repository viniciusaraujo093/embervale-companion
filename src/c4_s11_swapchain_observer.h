#pragma once
// C4-S11: experimental observation of Vulkan swapchain CREATE and GET_IMAGES.
// Requires the exact-match C4-S9 presenter and C4-S10 Vulkan headers.
// Does NOT allocate/submit GPU commands. Two more dispatch-slot writes; no hot reload.
#include <windows.h>
#include <vulkan/vulkan.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace embervale { namespace c4 { namespace swapchain {
using Logger = embervale::c4::present::Logger;
inline constexpr std::uintptr_t kCreate = 0x580;
inline constexpr std::uintptr_t kImages = 0x590;
inline std::atomic<std::uintptr_t> originalCreate{0}, originalImages{0};
inline std::atomic<std::uintptr_t> slotCreate{0}, slotImages{0};
inline std::atomic<bool> installed{false}, attempted{false};
inline std::atomic<std::uint32_t> inFlight{0};
inline std::atomic<std::uint64_t> creates{0}, imagesCalls{0}, created{0}, imageResults{0};
inline std::atomic<std::uint64_t> lastSwapchain{0};
inline std::atomic<std::uintptr_t> lastDevice{0};
inline std::atomic<std::uint32_t> format{0}, colorSpace{0}, width{0}, height{0}, minimumImages{0}, usage{0}, presentMode{0}, actualImages{0};
inline std::array<std::atomic<std::uint64_t>,8> imageHandles{};
inline std::atomic<ULONGLONG> reportTick{0};

inline std::uint64_t handle_bits(VkSwapchainKHR handle) noexcept {
    static_assert(sizeof(handle) <= sizeof(std::uint64_t), "Unsupported swapchain handle size");
    std::uint64_t bits = 0; std::memcpy(&bits, &handle, sizeof(handle)); return bits;
}
inline std::uint64_t image_bits(VkImage handle) noexcept {
    static_assert(sizeof(handle) <= sizeof(std::uint64_t), "Unsupported image handle size");
    std::uint64_t bits = 0; std::memcpy(&bits, &handle, sizeof(handle)); return bits;
}

inline VKAPI_ATTR VkResult VKAPI_CALL observe_create(
    VkDevice device, const VkSwapchainCreateInfoKHR* info,
    const VkAllocationCallbacks* allocator, VkSwapchainKHR* output) noexcept {
    inFlight.fetch_add(1,std::memory_order_acq_rel);
    creates.fetch_add(1,std::memory_order_relaxed);
    VkSwapchainCreateInfoKHR snapshot{};
    const bool readable = embervale::c4::present::sample_copy(info,&snapshot,sizeof(snapshot)) &&
        snapshot.sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR &&
        snapshot.imageExtent.width && snapshot.imageExtent.height &&
        snapshot.imageExtent.width <= 16384 && snapshot.imageExtent.height <= 16384;
    auto next = reinterpret_cast<PFN_vkCreateSwapchainKHR>(originalCreate.load(std::memory_order_acquire));
    VkResult result = next ? next(device,info,allocator,output) : VK_ERROR_INITIALIZATION_FAILED;
    VkSwapchainKHR chain = VK_NULL_HANDLE;
    if (readable && result == VK_SUCCESS && output &&
        embervale::c4::present::sample_copy(output,&chain,sizeof(chain)) && chain != VK_NULL_HANDLE) {
        lastSwapchain.store(handle_bits(chain),std::memory_order_relaxed);
        lastDevice.store(reinterpret_cast<std::uintptr_t>(device),std::memory_order_release);
        format.store(static_cast<std::uint32_t>(snapshot.imageFormat),std::memory_order_relaxed);
        colorSpace.store(static_cast<std::uint32_t>(snapshot.imageColorSpace),std::memory_order_relaxed);
        width.store(snapshot.imageExtent.width,std::memory_order_relaxed);
        height.store(snapshot.imageExtent.height,std::memory_order_relaxed);
        minimumImages.store(snapshot.minImageCount,std::memory_order_relaxed);
        usage.store(snapshot.imageUsage,std::memory_order_relaxed);
        presentMode.store(static_cast<std::uint32_t>(snapshot.presentMode),std::memory_order_relaxed);
        actualImages.store(0,std::memory_order_relaxed);
        for (auto& image : imageHandles) image.store(0,std::memory_order_relaxed);
        created.fetch_add(1,std::memory_order_release);
    }
    inFlight.fetch_sub(1,std::memory_order_acq_rel);
    return result;
}

inline VKAPI_ATTR VkResult VKAPI_CALL observe_images(
    VkDevice device, VkSwapchainKHR chain, std::uint32_t* count, VkImage* images) noexcept {
    inFlight.fetch_add(1,std::memory_order_acq_rel);
    imagesCalls.fetch_add(1,std::memory_order_relaxed);
    auto next = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(originalImages.load(std::memory_order_acquire));
    VkResult result = next ? next(device,chain,count,images) : VK_ERROR_INITIALIZATION_FAILED;
    std::uint32_t n = 0;
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && count &&
        embervale::c4::present::sample_copy(count,&n,sizeof(n)) && n > 0 && n <= imageHandles.size() &&
        images && handle_bits(chain) == lastSwapchain.load(std::memory_order_relaxed)) {
        std::array<VkImage,8> snapshot{};
        if (embervale::c4::present::sample_copy(images,snapshot.data(),n*sizeof(VkImage))) {
            for (std::uint32_t i=0;i<n;++i)
                imageHandles[i].store(image_bits(snapshot[i]),std::memory_order_relaxed);
            actualImages.store(n,std::memory_order_release);
            imageResults.fetch_add(1,std::memory_order_relaxed);
        }
    }
    inFlight.fetch_sub(1,std::memory_order_acq_rel);
    return result;
}

inline void emit(Logger log,const char* status) noexcept {
    if (!log) return;
    char buf[550]{};
    std::snprintf(buf,sizeof(buf),
       "C4-S11 status=%s create_calls=%llu created=%llu image_calls=%llu image_results=%llu "
       "swapchain=0x%llx format=%u colorspace=%u extent=%ux%u min_images=%u actual_images=%u "
       "usage=0x%x present_mode=%u image0=0x%llx image1=0x%llx image2=0x%llx gpu_work=0",
       status,static_cast<unsigned long long>(creates.load()),static_cast<unsigned long long>(created.load()),
       static_cast<unsigned long long>(imagesCalls.load()),static_cast<unsigned long long>(imageResults.load()),
       static_cast<unsigned long long>(lastSwapchain.load()),format.load(),colorSpace.load(),
       width.load(),height.load(),minimumImages.load(),actualImages.load(),usage.load(),presentMode.load(),
       static_cast<unsigned long long>(imageHandles[0].load()),
       static_cast<unsigned long long>(imageHandles[1].load()),
       static_cast<unsigned long long>(imageHandles[2].load()));
    try {log(std::string(buf));} catch (...) {}
}

inline void maybe_install(std::uintptr_t table,Logger log) noexcept {
    if (installed.load(std::memory_order_acquire) || attempted.load(std::memory_order_acquire) ||
        !embervale::c4::present::installed.load(std::memory_order_acquire) ||
        embervale::c4::present::tableSlot.load(std::memory_order_acquire) != table+0x5A0 ||
        !embervale::c4::present::verified_executable() || (table & 7u) ||
        table > UINTPTR_MAX - kImages - sizeof(void*)) return;
    const auto vulkan = GetModuleHandleW(L"vulkan-1.dll");
    if (!vulkan) return;
    const auto g = reinterpret_cast<embervale::c4::present::DeviceProcFn>(GetProcAddress(vulkan,"vkGetDeviceProcAddr"));
    if (!g) return;
    std::uintptr_t device=0,oldCreate=0,oldImages=0;
    if (!embervale::c4::present::get_ptr(table+8,device) || device < 0x10000 ||
        !embervale::c4::present::get_ptr(table+kCreate,oldCreate) ||
        !embervale::c4::present::get_ptr(table+kImages,oldImages) ||
        !embervale::c4::present::executable(oldCreate) ||
        !embervale::c4::present::executable(oldImages)) return;
    const auto resolvedCreate = reinterpret_cast<std::uintptr_t>(g(reinterpret_cast<void*>(device),"vkCreateSwapchainKHR"));
    const auto resolvedImages = reinterpret_cast<std::uintptr_t>(g(reinterpret_cast<void*>(device),"vkGetSwapchainImagesKHR"));
    if (resolvedCreate != oldCreate || resolvedImages != oldImages) return;
    MEMORY_BASIC_INFORMATION a{},b{};
    if (VirtualQuery(reinterpret_cast<void*>(table+kCreate),&a,sizeof(a)) != sizeof(a) ||
        VirtualQuery(reinterpret_cast<void*>(table+kImages),&b,sizeof(b)) != sizeof(b) ||
        a.State!=MEM_COMMIT || b.State!=MEM_COMMIT || a.Type!=MEM_PRIVATE || b.Type!=MEM_PRIVATE ||
        (a.Protect & PAGE_GUARD) || (b.Protect & PAGE_GUARD) ||
        ((a.Protect & 0xff)!=PAGE_READWRITE && (a.Protect & 0xff)!=PAGE_EXECUTE_READWRITE) ||
        ((b.Protect & 0xff)!=PAGE_READWRITE && (b.Protect & 0xff)!=PAGE_EXECUTE_READWRITE)) return;
    // S9 pinned this module before it published its callback; still pin explicitly.
    HMODULE self=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&observe_create),&self) || !self) return;
    if (attempted.exchange(true,std::memory_order_acq_rel)) return;
    originalCreate.store(oldCreate,std::memory_order_release);
    originalImages.store(oldImages,std::memory_order_release);
    const auto p1 = InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(table+kCreate),
        reinterpret_cast<void*>(&observe_create),reinterpret_cast<void*>(oldCreate));
    if (reinterpret_cast<std::uintptr_t>(p1)!=oldCreate) {emit(log,"create_slot_changed_no_hook");return;}
    const auto p2 = InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(table+kImages),
        reinterpret_cast<void*>(&observe_images),reinterpret_cast<void*>(oldImages));
    if (reinterpret_cast<std::uintptr_t>(p2)!=oldImages) {
        InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(table+kCreate),
            reinterpret_cast<void*>(oldCreate),reinterpret_cast<void*>(&observe_create));
        emit(log,"images_slot_changed_no_hook");return;
    }
    slotCreate.store(table+kCreate,std::memory_order_release);
    slotImages.store(table+kImages,std::memory_order_release);
    installed.store(true,std::memory_order_release);
    reportTick.store(GetTickCount64(),std::memory_order_relaxed);
    emit(log,"hooks_installed_waiting_for_recreation");
}
inline void report(Logger log) noexcept {
    if (!installed.load(std::memory_order_acquire)) return;
    auto now=GetTickCount64();auto last=reportTick.load(std::memory_order_relaxed);
    if (now-last<5000 || !reportTick.compare_exchange_strong(last,now)) return;
    emit(log,created.load(std::memory_order_acquire) ? "swapchain_observed" : "awaiting_swapchain_recreation");
}
inline void restore(std::uintptr_t slot,std::uintptr_t orig,void* hook) noexcept {
    if (!slot || !orig) return;
    MEMORY_BASIC_INFORMATION m{};
    if (VirtualQuery(reinterpret_cast<void*>(slot),&m,sizeof(m))!=sizeof(m) || m.State!=MEM_COMMIT ||
        (m.Protect & PAGE_GUARD) || ((m.Protect & 0xff)!=PAGE_READWRITE &&
        (m.Protect & 0xff)!=PAGE_EXECUTE_READWRITE)) return;
    InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(slot),reinterpret_cast<void*>(orig),hook);
}
inline void stop(Logger log) noexcept {
    if (!installed.exchange(false,std::memory_order_acq_rel)) return;
    restore(slotCreate.load(),originalCreate.load(),reinterpret_cast<void*>(&observe_create));
    restore(slotImages.load(),originalImages.load(),reinterpret_cast<void*>(&observe_images));
    // A pinned module and no hot-reload remain mandatory; draining is only best-effort.
    for (unsigned n=0;n<500 && inFlight.load(std::memory_order_acquire);++n) Sleep(1);
    emit(log,"hooks_restore_attempted");
}
}}} // namespace embervale::c4::swapchain

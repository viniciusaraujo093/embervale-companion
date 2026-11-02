#pragma once
// C4-S12: read-only acquisition telemetry. NO GPU commands and no image writes.
// Exact-game SHA check inherited from C4-S9; uses S11 snapshot for generation only.
// Observes vkAcquireNextImageKHR via the actual device-resolved +0x598 slot.
// If game uses vkAcquireNextImage2KHR, this observer may remain silent.
#include <windows.h>
#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
namespace embervale { namespace c4 { namespace acquire {
using Logger = embervale::c4::present::Logger;
inline constexpr std::uintptr_t kSlotOffset=0x598;
inline std::atomic<std::uintptr_t> original{0}, installedSlot{0};
inline std::atomic<bool> installed{false},attempted{false};
inline std::atomic<std::uint32_t> inFlight{0};
inline std::atomic<std::uint64_t> calls{0},successes{0},matchingGeneration{0},otherChain{0};
inline std::atomic<std::uint64_t> lastChain{0},lastGeneration{0};
inline std::atomic<std::uint32_t> lastImageIndex{UINT32_MAX};
inline std::atomic<ULONGLONG> reportTick{0};
inline std::array<std::atomic<std::uint64_t>,8> acquiredTokens{};
inline std::array<std::atomic<std::uint64_t>,8> acquiredChainByImage{};

inline VKAPI_ATTR VkResult VKAPI_CALL observe_acquire(
    VkDevice device,VkSwapchainKHR chain,std::uint64_t timeout,
    VkSemaphore semaphore,VkFence fence,std::uint32_t* index) noexcept {
    inFlight.fetch_add(1,std::memory_order_acq_rel);
    calls.fetch_add(1,std::memory_order_relaxed);
    const auto next=reinterpret_cast<PFN_vkAcquireNextImageKHR>(original.load(std::memory_order_acquire));
    const VkResult result=next ? next(device,chain,timeout,semaphore,fence,index)
                               : VK_ERROR_INITIALIZATION_FAILED;
    if(result==VK_SUCCESS || result==VK_SUBOPTIMAL_KHR) {
        successes.fetch_add(1,std::memory_order_relaxed);
        std::uint32_t captured=UINT32_MAX;
        if(index && embervale::c4::present::sample_copy(index,&captured,sizeof(captured))) {
            const std::uint64_t bits=embervale::c4::swapchain::handle_bits(chain);
            const auto generation=embervale::c4::swapchain::created.load(std::memory_order_acquire);
            lastChain.store(bits,std::memory_order_relaxed);
            lastImageIndex.store(captured,std::memory_order_relaxed);
            lastGeneration.store(generation,std::memory_order_relaxed);
            // Publish acquired chain then token for all successful acquisitions,
            // including the initial swapchain unseen by the S11 creation observer.
            if (captured < acquiredTokens.size()) {
                acquiredChainByImage[captured].store(bits,std::memory_order_release);
                acquiredTokens[captured].fetch_add(1,std::memory_order_release);
            }
            if (bits!=0 && bits==embervale::c4::swapchain::lastSwapchain.load(std::memory_order_acquire) &&
                generation>0 && captured<embervale::c4::swapchain::actualImages.load(std::memory_order_acquire))
            {
                matchingGeneration.fetch_add(1,std::memory_order_relaxed);

            }
            else
                otherChain.fetch_add(1,std::memory_order_relaxed);
        }
    }
    inFlight.fetch_sub(1,std::memory_order_acq_rel);
    return result;
}

inline void emit(Logger log,const char* status) noexcept {
    if(!log) return;
    char buf[410]{};
    std::snprintf(buf,sizeof(buf),
       "C4-S12 status=%s acquire_calls=%llu success=%llu matching_latest_generation=%llu "
       "other_or_unknown_generation=%llu last_chain=0x%llx last_index=%u "
       "last_generation=%llu current_generation=%llu present_calls=%llu gpu_work=0",
       status,static_cast<unsigned long long>(calls.load()),
       static_cast<unsigned long long>(successes.load()),
       static_cast<unsigned long long>(matchingGeneration.load()),
       static_cast<unsigned long long>(otherChain.load()),
       static_cast<unsigned long long>(lastChain.load()),lastImageIndex.load(),
       static_cast<unsigned long long>(lastGeneration.load()),
       static_cast<unsigned long long>(embervale::c4::swapchain::created.load()),
       static_cast<unsigned long long>(embervale::c4::present::calls.load()));
    try{log(std::string(buf));}catch(...){}
}
inline void maybe_install(std::uintptr_t table,Logger log) noexcept {
    if(installed.load(std::memory_order_acquire) || attempted.load(std::memory_order_acquire) ||
       !embervale::c4::present::verified_executable() ||
       !embervale::c4::present::installed.load(std::memory_order_acquire) ||
       !embervale::c4::swapchain::installed.load(std::memory_order_acquire) ||
       embervale::c4::present::tableSlot.load(std::memory_order_acquire)!=table+0x5A0 ||
       (table&7u) || table>UINTPTR_MAX-kSlotOffset-sizeof(void*)) return;
    auto loader=GetModuleHandleW(L"vulkan-1.dll");if(!loader)return;
    auto exportAddr=GetProcAddress(loader,"vkGetDeviceProcAddr");if(!exportAddr)return;
    std::uintptr_t device=0,prior=0;
    if(!embervale::c4::present::get_ptr(table+8,device) || device<0x10000 ||
       !embervale::c4::present::get_ptr(table+kSlotOffset,prior) ||
       !embervale::c4::present::executable(prior)) return;
    std::uintptr_t dispatch=0;
    if(!embervale::c4::present::get_ptr(device,dispatch) || dispatch<0x10000)return;
    const auto resolver=reinterpret_cast<embervale::c4::present::DeviceProcFn>(exportAddr);
    const auto resolved=reinterpret_cast<std::uintptr_t>(resolver(
        reinterpret_cast<void*>(device),"vkAcquireNextImageKHR"));
    if(!resolved || resolved!=prior || prior==reinterpret_cast<std::uintptr_t>(&observe_acquire)) {
        if(!attempted.exchange(true))emit(log,"resolver_mismatch_no_hook");
        return;
    }
    MEMORY_BASIC_INFORMATION m{};
    if(VirtualQuery(reinterpret_cast<void*>(table+kSlotOffset),&m,sizeof(m))!=sizeof(m) ||
       m.State!=MEM_COMMIT || m.Type!=MEM_PRIVATE || (m.Protect&PAGE_GUARD) ||
       ((m.Protect&0xff)!=PAGE_READWRITE && (m.Protect&0xff)!=PAGE_EXECUTE_READWRITE))return;
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
          reinterpret_cast<LPCWSTR>(&observe_acquire),&self) || !self)return;
    if(attempted.exchange(true,std::memory_order_acq_rel))return;
    original.store(prior,std::memory_order_release);
    const auto old=InterlockedCompareExchangePointer(
       reinterpret_cast<PVOID volatile*>(table+kSlotOffset),
       reinterpret_cast<void*>(&observe_acquire),reinterpret_cast<void*>(prior));
    if(reinterpret_cast<std::uintptr_t>(old)!=prior) {emit(log,"slot_changed_no_hook");return;}
    installedSlot.store(table+kSlotOffset,std::memory_order_release);
    installed.store(true,std::memory_order_release);
    reportTick.store(GetTickCount64(),std::memory_order_relaxed);
    emit(log,"hook_installed");
}
inline void report(Logger log) noexcept {
    if(!installed.load(std::memory_order_acquire))return;
    const auto now=GetTickCount64();auto previous=reportTick.load(std::memory_order_relaxed);
    if(now-previous<5000 || !reportTick.compare_exchange_strong(previous,now))return;
    emit(log,successes.load()?"acquire_observed":"waiting_for_acquisition");
}
inline void stop(Logger log) noexcept {
    if(!installed.exchange(false,std::memory_order_acq_rel))return;
    const auto slot=installedSlot.load(std::memory_order_acquire);
    const auto old=original.load(std::memory_order_acquire);
    if(slot && old) {
        MEMORY_BASIC_INFORMATION m{};
        if(VirtualQuery(reinterpret_cast<void*>(slot),&m,sizeof(m))==sizeof(m) &&
           m.State==MEM_COMMIT && !(m.Protect&PAGE_GUARD) &&
           ((m.Protect&0xff)==PAGE_READWRITE || (m.Protect&0xff)==PAGE_EXECUTE_READWRITE))
            InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(slot),
               reinterpret_cast<void*>(old),reinterpret_cast<void*>(&observe_acquire));
    }
    for(unsigned i=0;i<500 && inFlight.load(std::memory_order_acquire);++i)Sleep(1);
    emit(log,"restore_attempted");
    // Callback DLL remains pinned; no dynamic-unload safety guarantee.
}
}}} // namespace embervale::c4::acquire

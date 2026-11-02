#pragma once
// Embervale Companion C4-S13 -- verified-client native render context capture.
// No Vulkan commands, no rendering and no GPU synchronization changes.
// This hook is independent of the Vulkan dispatch table; MinHook patches a
// verified native function entry. An x64 relay calls our observer and jumps
// to MinHook's original trampoline without assuming the function return type.
// Reference: MIT-licensed Aerox912/Enshrouded-minimap, client 26082781.
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <iterator>
#include <string>
#include <vector>

namespace embervale { namespace c4 { namespace nativeframe {
using Logger = embervale::c4::present::Logger;
inline constexpr std::uintptr_t kNativePresentRva = 0xFD4620;
inline constexpr std::array<std::uint8_t,15> kExpected = {
    0x4c,0x89,0x44,0x24,0x18, 0x48,0x89,0x54,0x24,0x10,
    0x48,0x89,0x4c,0x24,0x08
};
inline std::atomic<bool> installed{false}, attempted{false};
inline std::atomic<std::uint64_t> entries{0}, checked{0}, contexts{0}, rejected{0};
inline std::atomic<std::uintptr_t> observedTable{0}, observedDevice{0}, graphicsContext{0}, queueState{0};
inline std::atomic<std::uint32_t> queueFamilyHint{UINT32_MAX};
inline std::atomic<ULONGLONG> lastTick{0}, reportTick{0};
inline std::atomic<std::uintptr_t> targetAddress{0};
inline void* relayMemory = nullptr; // Retained for process lifetime: in-flight callbacks may use it.
inline bool ownsMinHook = false;

inline bool copy(std::uintptr_t address,void* output,std::size_t size) noexcept {
    if (!address || !output || size==0 || address > UINTPTR_MAX - size) return false;
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),
        output,size,&copied) && copied==size;
}

template<typename T> inline bool read(std::uintptr_t address,T& out) noexcept {
    return copy(address,&out,sizeof(out));
}

// Callback receives the native function's argument registers in Windows x64 ABI.
// Only periodic sampling; never mutates the render context or invokes Vulkan.
inline void WINAPI capture(void* renderContext,void*,void*,void*) noexcept {
    const auto n=entries.fetch_add(1,std::memory_order_relaxed)+1;
    if ((n & 63u)!=0 || !renderContext) return;
    checked.fetch_add(1,std::memory_order_relaxed);
    const auto root=reinterpret_cast<std::uintptr_t>(renderContext);
    std::uintptr_t table=0,device=0,graphics=0,tableDevice=0,queue=0;
    std::uintptr_t module=0,instanceProc=0,createInstance=0,deviceProc=0;
    std::uint32_t family=UINT32_MAX;
    // This callback runs before S9 exists. Prove the Vulkan table identity from
    // the loader exports, not from an already-installed hook address.
    if (!read(root+0x08,table) || !read(root+0x10,device) ||
        !read(root+0x28,graphics) || table<0x10000 || (table&7u) ||
        table>UINTPTR_MAX-0x5A8 || device<0x10000 || graphics<0x10000 ||
        !read(table+0x08,tableDevice) || device!=tableDevice ||
        !read(table+0x10,module) || !read(table+0x18,instanceProc) ||
        !read(table+0x20,createInstance) || !read(table+0x78,deviceProc) ||
        !read(graphics+0xDD0,queue) || !read(graphics+0xE34,family) ||
        family>31u) {
        rejected.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    HMODULE loader=GetModuleHandleW(L"vulkan-1.dll");
    if (!loader || module!=reinterpret_cast<std::uintptr_t>(loader) ||
        instanceProc!=reinterpret_cast<std::uintptr_t>(GetProcAddress(loader,"vkGetInstanceProcAddr")) ||
        createInstance!=reinterpret_cast<std::uintptr_t>(GetProcAddress(loader,"vkCreateInstance")) ||
        deviceProc!=reinterpret_cast<std::uintptr_t>(GetProcAddress(loader,"vkGetDeviceProcAddr"))) {
        rejected.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    observedTable.store(table,std::memory_order_release);
    observedDevice.store(device,std::memory_order_relaxed);
    graphicsContext.store(graphics,std::memory_order_relaxed);
    queueState.store(queue,std::memory_order_relaxed);
    queueFamilyHint.store(family,std::memory_order_release);
    lastTick.store(GetTickCount64(),std::memory_order_release);
    contexts.fetch_add(1,std::memory_order_relaxed);
}

// This relay deliberately jumps to the MinHook trampoline. It does not call
// the original function as a guessed void function (which would lose return
// values), and it doesn't re-execute/relocate game bytes itself.
inline std::vector<std::uint8_t> make_relay() {
    std::vector<std::uint8_t> s={
        0x48,0x83,0xEC,0x48,                   // sub rsp, 48h: 16-byte align and shadow space
        0x48,0x89,0x4C,0x24,0x20,             // save rcx
        0x48,0x89,0x54,0x24,0x28,             // save rdx
        0x4C,0x89,0x44,0x24,0x30,             // save r8
        0x4C,0x89,0x4C,0x24,0x38,             // save r9
        0x48,0xB8                               // mov rax, &capture
    };
    const auto callback=reinterpret_cast<std::uintptr_t>(&capture);
    for(unsigned i=0;i<8;++i)s.push_back(static_cast<std::uint8_t>(callback>>(i*8)));
    const std::uint8_t rest[]={
        0xFF,0xD0,                             // call rax
        0x48,0x8B,0x4C,0x24,0x20,             // restore rcx
        0x48,0x8B,0x54,0x24,0x28,             // restore rdx
        0x4C,0x8B,0x44,0x24,0x30,             // restore r8
        0x4C,0x8B,0x4C,0x24,0x38,             // restore r9
        0x48,0x83,0xC4,0x48,                  // add rsp, 48h
        0xFF,0x25,0,0,0,0                     // jmp qword ptr [rip+0]
    };
    s.insert(s.end(),std::begin(rest),std::end(rest));
    for(int i=0;i<8;++i)s.push_back(0); // trampoline address filled before publication
    return s;
}

inline void emit(Logger log,const char* status,int detail=0) noexcept {
    if (!log) return;
    char buf[600]{};
    const auto now=GetTickCount64();
    const auto capturedAt=lastTick.load(std::memory_order_acquire);
    const auto age=capturedAt && now>=capturedAt ? now-capturedAt : 0;
    std::snprintf(buf,sizeof(buf),
        "C4-S13 status=%s entries=%llu sampled=%llu valid_contexts=%llu rejected=%llu "
        "table=0x%llx device=0x%llx graphics=0x%llx queue_state=0x%llx "
        "queue_family_hint=%u age_ms=%llu detail=%d hook=native_present gpu_work=0",
        status,static_cast<unsigned long long>(entries.load()),
        static_cast<unsigned long long>(checked.load()),
        static_cast<unsigned long long>(contexts.load()),
        static_cast<unsigned long long>(rejected.load()),
        static_cast<unsigned long long>(observedTable.load()),
        static_cast<unsigned long long>(observedDevice.load()),
        static_cast<unsigned long long>(graphicsContext.load()),
        static_cast<unsigned long long>(queueState.load()),queueFamilyHint.load(),
        static_cast<unsigned long long>(age),detail);
    try{log(std::string(buf));}catch(...){}
}

inline void maybe_install(Logger log) noexcept {
#if !defined(_WIN64) && !defined(_M_X64)
    (void)log;return;
#else
    if(installed.load(std::memory_order_acquire) || attempted.load(std::memory_order_acquire) ||
       !embervale::c4::present::verified_executable()) return;
    HMODULE exe=GetModuleHandleW(nullptr);
    if (!exe || reinterpret_cast<std::uintptr_t>(exe)>UINTPTR_MAX-kNativePresentRva-32) return;
    const auto address=reinterpret_cast<std::uintptr_t>(exe)+kNativePresentRva;
    std::array<std::uint8_t,kExpected.size()> original{};
    if (!embervale::c4::present::executable(address) ||
        !copy(address,original.data(),original.size()) || original!=kExpected) {
        if(!attempted.exchange(true)) emit(log,"native_prologue_mismatch_no_hook");
        return;
    }
    // One installer only. MinHook suspends other threads during patch enabling.
    if(attempted.exchange(true,std::memory_order_acq_rel))return;
    HMODULE pin=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
         reinterpret_cast<LPCWSTR>(&capture),&pin) || !pin) {
        emit(log,"pin_failed_no_hook");return;
    }
    const auto init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED){emit(log,"minhook_init_failed",static_cast<int>(init));return;}
    ownsMinHook=(init==MH_OK);
    auto bytes=make_relay();
    void* memory=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    if(!memory){emit(log,"relay_alloc_failed");return;}
    relayMemory=memory;
    void* trampoline=nullptr;
    const auto create=MH_CreateHook(reinterpret_cast<void*>(address),memory,&trampoline);
    if(create!=MH_OK || !trampoline){emit(log,"minhook_create_failed",static_cast<int>(create));return;}
    const auto trampolineBits=reinterpret_cast<std::uintptr_t>(trampoline);
    for(unsigned i=0;i<8;++i)bytes[bytes.size()-8+i]=static_cast<std::uint8_t>(trampolineBits>>(i*8));
    std::memcpy(memory,bytes.data(),bytes.size());
    DWORD previous=0;
    if(!VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&previous) ||
       !FlushInstructionCache(GetCurrentProcess(),memory,bytes.size())) {
        MH_RemoveHook(reinterpret_cast<void*>(address));
        emit(log,"relay_protect_failed");return;
    }
    const auto enable=MH_EnableHook(reinterpret_cast<void*>(address));
    if(enable!=MH_OK){MH_RemoveHook(reinterpret_cast<void*>(address));emit(log,"minhook_enable_failed",static_cast<int>(enable));return;}
    targetAddress.store(address,std::memory_order_release);
    installed.store(true,std::memory_order_release);
    reportTick.store(GetTickCount64(),std::memory_order_release);
    emit(log,"native_hook_installed");
#endif
}

inline void report(Logger log) noexcept {
    if(!installed.load(std::memory_order_acquire))return;
    const auto now=GetTickCount64();auto prior=reportTick.load(std::memory_order_relaxed);
    if(now-prior<5000 || !reportTick.compare_exchange_strong(prior,now))return;
    emit(log,contexts.load(std::memory_order_relaxed)?"context_captured":"waiting_for_context");
}

inline void stop(Logger log) noexcept {
    if(!installed.exchange(false,std::memory_order_acq_rel))return;
    const auto target=targetAddress.exchange(0,std::memory_order_acq_rel);
    if(target){
        const auto result=MH_DisableHook(reinterpret_cast<void*>(target));
        emit(log,result==MH_OK?"native_hook_disabled":"native_hook_disable_failed",static_cast<int>(result));
        // Do not remove/free the trampoline or relay: an in-flight thread may
        // already have selected it. The DLL has been pinned for this reason.
    }
}
}}} // namespace embervale::c4::nativeframe

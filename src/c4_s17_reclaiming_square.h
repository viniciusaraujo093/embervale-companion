#pragma once
// Embervale Companion C4-S14: opt-in orange square on the verified Enshrouded 1.0 client.
// S17: validate resource identity and reclaim retired generations only after
// an observed successor present/reacquisition synchronization proof.
// Unextended WSI limitations remain: this is an experimental bridge, not production.
// Existing swapchain bootstrap uses a verified-client-specific 2560x1440/format-37 assumption.
// No hot reload, do not use for production releases.
// Vulkan functions are resolved for the verified VkDevice (no loader layer needed).
#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace embervale { namespace c4 { namespace square {
using Logger = embervale::c4::present::Logger;
inline std::atomic<bool> armed{false};
inline std::atomic<std::uint64_t> drawCalls{0}, drawSuccess{0}, noToken{0}, skipped{0}, submitErrors{0};
inline std::atomic<int> lastError{0};
inline std::atomic<std::uint64_t> queueMismatches{0};
inline std::atomic<ULONGLONG> lastLog{0};
inline std::atomic<int> phase{0}; // 0=await setup,1=ready,2=retirement_limit,3=GPU failure
inline std::mutex gpuMutex;

struct DeviceFns {
    PFN_vkGetDeviceQueue getDeviceQueue=nullptr;
    PFN_vkGetSwapchainImagesKHR getSwapchainImages=nullptr;
    PFN_vkCreateCommandPool createCommandPool=nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool=nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers=nullptr;
    PFN_vkCreateRenderPass createRenderPass=nullptr;
    PFN_vkDestroyRenderPass destroyRenderPass=nullptr;
    PFN_vkCreateImageView createImageView=nullptr;
    PFN_vkDestroyImageView destroyImageView=nullptr;
    PFN_vkCreateFramebuffer createFramebuffer=nullptr;
    PFN_vkDestroyFramebuffer destroyFramebuffer=nullptr;
    PFN_vkCreateSemaphore createSemaphore=nullptr;
    PFN_vkDestroySemaphore destroySemaphore=nullptr;
    PFN_vkCreateFence createFence=nullptr;
    PFN_vkDestroyFence destroyFence=nullptr;
    PFN_vkWaitForFences waitForFences=nullptr;
    PFN_vkResetFences resetFences=nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer=nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer=nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer=nullptr;
    PFN_vkCmdBeginRenderPass cmdBeginRenderPass=nullptr;
    PFN_vkCmdClearAttachments cmdClearAttachments=nullptr;
    PFN_vkCmdEndRenderPass cmdEndRenderPass=nullptr;
    PFN_vkQueueSubmit queueSubmit=nullptr;
};
struct Runtime {
    VkDevice device=VK_NULL_HANDLE;
    VkSwapchainKHR chain=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    std::uint64_t generation=0;
    std::uint32_t family=UINT32_MAX, count=0, width=0, height=0;
    VkFormat format=VK_FORMAT_UNDEFINED;
    VkRenderPass pass=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE;
    std::array<VkImage,8> images{};
    std::array<VkImageView,8> views{};
    std::array<VkFramebuffer,8> buffers{};
    std::array<VkCommandBuffer,8> commands{};
    std::array<VkSemaphore,8> signals{};
    std::array<VkFence,8> fences{};
    std::array<std::uint64_t,8> handledTokens{};
    DeviceFns f{};
    bool ready=false;
    bool bootstrapped=false;
    // Presentation-completion proof for reclaiming earlier generations:
    // 0=await first present; 1=first submitted; 2=first image reacquired and
    // overlay submitted again; 3=second submit fence signaled.
    std::uint8_t proofStage=0;
    std::uint32_t proofImage=UINT32_MAX;
    std::uint64_t firstProofToken=0;
    std::uint64_t lastForeignCreateSeen=0;
};
inline Runtime runtime{};
// Old generations remain owned by the mod until the NEW swapchain's first
// overlay-presented image is reacquired, synchronized by normal game rendering,
// and our follow-up submission fence signals. If proof never arrives, retain
// rather than guessing based on age, vkQueueWaitIdle or submission fences alone.
// Six remains a hard emergency cap; S17 is intended to reclaim below it.
inline std::array<Runtime,6> retired{};
inline std::size_t retiredCount=0;
inline std::atomic<std::uint64_t> rebuildCount{0}, bootstrapCount{0}, retireLimitCount{0};
inline std::atomic<std::uint64_t> reclaimedGenerations{0}, proofCompletions{0}, deferredForeignCreates{0};
inline std::atomic<bool> warnedBootstrap{false};

inline bool get_known_native_client_extent(std::uint32_t& width,std::uint32_t& height) noexcept {
    width=height=0;
    HWND hwnd=GetForegroundWindow();
    if (!hwnd) return false;
    DWORD pid=0;
    GetWindowThreadProcessId(hwnd,&pid);
    if (pid!=GetCurrentProcessId()) return false;
    RECT rect{};
    if (!GetClientRect(hwnd,&rect) || rect.right<=rect.left || rect.bottom<=rect.top) return false;
    width=static_cast<std::uint32_t>(rect.right-rect.left);
    height=static_cast<std::uint32_t>(rect.bottom-rect.top);
    return width==2560 && height==1440;
}

inline bool get_live_images(Runtime& r) noexcept {
    if (r.device==VK_NULL_HANDLE || r.chain==VK_NULL_HANDLE || !r.f.getSwapchainImages)return false;
    std::uint32_t count=0;
    if(r.f.getSwapchainImages(r.device,r.chain,&count,nullptr)!=VK_SUCCESS || count<2 || count>8)return false;
    std::array<VkImage,8> handles{};
    auto actual=count;
    if(r.f.getSwapchainImages(r.device,r.chain,&actual,handles.data())!=VK_SUCCESS || actual!=count)return false;
    for(std::uint32_t i=0;i<count;++i) {
        if(handles[i]==VK_NULL_HANDLE)return false;
        r.images[i]=handles[i];
    }
    r.count=count;
    return true;
}

// State of the RUNTIME WE ARE PRESENTING (not merely the newest S11 create).
// 0=keep, 1=latest matching handle is not enumerated yet (defer draw),
// 2=resource set changed, requires retirement and rebuild.
inline int presented_chain_state(Runtime& r,VkSwapchainKHR presented) noexcept {
    if (!r.ready || r.chain!=presented) return 2;
    const auto generation=embervale::c4::swapchain::created.load(std::memory_order_acquire);
    if (r.generation==generation) return 0;
    const auto newest=embervale::c4::swapchain::lastSwapchain.load(std::memory_order_acquire);
    if (newest!=embervale::c4::swapchain::handle_bits(presented)) {
        // Another swapchain has been created, but the old chain is STILL
        // presented. Do not re-create its live renderer prematurely.
        if(r.lastForeignCreateSeen!=generation) {
            r.lastForeignCreateSeen=generation;
            deferredForeignCreates.fetch_add(1,std::memory_order_relaxed);
        }
        return 0;
    }
    // A successful vkCreateSwapchainKHR is a NEW object lifetime. Handle values
    // (including image handles) can be recycled by the implementation, so
    // numerical equality is NOT proof that old image views remain valid.
    // Defer until this newly created swapchain has enumerated images, then
    // rebuild even if every numeric handle happens to be identical.
    if(embervale::c4::swapchain::actualImages.load(std::memory_order_acquire)==0)
        return 1;
    return 2;
}

inline bool retire_current() noexcept {
    if(!runtime.ready)return true;
    if(retiredCount>=retired.size()) {
        retireLimitCount.fetch_add(1,std::memory_order_relaxed);
        phase.store(2,std::memory_order_release);
        return false;
    }
    // Keep all handles and backing memory alive: WSI may still reference the
    // present-complete semaphore after command submission fences have signaled.
    retired[retiredCount++]=runtime;
    runtime=Runtime{};
    rebuildCount.fetch_add(1,std::memory_order_relaxed);
    phase.store(0,std::memory_order_release);
    return true;
}


inline bool resolve_functions(VkDevice device, DeviceFns& f) noexcept {
    const auto loader=GetModuleHandleW(L"vulkan-1.dll");
    if (!loader) return false;
    const auto resolve=reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(loader,"vkGetDeviceProcAddr"));
    if (!resolve) return false;
#define EC_S14_GET(member,name) f.member=reinterpret_cast<PFN_vk##name>(resolve(device,"vk" #name)); if (!f.member) return false
    EC_S14_GET(getDeviceQueue,GetDeviceQueue);
    EC_S14_GET(getSwapchainImages,GetSwapchainImagesKHR);
    EC_S14_GET(createCommandPool,CreateCommandPool);
    EC_S14_GET(destroyCommandPool,DestroyCommandPool);
    EC_S14_GET(allocateCommandBuffers,AllocateCommandBuffers);
    EC_S14_GET(createRenderPass,CreateRenderPass);
    EC_S14_GET(destroyRenderPass,DestroyRenderPass);
    EC_S14_GET(createImageView,CreateImageView);
    EC_S14_GET(destroyImageView,DestroyImageView);
    EC_S14_GET(createFramebuffer,CreateFramebuffer);
    EC_S14_GET(destroyFramebuffer,DestroyFramebuffer);
    EC_S14_GET(createSemaphore,CreateSemaphore);
    EC_S14_GET(destroySemaphore,DestroySemaphore);
    EC_S14_GET(createFence,CreateFence);
    EC_S14_GET(destroyFence,DestroyFence);
    EC_S14_GET(waitForFences,WaitForFences);
    EC_S14_GET(resetFences,ResetFences);
    EC_S14_GET(resetCommandBuffer,ResetCommandBuffer);
    EC_S14_GET(beginCommandBuffer,BeginCommandBuffer);
    EC_S14_GET(endCommandBuffer,EndCommandBuffer);
    EC_S14_GET(cmdBeginRenderPass,CmdBeginRenderPass);
    EC_S14_GET(cmdClearAttachments,CmdClearAttachments);
    EC_S14_GET(cmdEndRenderPass,CmdEndRenderPass);
    EC_S14_GET(queueSubmit,QueueSubmit);
#undef EC_S14_GET
    return true;
}
// Called only for a failed partial setup, never while the GPU is using these resources.
inline void destroy_partial(Runtime& r) noexcept {
    if (r.device==VK_NULL_HANDLE) return;
    for(auto x:r.fences) if (x!=VK_NULL_HANDLE && r.f.destroyFence) r.f.destroyFence(r.device,x,nullptr);
    for(auto x:r.signals) if (x!=VK_NULL_HANDLE && r.f.destroySemaphore) r.f.destroySemaphore(r.device,x,nullptr);
    for(auto x:r.buffers) if (x!=VK_NULL_HANDLE && r.f.destroyFramebuffer) r.f.destroyFramebuffer(r.device,x,nullptr);
    for(auto x:r.views) if (x!=VK_NULL_HANDLE && r.f.destroyImageView) r.f.destroyImageView(r.device,x,nullptr);
    if (r.pass!=VK_NULL_HANDLE && r.f.destroyRenderPass) r.f.destroyRenderPass(r.device,r.pass,nullptr);
    if (r.pool!=VK_NULL_HANDLE && r.f.destroyCommandPool) r.f.destroyCommandPool(r.device,r.pool,nullptr);
}

// Reclaim only after successor completion is proven, AND old GPU submit fences
// are signaled. Do not wait/block on the render thread; poll (timeout=0).
inline void reclaim_retired(Runtime& successor) noexcept {
    if (successor.proofStage==2) {
        if(successor.proofImage>=successor.count)return;
        const auto& f=successor.f;
        VkFence fence=successor.fences[successor.proofImage];
        if(!f.waitForFences || fence==VK_NULL_HANDLE ||
           f.waitForFences(successor.device,1,&fence,VK_TRUE,0)!=VK_SUCCESS)return;
        successor.proofStage=3;
        proofCompletions.fetch_add(1,std::memory_order_relaxed);
    }
    // A retired submit fence may be unfinished when the successor proof first
    // completes. Continue polling it in later presents, never forget leftovers.
    if(successor.proofStage!=3 || retiredCount==0)return;
    std::size_t n=0;
    while(n<retiredCount) {
        auto& old=retired[n];
        // The proof's queue/surface ordering is only usable for the same
        // Vulkan device and queue. Different identities remain quarantined.
        if(old.device!=successor.device || old.queue!=successor.queue ||
           !old.f.waitForFences){++n;continue;}
        bool completed=true;
        for(std::uint32_t j=0;j<old.count;++j) {
            const auto fenceOld=old.fences[j];
            if(fenceOld==VK_NULL_HANDLE ||
               old.f.waitForFences(old.device,1,&fenceOld,VK_TRUE,0)!=VK_SUCCESS){
                completed=false;break;
            }
        }
        if(!completed){++n;continue;}
        destroy_partial(old);
        retired[n]=retired[--retiredCount];
        retired[retiredCount]=Runtime{};
        reclaimedGenerations.fetch_add(1,std::memory_order_relaxed);
    }
}

inline bool build(Runtime& out,VkQueue queue,const VkPresentInfoKHR& info) noexcept {
    if(info.swapchainCount!=1 || !info.pSwapchains || !info.pImageIndices)return false;
    VkSwapchainKHR chain=VK_NULL_HANDLE;
    if(!embervale::c4::present::sample_copy(info.pSwapchains,&chain,sizeof(chain)) || chain==VK_NULL_HANDLE)return false;

    // Validate native renderer identity before using the device or queue.
    const auto deviceBits=embervale::c4::nativeframe::observedDevice.load(std::memory_order_acquire);
    const auto family=embervale::c4::nativeframe::queueFamilyHint.load(std::memory_order_acquire);
    const auto table=embervale::c4::nativeframe::observedTable.load(std::memory_order_acquire);
    const auto seen=embervale::c4::nativeframe::lastTick.load(std::memory_order_acquire);
    const auto now=GetTickCount64();
    if(!embervale::c4::present::verified_executable() ||
       !embervale::c4::nativeframe::installed.load(std::memory_order_acquire) ||
       deviceBits<0x10000 || table+0x5a0!=embervale::c4::present::tableSlot.load(std::memory_order_acquire) ||
       !seen || now<seen || now-seen>500 || family>31)return false;

    Runtime r{};
    r.device=reinterpret_cast<VkDevice>(deviceBits);
    r.chain=chain; r.queue=queue; r.family=family;
    if(!resolve_functions(r.device,r.f))return false;
    VkQueue expected=VK_NULL_HANDLE;
    r.f.getDeviceQueue(r.device,family,0,&expected);
    if(expected==VK_NULL_HANDLE || expected!=queue){
        queueMismatches.fetch_add(1,std::memory_order_relaxed);
        return false;
    }

    const auto generation=embervale::c4::swapchain::created.load(std::memory_order_acquire);
    const auto expectedChain=embervale::c4::swapchain::lastSwapchain.load(std::memory_order_acquire);
    r.generation=generation;
    if(generation>0) {
        // Recorded S11 creation is authoritative. Never fall back to guessed
        // dimensions after a generation change or while image enumeration is pending.
        if(embervale::c4::swapchain::handle_bits(chain)!=expectedChain ||
           deviceBits!=embervale::c4::swapchain::lastDevice.load(std::memory_order_acquire))return false;
        r.width=embervale::c4::swapchain::width.load(std::memory_order_acquire);
        r.height=embervale::c4::swapchain::height.load(std::memory_order_acquire);
        r.format=static_cast<VkFormat>(embervale::c4::swapchain::format.load(std::memory_order_acquire));
        if(!(embervale::c4::swapchain::usage.load(std::memory_order_acquire)&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))return false;
        r.count=embervale::c4::swapchain::actualImages.load(std::memory_order_acquire);
        if(r.count<2 || r.count>8)return false;
        for(std::uint32_t i=0;i<r.count;++i){
            auto bits=embervale::c4::swapchain::imageHandles[i].load(std::memory_order_acquire);
            if(!bits)return false;
            std::memcpy(&r.images[i],&bits,sizeof(r.images[i]));
        }
        // Cross-check handles against Vulkan, not merely our asynchronous recorder.
        std::array<VkImage,8> recorded=r.images;
        if(!get_live_images(r) || r.images!=recorded)return false;
        if(generation!=embervale::c4::swapchain::created.load(std::memory_order_acquire))return false;
    } else {
#ifdef EMBERVALE_C4_S15_NATIVE_BOOTSTRAP
        // The Vulkan API does NOT let a late-loaded mod query existing swapchain
        // format/extent. This is an explicitly opt-in, exact-build native 1440p
        // assumption based on the confirmed S11 observations. Never use at
        // another window size, another game hash or an unknown image set.
        if(!get_known_native_client_extent(r.width,r.height))return false;
        r.format=VK_FORMAT_R8G8B8A8_UNORM;
        if(!get_live_images(r))return false;
        r.bootstrapped=true;
#else
        return false;
#endif
    }
    if(r.width<220 || r.height<220 || r.width>16384 || r.height>16384 ||
       r.format!=VK_FORMAT_R8G8B8A8_UNORM)return false;
    const auto count=r.count, width=r.width, height=r.height;
    const auto format=r.format;
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex=family;
    if(r.f.createCommandPool(r.device,&pool,nullptr,&r.pool)!=VK_SUCCESS) {destroy_partial(r);return false;}
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool=r.pool; ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount=count;
    if(r.f.allocateCommandBuffers(r.device,&ai,r.commands.data())!=VK_SUCCESS){destroy_partial(r);return false;}
    VkAttachmentDescription attachment{};
    attachment.format=format;
    attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachment.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount=1; sub.pColorAttachments=&ref;
    VkSubpassDependency dep{};
    dep.srcSubpass=VK_SUBPASS_EXTERNAL;dep.dstSubpass=0;
    dep.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount=1; rp.pAttachments=&attachment;
    rp.subpassCount=1;rp.pSubpasses=&sub;
    rp.dependencyCount=1;rp.pDependencies=&dep;
    if (r.f.createRenderPass(r.device,&rp,nullptr,&r.pass)!=VK_SUCCESS){destroy_partial(r);return false;}
    for(std::uint32_t i=0;i<count;++i){
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=r.images[i];vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;
        vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount=1; vi.subresourceRange.layerCount=1;
        if(r.f.createImageView(r.device,&vi,nullptr,&r.views[i])!=VK_SUCCESS){destroy_partial(r);return false;}
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass=r.pass;fb.attachmentCount=1;fb.pAttachments=&r.views[i];
        fb.width=width;fb.height=height;fb.layers=1;
        if(r.f.createFramebuffer(r.device,&fb,nullptr,&r.buffers[i])!=VK_SUCCESS){destroy_partial(r);return false;}
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if(r.f.createSemaphore(r.device,&si,nullptr,&r.signals[i])!=VK_SUCCESS){destroy_partial(r);return false;}
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags=VK_FENCE_CREATE_SIGNALED_BIT;
        if(r.f.createFence(r.device,&fi,nullptr,&r.fences[i])!=VK_SUCCESS){destroy_partial(r);return false;}
    }
    // Do not create resources if the game's swapchain changed during setup.
    if (generation!=embervale::c4::swapchain::created.load(std::memory_order_acquire)){
        destroy_partial(r); return false;
    }
    if(r.generation!=embervale::c4::swapchain::created.load(std::memory_order_acquire)){
        destroy_partial(r); return false;
    }
    r.ready=true;
    if(r.bootstrapped)bootstrapCount.fetch_add(1,std::memory_order_relaxed);
    out=r;
    return true;
}

inline void report(Logger log) noexcept {
    if(!log) return;
    const auto now=GetTickCount64();
    auto before=lastLog.load(std::memory_order_relaxed);
    if (now-before<5000 || !lastLog.compare_exchange_strong(before,now))return;
    std::uint32_t family=UINT32_MAX, imageCount=0, retiredLocal=0;
    bool bootstrapped=false;
    std::uint64_t generation=0;
    std::uint32_t proofStage=0;
    {
        std::lock_guard<std::mutex> guard(gpuMutex);
        proofStage=runtime.proofStage;
        family=runtime.family;
        imageCount=runtime.count;
        generation=runtime.generation;
        bootstrapped=runtime.bootstrapped;
        retiredLocal=static_cast<std::uint32_t>(retiredCount);
    }
    char buf[670]{};
    std::snprintf(buf,sizeof(buf),
        "C4-S15 status=%s present_attempts=%llu drawn=%llu skipped=%llu no_acquisition=%llu submit_errors=%llu "
        "family=%u generation=%llu image_count=%u queue_mismatch_count=%llu detail=%d "
        "retired=%u rebuilds=%llu reclaimed=%llu proofs=%llu proof_stage=%u deferred_foreign=%llu bootstrap=%llu assume_native=%u",
        phase.load()==3?"gpu_failure":phase.load()==2?"retirement_limit_reached":
        phase.load()==1?"renderer_ready":"waiting_for_verified_swapchain_or_queue",
        static_cast<unsigned long long>(drawCalls.load()),
        static_cast<unsigned long long>(drawSuccess.load()),
        static_cast<unsigned long long>(skipped.load()),
        static_cast<unsigned long long>(noToken.load()),
        static_cast<unsigned long long>(submitErrors.load()),family,
        static_cast<unsigned long long>(generation),imageCount,
        static_cast<unsigned long long>(queueMismatches.load()),lastError.load(),
        retiredLocal,static_cast<unsigned long long>(rebuildCount.load()),
        static_cast<unsigned long long>(reclaimedGenerations.load()),
        static_cast<unsigned long long>(proofCompletions.load()),proofStage,
        static_cast<unsigned long long>(deferredForeignCreates.load()),
        static_cast<unsigned long long>(bootstrapCount.load()),bootstrapped?1u:0u);
    try{log(std::string(buf));}catch(...){}
}

inline std::int32_t on_present(void* rawQueue,const void* rawInfo,
                              embervale::c4::present::QueuePresentFn next) noexcept {
    if(!next)return static_cast<std::int32_t>(VK_ERROR_INITIALIZATION_FAILED);
    if(!armed.load(std::memory_order_acquire) || !rawQueue || !rawInfo ||
       phase.load(std::memory_order_acquire)>=2) return next(rawQueue,rawInfo);
    drawCalls.fetch_add(1,std::memory_order_relaxed);
    VkPresentInfoKHR info{};
    if(!embervale::c4::present::sample_copy(rawInfo,&info,sizeof(info)) ||
        info.sType!=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR || info.swapchainCount!=1 ||
        !info.pImageIndices || !info.pSwapchains ||
        info.waitSemaphoreCount==0 || info.waitSemaphoreCount>8 || !info.pWaitSemaphores){
        skipped.fetch_add(1,std::memory_order_relaxed);return next(rawQueue,rawInfo);
    }
    std::unique_lock<std::mutex> lock(gpuMutex);
    // Never invoke third-party present code while holding our renderer mutex.
    const auto forward = [&](const void* updated) -> std::int32_t {
        lock.unlock();
        return next(rawQueue,updated);
    };
    if(!armed.load(std::memory_order_acquire))return forward(rawInfo);
    const auto queue=reinterpret_cast<VkQueue>(rawQueue);
    VkSwapchainKHR presented=VK_NULL_HANDLE;
    if(!embervale::c4::present::sample_copy(info.pSwapchains,&presented,sizeof(presented)) ||
       presented==VK_NULL_HANDLE) {
        skipped.fetch_add(1,std::memory_order_relaxed);return forward(rawInfo);
    }
    if(runtime.ready) {
        // Complete proof before any re-use/reset of the successor fence.
        reclaim_retired(runtime);
        const int state=presented_chain_state(runtime,presented);
        if(state==1) {
            skipped.fetch_add(1,std::memory_order_relaxed);return forward(rawInfo);
        }
        if(state==2 ||
           runtime.device!=reinterpret_cast<VkDevice>(embervale::c4::nativeframe::observedDevice.load(std::memory_order_acquire)) ||
           runtime.queue!=queue){
            if(!retire_current())return forward(rawInfo);
        }
    }
    if(!runtime.ready){
        if (!build(runtime,queue,info)){
            skipped.fetch_add(1,std::memory_order_relaxed);return forward(rawInfo);
        }
        phase.store(1,std::memory_order_release);
    }
    VkSwapchainKHR chain{}; std::uint32_t idx=UINT32_MAX;
    if(!embervale::c4::present::sample_copy(info.pSwapchains,&chain,sizeof(chain)) ||
       !embervale::c4::present::sample_copy(info.pImageIndices,&idx,sizeof(idx)) ||
       idx>=runtime.count || chain!=runtime.chain){
        skipped.fetch_add(1,std::memory_order_relaxed);return forward(rawInfo);
    }
    // Do not accept a token acquired for a different swapchain with the same
    // image index. S12 now publishes the acquired chain before the token.
    if(embervale::c4::acquire::acquiredChainByImage[idx].load(std::memory_order_acquire)!=
       embervale::c4::swapchain::handle_bits(chain)){
        noToken.fetch_add(1,std::memory_order_relaxed);return forward(rawInfo);
    }
    const auto token=embervale::c4::acquire::acquiredTokens[idx].load(std::memory_order_acquire);
    if(!token || token==runtime.handledTokens[idx]){
        noToken.fetch_add(1,std::memory_order_relaxed);return forward(rawInfo);
    }
    runtime.handledTokens[idx]=token; // Consume for this present, even if drawing is skipped.
    const auto& f=runtime.f;auto device=runtime.device;
    VkFence fence=runtime.fences[idx];
    if(f.waitForFences(device,1,&fence,VK_TRUE,0)!=VK_SUCCESS){
        skipped.fetch_add(1,std::memory_order_relaxed);return forward(rawInfo);
    }
    const auto cmd=runtime.commands[idx];
    bool recorded=false;
    do {
        if(f.resetCommandBuffer(cmd,0)!=VK_SUCCESS)break;
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if(f.beginCommandBuffer(cmd,&bi)!=VK_SUCCESS)break;
        VkRenderPassBeginInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        ri.renderPass=runtime.pass;ri.framebuffer=runtime.buffers[idx];
        ri.renderArea.extent={runtime.width,runtime.height};
        f.cmdBeginRenderPass(cmd,&ri,VK_SUBPASS_CONTENTS_INLINE);
        VkClearAttachment ca{};ca.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;ca.colorAttachment=0;
        ca.clearValue.color.float32[0]=0.85f;
        ca.clearValue.color.float32[1]=0.55f;
        ca.clearValue.color.float32[2]=0.12f;
        ca.clearValue.color.float32[3]=1.f;
        VkClearRect cr{};
        cr.rect.offset={static_cast<std::int32_t>(runtime.width-200),20};
        cr.rect.extent={180,180};cr.baseArrayLayer=0;cr.layerCount=1;
        f.cmdClearAttachments(cmd,1,&ca,1,&cr);
        f.cmdEndRenderPass(cmd);
        if(f.endCommandBuffer(cmd)!=VK_SUCCESS)break;
        recorded=true;
    }while(false);
    if(!recorded){
        phase.store(3,std::memory_order_release);
        skipped.fetch_add(1,std::memory_order_relaxed);return forward(rawInfo);
    }
    std::array<VkPipelineStageFlags,8> stages{};
    for(std::uint32_t i=0;i<info.waitSemaphoreCount;++i)
        stages[i]=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphore signal=runtime.signals[idx];
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount=info.waitSemaphoreCount;
    submit.pWaitSemaphores=info.pWaitSemaphores;
    submit.pWaitDstStageMask=stages.data();
    submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;
    submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&signal;
    if(f.resetFences(device,1,&fence)!=VK_SUCCESS){
        phase.store(3,std::memory_order_release);
        skipped.fetch_add(1,std::memory_order_relaxed);return forward(rawInfo);
    }
    const auto result=f.queueSubmit(queue,1,&submit,fence);
    if(result!=VK_SUCCESS){
        phase.store(3,std::memory_order_release);
        lastError.store(static_cast<int>(result),std::memory_order_release);
        submitErrors.fetch_add(1,std::memory_order_relaxed);
        // Submission might have consumed the game's wait semaphores. Never reuse
        // them by forwarding the original present after vkQueueSubmit failure.
        return static_cast<std::int32_t>(result);
    }
    VkPresentInfoKHR adjusted=info;
    adjusted.waitSemaphoreCount=1;
    adjusted.pWaitSemaphores=&signal;
    drawSuccess.fetch_add(1,std::memory_order_relaxed);
    // Publish the presentation proof ONLY after vkQueuePresentKHR has returned
    // success. The forwarding helper unlocks gpuMutex around third-party code.
    const auto presentResult=forward(&adjusted);
    if(presentResult==VK_SUCCESS || presentResult==VK_SUBOPTIMAL_KHR) {
        std::lock_guard<std::mutex> proofLock(gpuMutex);
        if(runtime.ready && runtime.chain==chain && runtime.queue==queue &&
           runtime.handledTokens[idx]==token) {
            if(runtime.proofStage==0) {
                runtime.proofStage=1;
                runtime.proofImage=idx;
                runtime.firstProofToken=token;
            } else if(runtime.proofStage==1 && runtime.proofImage==idx &&
                      token!=runtime.firstProofToken) {
                runtime.proofStage=2;
            }
        }
    }
    return presentResult;
}
inline void maybe_enable(Logger log) noexcept {
    if(!embervale::c4::present::verified_executable() ||
       !embervale::c4::present::installed.load(std::memory_order_acquire) ||
       !embervale::c4::nativeframe::installed.load(std::memory_order_acquire) ||
       !embervale::c4::swapchain::installed.load(std::memory_order_acquire) ||
       !embervale::c4::acquire::installed.load(std::memory_order_acquire))return;
    if(!armed.exchange(true,std::memory_order_acq_rel)){
        embervale::c4::present::renderDispatch.store(&on_present,std::memory_order_release);
        if(log) try {log("C4-S17 status=armed mode=orange_square_180px retirement=successor_reacquire_proof cap=6");}catch(...){}
    }
}
inline void stop(Logger log) noexcept {
    armed.store(false,std::memory_order_release);
    embervale::c4::present::renderDispatch.store(nullptr,std::memory_order_release);
    if(log) try {log("C4-S17 status=stopped resources_may_be_retained_on_unproven_present=1 hot_reload_unsupported=1");}catch(...){}
    // Runtime resources are intentionally retained in this smoke test. Unloading
    // is unsupported; S9/S13 pin callback code until process termination.
}
}}} // namespace embervale::c4::square

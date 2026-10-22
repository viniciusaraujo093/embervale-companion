#include <windows.h>

#include <vulkan/vulkan.h>

#include <vulkan/vk_layer.h>

#include <atomic>

#include <cstdio>

#include <cstring>

#include <fstream>

#include <mutex>

#include <string>

#include <unordered_map>

#include <vector>

#include <algorithm>
#include "c4_map_observer.h"

namespace {

constexpr const char* kLayerName = "VK_LAYER_EMBERVALE_companion";

std::mutex mutex;

std::unordered_map<void*, PFN_vkGetInstanceProcAddr> instance_next;

std::unordered_map<void*, VkInstance> instance_handles;

std::unordered_map<void*, PFN_vkGetDeviceProcAddr> device_next;

std::unordered_map<VkPhysicalDevice, VkInstance> physical_parents;

struct QueueDispatch { PFN_vkGetDeviceProcAddr next; VkDevice device; uint32_t family; uint32_t index; };

std::unordered_map<VkQueue, QueueDispatch> queue_next;

// B2: command resources are allocated, but never recorded or submitted.

struct DeviceObservation {

    std::vector<VkQueueFlags> family_flags;

    bool graphics_present_queue_seen = false;

};

struct SwapchainObservation {

    VkDevice device = VK_NULL_HANDLE;

    VkFormat format = VK_FORMAT_UNDEFINED;

    VkExtent2D extent{};

    std::vector<VkImage> images;

    VkCommandPool command_pool = VK_NULL_HANDLE;

    std::vector<VkCommandBuffer> command_buffers;

    uint32_t command_family = UINT32_MAX;

    bool b2_attempted = false;

    bool c2_attempted = false;

    bool c3_disabled = false;

    VkRenderPass c2_render_pass = VK_NULL_HANDLE;

    std::vector<VkImageView> c2_views;

    std::vector<VkFramebuffer> c2_framebuffers;

    std::vector<VkSemaphore> c2_semaphores;

    std::vector<VkFence> c2_fences;

    std::vector<bool> c2_submitted;

    // C3.2: each image must be acquired again before its overlay semaphore is reused.
    std::vector<bool> c32_acquired;

};

std::unordered_map<void*, DeviceObservation> observed_devices;

std::unordered_map<VkSwapchainKHR, SwapchainObservation> observed_swapchains;

std::atomic<uint64_t> present_count{0};

std::atomic<uint64_t> swapchain_count{0};

void log_event(const char* event) noexcept {

    try {

        wchar_t module_path[MAX_PATH] = {};

        HMODULE module = nullptr;

        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |

                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,

                                 reinterpret_cast<LPCWSTR>(&log_event), &module)) return;

        if (!GetModuleFileNameW(module, module_path, MAX_PATH)) return;

        std::wstring path(module_path);

        const auto slash = path.find_last_of(L"\\/");

        if (slash == std::wstring::npos) return;

        path.resize(slash + 1);

        path += L"embervale-vulkan-layer.log";

        std::lock_guard<std::mutex> lock(mutex);

        std::ofstream out(path, std::ios::app);

        if (out) out << "[Embervale Layer v0.7] " << event << '\n';

    } catch (...) {}

}

void* dispatch_key(const void* handle) {

    return handle ? *reinterpret_cast<void* const*>(handle) : nullptr;

}

// B2 helpers: called with the bookkeeping mutex held. No GPU work is submitted.

// C2: deliberately opt-in. Set EMBERVALE_C2_SQUARE=1 in the test launcher.

bool c2_enabled() {

    char value[8] = {};

    return GetEnvironmentVariableA("EMBERVALE_C2_SQUARE", value, sizeof(value)) > 0 &&

           value[0] == '1';

}

template <typename T>

T c2_proc(PFN_vkGetDeviceProcAddr next, VkDevice device, const char* name) {

    return reinterpret_cast<T>(next(device, name));

}

void release_c2(VkDevice device, PFN_vkGetDeviceProcAddr next,

                SwapchainObservation& sc) {

    if (!next) return;

    auto destroy_fence = c2_proc<PFN_vkDestroyFence>(next, device, "vkDestroyFence");

    auto destroy_semaphore = c2_proc<PFN_vkDestroySemaphore>(next, device, "vkDestroySemaphore");

    auto destroy_framebuffer = c2_proc<PFN_vkDestroyFramebuffer>(next, device, "vkDestroyFramebuffer");

    auto destroy_view = c2_proc<PFN_vkDestroyImageView>(next, device, "vkDestroyImageView");

    auto destroy_renderpass = c2_proc<PFN_vkDestroyRenderPass>(next, device, "vkDestroyRenderPass");

    for (auto fence : sc.c2_fences) if (fence && destroy_fence) destroy_fence(device, fence, nullptr);

    for (auto semaphore : sc.c2_semaphores) if (semaphore && destroy_semaphore) destroy_semaphore(device, semaphore, nullptr);

    for (auto framebuffer : sc.c2_framebuffers) if (framebuffer && destroy_framebuffer) destroy_framebuffer(device, framebuffer, nullptr);

    for (auto view : sc.c2_views) if (view && destroy_view) destroy_view(device, view, nullptr);

    if (sc.c2_render_pass && destroy_renderpass) destroy_renderpass(device, sc.c2_render_pass, nullptr);

    sc.c2_fences.clear();

    sc.c2_semaphores.clear();

    sc.c2_framebuffers.clear();

    sc.c2_views.clear();

    sc.c2_submitted.clear();

    sc.c32_acquired.clear();

    sc.c2_render_pass = VK_NULL_HANDLE;

}

bool prepare_c2(VkDevice device, PFN_vkGetDeviceProcAddr next, SwapchainObservation& sc) {

    if (sc.c2_attempted) return sc.c2_render_pass != VK_NULL_HANDLE;

    sc.c2_attempted = true;

    if (!next || sc.images.empty() || sc.command_buffers.size() != sc.images.size() ||

        sc.extent.width < 100 || sc.extent.height < 100) return false;

    auto create_rp = c2_proc<PFN_vkCreateRenderPass>(next, device, "vkCreateRenderPass");

    auto create_view = c2_proc<PFN_vkCreateImageView>(next, device, "vkCreateImageView");

    auto create_fb = c2_proc<PFN_vkCreateFramebuffer>(next, device, "vkCreateFramebuffer");

    auto create_sem = c2_proc<PFN_vkCreateSemaphore>(next, device, "vkCreateSemaphore");

    auto create_fence = c2_proc<PFN_vkCreateFence>(next, device, "vkCreateFence");

    if (!create_rp || !create_view || !create_fb || !create_sem || !create_fence) return false;

    VkAttachmentDescription attachment{};

    attachment.format = sc.format;

    attachment.samples = VK_SAMPLE_COUNT_1_BIT;

    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;

    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    attachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};

    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

    subpass.colorAttachmentCount = 1;

    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dependency{};

    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;

    dependency.dstSubpass = 0;

    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp{};

    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

    rp.attachmentCount = 1;

    rp.pAttachments = &attachment;

    rp.subpassCount = 1;

    rp.pSubpasses = &subpass;

    rp.dependencyCount = 1;

    rp.pDependencies = &dependency;

    if (create_rp(device, &rp, nullptr, &sc.c2_render_pass) != VK_SUCCESS) return false;

    for (VkImage image : sc.images) {

        VkImageViewCreateInfo vi{};

        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;

        vi.image = image;

        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;

        vi.format = sc.format;

        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        vi.subresourceRange.levelCount = 1;

        vi.subresourceRange.layerCount = 1;

        VkImageView view = VK_NULL_HANDLE;

        if (create_view(device, &vi, nullptr, &view) != VK_SUCCESS) {

            release_c2(device, next, sc); return false;

        }

        sc.c2_views.push_back(view);

        VkFramebufferCreateInfo fi{};

        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;

        fi.renderPass = sc.c2_render_pass;

        fi.attachmentCount = 1;

        fi.pAttachments = &sc.c2_views.back();

        fi.width = sc.extent.width;

        fi.height = sc.extent.height;

        fi.layers = 1;

        VkFramebuffer fb = VK_NULL_HANDLE;

        if (create_fb(device, &fi, nullptr, &fb) != VK_SUCCESS) {

            release_c2(device, next, sc); return false;

        }

        sc.c2_framebuffers.push_back(fb);

        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

        VkSemaphore sem = VK_NULL_HANDLE;

        if (create_sem(device, &si, nullptr, &sem) != VK_SUCCESS) {

            release_c2(device, next, sc); return false;

        }

        sc.c2_semaphores.push_back(sem);

        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};

        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VkFence fence = VK_NULL_HANDLE;

        if (create_fence(device, &fence_info, nullptr, &fence) != VK_SUCCESS) {

            release_c2(device, next, sc); return false;

        }

        sc.c2_fences.push_back(fence);

        sc.c2_submitted.push_back(false);

    }

    return true;

}

bool c2_draw(VkQueue queue, VkDevice device, PFN_vkGetDeviceProcAddr next,

             SwapchainObservation& sc, uint32_t image_index,

             const VkPresentInfoKHR* present, VkSemaphore& output, VkResult& submit_failure) {

    if (image_index >= sc.command_buffers.size() ||
        image_index >= sc.c32_acquired.size() || !sc.c32_acquired[image_index] ||
        present->waitSemaphoreCount == 0 ||

        !present->pWaitSemaphores || !sc.c2_render_pass) return false;

    auto wait_fences = c2_proc<PFN_vkWaitForFences>(next, device, "vkWaitForFences");

    auto reset_fences = c2_proc<PFN_vkResetFences>(next, device, "vkResetFences");

    auto reset_cmd = c2_proc<PFN_vkResetCommandBuffer>(next, device, "vkResetCommandBuffer");

    auto begin_cmd = c2_proc<PFN_vkBeginCommandBuffer>(next, device, "vkBeginCommandBuffer");

    auto end_cmd = c2_proc<PFN_vkEndCommandBuffer>(next, device, "vkEndCommandBuffer");

    auto begin_rp = c2_proc<PFN_vkCmdBeginRenderPass>(next, device, "vkCmdBeginRenderPass");

    auto clear = c2_proc<PFN_vkCmdClearAttachments>(next, device, "vkCmdClearAttachments");

    auto end_rp = c2_proc<PFN_vkCmdEndRenderPass>(next, device, "vkCmdEndRenderPass");

    auto submit = c2_proc<PFN_vkQueueSubmit>(next, device, "vkQueueSubmit");

    if (!wait_fences || !reset_fences || !reset_cmd || !begin_cmd || !end_cmd ||

        !begin_rp || !clear || !end_rp || !submit) return false;

    VkFence fence = sc.c2_fences[image_index];

    // Fence completion guards command-buffer reuse; successful reacquisition
    // of the same image guards reuse of its present-wait semaphore.

    if (wait_fences(device, 1, &fence, VK_TRUE, 100000000ULL) != VK_SUCCESS) return false;

    VkCommandBuffer cmd = sc.command_buffers[image_index];

    if (reset_cmd(cmd, 0) != VK_SUCCESS) return false;

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (begin_cmd(cmd, &bi) != VK_SUCCESS) return false;

    VkRenderPassBeginInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};

    ri.renderPass = sc.c2_render_pass;

    ri.framebuffer = sc.c2_framebuffers[image_index];

    ri.renderArea.extent = sc.extent;

    begin_rp(cmd, &ri, VK_SUBPASS_CONTENTS_INLINE);

    VkClearAttachment ca{};

    ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    ca.colorAttachment = 0;

    ca.clearValue.color.float32[0] = 0.85f;

    ca.clearValue.color.float32[1] = 0.55f;

    ca.clearValue.color.float32[2] = 0.12f;

    ca.clearValue.color.float32[3] = 1.0f;

    VkClearRect rect{};

    rect.rect.offset = {static_cast<int32_t>(sc.extent.width - 200), 20};

    rect.rect.extent = {180, 180};

    rect.baseArrayLayer = 0;

    rect.layerCount = 1;

    clear(cmd, 1, &ca, 1, &rect);

    end_rp(cmd);

    if (end_cmd(cmd) != VK_SUCCESS) return false;

    std::vector<VkPipelineStageFlags> stages(

        present->waitSemaphoreCount, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkSemaphore signal = sc.c2_semaphores[image_index];

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};

    si.waitSemaphoreCount = present->waitSemaphoreCount;

    si.pWaitSemaphores = present->pWaitSemaphores;

    si.pWaitDstStageMask = stages.data();

    si.commandBufferCount = 1;

    si.pCommandBuffers = &cmd;

    si.signalSemaphoreCount = 1;

    si.pSignalSemaphores = &signal;

    if (reset_fences(device, 1, &fence) != VK_SUCCESS) return false;

    // A failed submit after resetting the fence must not be retried with this fence.

    const VkResult submit_result = submit(queue, 1, &si, fence);

    if (submit_result != VK_SUCCESS) {
        // Do not forward a present using the game's original semaphores:
        // after a failed submit their state cannot be assumed unchanged.
        sc.c3_disabled = true;
        submit_failure = submit_result;
        return false;
    }

    sc.c2_submitted[image_index] = true;

    // Consume the acquisition token only after a successful overlay submission.
    sc.c32_acquired[image_index] = false;

    output = signal;

    return true;

}

void release_b2(VkDevice device, PFN_vkGetDeviceProcAddr gdpa,

                SwapchainObservation& observation) {

    if (!gdpa) return;

    release_c2(device, gdpa, observation);

    if (!observation.command_pool) return;

    auto destroy_pool = reinterpret_cast<PFN_vkDestroyCommandPool>(

        gdpa(device, "vkDestroyCommandPool"));

    if (destroy_pool) destroy_pool(device, observation.command_pool, nullptr);

    observation.command_pool = VK_NULL_HANDLE;

    observation.command_buffers.clear();

}

void prepare_b2(VkDevice device, PFN_vkGetDeviceProcAddr gdpa,

                SwapchainObservation& observation, uint32_t family) {

    if (observation.b2_attempted || observation.images.empty() || !gdpa) return;

    observation.b2_attempted = true;

    auto create_pool = reinterpret_cast<PFN_vkCreateCommandPool>(

        gdpa(device, "vkCreateCommandPool"));

    auto allocate_buffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(

        gdpa(device, "vkAllocateCommandBuffers"));

    auto destroy_pool = reinterpret_cast<PFN_vkDestroyCommandPool>(

        gdpa(device, "vkDestroyCommandPool"));

    if (!create_pool || !allocate_buffers || !destroy_pool) {

        return;

    }

    VkCommandPoolCreateInfo pool_info{};

    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

    pool_info.queueFamilyIndex = family;

    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkCommandPool pool = VK_NULL_HANDLE;

    VkResult result = create_pool(device, &pool_info, nullptr, &pool);

    if (result != VK_SUCCESS || !pool) {

        return;

    }

    std::vector<VkCommandBuffer> buffers(observation.images.size(), VK_NULL_HANDLE);

    VkCommandBufferAllocateInfo alloc{};

    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

    alloc.commandPool = pool;

    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    alloc.commandBufferCount = static_cast<uint32_t>(buffers.size());

    result = allocate_buffers(device, &alloc, buffers.data());

    if (result != VK_SUCCESS) {

        destroy_pool(device, pool, nullptr);

        return;

    }

    observation.command_pool = pool;

    observation.command_buffers = std::move(buffers);

    observation.command_family = family;

}

VkLayerInstanceCreateInfo* instance_link(const VkInstanceCreateInfo* info) {

    auto* node = reinterpret_cast<const VkBaseInStructure*>(info->pNext);

    while (node) {

        if (node->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO) {

            auto* link = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<VkBaseInStructure*>(node));

            if (link->function == VK_LAYER_LINK_INFO) return link;

        }

        node = node->pNext;

    }

    return nullptr;

}

VkLayerDeviceCreateInfo* device_link(const VkDeviceCreateInfo* info) {

    auto* node = reinterpret_cast<const VkBaseInStructure*>(info->pNext);

    while (node) {

        if (node->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) {

            auto* link = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<VkBaseInStructure*>(node));

            if (link->function == VK_LAYER_LINK_INFO) return link;

        }

        node = node->pNext;

    }

    return nullptr;

}

} // namespace

extern "C" {

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(

    const VkInstanceCreateInfo* info, const VkAllocationCallbacks* allocator,

    VkInstance* instance) {

    log_event("vkCreateInstance intercepted");

    if (!info || !instance) return VK_ERROR_INITIALIZATION_FAILED;

    auto* chain = instance_link(info);

    if (!chain || !chain->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;

    auto next_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;

    if (!next_gipa) return VK_ERROR_INITIALIZATION_FAILED;

    auto next_create = reinterpret_cast<PFN_vkCreateInstance>(

        next_gipa(VK_NULL_HANDLE, "vkCreateInstance"));

    if (!next_create) return VK_ERROR_INITIALIZATION_FAILED;

    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;

    VkResult result = next_create(info, allocator, instance);

    if (result == VK_SUCCESS) {

        {

            std::lock_guard<std::mutex> lock(mutex);

            instance_next[dispatch_key(*instance)] = next_gipa;

            instance_handles[dispatch_key(*instance)] = *instance;

        }

        log_event("vkCreateInstance forwarded successfully");

    } else {

        log_event("vkCreateInstance forwarded with failure");

    }

    return result;

}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(

    VkInstance instance, uint32_t* count, VkPhysicalDevice* devices) {

    PFN_vkGetInstanceProcAddr next = nullptr;

    {

        std::lock_guard<std::mutex> lock(mutex);

        auto it = instance_next.find(dispatch_key(instance));

        if (it != instance_next.end()) next = it->second;

    }

    if (!next) return VK_ERROR_INITIALIZATION_FAILED;

    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(

        next(instance, "vkEnumeratePhysicalDevices"));

    if (!enumerate) return VK_ERROR_INITIALIZATION_FAILED;

    VkResult result = enumerate(instance, count, devices);

    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && devices && count) {

        std::lock_guard<std::mutex> lock(mutex);

        for (uint32_t i = 0; i < *count; ++i)

            physical_parents[devices[i]] = instance;

    }

    return result;

}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroups(

    VkInstance instance, uint32_t* count, VkPhysicalDeviceGroupProperties* groups) {

    PFN_vkGetInstanceProcAddr next = nullptr;

    {

        std::lock_guard<std::mutex> lock(mutex);

        auto it = instance_next.find(dispatch_key(instance));

        if (it != instance_next.end()) next = it->second;

    }

    if (!next) return VK_ERROR_INITIALIZATION_FAILED;

    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDeviceGroups>(

        next(instance, "vkEnumeratePhysicalDeviceGroups"));

    if (!enumerate) return VK_ERROR_EXTENSION_NOT_PRESENT;

    VkResult result = enumerate(instance, count, groups);

    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && groups && count) {

        std::lock_guard<std::mutex> lock(mutex);

        for (uint32_t i = 0; i < *count; ++i)

            for (uint32_t j = 0; j < groups[i].physicalDeviceCount; ++j)

                physical_parents[groups[i].physicalDevices[j]] = instance;

    }

    return result;

}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance,

                                               const VkAllocationCallbacks* allocator) {

    PFN_vkGetInstanceProcAddr next = nullptr;

    {

        std::lock_guard<std::mutex> lock(mutex);

        auto it = instance_next.find(dispatch_key(instance));

        if (it != instance_next.end()) { next = it->second; instance_next.erase(it); }

        instance_handles.erase(dispatch_key(instance));

        for (auto it = physical_parents.begin(); it != physical_parents.end();) {

            if (it->second == instance) it = physical_parents.erase(it);

            else ++it;

        }

    }

    if (next) {

        auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(next(instance, "vkDestroyInstance"));

        if (destroy) destroy(instance, allocator);

    }

}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(

    VkPhysicalDevice physical, const VkDeviceCreateInfo* info,

    const VkAllocationCallbacks* allocator, VkDevice* device) {

    log_event("vkCreateDevice intercepted");

    if (!info || !device) return VK_ERROR_INITIALIZATION_FAILED;

    auto* chain = device_link(info);

    if (!chain || !chain->u.pLayerInfo) return VK_ERROR_INITIALIZATION_FAILED;

    auto next_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;

    auto next_gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;

    if (!next_gipa || !next_gdpa) return VK_ERROR_INITIALIZATION_FAILED;

    VkInstance parent_instance = VK_NULL_HANDLE;

    {

        std::lock_guard<std::mutex> lock(mutex);

        auto it = physical_parents.find(physical);

        if (it != physical_parents.end()) parent_instance = it->second;

    }

    if (!parent_instance) {

        log_event("vkCreateDevice: physical device parent not registered");

        return VK_ERROR_INITIALIZATION_FAILED;

    }

    auto next_create = reinterpret_cast<PFN_vkCreateDevice>(

        next_gipa(parent_instance, "vkCreateDevice"));

    if (!next_create) return VK_ERROR_INITIALIZATION_FAILED;

    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;

    VkResult result = next_create(physical, info, allocator, device);

    if (result == VK_SUCCESS) {

        // Query capabilities only. The physical device and parent instance are known here.

        std::vector<VkQueueFlags> flags;

        auto get_families = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(

            next_gipa(parent_instance, "vkGetPhysicalDeviceQueueFamilyProperties"));

        if (get_families) {

            uint32_t family_count = 0;

            get_families(physical, &family_count, nullptr);

            if (family_count > 0 && family_count < 1024) {

                std::vector<VkQueueFamilyProperties> properties(family_count);

                get_families(physical, &family_count, properties.data());

                flags.reserve(family_count);

                for (uint32_t i = 0; i < family_count; ++i)

                    flags.push_back(properties[i].queueFlags);

            }

        }

        {

            std::lock_guard<std::mutex> lock(mutex);

            device_next[dispatch_key(*device)] = next_gdpa;

            observed_devices[dispatch_key(*device)].family_flags = flags;

        }

        char message[128];

        std::snprintf(message, sizeof(message), "B1 device queue families discovered: %u",

                      static_cast<unsigned>(flags.size()));

        log_event(message);

    }

    log_event(result == VK_SUCCESS ? "vkCreateDevice forwarded successfully" : "vkCreateDevice forwarded with failure");

    return result;

}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(

    VkDevice device, uint32_t family, uint32_t index, VkQueue* queue) {

    PFN_vkGetDeviceProcAddr next = nullptr;

    {

        std::lock_guard<std::mutex> lock(mutex);

        auto it = device_next.find(dispatch_key(device));

        if (it != device_next.end()) next = it->second;

    }

    if (!next) return;

    auto fn = reinterpret_cast<PFN_vkGetDeviceQueue>(next(device, "vkGetDeviceQueue"));

    if (!fn) return;

    fn(device, family, index, queue);

    if (queue && *queue) {

        std::lock_guard<std::mutex> lock(mutex);

        queue_next[*queue] = {next, device, family, index};

    }

    char message[128];

    std::snprintf(message, sizeof(message), "vkGetDeviceQueue observed (family=%u, index=%u)", family, index);

    log_event(message);

}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(

    VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* queue) {

    PFN_vkGetDeviceProcAddr next = nullptr;

    {

        std::lock_guard<std::mutex> lock(mutex);

        auto it = device_next.find(dispatch_key(device));

        if (it != device_next.end()) next = it->second;

    }

    if (!next) return;

    auto fn = reinterpret_cast<PFN_vkGetDeviceQueue2>(next(device, "vkGetDeviceQueue2"));

    if (!fn) return;

    fn(device, info, queue);

    if (queue && *queue) {

        std::lock_guard<std::mutex> lock(mutex);

        queue_next[*queue] = {next, device, info ? info->queueFamilyIndex : UINT32_MAX, info ? info->queueIndex : UINT32_MAX};

    }

    char message[128];

    std::snprintf(message, sizeof(message), "vkGetDeviceQueue2 observed (family=%u, index=%u)",

                  info ? info->queueFamilyIndex : UINT32_MAX, info ? info->queueIndex : UINT32_MAX);

    log_event(message);

}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(

    VkDevice device, const VkSwapchainCreateInfoKHR* info,

    const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchain) {

    PFN_vkGetDeviceProcAddr next = nullptr;

    {

        std::lock_guard<std::mutex> lock(mutex);

        auto it = device_next.find(dispatch_key(device));

        if (it != device_next.end()) next = it->second;

    }

    if (!next) return VK_ERROR_INITIALIZATION_FAILED;

    auto fn = reinterpret_cast<PFN_vkCreateSwapchainKHR>(next(device, "vkCreateSwapchainKHR"));

    if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;

    VkResult result = fn(device, info, allocator, swapchain);

    if (result == VK_SUCCESS) {

        if (swapchain && *swapchain) {

            std::lock_guard<std::mutex> lock(mutex);

            SwapchainObservation observation;

            observation.device = device;

            if (info) { observation.format = info->imageFormat; observation.extent = info->imageExtent; }

            observed_swapchains[*swapchain] = std::move(observation);

        }

        const auto count = ++swapchain_count;

        char message[256];

        std::snprintf(message, sizeof(message),

            "vkCreateSwapchainKHR observed #%llu (%ux%u, minImages=%u, format=%d, presentMode=%d, imageUsage=0x%08X, sharingMode=%d)",

            static_cast<unsigned long long>(count),

            info ? info->imageExtent.width : 0,

            info ? info->imageExtent.height : 0,

            info ? info->minImageCount : 0,

            info ? static_cast<int>(info->imageFormat) : -1,

            info ? static_cast<int>(info->presentMode) : -1,

            info ? static_cast<unsigned int>(info->imageUsage) : 0u,

            info ? static_cast<int>(info->imageSharingMode) : -1);

        log_event(message);

    } else {

        log_event("vkCreateSwapchainKHR forwarded with failure");

    }

    return result;

}

// Passive observation only: forward the query unchanged and record returned images.

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(

    VkDevice device, VkSwapchainKHR swapchain, uint32_t* count, VkImage* images) {

    PFN_vkGetDeviceProcAddr next = nullptr;

    {

        std::lock_guard<std::mutex> lock(mutex);

        auto it = device_next.find(dispatch_key(device));

        if (it != device_next.end()) next = it->second;

    }

    if (!next) return VK_ERROR_INITIALIZATION_FAILED;

    auto fn = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(

        next(device, "vkGetSwapchainImagesKHR"));

    if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;

    const VkResult result = fn(device, swapchain, count, images);

    if (count && images && result == VK_SUCCESS) {

        std::lock_guard<std::mutex> lock(mutex);

        auto it = observed_swapchains.find(swapchain);

        if (it != observed_swapchains.end() && it->second.device == device)

            {
                it->second.images.assign(images, images + *count);
                it->second.c32_acquired.resize(*count, false);
            }

    }

    if (count && (result == VK_SUCCESS || result == VK_INCOMPLETE)) {

        char message[256];

        std::snprintf(message, sizeof(message),

            "vkGetSwapchainImagesKHR observed (count=%u, populated=%u, result=%d, firstImage=%p)",

            *count, images ? 1u : 0u, static_cast<int>(result),

            images && *count ? reinterpret_cast<void*>(images[0]) : nullptr);

        log_event(message);

    }

    return result;

}

// Resolve presentation through the device dispatch table. The queue handle seen by

// the layer may differ from the one returned by vkGetDeviceQueue (loader/layer

// wrapping), so a raw queue lookup alone is not sufficient.

// C1: observe image acquisition without changing synchronization or presentation.

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(

    VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,

    VkSemaphore semaphore, VkFence fence, uint32_t* image_index) {

    PFN_vkGetDeviceProcAddr next = nullptr;

    {

        std::lock_guard<std::mutex> lock(mutex);

        const auto it = device_next.find(dispatch_key(device));

        if (it != device_next.end()) next = it->second;

    }

    if (!next) return VK_ERROR_INITIALIZATION_FAILED;

    const auto fn = reinterpret_cast<PFN_vkAcquireNextImageKHR>(

        next(device, "vkAcquireNextImageKHR"));

    if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;

    const VkResult result = fn(device, swapchain, timeout, semaphore, fence, image_index);

    if ((result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) && image_index) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = observed_swapchains.find(swapchain);
        if (it != observed_swapchains.end() && *image_index < it->second.c32_acquired.size())
            it->second.c32_acquired[*image_index] = true;
    }

    static std::atomic<uint64_t> acquired{0};

    if ((result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) && image_index) {

        const auto n = ++acquired;

        if (n == 1 || n % 300 == 0) {

            char message[192];

            std::snprintf(message, sizeof(message),

                "C1 acquire: calls=%llu, image=%u, semaphore=%u, fence=%u, result=%d",

                static_cast<unsigned long long>(n), *image_index,

                semaphore != VK_NULL_HANDLE ? 1u : 0u,

                fence != VK_NULL_HANDLE ? 1u : 0u, static_cast<int>(result));

            log_event(message);

        }

    }

    return result;

}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(

    VkDevice device, const VkAcquireNextImageInfoKHR* info, uint32_t* image_index) {

    PFN_vkGetDeviceProcAddr next = nullptr;

    {

        std::lock_guard<std::mutex> lock(mutex);

        const auto it = device_next.find(dispatch_key(device));

        if (it != device_next.end()) next = it->second;

    }

    if (!next) return VK_ERROR_INITIALIZATION_FAILED;

    const auto fn = reinterpret_cast<PFN_vkAcquireNextImage2KHR>(

        next(device, "vkAcquireNextImage2KHR"));

    if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;

    const VkResult result = fn(device, info, image_index);

    if ((result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) && info && image_index) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = observed_swapchains.find(info->swapchain);
        if (it != observed_swapchains.end() && *image_index < it->second.c32_acquired.size())
            it->second.c32_acquired[*image_index] = true;
    }

    static std::atomic<uint64_t> acquired{0};

    if ((result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) && image_index) {

        const auto n = ++acquired;

        if (n == 1 || n % 300 == 0) {

            char message[192];

            std::snprintf(message, sizeof(message),

                "C1 acquire2: calls=%llu, image=%u, semaphore=%u, fence=%u, result=%d",

                static_cast<unsigned long long>(n), *image_index,

                info && info->semaphore != VK_NULL_HANDLE ? 1u : 0u,

                info && info->fence != VK_NULL_HANDLE ? 1u : 0u, static_cast<int>(result));

            log_event(message);

        }

    }

    return result;

}

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(

    VkQueue queue, const VkPresentInfoKHR* info) {

    PFN_vkGetDeviceProcAddr next = nullptr;

    VkDevice device = VK_NULL_HANDLE;

    bool used_dispatch_fallback = false;

    uint32_t queue_family = UINT32_MAX;

    {

        std::lock_guard<std::mutex> lock(mutex);

        auto q = queue_next.find(queue);

        if (q != queue_next.end()) {

            next = q->second.next;

            device = q->second.device;

            queue_family = q->second.family;

        } else {

            // VkQueue and its parent VkDevice share the device dispatch table.

            // This avoids relying on equality of wrapped VkQueue handles.

            auto key = dispatch_key(queue);

            auto d = device_next.find(key);

            if (d != device_next.end()) {

                next = d->second;

                for (const auto& entry : queue_next) {

                    if (dispatch_key(entry.second.device) == key) {

                        device = entry.second.device;

                        break;

                    }

                }

                used_dispatch_fallback = true;

            }

        }

    }

    static std::atomic<bool> logged_fallback{false};

    static std::atomic<bool> logged_missing{false};

    if (used_dispatch_fallback && !logged_fallback.exchange(true))

        log_event("vkQueuePresentKHR: using device dispatch fallback");

    if (!next || !device) {

        if (!logged_missing.exchange(true))

            log_event("vkQueuePresentKHR: no device dispatch available");

        return VK_ERROR_INITIALIZATION_FAILED;

    }

    auto fn = reinterpret_cast<PFN_vkQueuePresentKHR>(next(device, "vkQueuePresentKHR"));

    if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;

    // Experimental C2: single-swapchain, opt-in render pass before presentation.

    VkSemaphore c2_signal = VK_NULL_HANDLE;

    bool c2_drawn = false;

    VkResult c3_submit_failure = VK_SUCCESS;

    static std::atomic<bool> c2_logged{false};

    if (c2_enabled() && info && info->swapchainCount == 1 && info->pSwapchains &&

        info->pImageIndices && queue_family != UINT32_MAX) {

        std::lock_guard<std::mutex> lock(mutex);

        const auto it = observed_swapchains.find(info->pSwapchains[0]);

        if (it != observed_swapchains.end() && it->second.device == device &&

            it->second.command_family == queue_family &&

            it->second.command_buffers.size() == it->second.images.size()) {

            auto& sc = it->second;

            if (!sc.c3_disabled && prepare_c2(device, next, sc)) {

                c2_drawn = c2_draw(queue, device, next, sc,

                                   info->pImageIndices[0], info, c2_signal, c3_submit_failure);

            }

        }

    }

    if (c3_submit_failure != VK_SUCCESS) {
        char message[160];
        std::snprintf(message, sizeof(message),
                      "C3.1 overlay disabled: vkQueueSubmit failed (%d); presentation not forwarded",
                      static_cast<int>(c3_submit_failure));
        log_event(message);
        return c3_submit_failure;
    }

    VkPresentInfoKHR forwarded{};

    if (info) forwarded = *info;

    if (c2_drawn) {

        forwarded.waitSemaphoreCount = 1;

        forwarded.pWaitSemaphores = &c2_signal;

    }

    if (c2_drawn && !c2_logged.exchange(true))

        log_event("C2 square: first GPU submission recorded (180x180, top-right)");

    VkResult result = fn(queue, c2_drawn ? &forwarded : info);

    // B2: create command resources after the first successful presentation.

    // No command buffer recording, submission, semaphore or fence manipulation.

    if (result == VK_SUCCESS && queue_family != UINT32_MAX && info && info->pSwapchains) {

        uint32_t created_buffers = 0;

        bool attempted = false;

        {

            std::lock_guard<std::mutex> lock(mutex);

            auto dev_it = observed_devices.find(dispatch_key(device));

            const bool graphics = dev_it != observed_devices.end() &&

                queue_family < dev_it->second.family_flags.size() &&

                (dev_it->second.family_flags[queue_family] & VK_QUEUE_GRAPHICS_BIT) != 0;

            if (graphics) {

                for (uint32_t i = 0; i < info->swapchainCount; ++i) {

                    auto sc = observed_swapchains.find(info->pSwapchains[i]);

                    if (sc == observed_swapchains.end() || sc->second.device != device ||

                        sc->second.b2_attempted || sc->second.images.empty()) continue;

                    prepare_b2(device, next, sc->second, queue_family);

                    created_buffers = static_cast<uint32_t>(sc->second.command_buffers.size());

                    attempted = true;

                }

            }

        }

        if (attempted) {

            char message[160];

            std::snprintf(message, sizeof(message),

                "B2 resources: family=%u, commandBuffers=%u, submit=0",

                queue_family, created_buffers);

            log_event(message);

        }

    }

    const auto count = ++present_count;
    embervale::c4::observe(log_event, count);

    if (count == 1) {

        VkQueueFlags flags = 0;

        bool known = false;

        {

            std::lock_guard<std::mutex> lock(mutex);

            auto q = queue_next.find(queue);

            if (q != queue_next.end()) {

                auto d = observed_devices.find(dispatch_key(q->second.device));

                if (d != observed_devices.end() && q->second.family < d->second.family_flags.size()) {

                    flags = d->second.family_flags[q->second.family];

                    known = true;

                    d->second.graphics_present_queue_seen = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;

                }

            }

        }

        char message[160];

        std::snprintf(message, sizeof(message),

            "B1 present queue capabilities: known=%u, family=%u, flags=0x%08X, graphics=%u",

            known ? 1u : 0u, queue_family, static_cast<unsigned>(flags),

            (flags & VK_QUEUE_GRAPHICS_BIT) ? 1u : 0u);

        log_event(message);

    }

    if (count == 1 || count % 300 == 0) {

        char message[192];

        const uint32_t swapchains = info ? info->swapchainCount : 0;

        const uint32_t image_index =

            (info && swapchains > 0 && info->pImageIndices) ? info->pImageIndices[0] : UINT32_MAX;

        const uint32_t waits = info ? info->waitSemaphoreCount : 0;

        std::snprintf(message, sizeof(message),

            "vkQueuePresentKHR observed: %llu calls, swapchains=%u, firstImage=%u, waitSemaphores=%u, result=%d, queueFamily=%u",

            static_cast<unsigned long long>(count), swapchains, image_index, waits,

            static_cast<int>(result), queue_family);

        log_event(message);

    }

    return result;

}

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(

    VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* allocator) {

    PFN_vkGetDeviceProcAddr next = nullptr;
    VkResult c32_idle_result = VK_SUCCESS;
    bool c32_idle_called = false;

    {

        std::lock_guard<std::mutex> lock(mutex);

        auto it = device_next.find(dispatch_key(device));

        if (it != device_next.end()) next = it->second;

        auto sc = observed_swapchains.find(swapchain);

        if (sc != observed_swapchains.end()) {

            if (next && sc->second.c2_render_pass) {

                auto idle = c2_proc<PFN_vkDeviceWaitIdle>(next, device, "vkDeviceWaitIdle");

                if (idle) { c32_idle_called = true; c32_idle_result = idle(device); }

            }

            release_b2(device, next, sc->second);

            observed_swapchains.erase(sc);

        }

    }

    if (next) {

        auto fn = reinterpret_cast<PFN_vkDestroySwapchainKHR>(next(device, "vkDestroySwapchainKHR"));

        if (fn) fn(device, swapchain, allocator);

    }

    if (c32_idle_called) {
        char message[128];
        std::snprintf(message, sizeof(message), "C3.2 swapchain cleanup: vkDeviceWaitIdle result=%d",
                      static_cast<int>(c32_idle_result));
        log_event(message);
    }
    log_event("B1 swapchain metadata released");

}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device,

                                              const VkAllocationCallbacks* allocator) {

    PFN_vkGetDeviceProcAddr next = nullptr;

    {

        std::lock_guard<std::mutex> lock(mutex);

        auto it = device_next.find(dispatch_key(device));

        if (it != device_next.end()) { next = it->second; device_next.erase(it); }

        observed_devices.erase(dispatch_key(device));

        for (auto sc = observed_swapchains.begin(); sc != observed_swapchains.end();) {

            if (sc->second.device == device) {

                if (next && sc->second.c2_render_pass) {

                    auto idle = c2_proc<PFN_vkDeviceWaitIdle>(next, device, "vkDeviceWaitIdle");

                    if (idle) { const VkResult idle_result = idle(device); (void)idle_result; }

                }

                release_b2(device, next, sc->second);

                sc = observed_swapchains.erase(sc);

            }

            else ++sc;

        }

        for (auto q = queue_next.begin(); q != queue_next.end();) {

            if (q->second.device == device) q = queue_next.erase(q);

            else ++q;

        }

    }

    if (next) {

        auto destroy = reinterpret_cast<PFN_vkDestroyDevice>(next(device, "vkDestroyDevice"));

        if (destroy) destroy(device, allocator);

    }

}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,

                                                               const char* name) {

    if (!name) return nullptr;

    if (std::strcmp(name, "vkGetDeviceQueue") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceQueue);

    if (std::strcmp(name, "vkGetDeviceQueue2") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceQueue2);

    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateSwapchainKHR);

    if (std::strcmp(name, "vkGetSwapchainImagesKHR") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkGetSwapchainImagesKHR);

    if (std::strcmp(name, "vkAcquireNextImageKHR") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkAcquireNextImageKHR);

    if (std::strcmp(name, "vkAcquireNextImage2KHR") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkAcquireNextImage2KHR);

    if (std::strcmp(name, "vkQueuePresentKHR") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkQueuePresentKHR);

    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);

    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroySwapchainKHR);

    if (std::strcmp(name, "vkDestroyDevice") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDevice);

    PFN_vkGetDeviceProcAddr next = nullptr;

    {

        std::lock_guard<std::mutex> lock(mutex);

        auto it = device_next.find(dispatch_key(device));

        if (it != device_next.end()) next = it->second;

    }

    PFN_vkVoidFunction result = next ? next(device, name) : nullptr;

    return result;

}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,

                                                                 const char* name) {

    if (!name) return nullptr;

    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);

    if (std::strcmp(name, "vkCreateInstance") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);

    if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumeratePhysicalDevices);

    if (std::strcmp(name, "vkEnumeratePhysicalDeviceGroups") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkEnumeratePhysicalDeviceGroups);

    if (std::strcmp(name, "vkDestroyInstance") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyInstance);

    if (std::strcmp(name, "vkCreateDevice") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);

    if (std::strcmp(name, "vkGetDeviceQueue") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceQueue);

    if (std::strcmp(name, "vkGetDeviceQueue2") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceQueue2);

    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateSwapchainKHR);

    if (std::strcmp(name, "vkGetSwapchainImagesKHR") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkGetSwapchainImagesKHR);

    if (std::strcmp(name, "vkAcquireNextImageKHR") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkAcquireNextImageKHR);

    if (std::strcmp(name, "vkAcquireNextImage2KHR") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkAcquireNextImage2KHR);

    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroySwapchainKHR);

    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)

        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);

    PFN_vkGetInstanceProcAddr next = nullptr;

    {

        std::lock_guard<std::mutex> lock(mutex);

        auto it = instance_next.find(dispatch_key(instance));

        if (it != instance_next.end()) next = it->second;

    }

    return next ? next(instance, name) : nullptr;

}

VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(

    VkNegotiateLayerInterface* info) {

    if (!info || info->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT ||

        info->loaderLayerInterfaceVersion < 2) return VK_ERROR_INITIALIZATION_FAILED;

    info->loaderLayerInterfaceVersion = 2;

    info->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;

    info->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;

    info->pfnGetPhysicalDeviceProcAddr = nullptr;

    log_event("Loader negotiation successful");

    return VK_SUCCESS;

}

} // extern C

#pragma comment(linker, "/EXPORT:vkNegotiateLoaderLayerInterfaceVersion")

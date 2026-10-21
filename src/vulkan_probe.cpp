// Standalone Vulkan loader diagnostic. Does not load into or modify the game.
// Uses the Vulkan 1.0 ABI without requiring the Vulkan SDK to build.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <iostream>
#include <iomanip>

namespace {
using VkResult = int32_t;
using VkInstance = void*;
using VkPhysicalDevice = void*;
using VkFlags = uint32_t;
constexpr VkResult VK_SUCCESS = 0;
constexpr uint32_t VK_STRUCTURE_TYPE_APPLICATION_INFO = 0;
constexpr uint32_t VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1;
struct VkApplicationInfo {
    uint32_t sType; const void* pNext; const char* pApplicationName;
    uint32_t applicationVersion; const char* pEngineName;
    uint32_t engineVersion; uint32_t apiVersion;
};
struct VkInstanceCreateInfo {
    uint32_t sType; const void* pNext; VkFlags flags;
    const VkApplicationInfo* pApplicationInfo;
    uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames;
};
using PFN_vkEnumerateInstanceVersion = VkResult (WINAPI*)(uint32_t*);
using PFN_vkCreateInstance = VkResult (WINAPI*)(const VkInstanceCreateInfo*, const void*, VkInstance*);
using PFN_vkEnumeratePhysicalDevices = VkResult (WINAPI*)(VkInstance, uint32_t*, VkPhysicalDevice*);
using PFN_vkDestroyInstance = void (WINAPI*)(VkInstance, const void*);
void print_version(uint32_t version) {
    std::cout << (version >> 22) << '.' << ((version >> 12) & 0x3ff) << '.'
              << (version & 0xfff);
}
}
int main() {
    std::cout << "Embervale Companion v0.5 - Vulkan diagnostics\n";
    HMODULE loader = LoadLibraryW(L"vulkan-1.dll");
    if (!loader) {
        std::cerr << "ERROR: Vulkan loader (vulkan-1.dll) not found.\n";
        return 1;
    }
    const auto load = [loader](const char* name) { return GetProcAddress(loader, name); };
    auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(load("vkEnumerateInstanceVersion"));
    auto create = reinterpret_cast<PFN_vkCreateInstance>(load("vkCreateInstance"));
    auto enumerateDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(load("vkEnumeratePhysicalDevices"));
    auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(load("vkDestroyInstance"));
    uint32_t version = 1u << 22; // Vulkan 1.0 fallback for old loaders.
    if (enumerateVersion && enumerateVersion(&version) != VK_SUCCESS) {
        std::cerr << "ERROR: vkEnumerateInstanceVersion failed.\n";
        FreeLibrary(loader); return 2;
    }
    std::cout << "Vulkan loader API: "; print_version(version); std::cout << '\n';
    if (!create || !enumerateDevices || !destroy) {
        std::cerr << "ERROR: Required Vulkan entry points unavailable.\n";
        FreeLibrary(loader); return 3;
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr,
        "EmbervaleCompanionVulkanProbe", 1, "None", 0, 1u << 22};
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0,
        &app, 0, nullptr, 0, nullptr};
    VkInstance instance = nullptr;
    VkResult result = create(&info, nullptr, &instance);
    if (result != VK_SUCCESS || !instance) {
        std::cerr << "ERROR: vkCreateInstance returned " << result << '\n';
        FreeLibrary(loader); return 4;
    }
    uint32_t count = 0;
    result = enumerateDevices(instance, &count, nullptr);
    if (result == VK_SUCCESS) {
        std::cout << "Vulkan physical devices: " << count << '\n';
        std::cout << (count ? "PASS: Vulkan is available.\n" : "WARNING: No Vulkan devices found.\n");
    } else {
        std::cerr << "ERROR: vkEnumeratePhysicalDevices returned " << result << '\n';
    }
    destroy(instance, nullptr);
    FreeLibrary(loader);
    std::cout << "This probe does not detect the API used by Enshrouded.\n";
    return result == VK_SUCCESS && count > 0 ? 0 : 5;
}

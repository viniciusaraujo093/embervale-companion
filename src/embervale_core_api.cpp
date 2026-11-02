#include "embervale/core_api.h"
#include "embervale_core_runtime.h"
#ifdef EMBERVALE_C4_S19_STORAGE
#include "embervale_core_storage.h"
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace {
std::atomic<std::uint32_t> g_state{0};

std::uint32_t EMBERVALE_CALL query_state() noexcept {
    return g_state.load(std::memory_order_acquire);
}

const char* EMBERVALE_CALL query_version() noexcept {
    return "0.7.0"; // Match current Shroudtopia adapter metadata.
}

#ifdef EMBERVALE_C4_S19_STORAGE
constexpr std::uint32_t kRuntimeAbiMinor = 1;
constexpr std::uint32_t kRuntimeCapabilities =
    EMBERVALE_CORE_CAP_LIFECYCLE_STATUS | EMBERVALE_CORE_CAP_MODULE_STORAGE;
constexpr std::uint32_t kRuntimeStructSize = EMBERVALE_CORE_API_V1_1_SIZE;
#else
// An S18-only build continues to advertise exactly ABI v1.0.
constexpr std::uint32_t kRuntimeAbiMinor = 0;
constexpr std::uint32_t kRuntimeCapabilities = EMBERVALE_CORE_CAP_LIFECYCLE_STATUS;
constexpr std::uint32_t kRuntimeStructSize = EMBERVALE_CORE_API_V1_0_SIZE;
#endif

const EmbervaleCoreApi g_api_v1 = {
    kRuntimeStructSize,
    EMBERVALE_CORE_ABI_MAJOR,
    kRuntimeAbiMinor,
    kRuntimeCapabilities,
    &query_state,
    &query_version,
#ifdef EMBERVALE_C4_S19_STORAGE
    &embervale::core::storage::read,
    &embervale::core::storage::write,
    &embervale::core::storage::erase
#else
    nullptr, nullptr, nullptr
#endif
};
} // namespace

namespace embervale { namespace core {
void set_loaded(bool value) noexcept {
    if (value) {
        g_state.fetch_or(EMBERVALE_CORE_STATE_LOADED, std::memory_order_release);
    } else {
        g_state.store(0, std::memory_order_release);
    }
}
void set_active(bool value) noexcept {
    if (value) {
        g_state.fetch_or(EMBERVALE_CORE_STATE_LOADED | EMBERVALE_CORE_STATE_ACTIVE,
                         std::memory_order_release);
    } else {
        g_state.fetch_and(~static_cast<std::uint32_t>(EMBERVALE_CORE_STATE_ACTIVE),
                          std::memory_order_release);
    }
}
}} // namespace embervale::core

#if defined(_WIN32)
# define EMBERVALE_EXPORT __declspec(dllexport)
#else
# define EMBERVALE_EXPORT __attribute__((visibility("default")))
#endif
extern "C" EMBERVALE_EXPORT const EmbervaleCoreApi* EMBERVALE_CALL
Embervale_GetApi(std::uint32_t major, std::uint32_t min_size) noexcept {
    constexpr std::uint32_t kV1RequiredBytes = EMBERVALE_CORE_API_V1_0_SIZE;
    if (major != EMBERVALE_CORE_ABI_MAJOR || min_size < kV1RequiredBytes ||
        min_size > kRuntimeStructSize)
        return nullptr;
    return &g_api_v1;
}

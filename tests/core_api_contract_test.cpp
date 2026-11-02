#include "embervale/core_api.h"
#include "embervale_core_runtime.h"
#include <cassert>
#include <cstring>
#include <cstdint>

extern "C" const EmbervaleCoreApi* EMBERVALE_CALL
Embervale_GetApi(std::uint32_t, std::uint32_t) noexcept;

int main() {
    const auto v1prefix = EMBERVALE_CORE_API_V1_0_SIZE;
    assert(Embervale_GetApi(999, v1prefix) == nullptr);
    assert(Embervale_GetApi(EMBERVALE_CORE_ABI_MAJOR, v1prefix - 1) == nullptr);
    assert(Embervale_GetApi(EMBERVALE_CORE_ABI_MAJOR, 0) == nullptr);
    const auto* api = Embervale_GetApi(EMBERVALE_CORE_ABI_MAJOR, v1prefix);
    assert(api && api->abi_major == EMBERVALE_CORE_ABI_MAJOR);
    assert(std::strcmp(api->get_version(), "0.7.0") == 0);
#ifdef EMBERVALE_C4_S19_STORAGE
    assert(api->abi_minor == 1);
    assert(api->struct_size == EMBERVALE_CORE_API_V1_1_SIZE);
    assert((api->capabilities & EMBERVALE_CORE_CAP_MODULE_STORAGE) != 0);
    assert(api->storage_read && api->storage_write && api->storage_delete);
    assert(Embervale_GetApi(1, EMBERVALE_CORE_API_V1_1_SIZE) == api);
#else
    // S18-only builds remain true ABI v1.0, even when compiled with the new header.
    assert(api->abi_minor == 0);
    assert(api->struct_size == EMBERVALE_CORE_API_V1_0_SIZE);
    assert(api->capabilities == EMBERVALE_CORE_CAP_LIFECYCLE_STATUS);
    assert(api->storage_read == nullptr);
    assert(Embervale_GetApi(1, v1prefix + 1) == nullptr);
#endif
    assert(Embervale_GetApi(1, api->struct_size + 1) == nullptr);
    assert(api->get_state() == 0);
    embervale::core::set_loaded(true);
    assert(api->get_state() == EMBERVALE_CORE_STATE_LOADED);
    embervale::core::set_active(true);
    assert(api->get_state() == (EMBERVALE_CORE_STATE_LOADED | EMBERVALE_CORE_STATE_ACTIVE));
    embervale::core::set_active(false);
    assert(api->get_state() == EMBERVALE_CORE_STATE_LOADED);
    embervale::core::set_loaded(false);
    assert(api->get_state() == 0);
}

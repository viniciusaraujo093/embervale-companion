#pragma once
#include "embervale/core_api.h"

namespace embervale { namespace core { namespace storage {
// Functions implement the public ABI but do not export additional DLL symbols.
// They never propagate a C++ exception across the ABI boundary.
int32_t EMBERVALE_CALL read(const char* module_id, const char* key,
                            void* output, uint32_t capacity_bytes,
                            uint32_t* required_bytes) noexcept;
int32_t EMBERVALE_CALL write(const char* module_id, const char* key,
                             const void* payload, uint32_t payload_bytes) noexcept;
int32_t EMBERVALE_CALL erase(const char* module_id, const char* key) noexcept;
}}} // namespace embervale::core::storage

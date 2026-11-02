#ifndef EMBERVALE_CORE_API_V1_H
#define EMBERVALE_CORE_API_V1_H

// Embervale Companion public C ABI. Consumers use GetProcAddress("Embervale_GetApi").
// Version 1.0 prefix is unchanged. Version 1.1 appends a bounded module storage API.
// No C++ ownership, Vulkan object pointers, hooks, or hot-reload guarantees.
#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
# define EMBERVALE_CALL __cdecl
#else
# define EMBERVALE_CALL
#endif

#define EMBERVALE_CORE_ABI_MAJOR 1u
#define EMBERVALE_CORE_ABI_MINOR 1u
#define EMBERVALE_CORE_CAP_LIFECYCLE_STATUS (1u << 0)
#define EMBERVALE_CORE_CAP_MODULE_STORAGE (1u << 1)
#define EMBERVALE_CORE_STATE_LOADED (1u << 0)
#define EMBERVALE_CORE_STATE_ACTIVE (1u << 1)
#define EMBERVALE_CORE_MODULE_NAME_W L"EmbervaleCompanionShroudtopia.dll"
#define EMBERVALE_CORE_GET_API_SYMBOL "Embervale_GetApi"
#define EMBERVALE_CORE_STORAGE_MAX_BYTES (1024u * 1024u)

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (EMBERVALE_CALL *EmbervaleGetStateFn)(void);
typedef const char* (EMBERVALE_CALL *EmbervaleGetVersionFn)(void);

// Storage values are byte sequences, normally UTF-8 JSON owned by the module.
// The Core does not parse JSON: versioning and migrations belong to the module.
// Key and module_id: 1..64 ASCII chars, starting a-z or 0-9, followed by
// a-z, 0-9, '.', '_' or '-'; consecutive dots are prohibited.
// Storage is local per Windows user. Reads return bytes WITHOUT a NUL terminator.
// On a small buffer, *required_bytes is set and no partial payload is copied.
// Missing files return NOT_FOUND; callers must explicitly handle this.
#define EMBERVALE_STORAGE_OK 0
#define EMBERVALE_STORAGE_NOT_FOUND 1
#define EMBERVALE_STORAGE_BUFFER_TOO_SMALL 2
#define EMBERVALE_STORAGE_INVALID_ARGUMENT 3
#define EMBERVALE_STORAGE_TOO_LARGE 4
#define EMBERVALE_STORAGE_IO_ERROR 5

typedef int32_t (EMBERVALE_CALL *EmbervaleStorageReadFn)(
    const char* module_id, const char* key,
    void* output, uint32_t capacity_bytes, uint32_t* required_bytes);
typedef int32_t (EMBERVALE_CALL *EmbervaleStorageWriteFn)(
    const char* module_id, const char* key,
    const void* payload, uint32_t payload_bytes);
typedef int32_t (EMBERVALE_CALL *EmbervaleStorageDeleteFn)(
    const char* module_id, const char* key);

typedef struct EmbervaleCoreApi {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t capabilities;
    EmbervaleGetStateFn get_state;
    EmbervaleGetVersionFn get_version;
    // Added in ABI 1.1. Check struct_size AND EMBERVALE_CORE_CAP_MODULE_STORAGE
    // before using these fields. Pointers are null when storage is unavailable.
    EmbervaleStorageReadFn storage_read;
    EmbervaleStorageWriteFn storage_write;
    EmbervaleStorageDeleteFn storage_delete;
} EmbervaleCoreApi;

#define EMBERVALE_CORE_API_V1_0_SIZE ((uint32_t)offsetof(EmbervaleCoreApi, storage_read))
#define EMBERVALE_CORE_API_V1_1_SIZE ((uint32_t)sizeof(EmbervaleCoreApi))

typedef const EmbervaleCoreApi* (EMBERVALE_CALL *EmbervaleGetApiFn)(
    uint32_t requested_abi_major, uint32_t minimum_struct_size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // EMBERVALE_CORE_API_V1_H

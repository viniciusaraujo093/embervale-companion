#include "embervale/core_api.h"
#include <cassert>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#endif
extern "C" const EmbervaleCoreApi* EMBERVALE_CALL
Embervale_GetApi(std::uint32_t, std::uint32_t) noexcept;

int main(int argc, char** argv) {
    if (argc != 2 || (std::strcmp(argv[1], "write") != 0 &&
                      std::strcmp(argv[1], "verify") != 0)) return 2;
    const auto test_root = std::filesystem::temp_directory_path() / "Embervale_C4_S19_contract";
#ifdef _WIN32
    assert(SetEnvironmentVariableW(L"LOCALAPPDATA", test_root.c_str()));
#else
    assert(setenv("XDG_DATA_HOME", test_root.c_str(), 1) == 0);
#endif
    const auto* api = Embervale_GetApi(1, EMBERVALE_CORE_API_V1_0_SIZE);
    assert(api && api->struct_size >= EMBERVALE_CORE_API_V1_1_SIZE);
    assert((api->capabilities & EMBERVALE_CORE_CAP_MODULE_STORAGE) != 0);
    assert(api->storage_read && api->storage_write && api->storage_delete);
    const char* ns = "s19-worldmap-test";
    const char* key = "settings";
    constexpr char json[] = "{\"schema_version\":1,\"show_altars\":false}";
    const auto len = static_cast<std::uint32_t>(sizeof(json) - 1);
    if (std::strcmp(argv[1], "write") == 0) {
        assert(api->storage_write("../escape", key, json, len) == EMBERVALE_STORAGE_INVALID_ARGUMENT);
        assert(api->storage_write(ns, "bad/key", json, len) == EMBERVALE_STORAGE_INVALID_ARGUMENT);
        assert(api->storage_write(ns, key, json, EMBERVALE_CORE_STORAGE_MAX_BYTES + 1) == EMBERVALE_STORAGE_TOO_LARGE);
        assert(api->storage_write(ns, key, json, len) == EMBERVALE_STORAGE_OK);
        std::uint32_t size = 0;
        assert(api->storage_read(ns, key, nullptr, 0, &size) == EMBERVALE_STORAGE_BUFFER_TOO_SMALL);
        assert(size == len);
        char tiny[2] = {};
        assert(api->storage_read(ns, key, tiny, 2, &size) == EMBERVALE_STORAGE_BUFFER_TOO_SMALL);
        assert(tiny[0] == 0); // no partial copy
        std::cout << "C4-S19 write OK; reopening in a NEW process validates persistence\n";
    } else {
        std::uint32_t needed = 0;
        assert(api->storage_read(ns, key, nullptr, 0, &needed) == EMBERVALE_STORAGE_BUFFER_TOO_SMALL);
        std::vector<char> bytes(needed);
        assert(api->storage_read(ns, key, bytes.data(), needed, &needed) == EMBERVALE_STORAGE_OK);
        assert(std::string(bytes.begin(), bytes.end()) == json);
        assert(api->storage_delete(ns, key) == EMBERVALE_STORAGE_OK);
        assert(api->storage_read(ns, key, nullptr, 0, &needed) == EMBERVALE_STORAGE_NOT_FOUND);
        std::cout << "C4-S19 persistence/read/delete OK\n";
    }
}

#include "embervale_core_storage.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <unistd.h>
#endif

namespace embervale { namespace core { namespace storage {
namespace {
namespace fs = std::filesystem;
std::mutex g_storage_mutex;
std::atomic<std::uint64_t> g_temp_counter{0};

bool valid_part(const char* part) noexcept {
    if (!part) return false;
    std::size_t len = 0;
    char previous = '\0';
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(part);
         *p != 0; ++p) {
        if (++len > 64) return false;
        const char c = static_cast<char>(*p);
        const bool digit = c >= '0' && c <= '9';
        const bool lower = c >= 'a' && c <= 'z';
        if (len == 1 && !digit && !lower) return false;
        if (!digit && !lower && c != '_' && c != '-' && c != '.') return false;
        if (c == '.' && previous == '.') return false;
        previous = c;
    }
    return len > 0;
}

bool storage_root(fs::path& out) {
#ifdef _WIN32
    // Use the Unicode environment API; a narrow getenv() breaks accented user paths.
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (length <= 1 || length > 32768) return false;
    std::wstring buffer(length, L'\0');
    const DWORD actual = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), length);
    if (actual == 0 || actual >= length) return false;
    buffer.resize(actual);
    out = fs::path(buffer) / L"EmbervaleCompanion" / L"modules";
#else
    // Native Linux builds are only used for offline contract testing.
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (!xdg || !*xdg) return false;
    out = fs::path(xdg) / "EmbervaleCompanion" / "modules";
#endif
    return true;
}

bool storage_path(const char* module_id, const char* key, fs::path& out) {
    if (!valid_part(module_id) || !valid_part(key)) return false;
    fs::path root;
    if (!storage_root(root)) return false;
    out = root / module_id / (std::string(key) + ".json");
    return true;
}

bool replace_file(const fs::path& temporary, const fs::path& destination) {
#ifdef _WIN32
    // Same-directory replacement; Windows fails rather than silently dropping
    // the old file if replacement is denied by another process.
    return MoveFileExW(temporary.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    fs::rename(temporary, destination, ec);
    return !ec;
#endif
}

std::uint64_t process_id() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

} // namespace

int32_t EMBERVALE_CALL read(const char* module_id, const char* key,
                            void* output, uint32_t capacity_bytes,
                            uint32_t* required_bytes) noexcept {
    if (required_bytes) *required_bytes = 0;
    if (!required_bytes || (!output && capacity_bytes != 0) ||
        !valid_part(module_id) || !valid_part(key))
        return EMBERVALE_STORAGE_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(g_storage_mutex);
        fs::path path;
        if (!storage_path(module_id, key, path)) return EMBERVALE_STORAGE_IO_ERROR;
        std::error_code ec;
        if (!fs::exists(path, ec))
            return ec ? EMBERVALE_STORAGE_IO_ERROR : EMBERVALE_STORAGE_NOT_FOUND;
        const auto size = fs::file_size(path, ec);
        if (ec) return EMBERVALE_STORAGE_IO_ERROR;
        if (size > EMBERVALE_CORE_STORAGE_MAX_BYTES)
            return EMBERVALE_STORAGE_TOO_LARGE;
        *required_bytes = static_cast<uint32_t>(size);
        if (capacity_bytes < size || (size != 0 && !output))
            return EMBERVALE_STORAGE_BUFFER_TOO_SMALL;
        std::ifstream input(path, std::ios::binary);
        if (!input) return EMBERVALE_STORAGE_IO_ERROR;
        if (size) {
            input.read(static_cast<char*>(output), static_cast<std::streamsize>(size));
            if (!input || input.gcount() != static_cast<std::streamsize>(size))
                return EMBERVALE_STORAGE_IO_ERROR;
        }
        return EMBERVALE_STORAGE_OK;
    } catch (...) {
        return EMBERVALE_STORAGE_IO_ERROR;
    }
}

int32_t EMBERVALE_CALL write(const char* module_id, const char* key,
                             const void* payload, uint32_t payload_bytes) noexcept {
    if (!valid_part(module_id) || !valid_part(key) ||
        !payload || payload_bytes == 0)
        return EMBERVALE_STORAGE_INVALID_ARGUMENT;
    if (payload_bytes > EMBERVALE_CORE_STORAGE_MAX_BYTES)
        return EMBERVALE_STORAGE_TOO_LARGE;
    try {
        std::lock_guard<std::mutex> lock(g_storage_mutex);
        fs::path path;
        if (!storage_path(module_id, key, path)) return EMBERVALE_STORAGE_IO_ERROR;
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (ec) return EMBERVALE_STORAGE_IO_ERROR;
        const auto suffix = ".tmp-" + std::to_string(process_id()) + "-" +
#ifdef _WIN32
            std::to_string(GetTickCount64()) + "-" +
#endif
            std::to_string(g_temp_counter.fetch_add(1, std::memory_order_relaxed));
        fs::path temp = path;
        temp += suffix; // Preserve a Unicode Windows profile path.
        bool written = false;
#ifdef _WIN32
        // CREATE_NEW prevents accidentally truncating a pre-existing temporary.
        HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            written = true;
            const auto* bytes = static_cast<const char*>(payload);
            DWORD offset = 0;
            while (offset < payload_bytes) {
                DWORD count = 0;
                if (!WriteFile(handle, bytes + offset, payload_bytes - offset,
                               &count, nullptr) || count == 0) {
                    written = false;
                    break;
                }
                offset += count;
            }
            if (written) written = FlushFileBuffers(handle) != 0;
            if (!CloseHandle(handle)) written = false;
        }
#else
        {
            std::ofstream output(temp, std::ios::binary | std::ios::trunc);
            if (output) {
                output.write(static_cast<const char*>(payload), payload_bytes);
                output.flush();
                written = output.good();
            }
        }
#endif
        if (written) written = replace_file(temp, path);
        if (!written) {
            fs::remove(temp, ec);
            return EMBERVALE_STORAGE_IO_ERROR;
        }
        return EMBERVALE_STORAGE_OK;
    } catch (...) {
        return EMBERVALE_STORAGE_IO_ERROR;
    }
}

int32_t EMBERVALE_CALL erase(const char* module_id, const char* key) noexcept {
    if (!valid_part(module_id) || !valid_part(key))
        return EMBERVALE_STORAGE_INVALID_ARGUMENT;
    try {
        std::lock_guard<std::mutex> lock(g_storage_mutex);
        fs::path path;
        if (!storage_path(module_id, key, path)) return EMBERVALE_STORAGE_IO_ERROR;
        std::error_code ec;
        const bool removed = fs::remove(path, ec);
        if (ec) return EMBERVALE_STORAGE_IO_ERROR;
        return removed ? EMBERVALE_STORAGE_OK : EMBERVALE_STORAGE_NOT_FOUND;
    } catch (...) {
        return EMBERVALE_STORAGE_IO_ERROR;
    }
}
}}} // namespace embervale::core::storage

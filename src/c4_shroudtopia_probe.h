#pragma once
// C4-S2 - read-only inspection of historical render-present signature.
// No code patching, hooks, Vulkan calls or GPU work. A matching prologue is
// only a candidate: it is NOT proof of a compatible Enshrouded function.
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace embervale { namespace c4 {

inline bool c4_readable_span(std::uintptr_t addr, std::size_t count) noexcept {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<const void*>(addr), &info, sizeof(info)) != sizeof(info))
        return false;
    const DWORD protection = info.Protect & 0xffu;
    const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
        protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    const auto start = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const auto end = start + info.RegionSize;
    return info.State == MEM_COMMIT && !(info.Protect & PAGE_GUARD) &&
           readable && end >= addr && count <= end - addr;
}

inline std::string c4_hex_sample(std::uintptr_t addr, std::size_t count) {
    if (!c4_readable_span(addr, count)) return "unreadable";
    std::string hex;
    hex.reserve(count * 2);
    static const char* digits = "0123456789abcdef";
    const auto* bytes = reinterpret_cast<const unsigned char*>(addr);
    for (std::size_t i = 0; i < count; ++i) {
        hex.push_back(digits[bytes[i] >> 4]);
        hex.push_back(digits[bytes[i] & 0x0f]);
    }
    return hex;
}

inline std::string inspect_render_present() {
    constexpr std::uint32_t kHistoricalRva = 0xDCFAE0u;
    constexpr std::uint64_t kRadius = 0x400000u;
    constexpr unsigned char kPrologue[] = {
        0x4c, 0x89, 0x4c, 0x24, 0x20, 0x48, 0x89, 0x54,
        0x24, 0x10, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54
    };
    const HMODULE executable = GetModuleHandleW(nullptr);
    if (!executable || sizeof(void*) != 8) return "C4-S2 skipped: no_exe_or_not_x64";
    const auto* base = reinterpret_cast<const unsigned char*>(executable);
    const auto imageBase = reinterpret_cast<std::uintptr_t>(base);
    if (!c4_readable_span(imageBase, sizeof(IMAGE_DOS_HEADER)))
        return "C4-S2 skipped: unreadable_dos_header";
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096)
        return "C4-S2 skipped: invalid_dos_header";
    const auto ntAddr = imageBase + static_cast<std::uintptr_t>(dos->e_lfanew);
    if (!c4_readable_span(ntAddr, sizeof(IMAGE_NT_HEADERS64)))
        return "C4-S2 skipped: unreadable_nt_header";
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(ntAddr);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.NumberOfSections > 96)
        return "C4-S2 skipped: invalid_pe_header";
    const std::uint64_t imageSize = nt->OptionalHeader.SizeOfImage;
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    if (!c4_readable_span(reinterpret_cast<std::uintptr_t>(sections),
                          nt->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER)))
        return "C4-S2 skipped: invalid_section_headers";

    std::uint64_t inspected = 0;
    std::uint64_t matches = 0;
    std::vector<std::uint32_t> samples;
    bool executableText = false;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        if (std::memcmp(section.Name, ".text", 5) != 0 ||
            !(section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        executableText = true;
        const std::uint64_t sectionStart = section.VirtualAddress;
        const std::uint64_t sectionLength = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;
        const std::uint64_t sectionEnd = std::min(imageSize, sectionStart + sectionLength);
        const std::uint64_t from = std::max(sectionStart, std::uint64_t(kHistoricalRva) - kRadius);
        const std::uint64_t to = std::min(sectionEnd, std::uint64_t(kHistoricalRva) + kRadius);
        if (from >= to) break;
        std::uintptr_t cursor = imageBase + static_cast<std::uintptr_t>(from);
        const std::uintptr_t stop = imageBase + static_cast<std::uintptr_t>(to);
        while (cursor < stop) {
            MEMORY_BASIC_INFORMATION region{};
            if (VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) != sizeof(region)) break;
            const auto regionBegin = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            const auto regionEnd = regionBegin + region.RegionSize;
            if (regionEnd <= cursor) break;
            const auto upper = std::min(stop, regionEnd);
            const DWORD protection = region.Protect & 0xffu;
            const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
                protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
                protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
            if (region.State == MEM_COMMIT && !(region.Protect & PAGE_GUARD) && readable) {
                for (std::uintptr_t addr = cursor;
                     addr <= upper && upper - addr >= sizeof(kPrologue);
                     ++addr) {
                    ++inspected;
                    if (std::memcmp(reinterpret_cast<const void*>(addr), kPrologue, sizeof(kPrologue)) == 0) {
                        ++matches;
                        if (samples.size() < 16)
                            samples.push_back(static_cast<std::uint32_t>(addr - imageBase));
                    }
                }
            }
            cursor = upper;
        }
        break;
    }
    std::ostringstream result;
    result << "C4-S2 scan: read_only=1, hooks=0, gpu_work=0, pe_timestamp=0x"
           << std::hex << nt->FileHeader.TimeDateStamp << ", image_size=0x"
           << imageSize << ", reference_rva=0x" << kHistoricalRva << std::dec
           << ", inspected_positions=" << inspected << ", matches=" << matches
           << ", listed=" << samples.size();
    if (!executableText) result << ", note=no_executable_text";
    result << ", candidates=";
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (i) result << ";";
        result << "0x" << std::hex << samples[i] << std::dec;
    }
    if (kHistoricalRva < imageSize && imageSize - kHistoricalRva >= 32) {
        result << ", reference_bytes=" << c4_hex_sample(imageBase + kHistoricalRva, 32);
    }
    if (!samples.empty())
        result << ", first_bytes=" << c4_hex_sample(imageBase + samples[0], 32);
    return result.str();
}

}} // namespace embervale::c4
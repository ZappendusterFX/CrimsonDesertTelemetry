#pragma once
#include "console/mem.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace cdt::recovery::roles
{
namespace detail
{
struct Function { std::uint32_t begin{}, end{}, unwind{}; };
static_assert(sizeof(Function) == 12);
struct Image
{
    std::uint64_t base{};
    std::uint32_t size{};
    IMAGE_DATA_DIRECTORY functions{};
    std::vector<IMAGE_SECTION_HEADER> sections;

    bool Read(std::uint64_t rva, void* output, size_t count) const
    {
        return rva < size && count <= size - rva &&
            ch::mem::SafeRead(reinterpret_cast<const void*>(base + rva), output, count);
    }
    bool In(std::uint64_t rva, size_t count, DWORD required, DWORD prohibited = 0) const
    {
        if (!rva || rva >= size || count > size - rva) return false;
        return std::any_of(sections.begin(), sections.end(), [&](const auto& section) {
            return (section.Characteristics & required) == required && !(section.Characteristics & prohibited) &&
                rva >= section.VirtualAddress && rva + count <= std::uint64_t{section.VirtualAddress} + section.Misc.VirtualSize;
        });
    }
    bool Open(std::uint64_t moduleBase, std::uint32_t imageSize)
    {
        base = moduleBase; size = imageSize;
        if (!base || size < 4096 || size > 0x40000000 || base > UINT64_MAX - size) return false;
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        if (!Read(0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 || dos.e_lfanew > 0x100000 ||
            !Read(static_cast<std::uint32_t>(dos.e_lfanew), &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.OptionalHeader.SizeOfImage != size || nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
            nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION ||
            !nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections > 96) return false;
        const auto table = static_cast<std::uint32_t>(dos.e_lfanew) + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
        sections.resize(nt.FileHeader.NumberOfSections);
        if (!Read(table, sections.data(), sections.size() * sizeof(IMAGE_SECTION_HEADER))) return false;
        functions = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        return functions.Size >= sizeof(Function) && functions.Size <= 16 * 1024 * 1024 && functions.Size % sizeof(Function) == 0 &&
            In(functions.VirtualAddress, functions.Size, IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_WRITE);
    }
    bool Entry(size_t index, Function& result) const
    {
        return index < functions.Size / sizeof(Function) &&
            Read(functions.VirtualAddress + index * sizeof(Function), &result, sizeof(result));
    }
    bool Containing(std::uint64_t rva, Function& result) const
    {
        // AMD64 exception entries are sorted by start RVA. Lookup is bounded by
        // log2(table entries), never a scan of game heaps or executable code.
        size_t low = 0, high = functions.Size / sizeof(Function);
        while (low < high)
        {
            const auto middle = low + (high - low) / 2;
            Function item{};
            if (!Entry(middle, item)) return false;
            if (item.begin <= rva) low = middle + 1; else high = middle;
        }
        if (!low || !Entry(low - 1, result) || !result.begin || result.end <= result.begin ||
            result.end - result.begin > 32768 || rva < result.begin || rva >= result.end ||
            !In(result.begin, result.end - result.begin, IMAGE_SCN_MEM_EXECUTE)) return false;
        Function neighbor{};
        if (low > 1 && (!Entry(low - 2, neighbor) || neighbor.begin >= result.begin || neighbor.end > result.begin)) return false;
        if (low < functions.Size / sizeof(Function) && (!Entry(low, neighbor) || neighbor.begin < result.end)) return false;
        return true;
    }
    bool NameAt(std::uint64_t rva, std::string_view name) const
    {
        std::array<char, 128> text{};
        return !name.empty() && name.size() < text.size() &&
            In(rva, name.size() + 1, IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_WRITE) &&
            Read(rva, text.data(), name.size() + 1) && text[name.size()] == '\0' &&
            std::memcmp(text.data(), name.data(), name.size()) == 0;
    }
};
}

inline bool IsFunctionEntry(std::uint64_t moduleBase, std::uint32_t imageSize, std::uint64_t candidateRva)
{
    try
    {
        detail::Image image;
        detail::Function function{};
        return image.Open(moduleBase, imageSize) && image.Containing(candidateRva, function) && function.begin == candidateRva;
    }
    catch (...) { return false; }
}

// A bounded RIP-relative LEA to the exact engine profiler label adds role
// evidence to an independently checked code signature. It is not a substitute
// for that signature, the caller ABI or the live natural-query controls.
inline bool VerifyRole(std::uint64_t moduleBase, std::uint32_t imageSize, std::uint64_t candidateRva,
    std::string_view name, bool exactEntry)
{
    try
    {
        detail::Image image;
        detail::Function function{};
        if (name.empty() || name.size() >= 128 || !image.Open(moduleBase, imageSize) ||
            !image.Containing(candidateRva, function) || (exactEntry && function.begin != candidateRva)) return false;
        const auto count = exactEntry ? std::min<std::uint32_t>(512, function.end - function.begin) : function.end - function.begin;
        std::vector<unsigned char> code(count);
        if (!image.Read(function.begin, code.data(), code.size())) return false;
        unsigned matches = 0;
        for (size_t at = 0; at + 7 <= code.size(); ++at)
        {
            if (code[at] < 0x48 || code[at] > 0x4F || code[at + 1] != 0x8D || (code[at + 2] & 0xC7) != 0x05) continue;
            std::int32_t relative{};
            std::memcpy(&relative, code.data() + at + 3, sizeof(relative));
            const auto target = static_cast<std::int64_t>(function.begin) + static_cast<std::int64_t>(at) + 7 + relative;
            if (target > 0 && image.NameAt(static_cast<std::uint64_t>(target), name) && ++matches > 1) return false;
        }
        return matches == 1;
    }
    catch (...) { return false; }
}
}

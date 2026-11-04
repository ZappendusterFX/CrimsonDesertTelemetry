#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace cdt::simulation_tick_probe::scan
{
// World Builder movement-pump entry, observed uniquely in preserved builds
// 25477059/26361232/26423880. Exact prefix is required separately from the mask.
inline constexpr std::array<std::uint8_t, 28> Prefix{
    0x48,0x8B,0xC4,0x4C,0x89,0x48,0x20,0x48,0x89,0x50,0x10,0x55,0x41,0x56,
    0x48,0x8D,0xA8,0x38,0xFB,0xFF,0xFF,0x48,0x81,0xEC,0xB8,0x05,0x00,0x00};
inline constexpr std::size_t PatternBytes = 14, ChunkBytes = 65536;
struct Section { std::uint32_t rva{}, bytes{}; bool executable{}; };
enum class Result { Unique, Missing, Ambiguous, InvalidImage, Unreadable, PrefixMismatch };
struct Match { Result result{Result::Missing}; std::uint32_t rva{}, count{}; };

// Reader(rva, span) is read-only. No fallback address and no incomplete scan may
// establish uniqueness. Overlap covers a signature crossing a chunk boundary.
template<class Reader>
Match Find(std::uint32_t imageBytes, std::span<const Section> sections, Reader&& read)
{
    Match found;
    if (imageBytes < 4096 || imageBytes > 0x40000000 || sections.empty() || sections.size() > 96)
        return {Result::InvalidImage};
    std::uint64_t total = 0;
    for (const auto& section : sections)
    {
        if (!section.rva || section.rva >= imageBytes || section.bytes > imageBytes - section.rva)
            return {Result::InvalidImage};
        if (section.executable && (total += section.bytes) > imageBytes) return {Result::InvalidImage};
    }
    std::array<std::uint8_t, ChunkBytes + PatternBytes - 1> bytes{};
    for (const auto& section : sections)
    {
        if (!section.executable || section.bytes < PatternBytes) continue;
        for (std::uint32_t offset = 0; offset < section.bytes;)
        {
            const auto remaining = section.bytes - offset;
            const auto length = std::min<std::size_t>(bytes.size(), remaining);
            if (!read(section.rva + offset, std::span(bytes).first(length))) return {Result::Unreadable};
            const auto candidates = std::min<std::size_t>(ChunkBytes,
                length >= PatternBytes ? length - PatternBytes + 1 : 0);
            for (std::size_t i = 0; i < candidates; ++i)
            {
                if (bytes[i] != Prefix[0]) continue;
                bool match = true;
                for (std::size_t n = 1; n < PatternBytes; ++n)
                    if (n != 6 && n != 10 && bytes[i + n] != Prefix[n]) { match = false; break; }
                if (!match) continue;
                found.rva = section.rva + offset + static_cast<std::uint32_t>(i);
                if (++found.count != 1) return {Result::Ambiguous, 0, found.count};
            }
            offset += static_cast<std::uint32_t>(std::min<std::size_t>(ChunkBytes, remaining));
        }
    }
    if (!found.count) return found;
    if (!std::any_of(sections.begin(), sections.end(), [&](const Section& s) {
        return s.executable && found.rva >= s.rva && found.rva - s.rva <= s.bytes &&
            Prefix.size() <= s.bytes - (found.rva - s.rva);
    })) return {Result::PrefixMismatch, found.rva, found.count};
    std::array<std::uint8_t, Prefix.size()> head{};
    if (found.rva > imageBytes - head.size() || !read(found.rva, std::span(head)))
        return {Result::Unreadable};
    found.result = head == Prefix ? Result::Unique : Result::PrefixMismatch;
    return found;
}
}

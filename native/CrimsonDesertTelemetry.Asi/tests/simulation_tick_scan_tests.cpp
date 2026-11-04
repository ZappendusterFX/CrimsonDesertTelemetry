#include "simulation_tick_scan.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace cdt::simulation_tick_probe::scan;
void Require(bool value, const char* description)
{
    if (!value) throw std::runtime_error(description);
}
int main()
{
    try
    {
        std::vector<std::uint8_t> image(200000);
        std::vector<Section> sections{{4096, 140000, true}, {150000, 40000, false}};
        const auto put = [&](std::size_t at) { std::memcpy(image.data() + at, Prefix.data(), Prefix.size()); };
        const auto read = [&](std::uint32_t at, std::span<std::uint8_t> bytes) {
            if (at > image.size() || bytes.size() > image.size() - at) return false;
            std::memcpy(bytes.data(), image.data() + at, bytes.size()); return true;
        };
        const auto find = [&] { return Find(static_cast<std::uint32_t>(image.size()), sections, read); };
        put(150100); // Identical data bytes must not count as executable code.
        Require(find().result == Result::Missing, "data signature counted as code");
        const auto across = 4096u + static_cast<std::uint32_t>(ChunkBytes) - 4u;
        put(across);
        auto match = find();
        Require(match.result == Result::Unique && match.rva == across && match.count == 1,
            "chunk-boundary match missed or counted twice");
        image[across + 6] ^= 1;
        Require(find().result == Result::PrefixMismatch, "masked-byte change accepted without exact prefix");
        image[across + 6] ^= 1;
        put(80000);
        Require(find().result == Result::Ambiguous, "ambiguous signature accepted");
        std::fill_n(image.data() + 80000, Prefix.size(), std::uint8_t{0});
        Require(Find(static_cast<std::uint32_t>(image.size()), sections,
            [&](std::uint32_t at, std::span<std::uint8_t> bytes) { return at < 70000 && read(at, bytes); }).result == Result::Unreadable,
            "partial scan accepted as unique");
        sections[0].bytes = 200000;
        Require(find().result == Result::InvalidImage, "section escaping image accepted");
        sections[0] = {4096, 140000, true};
        image[across] = 0xE9; // Foreign detour/prologue has no fallback RVA.
        Require(find().result == Result::Missing, "foreign hook accepted through fallback");
        std::cout << "PASS executable-only uniqueness, chunk overlap, exact prefix, unreadable rejection, PE bounds and foreign-hook refusal.\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

#include <windows.h>
#include "build_guard.h"
#include "native_contract.generated.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>

namespace
{
using Digest = std::array<unsigned char, 32>;

// Local SHA-256 implementation: VerifyExecutable also runs under DllMain for
// early debug capture, where opening a dynamically loaded CNG provider is unsafe.
// All hashing state is stack storage; no provider, heap or module loading.
class Sha256
{
    std::array<std::uint32_t, 8> state{
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<unsigned char, 64> pending{};
    std::uint64_t byteCount{};
    size_t used{};

    void Block(const unsigned char* bytes)
    {
        constexpr std::array<std::uint32_t, 64> constants{
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::array<std::uint32_t, 64> words{};
        for (size_t i = 0; i < 16; ++i)
            words[i] = (std::uint32_t(bytes[i * 4]) << 24) | (std::uint32_t(bytes[i * 4 + 1]) << 16) |
                (std::uint32_t(bytes[i * 4 + 2]) << 8) | std::uint32_t(bytes[i * 4 + 3]);
        for (size_t i = 16; i < words.size(); ++i)
        {
            const auto x = words[i - 15], y = words[i - 2];
            const auto small0 = std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
            const auto small1 = std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10);
            words[i] = words[i - 16] + small0 + words[i - 7] + small1;
        }
        auto a = state[0], b = state[1], c = state[2], d = state[3];
        auto e = state[4], f = state[5], g = state[6], h = state[7];
        for (size_t i = 0; i < words.size(); ++i)
        {
            const auto large1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const auto choose = (e & f) ^ (~e & g);
            const auto first = h + large1 + choose + constants[i] + words[i];
            const auto large0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto second = large0 + majority;
            h = g; g = f; f = e; e = d + first;
            d = c; c = b; b = a; a = first + second;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

public:
    void Update(const unsigned char* bytes, size_t length)
    {
        byteCount += length;
        while (length != 0)
        {
            if (used == 0 && length >= pending.size())
            {
                Block(bytes);
                bytes += pending.size(); length -= pending.size();
                continue;
            }
            const auto count = std::min(length, pending.size() - used);
            std::memcpy(pending.data() + used, bytes, count);
            used += count; bytes += count; length -= count;
            if (used == pending.size()) { Block(pending.data()); used = 0; }
        }
    }

    Digest Finish()
    {
        const auto bits = byteCount * 8;
        pending[used++] = 0x80;
        if (used > 56)
        {
            std::fill(pending.begin() + used, pending.end(), static_cast<unsigned char>(0));
            Block(pending.data()); used = 0;
        }
        std::fill(pending.begin() + used, pending.begin() + 56, static_cast<unsigned char>(0));
        for (size_t i = 0; i < 8; ++i)
            pending[63 - i] = static_cast<unsigned char>(bits >> (i * 8));
        Block(pending.data());
        Digest result{};
        for (size_t i = 0; i < state.size(); ++i)
            for (size_t j = 0; j < 4; ++j)
                result[i * 4 + j] = static_cast<unsigned char>(state[i] >> (24 - j * 8));
        return result;
    }
};

bool HashExecutableFile(const wchar_t* path, Digest& digest)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    Sha256 hash;
    std::array<unsigned char, 65536> buffer{};
    DWORD bytes{};
    bool ok = true;
    for (;;)
    {
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr)) { ok = false; break; }
        if (!bytes) break;
        hash.Update(buffer.data(), bytes);
    }
    if (ok) digest = hash.Finish();
    CloseHandle(file);
    return ok;
}
}

namespace cdt::instruments
{
bool VerifyExecutable()
{
    Digest digest{};
    return ReadExecutableHash(digest) && digest == native_contract::ExecutableSha256;
}
bool ReadExecutableHash(Digest& digest)
{
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return false;
    return HashExecutableFile(path.data(), digest);
}

#ifdef CDT_BUILD_GUARD_TEST
namespace test
{
Digest HashBytes(const unsigned char* bytes, size_t length, size_t chunkBytes)
{
    Sha256 hash;
    if (chunkBytes == 0) chunkBytes = length;
    while (length != 0)
    {
        const auto count = std::min(length, chunkBytes);
        hash.Update(bytes, count);
        bytes += count; length -= count;
    }
    return hash.Finish();
}
bool HashFile(const wchar_t* path, Digest& digest) { return HashExecutableFile(path, digest); }
}
#endif
}

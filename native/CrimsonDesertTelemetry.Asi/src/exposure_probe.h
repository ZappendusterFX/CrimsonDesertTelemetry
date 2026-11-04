#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "native_contract.generated.h"

namespace cdt::render
{
inline constexpr bool CurrentRendererLayout = native_contract::BuildId == "26361232";
inline constexpr uint32_t RendererSkyOffset = native_contract::support::Available ? native_contract::support::RendererSkyOffset : CurrentRendererLayout ? 0x690 : 0x668;
inline constexpr uint32_t RendererExposureOffset = native_contract::support::Available ? native_contract::support::RendererExposureOffset : CurrentRendererLayout ? 0x6B8 : 0x690;
// Private diagnostic appendix. This is the engine's GPU-derived CPU cache,
// NOT a GPU exposure sample fenced with ambient. Its source-frame age is unknown.
// +118 contains pointers: copy ONLY +D8..+117 (64 bytes), never all 80 CBV bytes.
struct ExposureCacheContext
{
    uint32_t magic = 0x58455443, version = 1, bytes = 224, flags = 0;
    uint64_t beginTick{}, endTick{}, renderer{}, owner{}, outer{}, inner{}, resource{}, readbackOwner{};
    std::array<uint8_t, 64> before{}, after{};
    uint32_t stride{}, count{}, innerMode{}, reserved{};
};
static_assert(sizeof(ExposureCacheContext) == 224);
enum ExposureCacheFlag : uint32_t
{
    ExposureBefore = 1, ExposureAfter = 2, ExposureSameIdentity = 4,
    ExposureSameBytes = 8, ExposurePositiveScalar = 16
};

struct ExposureCacheSample
{
    uint64_t renderer{}, owner{}, outer{}, inner{}, resource{}, readbackOwner{};
    std::array<uint8_t, 64> data{};
    uint32_t stride{}, count{}, innerMode{};
};

inline bool ExposurePointer(uint64_t p)
{ return p >= 0x10000 && p <= 0x00007FFFFFFF0000ULL; }

// Injectable bounded reader permits synthetic short-read and changing-chain
// controls. Production uses SafeRead only; no game calls, Map, locks or writes.
template<class Reader>
bool ReadExposureCache(uint64_t sky, ExposureCacheSample& result, Reader read)
{
    result = {};
    ExposureCacheSample s{};
    const auto pointer = [&](uint64_t address, uint64_t& value) {
        return read(address, &value, sizeof(value)) && ExposurePointer(value);
    };
    uint64_t back{}, check{}; uint8_t mode{};
    if (!ExposurePointer(sky) || !pointer(sky + 0x10, s.renderer) ||
        !pointer(s.renderer + RendererSkyOffset, back) || back != sky ||
        !pointer(s.renderer + RendererExposureOffset, s.owner) ||
        !pointer(s.owner + 0x10, back) || back != s.renderer ||
        !pointer(s.owner + 0xC0, s.outer) || !pointer(s.outer + 0x30, s.inner) ||
        !pointer(s.inner + 0x168, s.resource) || !pointer(s.owner + 0xD0, s.readbackOwner) ||
        !read(s.inner + 0xC0, &s.stride, 4) || !read(s.inner + 0xC4, &s.count, 4) ||
        s.stride != 4 || s.count < 20 || s.count > 16384 ||
        !read(s.inner + 0xAE, &mode, 1) || mode != 2 ||
        !read(s.owner + 0xD8, s.data.data(), s.data.size())) return false;
    s.innerMode = mode;
    // Detect lifetime/bank changes while chasing the known chain. This is a
    // double-read control, not an engine-owned synchronization guarantee.
    if (!pointer(sky + 0x10, check) || check != s.renderer ||
        !pointer(s.renderer + RendererSkyOffset, check) || check != sky ||
        !pointer(s.renderer + RendererExposureOffset, check) || check != s.owner ||
        !pointer(s.owner + 0x10, check) || check != s.renderer ||
        !pointer(s.owner + 0xC0, check) || check != s.outer ||
        !pointer(s.outer + 0x30, check) || check != s.inner ||
        !pointer(s.inner + 0x168, check) || check != s.resource ||
        !pointer(s.owner + 0xD0, check) || check != s.readbackOwner) return false;
    result = s;
    return true;
}

inline void BeginExposureCache(ExposureCacheContext& c, const ExposureCacheSample& s, bool valid, uint64_t tick)
{
    c = {}; c.beginTick = tick;
    if (!valid) return;
    c.flags = ExposureBefore; c.renderer = s.renderer; c.owner = s.owner;
    c.outer = s.outer; c.inner = s.inner; c.resource = s.resource;
    c.readbackOwner = s.readbackOwner; c.before = s.data;
    c.stride = s.stride; c.count = s.count; c.innerMode = s.innerMode;
}
inline void EndExposureCache(ExposureCacheContext& c, const ExposureCacheSample& s, bool valid, uint64_t tick)
{
    c.endTick = tick;
    if (!valid) return;
    c.flags |= ExposureAfter; c.after = s.data;
    if (!(c.flags & ExposureBefore) || c.renderer != s.renderer || c.owner != s.owner ||
        c.outer != s.outer || c.inner != s.inner || c.resource != s.resource ||
        c.readbackOwner != s.readbackOwner || c.stride != s.stride || c.count != s.count ||
        c.innerMode != s.innerMode) return;
    c.flags |= ExposureSameIdentity;
    if (c.before != c.after) return;
    c.flags |= ExposureSameBytes;
    float value{}; memcpy(&value, c.before.data(), sizeof(value));
    // Other lanes include packed integers/NaN bit patterns. Preserve them;
    // they are not grounds to reject this measured exposure0.x scalar.
    if (std::isfinite(value) && value > 0) c.flags |= ExposurePositiveScalar;
}
}

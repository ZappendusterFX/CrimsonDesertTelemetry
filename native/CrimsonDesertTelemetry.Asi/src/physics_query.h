#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cdt::physics
{
using Vec3 = std::array<float, 3>;
// Continuous mode shares the HUD's configurable 1..500gu player radius.
// Camera offset and fan endpoints require a slightly longer segment allowance.
inline constexpr float MaximumVisibilityRadius = 500.f;
inline constexpr float MaximumVisibilityCameraOffset = 12.f;
inline constexpr float MaximumVisibilityRayLength = 513.f;
inline bool Finite(const Vec3& v)
{
    for (float x : v) if (!std::isfinite(x) || std::abs(x) > 1000000.f) return false;
    return true;
}
// Same player-near selection as WB. Truncate toward zero, NOT floor, for negative
// world coordinates. This only selects an observation; it is not a visibility test.
inline bool NearPlayer(const Vec3& queryStart, const Vec3& player)
{
    if (!Finite(queryStart) || !Finite(player) || (std::abs(player[0]) < 1 && std::abs(player[2]) < 1)) return false;
    const float x = player[0] - static_cast<float>(static_cast<int>(player[0] * .001f)) * 1000.f;
    const float z = player[2] - static_cast<float>(static_cast<int>(player[2] * .001f)) * 1000.f;
    const float dx = queryStart[0] - x, dz = queryStart[2] - z;
    return dx * dx + dz * dz <= 9.f && std::abs(queryStart[1] - player[1]) <= 3.f;
}
template<class T, std::size_t N> T Read(const std::array<std::uint8_t, N>& b, std::size_t offset)
{
    T value{};
    if (offset <= N && sizeof(T) <= N - offset) std::memcpy(&value, b.data() + offset, sizeof value);
    return value;
}
template<class T, std::size_t N> void Write(std::array<std::uint8_t, N>& b, std::size_t offset, const T& value)
{
    if (offset <= N && sizeof(T) <= N - offset) std::memcpy(b.data() + offset, &value, sizeof value);
}
inline float Distance(const Vec3& a, const Vec3& b)
{
    float squared = 0;
    for (unsigned i = 0; i < 3; ++i) squared += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(squared);
}
// Diagnostic sensitivity stencil, NOT an emitter radius/area or visibility %.
// Center, +/-right/up at 0.05 and 0.15 gu, in the plane normal to the sightline.
inline bool RayFanTargets(const Vec3& start, const Vec3& center, std::array<Vec3, 9>& targets,
    float maximumDistance = 49.5f)
{
    if (!Finite(start) || !Finite(center) || !std::isfinite(maximumDistance) ||
        maximumDistance < .25f || maximumDistance > MaximumVisibilityRadius + MaximumVisibilityCameraOffset) return false;
    const float distance = Distance(start, center);
    if (distance < .25f || distance > maximumDistance) return false;
    Vec3 forward{};
    for (unsigned i = 0; i < 3; ++i) forward[i] = (center[i] - start[i]) / distance;
    Vec3 right{forward[2], 0, -forward[0]};
    float length = Distance(right, Vec3{});
    if (length < .01f) { right = {0, -forward[2], forward[1]}; length = Distance(right, Vec3{}); }
    for (auto& v : right) v /= length;
    const Vec3 up{forward[1]*right[2]-forward[2]*right[1],
        forward[2]*right[0]-forward[0]*right[2], forward[0]*right[1]-forward[1]*right[0]};
    targets[0] = center;
    size_t index = 1;
    for (float radius : {.05f, .15f})
        for (const auto& axis : {right, up})
            for (float sign : {1.f, -1.f})
            {
                for (unsigned i = 0; i < 3; ++i) targets[index][i] = center[i] + radius*sign*axis[i];
                ++index;
            }
    return true;
}
// Native ray captures (physics.3): double origin/delta, float inverse/length.
// Keep the zero-axis convention refused for now despite one FLT_MAX observation.
template<std::size_t N> bool SetRaySegment(std::array<std::uint8_t, N>& q,
    const Vec3& start, const Vec3& end, const Vec3& player, float maximumDistance = 50.f)
{
    if constexpr (N < 0x90) return false;
    if (!Finite(start) || !Finite(end) || !Finite(player) || Distance(start, player) > 20.f ||
        !std::isfinite(maximumDistance) || maximumDistance < .05f || maximumDistance > MaximumVisibilityRayLength) return false;
    std::array<double, 3> local{}, delta{};
    Vec3 inverse{};
    double squared = 0;
    for (unsigned i = 0; i < 3; ++i)
    {
        local[i] = start[i]; delta[i] = static_cast<double>(end[i]) - start[i];
        if (std::abs(delta[i]) < .0001) return false;
        inverse[i] = static_cast<float>(1.0 / delta[i]); squared += delta[i] * delta[i];
    }
    const double length = std::sqrt(squared);
    if (length < .05 || length > maximumDistance) return false;
    local[0] -= static_cast<int>(player[0] * .001f) * 1000.0;
    local[2] -= static_cast<int>(player[2] * .001f) * 1000.0;
    Write(q, 0x40, local); Write(q, 0x58, 0.0);
    Write(q, 0x60, delta); Write(q, 0x78, 1.0);
    Write(q, 0x80, inverse); Write(q, 0x8C, static_cast<float>(length));
    return true;
}
// Live captures establish reciprocal delta at +0x50 and length at +0x5C.
// Zero-component convention is unverified: reject near-axis cases instead of
// inventing infinity/FLT_MAX behavior. This remains a private sphere sweep.
template<std::size_t N> bool SetSegment(std::array<std::uint8_t, N>& q,
    const Vec3& start, const Vec3& end, const Vec3& player)
{
    if (!Finite(start) || !Finite(end) || !Finite(player)) return false;
    const float length = Distance(start, end);
    if (N < 0x60 || length < .05f || length > 50.f || Distance(start, player) > 20.f) return false;
    Vec3 delta{}, inverse{}, local = start;
    for (unsigned i = 0; i < 3; ++i)
    {
        delta[i] = end[i] - start[i];
        if (std::abs(delta[i]) < .0001f) return false;
        inverse[i] = 1.f / delta[i];
    }
    local[0] -= static_cast<float>(static_cast<int>(player[0] * .001f)) * 1000.f;
    local[2] -= static_cast<float>(static_cast<int>(player[2] * .001f)) * 1000.f;
    Write(q, 0x30, local); Write(q, 0x3C, 0.f);
    Write(q, 0x40, delta); Write(q, 0x4C, 1.f);
    Write(q, 0x50, inverse); Write(q, 0x5C, length);
    return true;
}
template<std::size_t N> bool PlausibleResult(const std::array<std::uint8_t, N>& c)
{
    const auto count = Read<std::uint32_t>(c, 0xC);
    const auto fraction = Read<double>(c, 0x10);
    return count <= 64 && std::isfinite(fraction) && fraction >= -1 && fraction <= 1.00001 &&
        (count == 0 || Finite(Read<Vec3>(c, 0x80)));
}
template<std::size_t N, std::size_t M> bool SameResult(const std::array<std::uint8_t, N>& a,
    const std::array<std::uint8_t, M>& b)
{
    return PlausibleResult(a) && PlausibleResult(b) && Read<std::uint32_t>(a, 0xC) == Read<std::uint32_t>(b, 0xC) &&
        std::abs(Read<double>(a, 0x10) - Read<double>(b, 0x10)) <= .00001 &&
        (Read<std::uint32_t>(a, 0xC) == 0 || Distance(Read<Vec3>(a, 0x80), Read<Vec3>(b, 0x80)) < .001f);
}

// The continuous classifier uses the same contact consistency check as the
// offline ray decoder; malformed/penetrating results never become "blocked".
template<std::size_t N> bool ValidRaySample(const std::array<std::uint8_t, N>& c,
    const Vec3& start, const Vec3& end, const Vec3& player)
{
    const auto count = Read<std::uint32_t>(c, 0xC);
    const auto fraction = Read<double>(c, 0x10);
    if (!std::isfinite(fraction) || count > 1 || fraction < 0 || fraction > 1.00001) return false;
    if (!count) return std::abs(fraction - 1) < .00001;
    const auto normal = Read<Vec3>(c, 0x80);
    if (!Finite(normal) || std::abs(Distance(normal, Vec3{}) - 1) > .01f) return false;
    auto contact = Read<std::array<double,3>>(c, 0x90);
    contact[0] += std::trunc(static_cast<double>(player[0])/1000)*1000;
    contact[2] += std::trunc(static_cast<double>(player[2])/1000)*1000;
    double error = 0;
    for (unsigned i=0;i<3;++i)
    {
        const double d = contact[i] - (start[i] + fraction*(static_cast<double>(end[i])-start[i]));
        if (!std::isfinite(d)) return false;
        error += d*d;
    }
    return error < .000025; // 0.005gu, same as offline decoder
}
template<std::size_t N> struct alignas(16) GuardedBytes
{
    static constexpr std::array<std::uint64_t, 2> Tag{0xDFC521184390B7A6ULL, 0x84B176A204EF95C3ULL};
    std::array<std::uint64_t, 2> before{Tag};
    alignas(16) std::array<std::uint8_t, N> data{};
    std::array<std::uint64_t, 2> after{Tag};
    bool Intact() const { return before == Tag && after == Tag; }
};
struct NativeCopy
{
    GuardedBytes<0x200> query;
    GuardedBytes<0x100> transform;
    GuardedBytes<0x300> collector;
    GuardedBytes<0x200> shape;
    bool Intact() const { return query.Intact() && transform.Intact() && collector.Intact() && shape.Intact(); }
};
struct NativeRayCopy
{
    GuardedBytes<0x100> query;
    GuardedBytes<0x300> collector;
    bool Intact() const { return query.Intact() && collector.Intact(); }
};
struct alignas(16) QueryCopy
{
    alignas(16) std::array<std::uint8_t, 0x200> query{};
    alignas(16) std::array<std::uint8_t, 0x100> transform{};
    alignas(16) std::array<std::uint8_t, 0x200> collector{};
    alignas(16) std::array<std::uint8_t, 0x200> shape{};
};
}

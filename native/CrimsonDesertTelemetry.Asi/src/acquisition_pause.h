#pragma once
#include <atomic>
#include <cstdint>

namespace cdt::acquisition_pause
{
// One worker changes the phase; render/physics callbacks only read it. Packing
// phase and generation in one atomic prevents accepting a pre-pause transaction
// after a complete pause/resume cycle. GPU work still owns its ordinary fences.
class Gate
{
public:
    bool Paused() const noexcept { return (phase_.load(std::memory_order_acquire) & 1) != 0; }
    std::uint64_t Epoch() const noexcept { return phase_.load(std::memory_order_acquire) >> 1; }
    bool Current(std::uint64_t epoch) const noexcept
    { return epoch != 0 && phase_.load(std::memory_order_acquire) == (epoch << 1); }
    std::uint64_t ResumedAt() const noexcept { return resumedAt_.load(std::memory_order_acquire); }
    void Update(bool paused, std::uint64_t now) noexcept
    {
        const auto previous = phase_.load(std::memory_order_relaxed);
        if (((previous & 1) != 0) == paused) return;
        if (!paused) resumedAt_.store(now, std::memory_order_release);
        phase_.store((((previous >> 1) + 1) << 1) | (paused ? 1u : 0u), std::memory_order_release);
    }
private:
    std::atomic<std::uint64_t> phase_{2}; // epoch 1, normal acquisition until known paused
    std::atomic<std::uint64_t> resumedAt_{};
};
inline Gate gate;
}

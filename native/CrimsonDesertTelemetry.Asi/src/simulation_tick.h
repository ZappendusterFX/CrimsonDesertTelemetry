#pragma once
#include <chrono>
#include <cstdint>
#include <optional>

namespace cdt::simulation_tick
{
enum class State { UnknownUntilProgress, Running, PausedAfter500ms };

// Pure counter-liveness policy. A missing tick is an observed simulation stall,
// not proof of a particular game menu. The owner supplies an independent game
// simulation counter and steady timestamps, and serializes Observe/Reset calls.
// Rendering/capture counters must not feed this monitor or pause becomes circular.
class Monitor
{
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto PauseAfter = std::chrono::milliseconds(500);

    State Observe(std::uint64_t counter, Clock::time_point now) noexcept
    {
        if (!initialized_ || now < lastSample_ || counter < counter_)
        {
            initialized_ = true;
            progressed_ = false;
            counter_ = counter;
            lastSample_ = lastProgress_ = now;
            return State::UnknownUntilProgress;
        }

        lastSample_ = now;
        if (counter != counter_)
        {
            counter_ = counter;
            lastProgress_ = now;
            progressed_ = true;
            return State::Running;
        }

        if (!progressed_) return State::UnknownUntilProgress;
        return now - lastProgress_ >= PauseAfter ? State::PausedAfter500ms : State::Running;
    }

    std::optional<Clock::time_point> LastProgress() const noexcept
    {
        return progressed_ ? std::optional<Clock::time_point>(lastProgress_) : std::nullopt;
    }

    void Reset() noexcept { *this = Monitor{}; }

private:
    bool initialized_{};
    bool progressed_{};
    std::uint64_t counter_{};
    Clock::time_point lastSample_{};
    Clock::time_point lastProgress_{};
};
}

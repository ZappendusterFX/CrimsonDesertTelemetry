#include "../src/simulation_tick.h"
#include "../src/acquisition_pause.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using cdt::simulation_tick::Monitor;
using cdt::simulation_tick::State;
using namespace std::chrono_literals;
const auto start = Monitor::Clock::time_point{} + 1h;

void Require(State actual, State expected, const char* message)
{
    if (actual != expected) throw std::runtime_error(message);
}

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void StartupNeedsProgress()
{
    Monitor monitor;
    Require(!monitor.LastProgress(), "New monitor has no trusted progress timestamp");
    Require(monitor.Observe(42, start), State::UnknownUntilProgress, "First nonzero counter is not proof of progress");
    Require(monitor.Observe(42, start + 500ms), State::UnknownUntilProgress, "Startup must not time out to paused");
    Require(monitor.Observe(42, start + 24h), State::UnknownUntilProgress, "Constant startup counter stays unknown");
    Require(!monitor.LastProgress(), "Constant startup counter cannot provide a progress age");
    Require(monitor.Observe(43, start + 24h), State::Running, "Actual progress establishes running immediately");
}

void BoundaryAndResume()
{
    Monitor monitor;
    Require(monitor.Observe(0, start), State::UnknownUntilProgress, "Zero startup is unknown");
    Require(monitor.Observe(1, start + 10ms), State::Running, "First advance starts running");
    Require(monitor.LastProgress() == start + 10ms, "Timestamp records observed progress");
    for (int step = 1; step < 5; ++step)
        Require(monitor.Observe(1, start + 10ms + step * 100ms), State::Running, "Polling does not change the progress time");
    Require(monitor.Observe(1, start + 510ms - 1ns), State::Running, "Still running immediately before threshold");
    Require(monitor.Observe(1, start + 510ms), State::PausedAfter500ms, "Paused exactly at 500ms without progress");
    Require(monitor.LastProgress() == start + 10ms, "Duplicate polling cannot refresh the published progress age");
    Require(monitor.Observe(1, start + 2s), State::PausedAfter500ms, "Unchanged paused counter stays paused");
    Require(monitor.Observe(8, start + 2s), State::Running, "Counter advance resumes immediately, including skipped ticks");
    Require(monitor.LastProgress() == start + 2s, "Resume updates progress timestamp immediately");
    Require(monitor.Observe(8, start + 2500ms), State::PausedAfter500ms, "Resumed progress establishes a new timeout");
}

void CounterResetAndWrap()
{
    Monitor monitor;
    monitor.Observe(100, start);
    monitor.Observe(101, start + 1ms);
    Require(monitor.Observe(0, start + 2ms), State::UnknownUntilProgress, "Counter reset invalidates prior running state");
    Require(!monitor.LastProgress(), "Counter reset invalidates previous progress age");
    Require(monitor.Observe(0, start + 2s), State::UnknownUntilProgress, "Reset requires new progress, not elapsed time");
    Require(monitor.Observe(1, start + 2s), State::Running, "Progress after reset is usable");
    monitor.Observe(1, start + 2500ms);
    Require(monitor.Observe(0, start + 3s), State::UnknownUntilProgress, "Reset also invalidates paused state");

    monitor.Reset();
    monitor.Observe(std::numeric_limits<std::uint64_t>::max() - 1, start);
    Require(monitor.Observe(std::numeric_limits<std::uint64_t>::max(), start + 1ms), State::Running, "Large counter can advance");
    Require(monitor.Observe(0, start + 2ms), State::UnknownUntilProgress, "Counter wrap is treated as reset without overflow");
    Require(monitor.Observe(1, start + 3ms), State::Running, "Progress after wrap recovers");
}

void ClockRollbackAndExplicitReset()
{
    Monitor monitor;
    monitor.Observe(10, start);
    monitor.Observe(11, start + 1s);
    monitor.Observe(11, start + 1200ms);
    Require(monitor.Observe(12, start + 1100ms), State::UnknownUntilProgress, "Clock rollback invalidates even simultaneous counter progress");
    Require(!monitor.LastProgress(), "Clock rollback removes the old progress timestamp");
    Require(monitor.Observe(12, start + 3s), State::UnknownUntilProgress, "Constant counter after clock rollback stays unknown");
    Require(monitor.Observe(13, start + 3s), State::Running, "New progress after rollback resumes immediately");
    monitor.Observe(13, start + 3500ms);
    Require(monitor.Observe(13, start + 3400ms), State::UnknownUntilProgress, "Clock rollback also invalidates paused state");

    monitor.Observe(14, start + 4s);
    monitor.Reset();
    Require(!monitor.LastProgress(), "Explicit reset removes the old progress timestamp");
    Require(monitor.Observe(14, start + 10s), State::UnknownUntilProgress, "Explicit lifecycle reset drops earlier progress");
    Require(monitor.Observe(14, start + 20s), State::UnknownUntilProgress, "Explicit reset cannot retain paused authorization");
    Require(monitor.Observe(15, start + 20s), State::Running, "Reset monitor can establish fresh progress");
}
}

void AcquisitionGenerations()
{
    cdt::acquisition_pause::Gate gate;
    Require(!gate.Paused() && gate.Epoch() == 1 && gate.Current(1) && gate.ResumedAt() == 0,
        "Unknown startup retains ordinary acquisition in generation one");
    const auto inFlight = gate.Epoch();
    gate.Update(true, 1000);
    Require(gate.Paused() && gate.Epoch() == 2 && !gate.Current(inFlight) && !gate.Current(2),
        "Pause rejects both old in-flight completion and newly attempted copies");
    gate.Update(true, 1600);
    Require(gate.Epoch() == 2 && gate.ResumedAt() == 0, "Repeated paused samples do not churn generations");
    const auto duringPause = gate.Epoch();
    gate.Update(false, 2000);
    Require(!gate.Paused() && gate.Epoch() == 3 && gate.ResumedAt() == 2000 && gate.Current(3) &&
        !gate.Current(inFlight) && !gate.Current(duringPause), "Resume permits only fresh-generation work");
    gate.Update(false, 2100);
    Require(gate.Epoch() == 3 && gate.ResumedAt() == 2000, "Running samples keep the exact resume boundary");
    gate.Update(true, 2500); gate.Update(false, 2600);
    Require(gate.Epoch() == 5 && !gate.Current(3) && !gate.Current(0), "Unobserved whole pause cycle cannot resurrect old work");
}

int main()
{
    try
    {
        StartupNeedsProgress();
        BoundaryAndResume();
        CounterResetAndWrap();
        ClockRollbackAndExplicitReset();
        AcquisitionGenerations();
        std::cout << "PASS simulation tick policy: startup, 500ms boundary, duplicate polling, resume, counter reset/wrap, clock rollback, lifecycle reset.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL simulation tick policy: " << error.what() << '\n';
        return 1;
    }
}

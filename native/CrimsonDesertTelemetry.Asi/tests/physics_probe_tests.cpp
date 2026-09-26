// Synthetic native observation tests; these do NOT validate game collision semantics.
#include "../src/physics_probe.cpp"
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

namespace ch { void Log(const char*, ...) {} }
namespace ch::mem
{
bool SafeRead(const void* src, void* dst, size_t n)
{
    __try { std::memcpy(dst, src, n); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
using namespace cdt::physics;
namespace
{
unsigned checks{};
void Check(bool ok, const char* name)
{
    ++checks;
    if (!ok) { std::cerr << "FAIL " << name << '\n'; std::exit(1); }
}
template<class T, std::size_t N> void Put(std::array<std::uint8_t, N>& b, size_t offset, T value)
{ std::memcpy(b.data() + offset, &value, sizeof value); }
std::array<std::uint8_t, 0x1000> image{}, worldBytes{};
QueryCopy query;
std::atomic<unsigned> calls{}, badArgs{};
std::uint64_t __fastcall Original(void* w, void* q, void* xf, void* c, void* e)
{
    ++calls;
    if (w != worldBytes.data() || q != query.query.data() || xf != query.transform.data() ||
        c != query.collector.data() || e != query.collector.data()) ++badArgs;
    // Do not mutate shared synthetic arguments in concurrent pass-through test.
    return 0x123456789ABCDEF0ULL;
}
std::uint64_t __fastcall OriginalRay(void* w, void* q, void* c)
{
    ++calls;
    if (w != worldBytes.data() || q != query.query.data() || c != query.collector.data()) ++badArgs;
    return 0xFE123456789ABCDEULL;
}
void Setup()
{
    work = {}; query = {}; calls = 0; badArgs = 0;
    replayFaulted = false; replayTransactions = 0; extraCalls = 0;
    continuousVisibility = false;
    work.mode = "observe";
    base = reinterpret_cast<std::uint64_t>(image.data());
    Put(image, 0x1F8, base + 0x100);
    Put(image, 0x100, std::uint32_t{1});
    Put(image, 0x10C, std::uint32_t{0x300});
    Put(image, 0x114, std::uint32_t{0x100});
    constexpr char name[] = ".?AVhknpSphereShape@@";
    std::memcpy(image.data() + 0x310, name, sizeof name);
    Put(query.shape, 0, base + 0x200);
    Put(query.query, 0x28, reinterpret_cast<std::uint64_t>(query.shape.data()));
    Put(query.query, 0x30, Vec3{-529.755f, 611.292f, -420.300f});
    Put(query.collector, 0, base + 0x5D13528);
    Put(query.collector, 0x20, reinterpret_cast<std::uint64_t>(query.collector.data()) + 0x30);
    Put(query.collector, 0xC, std::uint32_t{1});
    Put(query.collector, 0x10, .25);
    Put(query.collector, 0x80, Vec3{0, 1, 0});
    Put(worldBytes, 0xB70, base + 0x800);
    work.phase = Phase::Waiting;
    work.player = {-10529.755f, 611.292f, -4420.300f};
    work.issued = GetTickCount64();
    originalShape = Original; armed = true;
}
std::uint64_t Invoke()
{ return ShapeHook(worldBytes.data(), query.query.data(), query.transform.data(), query.collector.data(), query.collector.data()); }
enum class MockMode { Match, Disagree, Fault, Overwrite };
MockMode mockMode{};
unsigned rayFaultAt{}, rayExpireAt{};
std::uint64_t __fastcall MockReplay(void* w, void* q, void* xf, void* c, void* e)
{
    ++calls;
    if (w != worldBytes.data() || q == query.query.data() || xf == query.transform.data() ||
        c == query.collector.data() || c != e) ++badArgs;
    auto* bytes = static_cast<std::uint8_t*>(c);
    std::uint64_t hits{};
    std::memcpy(&hits, bytes + 0x20, sizeof hits);
    if (hits != reinterpret_cast<std::uint64_t>(c) + 0x30) ++badArgs;
    if (mockMode == MockMode::Fault) RaiseException(0xE001ABCD, 0, 0, nullptr);
    const std::uint32_t count = 1;
    const double fraction = mockMode == MockMode::Disagree ? .75 : .25;
    const Vec3 normal{0, 1, 0};
    std::memcpy(bytes + 0xC, &count, sizeof count);
    std::memcpy(bytes + 0x10, &fraction, sizeof fraction);
    std::memcpy(bytes + 0x80, &normal, sizeof normal);
    if (mockMode == MockMode::Overwrite) bytes[0x300] ^= 1; // synthetic native overrun into own guard
    return 0;
}
void SetupControl()
{
    Setup(); Invoke();
    // Fixture takes the natural snapshot then supplies its pre-call empty count.
    Put(work.snapshot.collector, 0xC, std::uint32_t{0});
    work.mode = "replay";
    work.caller = base + 0x32554FA;
    work.stackLow = reinterpret_cast<std::uint64_t>(&query);
    work.stackHigh = work.stackLow + sizeof query;
    calls = 0; mockMode = MockMode::Match;
    originalShape = MockReplay;
}
unsigned rayBatchExpireAt{};
std::uint64_t __fastcall MockRayReplay(void* w, void* q, void* c)
{
    // Reuse mock collector behavior, with no transform or fifth argument in the
    // actual ray ABI. Check that query cloning stopped before the old collector.
    auto* bytes = static_cast<const std::uint8_t*>(q);
    for (size_t i = 0xA0; i < 0x100; ++i) if (bytes[i]) ++badArgs;
    if (rayFaultAt && calls + 1 == rayFaultAt) mockMode = MockMode::Fault;
    if (rayExpireAt && calls + 1 == rayExpireAt) work.captured = GetTickCount64() - 101;
    if (rayBatchExpireAt && calls + 1 == rayBatchExpireAt) visibilityDeadline = std::chrono::steady_clock::now();
    const auto result = MockReplay(w, q, nullptr, c, c);
    std::array<double,3> start{}, delta{}, contact{};
    std::memcpy(start.data(), bytes+0x40, sizeof start); std::memcpy(delta.data(), bytes+0x60, sizeof delta);
    for(unsigned i=0;i<3;++i) contact[i]=start[i]+delta[i]*.25;
    std::memcpy(static_cast<std::uint8_t*>(c)+0x90,contact.data(),sizeof contact);
    return result;
}
void SetupRayControl()
{
    Setup(); originalRay = OriginalRay; work.mode = "rayobserve";
    Put(query.query, 0x14, VisibilityRayProfile);
    Put(query.query, 0x40, std::array<double, 3>{-529.755, 615.7, -420.3});
    // Simulate an overlapping observer window, which must NOT be cloned.
    std::fill(query.query.begin() + 0xA0, query.query.begin() + 0x100, std::uint8_t{0xCC});
    RayHook(worldBytes.data(), query.query.data(), query.collector.data());
    Put(work.snapshot.collector, 0xC, std::uint32_t{0});
    work.mode = "rayreplay"; work.caller = base + 0x32551AF;
    work.stackLow = reinterpret_cast<std::uint64_t>(&query);
    work.stackHigh = work.stackLow + sizeof query;
    calls = 0; rayFaultAt = 0; rayExpireAt = 0; rayBatchExpireAt = 0; mockMode = MockMode::Match; originalRay = MockRayReplay;
}
}
int main()
{
    Check(NearPlayer({-529.75f, 611, -420.3f}, {-10529.75f, 611, -4420.3f}), "negative tile truncation");
    Check(NearPlayer({529.75f, 611, 420.3f}, {10529.75f, 611, 4420.3f}), "positive tile");
    Check(!NearPlayer({470.25f, 611, 579.7f}, {-10529.75f, 611, -4420.3f}), "floor convention rejected");
    Check(NearPlayer({3, 6, 0}, {1000, 3, 1000}), "independent horizontal and vertical bounds");
    Check(!NearPlayer({3.01f, 6, 0}, {1000, 3, 1000}), "horizontal bound");
    Check(!NearPlayer({0, 6.01f, 0}, {1000, 3, 1000}), "vertical bound");
    Check(!NearPlayer({0, 1000, 0}, {0, 1000, 0}), "loading placeholder");
    Check(!Finite({std::numeric_limits<float>::quiet_NaN(), 0, 0}), "NaN rejected");
    Check(!Finite({0, std::numeric_limits<float>::infinity(), 0}), "infinity rejected");
    Check(!Finite({1000001.f, 0, 0}), "coordinate bound");
    std::array<std::uint8_t, 8> bytes{1,2,3,4,5,6,7,8};
    Check(Read<std::uint32_t>(bytes, 4) == 0x08070605, "bounded unaligned read");
    Check(Read<std::uint64_t>(bytes, 1) == 0 && Read<std::uint64_t>(bytes, SIZE_MAX) == 0, "out of bounds read");
    Setup();
    Check(Invoke() == 0x123456789ABCDEF0ULL, "original return preserved");
    Check(calls == 1 && badArgs == 0, "exactly one original call, five arguments preserved");
    Check(work.phase == Phase::Done && work.copied && work.afterCopied, "capture completed");
    Check(!armed && work.attempts == 1 && work.thread == GetCurrentThreadId(), "one-shot original thread");
    Check(work.snapshot.query == query.query && work.collectorAfter == query.collector, "owned exact snapshots");
    Check(Read<double>(work.collectorAfter, 0x10) == .25, "raw fraction preserved");
    Setup();
    Put(query.collector, 0, std::uint64_t{0});
    Invoke();
    Check(work.phase == Phase::Waiting && !work.copied && calls == 1, "foreign collector passed through");
    Setup();
    Put(query.query, 0x30, Vec3{0, 0, 0});
    Invoke();
    Check(!work.copied && calls == 1, "far query passed through");
    Setup();
    work.issued = GetTickCount64() - 3000;
    Invoke();
    Check(!work.copied && calls == 1, "expired request passed through");
    Setup();
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 8; ++t) threads.emplace_back([] { for (unsigned n = 0; n < 64; ++n) Invoke(); });
    for (auto& thread : threads) thread.join();
    Check(calls == 512 && badArgs == 0, "concurrent original calls preserved");
    Check(work.phase == Phase::Done && work.attempts == 1 && work.copied && work.afterCopied, "concurrent one-shot claim");
    Check(!Copy(0, bytes.data(), bytes.size()), "null read rejected");
    Check(!Start(base, L"."), "synthetic module cannot pass instruction guard");
    std::array<std::uint8_t, 0x200> segment{};
    Check(SetSegment(segment, {-10529,611,-4419}, {-10528,608,-4417}, {-10529,609,-4419}), "bounded segment accepted");
    Check(Read<Vec3>(segment, 0x30) == Vec3{-529,611,-419}, "segment tile origin");
    Check(Read<Vec3>(segment, 0x40) == Vec3{1,-3,2}, "segment displacement");
    Check(Read<Vec3>(segment, 0x50) == Vec3{1,-1.f/3,.5f}, "reciprocal displacement NOT duplicated delta");
    Check(std::abs(Read<float>(segment, 0x5C) - std::sqrt(14.f)) < 1e-6f, "segment length");
    const auto unchanged = segment;
    Check(!SetSegment(segment, {100,1,100}, {100,2,101}, {100,0,100}) && segment == unchanged, "unknown zero-axis rejected before mutation");
    Check(!SetSegment(segment, {100,1,100}, {101,2,200}, {100,0,100}), "long segment rejected");
    Check(!SetSegment(segment, {100,1,100}, {101,2,101}, {200,0,100}), "remote origin rejected");
    SetupControl(); const auto originalBytes = query;
    RunControl();
    Check(work.controlMatched && work.guardsIntact && calls == 1 && badArgs == 0, "owned replay matches natural control");
    Check(query.query == originalBytes.query && query.collector == originalBytes.collector &&
        query.transform == originalBytes.transform && query.shape == originalBytes.shape, "original inputs/output unchanged");
    SetupControl(); work.mode = "segment"; work.segmentStart = {-10529,611,-4419}; work.segmentEnd = {-10528,608,-4417};
    RunControl();
    Check(work.controlMatched && work.segmentCalled && calls == 2 && work.guardsIntact, "segment only after matching control");
    SetupControl(); mockMode = MockMode::Disagree; work.mode = "segment";
    work.segmentStart = {-10529,611,-4419}; work.segmentEnd = {-10528,608,-4417}; RunControl();
    Check(!work.controlMatched && !work.segmentCalled && calls == 1, "mismatch prevents segment");
    SetupControl(); work.caller = 0; RunControl();
    Check(calls == 0, "wrong context rejects replay");
    SetupControl(); work.stackHigh = work.stackLow + 16; RunControl();
    Check(calls == 0, "missing lifetime rejects replay");
    SetupControl(); Put(query.query, 0x30, 99.f); RunControl();
    Check(calls == 0, "changed input rejects replay");
    SetupControl(); Put(query.shape, 0x68, .5f); RunControl();
    Check(calls == 0, "changed radius rejects replay");
    SetupControl(); mockMode = MockMode::Fault; RunControl();
    Check(replayFaulted && work.callException == 0xE001ABCD && !work.segmentCalled, "native fault disables future replay");
    calls = 0; RunControl(); Check(calls == 0, "fault latch prevents retry");
    SetupControl(); mockMode = MockMode::Overwrite; RunControl();
    Check(replayFaulted && !work.guardsIntact, "canary damage disables replay");
    SetupControl(); replayTransactions = 12; RunControl();
    Check(calls == 0, "per-process replay bound");
    Setup(); work.mode = "segment";
    ShapeHook(worldBytes.data(), query.query.data(), query.transform.data(), query.collector.data(), nullptr);
    Check(!work.copied && work.contextRejected == 1 && calls == 1, "unsuitable fifth argument passed through without claiming replay");
    Setup(); originalRay = OriginalRay; work.mode = "rayobserve";
    Invoke();
    Check(!work.copied && work.attempts == 0, "sphere hook cannot claim ray request");
    calls = 0;
    const auto rayReturn = RayHook(worldBytes.data(), query.query.data(), query.collector.data());
    Check(rayReturn == 0xFE123456789ABCDEULL && calls == 1 && badArgs == 0, "ray three-argument call and return preserved");
    Check(work.copied && work.afterCopied && !work.controlCalled && !work.segmentCalled && replayTransactions == 0, "ray observation adds no query");
    Check(!std::memcmp(work.snapshot.query.data(), query.query.data(), 0x100) &&
        !std::memcmp(work.collectorAfter.data(), query.collector.data(), 0x140), "ray bounded owned snapshots");
    Check(work.rayQueryAfterCopied && !std::memcmp(work.rayQueryAfter.data(), query.query.data(), 0x100), "ray query captured after original too");
    Check(work.phase == Phase::Done && !armed && std::strcmp(work.controlStatus, "observed-game-ray") == 0, "ray one-shot completed");
    Setup(); originalRay = OriginalRay; work.mode = "rayobserve";
    RayHook(worldBytes.data(), nullptr, query.collector.data());
    Check(!work.copied && calls == 1, "unreadable ray query passed through");
    Setup(); originalRay = OriginalRay; work.mode = "rayobserve";
    work.issued = GetTickCount64() - 3000;
    RayHook(worldBytes.data(), query.query.data(), query.collector.data());
    Check(!work.copied && calls == 1, "expired ray request passed through");
    Setup(); originalRay = OriginalRay; work.mode = "rayobserve";
    threads.clear();
    for (unsigned t = 0; t < 8; ++t) threads.emplace_back([] {
        for (unsigned n = 0; n < 64; ++n) RayHook(worldBytes.data(), query.query.data(), query.collector.data());
    });
    for (auto& thread : threads) thread.join();
    Check(calls == 512 && badArgs == 0 && work.attempts == 1 && work.copied && work.afterCopied, "concurrent ray originals preserved, one capture");
    Check(replayTransactions == 0 && !work.segmentCalled, "concurrent ray observer never replays");
    Setup(); originalRay = OriginalRay;
    RayHook(worldBytes.data(), query.query.data(), query.collector.data());
    Check(!work.copied && work.attempts == 0 && calls == 1, "ray hook cannot consume sphere observation");
    std::array<std::uint8_t, 0x100> raySegment{};
    Put(raySegment, 0x98, std::uint64_t{0x12345678});
    Check(SetRaySegment(raySegment, {-10529,611,-4419}, {-10528,608,-4417}, {-10529,609,-4419}), "ray bounded segment accepted");
    Check(Read<std::array<double,3>>(raySegment, 0x40) == std::array<double,3>{-529,611,-419}, "ray double tile origin");
    Check(Read<std::array<double,3>>(raySegment, 0x60) == std::array<double,3>{1,-3,2}, "ray double displacement");
    Check(Read<Vec3>(raySegment, 0x80) == Vec3{1,-1.f/3,.5f} &&
        std::abs(Read<float>(raySegment, 0x8C)-std::sqrt(14.f)) < 1e-6f, "ray float reciprocal and length");
    Check(Read<double>(raySegment, 0x58) == 0 && Read<double>(raySegment, 0x78) == 1 &&
        Read<std::uint64_t>(raySegment, 0x98) == 0x12345678, "ray vector fourth lanes and opaque tail preserved");
    const auto rayUnchanged = raySegment;
    Check(!SetRaySegment(raySegment, {100,1,100}, {100,2,101}, {100,0,100}) && raySegment == rayUnchanged, "ray zero component rejected without mutation");
    Check(!SetRaySegment(raySegment, {100,1,100}, {101,2,200}, {100,0,100}), "ray long segment rejected");
    Check(!SetRaySegment(raySegment, {100,1,100}, {101,2,101}, {200,0,100}), "ray remote origin rejected");
    Check(!SetRaySegment(bytes, {100,1,100}, {101,2,101}, {100,0,100}), "ray undersized query rejected");
    SetupRayControl(); const auto rayOriginal = query;
    RunRayControl();
    Check(work.controlMatched && work.guardsIntact && work.originalsPreserved && calls == 1 && badArgs == 0, "ray isolated replay matches control");
    Check(query.query == rayOriginal.query && query.collector == rayOriginal.collector, "ray originals unchanged");
    SetupRayControl(); work.mode = "raysegment";
    work.segmentStart = {-10529,611,-4419}; work.segmentEnd = {-10528,608,-4417}; RunRayControl();
    Check(work.controlMatched && work.segmentCalled && calls == 2 && badArgs == 0 && work.guardsIntact, "ray segment after matching control only");
    SetupRayControl(); work.mode = "raysegment"; mockMode = MockMode::Disagree;
    work.segmentStart = {-10529,611,-4419}; work.segmentEnd = {-10528,608,-4417}; RunRayControl();
    Check(calls == 1 && !work.controlMatched && !work.segmentCalled, "ray control mismatch blocks segment");
    SetupRayControl(); work.caller = 0; RunRayControl();
    Check(calls == 0, "ray wrong caller rejected");
    SetupRayControl(); work.stackHigh = 0; RunRayControl();
    Check(calls == 0, "ray invalid stack rejected without underflow");
    SetupRayControl(); work.captured = GetTickCount64()-101; RunRayControl();
    Check(calls == 0, "ray expired original context rejected");
    SetupRayControl(); Put(query.query, 0x60, 99.0); RunRayControl();
    Check(calls == 0, "ray changed input rejected");
    SetupRayControl(); Put(work.snapshot.collector, 0, base + 0x123); RunRayControl();
    Check(calls == 0, "ray foreign collector cannot replay");
    SetupRayControl(); Put(worldBytes, 0xB70, base + 0x900); RunRayControl();
    Check(calls == 0, "ray changed world rejected");
    SetupRayControl(); mockMode = MockMode::Fault; RunRayControl();
    Check(replayFaulted && work.callException == 0xE001ABCD, "ray fault latches replay disable");
    calls = 0; RunRayControl(); Check(calls == 0, "ray fault retry prevented");
    SetupRayControl(); mockMode = MockMode::Overwrite; RunRayControl();
    Check(replayFaulted && !work.guardsIntact, "ray canary damage latches disable");
    SetupRayControl(); replayTransactions = 12; RunRayControl();
    Check(calls == 0, "ray shares process replay budget");
    Setup(); originalRay = OriginalRay; work.mode = "rayreplay";
    Put(query.collector, 0, base + 0x123);
    RayHook(worldBytes.data(), query.query.data(), query.collector.data());
    Check(calls == 1 && !work.copied && work.contextRejected == 1, "ray foreign collector passed through without claim");
    Setup(); originalRay = OriginalRay; work.mode = "rayreplay";
    Put(query.query, 0x40, std::array<double,3>{-529.755, 615.7, -420.3});
    RayHook(worldBytes.data(), query.query.data(), query.collector.data());
    Check(calls == 1 && work.copied && work.nearPlayer == 1 && !work.controlCalled, "ray elevated player-near selection, wrong test caller no replay");
    Setup(); work.mode = "rayfan"; work.continuous = true;
    Put(query.query, 0x40, std::array<double,3>{-529.755, 615.7, -420.3});
    for (auto profile : {0x40004027u, 0x40004024u, 0u})
    {
        Put(query.query, 0x14, profile);
        const auto before = query;
        Check(!CaptureRay(worldBytes.data(), query.query.data(), query.collector.data()) &&
            !work.copied && work.phase == Phase::Waiting && armed,
            "foreign continuous profile does not claim the pending measurement");
        Check(query.query == before.query && query.collector == before.collector && calls == 0,
            "profile selection never modifies original queries or adds physics calls");
    }
    Put(query.query, 0x14, VisibilityRayProfile);
    Check(CaptureRay(worldBytes.data(), query.query.data(), query.collector.data()) && work.copied &&
        work.contextRejected == 3, "next validated profile still captures in same pending round");
    Setup(); work.mode = "rayreplay";
    Put(query.query, 0x40, std::array<double,3>{-529.755, 615.7, -420.3});
    Put(query.query, 0x14, 0x40004027u);
    Check(CaptureRay(worldBytes.data(), query.query.data(), query.collector.data()),
        "manual research capture retains foreign contexts for diagnosis");
    SetupRayControl(); work.continuous = true; continuousVisibility = true; work.mode = "rayfan";
    Put(work.snapshot.query, 0x14, 0x40004027u); RunRayControl();
    Check(calls == 0 && !replayFaulted && std::strcmp(work.controlStatus, "unknown-visibility-query-profile") == 0,
        "replay defensively refuses unvalidated continuous profile without fault latch");
    std::array<Vec3, 9> fanTargets{};
    Check(RayFanTargets({-10535,612,-4421}, {-10529,611,-4420}, fanTargets), "diagnostic fan generated");
    Check(fanTargets[0] == Vec3{-10529,611,-4420}, "fan center exact");
    for (size_t i = 1; i < fanTargets.size(); ++i)
    {
        const float expected = i < 5 ? .05f : .15f;
        Check(std::abs(Distance(fanTargets[i],fanTargets[0])-expected) < .0015f, "fan diagnostic distance preserved at world-coordinate precision");
        const Vec3 delta{fanTargets[i][0]+10529,fanTargets[i][1]-611,fanTargets[i][2]+4420};
        Check(std::abs(delta[0]*6-delta[1]+delta[2]) < .007f, "fan offset plane perpendicular to sightline");
    }
    Check(RayFanTargets({0,0,0}, {0,2,0}, fanTargets) && Finite(fanTargets[1]), "vertical stencil has stable basis");
    Check(!RayFanTargets({0,0,0}, {.01f,.01f,.01f}, fanTargets), "too-close fan refused");
    Check(!RayFanTargets({988,22,999}, {1100,20,1000}, fanTargets), "manual fan keeps original short range");
    Check(RayFanTargets({988,22,999}, {1100,20,1000}, fanTargets, 512), "100gu player radius with camera offset builds full fan");
    for (const auto& endpoint : fanTargets)
        Check(SetRaySegment(raySegment, {988,22,999}, endpoint, {1000,20,1000}, MaximumVisibilityRayLength),
            "all extended fan endpoints pass native ray construction");
    Check(RayFanTargets({988,20,1000}, {1500,20,1000}, fanTargets, 512), "maximum 500gu radius plus 12gu camera offset supported");
    Check(!RayFanTargets({988,20,1000}, {1500.5f,20,1000}, fanTargets, 512), "fan remains bounded beyond hard maximum");
    Check(!SetRaySegment(raySegment, {988,22,999}, {1502,20,1000}, {1000,20,1000}, MaximumVisibilityRayLength),
        "expanded segment maximum still enforced");
    Check(!SetRaySegment(raySegment, {988,22,999}, {1100,20,1000}, {1000,20,1000}, std::numeric_limits<float>::quiet_NaN()),
        "NaN distance override rejected");
    SetupRayControl(); work.mode = "rayfan";
    work.segmentStart = {-10535,612,-4421}; work.segmentEnd = {-10529,611,-4420};
    RunRayControl();
    Check(work.controlMatched && work.fanCompleted == 9 && calls == 10 && extraCalls == 10 && badArgs == 0, "fan performs one matching control and nine rays");
    Check(work.guardsIntact && work.originalsPreserved && !work.segmentCalled &&
        std::strcmp(work.controlStatus,"diagnostic-ray-fan-completed") == 0, "fan separate from single-ray result");
    for (const auto& item : work.fan)
        Check(item.called && item.plausible && item.guards && item.originals, "each fan sample independently checked");
    SetupRayControl(); work.mode = "rayfan"; mockMode = MockMode::Disagree;
    work.segmentStart = {-10535,612,-4421}; work.segmentEnd = {-10529,611,-4420}; RunRayControl();
    Check(calls == 1 && !work.controlMatched && work.fanCompleted == 0, "fan mismatch prevents all samples");
    SetupRayControl(); work.mode = "rayfan"; rayFaultAt = 4;
    work.segmentStart = {-10535,612,-4421}; work.segmentEnd = {-10529,611,-4420}; RunRayControl();
    Check(calls == 4 && work.fanCompleted == 2 && replayFaulted && !work.fan[3].called, "fan stops on first native failure, no invented clear samples");
    SetupRayControl(); work.mode = "rayfan"; extraCalls = 15;
    work.segmentStart = {-10535,612,-4421}; work.segmentEnd = {-10529,611,-4420}; RunRayControl();
    Check(calls == 0 && extraCalls == 15, "whole fan budget reserved before first query");
    SetupRayControl(); work.mode = "rayfan";
    work.segmentStart = {-10529,611,-4421}; work.segmentEnd = {-10529,611,-4420}; RunRayControl();
    Check(calls == 0, "fan invalid axis rejected before control");
    SetupRayControl(); work.mode = "rayfan"; work.sourceAgeAtRequest = 250; work.issued = work.captured - 300;
    RunRayControl();
    Check(calls == 0 && std::strcmp(work.controlStatus,"unknown-fan-source-stale") == 0, "fan includes request delay in source freshness");
    SetupRayControl(); work.mode = "rayfan"; work.issued = work.captured + 1;
    RunRayControl();
    Check(calls == 0 && std::strcmp(work.controlStatus,"unknown-fan-source-stale") == 0, "fan rejects future issue time");
    SetupRayControl(); work.mode = "rayfan"; rayExpireAt = 2;
    work.segmentStart = {-10535,612,-4421}; work.segmentEnd = {-10529,611,-4420}; RunRayControl();
    Check(calls == 2 && work.fanCompleted == 1 && !work.fan[1].called &&
        std::strcmp(work.controlStatus,"unknown-fan-time-budget") == 0, "fan stops further calls on expired original context");
    SetupControl(); work.mode = "segment"; extraCalls = 23; RunControl();
    Check(calls == 0, "sphere obeys shared extra-call budget too");
    VisibilityPacket packet{};
    packet.magic=VisibilityQueryMagic;packet.pid=123;packet.processStart=456;packet.sequence=1;packet.lightSequence=7;
    packet.issued=900;packet.sourceAge=20;packet.player={10,20,30};packet.camera={11,22,31};packet.target={15,21,35};
    Check(ValidVisibilityQuery(packet,123,456,1000), "continuous query ABI valid");
    Check(!ValidVisibilityQuery(packet,124,456,1000)&&!ValidVisibilityQuery(packet,123,457,1000), "continuous PID/epoch mismatch rejected");
    Check(!ValidVisibilityQuery(packet,123,456,1200)&&!ValidVisibilityQuery(packet,123,456,800), "continuous stale/future query refused");
    packet.sourceAge=std::numeric_limits<float>::quiet_NaN();
    Check(!ValidVisibilityQuery(packet,123,456,1000), "continuous NaN age refused");
    packet.sourceAge=20;packet.camera={50,22,31};
    Check(!ValidVisibilityQuery(packet,123,456,1000), "continuous camera far from player refused");
    packet.camera={11,22,31};packet.target={110,20,30};
    Check(ValidVisibilityQuery(packet,123,456,1000), "continuous 100gu target accepted");
    packet.camera={-2,20,30};packet.target={510,20,30};
    Check(ValidVisibilityQuery(packet,123,456,1000), "500gu player sphere accepts camera offset without clipping edge");
    packet.target={510.5f,20,30};
    Check(!ValidVisibilityQuery(packet,123,456,1000), "continuous target range bounded by player sphere");
    packet.camera={11,22,31};
    SetupRayControl(); continuousVisibility=true;work.continuous=true;work.mode="rayfan";
    extraCalls=100;replayTransactions=100;work.segmentStart={-10535,612,-4421};work.segmentEnd={-10529,611,-4420};
    RunRayControl();
    Check(calls==10&&work.fanCompleted==9,"explicit continuous mode uses per-series rather than manual lifetime budget");
    SetupRayControl(); continuousVisibility=true;work.continuous=true;work.mode="rayfan";
    work.segmentStart={-10535,612,-4421};work.segmentEnd={-10429,611,-4420};RunRayControl();
    Check(calls==10&&work.fanCompleted==9,"continuous fan no longer hits hidden 50gu segment ceiling");
    SetupRayControl(); continuousVisibility=true;work.continuous=true;work.mode="rayfan";rayExpireAt=2;
    work.segmentStart={-10535,612,-4421};work.segmentEnd={-10529,611,-4420};RunRayControl();
    Check(replayFaulted&&calls==2&&work.fanCompleted==1,"slow continuous context latches off instead of repeated stalls");
    packet.target={15,21,35};
    VisibilityBatch batch{}; batch.magic=VisibilityQueryMagic; batch.count=2; batch.sequence=1;
    batch.entries[0]=packet; batch.entries[1]=packet; batch.entries[1].target={16,22,36};
    Check(ValidVisibilityBatch(batch,123,456,1000),"batch ABI and shared provenance valid");
    batch.entries[1].camera[0]+=1;
    Check(!ValidVisibilityBatch(batch,123,456,1000),"mixed camera batch refused atomically");
    batch.entries[1]=packet; batch.count=MaximumVisibilityTargets+1;
    Check(!ValidVisibilityBatch(batch,123,456,1000),"oversized batch refused");
    batch.count=0;
    Check(!ValidVisibilityBatch(batch,123,456,1000),"empty batch refused");
    SetupRayControl(); continuousVisibility=true; work.continuous=true; work.mode="rayfan";
    visibilityRequest={}; visibilityRequest.count=2;
    for(auto& p:visibilityRequest.entries) { p.camera={-10535,612,-4421};p.target={-10529,611,-4420}; }
    RunVisibilityBatch();
    Check(calls==20&&visibilityResult.entries[0].code==1&&visibilityResult.entries[1].code==1,
        "two targets completed in same original lifetime, independent controls");
    Check(visibilityResult.entries[0].samples==9&&visibilityResult.entries[1].samples==9&&
        visibilityResult.entries[0].completed>0,"batch target counts and completion ticks");
    calls=0; RunVisibilityBatch(std::chrono::microseconds(0));
    Check(calls==0&&visibilityResult.entries[0].code==4&&visibilityResult.entries[1].code==4&&!replayFaulted,
        "shared budget exhaustion skips safely without latching or fake results");
    SetupRayControl();continuousVisibility=true;work.continuous=true;work.mode="rayfan";rayBatchExpireAt=2;
    RunVisibilityBatch();
    Check(calls==2&&visibilityResult.entries[0].code==4&&visibilityResult.entries[1].code==4&&!replayFaulted,
        "mid-fan budget exhaustion skips entire incomplete fan and all later targets");
    SetupRayControl(); continuousVisibility=true;work.continuous=true;work.mode="rayfan";rayFaultAt=4;
    RunVisibilityBatch();
    Check(calls==4&&replayFaulted&&visibilityResult.entries[0].code==3&&visibilityResult.entries[1].code==3,
        "native fault prevents every remaining target and invalidates batch");
    // Context tracing is passive: serialize already captured data, never replay.
    SetupRayControl();
    VisibilityBatch traceBatch{}; traceBatch.sequence = 12345; traceBatch.count = 3;
    traceBatch.entries[0].code = 1; traceBatch.entries[0].samples = 9; traceBatch.entries[0].clear = 9;
    traceBatch.entries[1].code = 1; traceBatch.entries[1].samples = 9;
    traceBatch.entries[2].code = 4;
    const auto savedQuery = work.snapshot.query;
    const auto savedCollector = work.snapshot.collector;
    const auto trace = ContextTraceRecord(work, traceBatch);
    Check(trace["measurementSequence"] == 12345 && trace["complete"] == 2 && trace["allClear"] == 1 &&
        trace["blocked"] == 1 && trace["skipped"] == 1, "trace correlates batch identity and verdict counts");
    Check(trace["queryPrefixHex"].get<std::string>().size() == 0xA0 * 2 &&
        trace["collectorBeforeHex"].get<std::string>().size() == 0x140 * 2 &&
        trace["targets"].size() == 3, "trace retains bounded disjoint raw windows and per-target results");
    Check(calls == 0 && work.snapshot.query == savedQuery && work.snapshot.collector == savedCollector,
        "trace makes no physics call and does not modify borrowed snapshots");
    const auto savedFolder = folder;
    folder = std::filesystem::temp_directory_path() / ("cdt-context-trace-test-" +
        std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directory(folder);
    contextTraceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Check(contextTraceEvent != nullptr, "synthetic trace trigger created");
    contextTraceSessions = 0;
    PollContextTrace(1000);
    Check(contextTraceFile == INVALID_HANDLE_VALUE, "trace stays idle without a trigger");
    SetEvent(contextTraceEvent); PollContextTrace(1000);
    Check(contextTraceFile != INVALID_HANDLE_VALUE && contextTraceDeadline == 21000, "trigger arms exactly 20 seconds");
    SaveContextTrace(work, traceBatch, 1001);
    Check(contextTraceRows == 1 && contextTraceBytes > 0 && calls == 0, "worker trace writes without extra rays");
    SetEvent(contextTraceEvent); PollContextTrace(2000);
    Check(contextTraceSessions == 1 && contextTraceDeadline == 21000, "active trigger cannot extend recording");
    PollContextTrace(21000);
    Check(contextTraceFile == INVALID_HANDLE_VALUE, "trace stops at deadline");
    SetEvent(contextTraceEvent); PollContextTrace(22000);
    contextTraceRows = ContextTraceMaxRows; SaveContextTrace(work, traceBatch, 22001);
    Check(contextTraceFile == INVALID_HANDLE_VALUE, "trace row bound stops recording");
    SetEvent(contextTraceEvent); PollContextTrace(23000);
    contextTraceBytes = ContextTraceMaxBytes; SaveContextTrace(work, traceBatch, 23001);
    Check(contextTraceFile == INVALID_HANDLE_VALUE, "trace byte bound stops recording");
    SetEvent(contextTraceEvent); PollContextTrace(24000);
    Check(contextTraceFile == INVALID_HANDLE_VALUE && contextTraceSessions == 3, "trace limited to three explicit sessions per process");
    CloseHandle(contextTraceEvent); contextTraceEvent = nullptr;
    for (const auto& file : std::filesystem::directory_iterator(folder)) std::filesystem::remove(file.path());
    std::filesystem::remove(folder); folder = savedFolder;
    std::cout << checks << " physics observer synthetic checks passed; no live-game claim.\n";
}

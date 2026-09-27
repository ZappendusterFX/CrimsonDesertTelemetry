#include "modulator_bridge.h"
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <atomic>
#include <string>
#include <thread>

#define TEST_CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "Assertion failed at line " << __LINE__ << ": " #expr << std::endl; \
            std::exit(1); \
        } \
    } while (0)

int main()
{
    using namespace cdt::modulator;

    const uint32_t testPid = GetCurrentProcessId();

    std::cout << "[Test 1] Opening host modulator bridge..." << std::endl;
    TEST_CHECK(OpenModulatorBridge(testPid));
    TEST_CHECK(IsModulatorBridgeOpen());

    std::cout << "[Test 2] Connecting client to modulator bridge..." << std::endl;
    ModulatorClient client;
    TEST_CHECK(client.Open(testPid));
    TEST_CHECK(client.IsOpen());

    std::cout << "[Test 3] Setting modulation zones via client..." << std::endl;
    // Zone 0: Campfire OFF
    TEST_CHECK(client.SetZone(0, 100.0f, 200.0f, 300.0f, 5.0f, 0.0f, 0.0f, 0.0f, true));
    // Zone 1: Torch 1 RED tint
    TEST_CHECK(client.SetZone(1, 110.0f, 205.0f, 302.0f, 3.0f, 2.0f, 0.2f, 0.2f, true));
    // Zone 2: Inactive zone
    TEST_CHECK(client.SetZone(2, 50.0f, 50.0f, 50.0f, 1.0f, 1.0f, 1.0f, 1.0f, false));

    // Overrides:
    // Slot 0: Light 42 -> OFF (0, 0, 0)
    TEST_CHECK(client.SetOverride(0, 42, 0.0f, 0.0f, 0.0f, 0.0f, true));
    // Slot 1: Light 99 -> Cyan (0, 2.0, 2.0)
    TEST_CHECK(client.SetOverride(1, 99, 0.0f, 2.0f, 2.0f, 1.0f, true));
    // Slot 2: Disabled
    TEST_CHECK(client.SetOverride(2, 105, 1.0f, 1.0f, 1.0f, 1.0f, false));

    TEST_CHECK(client.SetMasterEnable(true));
    TEST_CHECK(client.Commit());

    std::cout << "[Test 4] Polling zones and overrides on host side..." << std::endl;
    std::vector<ModulatorZone> activeZones;
    std::vector<LightOverride> activeOverrides;
    bool masterEnable = false;
    TEST_CHECK(PollModulatorZones(activeZones, masterEnable));
    TEST_CHECK(masterEnable == true);
    TEST_CHECK(activeZones.size() == 2);

    TEST_CHECK(PollModulatorOverrides(activeOverrides, masterEnable));
    TEST_CHECK(masterEnable == true);
    TEST_CHECK(activeOverrides.size() == 2);

    // Verify Override 0
    TEST_CHECK(activeOverrides[0].lightIndex == 42);
    TEST_CHECK(std::fabs(activeOverrides[0].rgb[0] - 0.0f) < 1e-4f);
    TEST_CHECK(std::fabs(activeOverrides[0].rgb[1] - 0.0f) < 1e-4f);
    TEST_CHECK(std::fabs(activeOverrides[0].rgb[2] - 0.0f) < 1e-4f);
    TEST_CHECK(std::fabs(activeOverrides[0].colorW - 0.0f) < 1e-4f);

    // Verify Override 1
    TEST_CHECK(activeOverrides[1].lightIndex == 99);
    TEST_CHECK(std::fabs(activeOverrides[1].rgb[1] - 2.0f) < 1e-4f);
    TEST_CHECK(std::fabs(activeOverrides[1].rgb[2] - 2.0f) < 1e-4f);

    std::cout << "[Test 4b] Reopening an existing mapping preserves its state..." << std::endl;
    CloseModulatorBridge();
    TEST_CHECK(OpenModulatorBridge(testPid));
    TEST_CHECK(PollModulatorOverrides(activeOverrides, masterEnable));
    TEST_CHECK(masterEnable);
    TEST_CHECK(activeOverrides.size() == 2);
    TEST_CHECK(activeOverrides[0].lightIndex == 42);
    TEST_CHECK(activeOverrides[1].lightIndex == 99);

    std::cout << "[Test 5] Test master bypass..." << std::endl;
    TEST_CHECK(client.SetMasterEnable(false));
    TEST_CHECK(client.Commit());
    TEST_CHECK(!PollModulatorOverrides(activeOverrides, masterEnable));
    TEST_CHECK(masterEnable == false);

    std::cout << "[Test 6] Clear all overrides..." << std::endl;
    TEST_CHECK(client.SetMasterEnable(true));
    TEST_CHECK(client.ClearAllOverrides());
    TEST_CHECK(client.Commit());
    TEST_CHECK(!PollModulatorOverrides(activeOverrides, masterEnable));
    TEST_CHECK(masterEnable == true);
    TEST_CHECK(activeOverrides.empty());

    std::cout << "[Test 7] Concurrent commits publish complete snapshots..." << std::endl;
    constexpr uint32_t kGenerations = 1000;
    for (uint32_t slot = 0; slot < MaxLightOverrides; ++slot)
    {
        TEST_CHECK(client.SetOverride(slot, slot, 1.0f, 1.0f, 1.0f));
    }
    TEST_CHECK(client.Commit());

    std::atomic<bool> writerDone{false};
    std::atomic<bool> writerFailed{false};
    std::thread writer([&]
    {
        for (uint32_t generation = 2; generation <= kGenerations; ++generation)
        {
            const float value = static_cast<float>(generation);
            for (uint32_t slot = 0; slot < MaxLightOverrides; ++slot)
            {
                if (!client.SetOverride(slot, slot, value, value, value))
                {
                    writerFailed = true;
                    break;
                }
            }
            if (writerFailed || !client.Commit())
            {
                writerFailed = true;
                break;
            }
            std::this_thread::yield();
        }
        writerDone = true;
    });

    bool tornSnapshot = false;
    uint32_t snapshotsChecked = 0;
    while (!writerDone)
    {
        if (!PollModulatorOverrides(activeOverrides, masterEnable)) continue;
        if (activeOverrides.size() != MaxLightOverrides)
        {
            tornSnapshot = true;
            break;
        }
        const float generation = activeOverrides[0].rgb[0];
        for (uint32_t slot = 0; slot < MaxLightOverrides; ++slot)
        {
            const auto& current = activeOverrides[slot];
            if (current.lightIndex != slot || current.rgb[0] != generation ||
                current.rgb[1] != generation || current.rgb[2] != generation)
            {
                tornSnapshot = true;
                break;
            }
        }
        ++snapshotsChecked;
        if (tornSnapshot) break;
    }
    writer.join();
    TEST_CHECK(!writerFailed);
    TEST_CHECK(!tornSnapshot);
    TEST_CHECK(snapshotsChecked > 0);

    std::cout << "[Test 8] Stuck writer cannot keep stale overrides active..." << std::endl;
    TEST_CHECK(PollModulatorOverrides(activeOverrides, masterEnable));
    const std::wstring mappingName = L"Local\\CrimsonDesertTelemetry.Modulator." + std::to_wstring(testPid);
    HANDLE mappingHandle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mappingName.c_str());
    TEST_CHECK(mappingHandle != nullptr);
    auto* shared = static_cast<ModulatorMapping*>(MapViewOfFile(mappingHandle, FILE_MAP_ALL_ACCESS, 0, 0,
                                                               sizeof(ModulatorMapping)));
    TEST_CHECK(shared != nullptr);
    auto* sequence = reinterpret_cast<volatile LONG64*>(&shared->header.seqlock);
    const LONG64 stableSequence = InterlockedCompareExchange64(sequence, 0, 0);
    TEST_CHECK((stableSequence & 1) == 0);
    InterlockedExchange64(sequence, stableSequence + 1); // Simulate a writer that stops mid-commit.
    TEST_CHECK(PollModulatorOverrides(activeOverrides, masterEnable));
    TEST_CHECK(masterEnable);
    Sleep(400); // Beyond the bridge's 350 ms heartbeat window.
    TEST_CHECK(!PollModulatorOverrides(activeOverrides, masterEnable));
    TEST_CHECK(!masterEnable);
    TEST_CHECK(activeOverrides.empty());
    InterlockedExchange64(sequence, stableSequence + 2);
    UnmapViewOfFile(shared);
    CloseHandle(mappingHandle);

    client.Close();
    CloseModulatorBridge();
    TEST_CHECK(!IsModulatorBridgeOpen());

    std::cout << "All Modulator Bridge tests passed successfully!" << std::endl;
    return 0;
}

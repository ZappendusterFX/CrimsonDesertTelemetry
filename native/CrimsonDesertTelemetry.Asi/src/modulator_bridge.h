#pragma once
#include <windows.h>
#include <cstdint>
#include <array>
#include <vector>

namespace cdt::modulator
{

inline constexpr uint32_t ModulatorMagic = 0x4D4C4443; // 'CDLM'
inline constexpr uint32_t ModulatorVersion = 2;
inline constexpr uint32_t MaxLightOverrides = 2048;
inline constexpr uint32_t MaxModulationZones = 16;

struct LightOverride
{
    uint32_t lightIndex;  // 0 .. 2047 (index into ManyLights buffer)
    float    rgb[3];      // Target RGB color
    float    colorW;      // Intensity / multiplier (0.0 = completely off, 1.0 = normal)
    uint32_t enabled;     // 1 = active, 0 = inactive
};

struct ModulatorZone
{
    float targetWorldPos[3];
    float radius;
    float scaleRgb[3];
    float enabled;        // > 0.5f means active
};

struct ModulatorHeader
{
    uint32_t magic;          // 'CDLM'
    uint32_t version;        // 2
    int64_t  seqlock;        // Seqlock: odd during write, even when stable
    uint32_t zoneCount;      // 0 .. 16
    uint32_t overrideCount;  // 0 .. 64
    uint32_t masterEnable;   // 1 = enabled, 0 = bypassed
    uint64_t lastUpdateTick; // GetTickCount64()
    uint32_t reserved[6];
};

struct ModulatorMapping
{
    ModulatorHeader header;
    ModulatorZone   zones[MaxModulationZones];
    LightOverride   overrides[MaxLightOverrides];
};

static_assert(sizeof(ModulatorHeader) == 64);
static_assert(sizeof(LightOverride) == 24);
static_assert(sizeof(ModulatorZone) == 32);

// Host / ASI Server API:
bool OpenModulatorBridge(uint32_t pid = 0);
void CloseModulatorBridge();
bool IsModulatorBridgeOpen();

// Reads active overrides and zones. Returns true if masterEnable is true.
bool PollModulatorOverrides(std::vector<LightOverride>& outOverrides, bool& masterEnable);
bool PollModulatorZones(std::vector<ModulatorZone>& outZones, bool& masterEnable);

// Client API (used by external apps, audio visualizers, or scripts):
class ModulatorClient
{
public:
    ModulatorClient() = default;
    ~ModulatorClient() { Close(); }

    bool Open(uint32_t pid = 0);
    void Close();
    bool IsOpen() const { return mapping_ != nullptr; }

    bool SetOverride(uint32_t slot, uint32_t lightIndex, float r, float g, float b, float intensity = 1.0f, bool enabled = true);
    bool ClearAllOverrides();

    bool SetZone(uint32_t index, float targetX, float targetY, float targetZ,
                 float radius, float scaleR, float scaleG, float scaleB, bool enabled = true);
    bool ClearAllZones();

    bool SetMasterEnable(bool enable);
    bool Commit();

private:
    HANDLE handle_ = nullptr;
    ModulatorMapping* mapping_ = nullptr;
    ModulatorMapping localState_{};
};

} // namespace cdt::modulator

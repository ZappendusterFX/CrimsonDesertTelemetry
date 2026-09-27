#include "modulator_bridge.h"
#include <string>
#include <cstring>
#include <algorithm>
#include <cstddef>

namespace cdt::modulator
{

namespace
{
HANDLE s_mappingHandle = nullptr;
ModulatorMapping* s_mapping = nullptr;
int64_t s_lastObservedSeqlock = -1;
uint64_t s_cachedUpdateTick = 0;
std::vector<LightOverride> s_cachedOverrides;
std::vector<ModulatorZone> s_cachedZones;
bool s_cachedMasterEnable = false;

std::wstring GetMappingName(uint32_t pid)
{
    if (pid == 0) pid = GetCurrentProcessId();
    return L"Local\\CrimsonDesertTelemetry.Modulator." + std::to_wstring(pid);
}
} // namespace

bool OpenModulatorBridge(uint32_t pid)
{
    if (s_mappingHandle) return true;

    const std::wstring name = GetMappingName(pid);
    s_mappingHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                         sizeof(ModulatorMapping), name.c_str());
    const DWORD createError = GetLastError();
    if (!s_mappingHandle) return false;

    s_mapping = static_cast<ModulatorMapping*>(MapViewOfFile(s_mappingHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ModulatorMapping)));
    if (!s_mapping)
    {
        CloseHandle(s_mappingHandle);
        s_mappingHandle = nullptr;
        return false;
    }

    if (createError != ERROR_ALREADY_EXISTS)
    {
        std::memset(s_mapping, 0, sizeof(ModulatorMapping));
        s_mapping->header.magic = ModulatorMagic;
        s_mapping->header.version = ModulatorVersion;
        s_mapping->header.masterEnable = 1;
        s_mapping->header.seqlock = 0;
    }

    s_lastObservedSeqlock = -1;
    s_cachedUpdateTick = 0;
    s_cachedOverrides.clear();
    s_cachedZones.clear();
    s_cachedMasterEnable = false;
    return true;
}

void CloseModulatorBridge()
{
    if (s_mapping)
    {
        UnmapViewOfFile(s_mapping);
        s_mapping = nullptr;
    }
    if (s_mappingHandle)
    {
        CloseHandle(s_mappingHandle);
        s_mappingHandle = nullptr;
    }
    s_lastObservedSeqlock = -1;
    s_cachedUpdateTick = 0;
    s_cachedOverrides.clear();
    s_cachedZones.clear();
    s_cachedMasterEnable = false;
}

bool IsModulatorBridgeOpen()
{
    return s_mapping != nullptr;
}

static bool ReadSnapshot(std::vector<LightOverride>& outOverrides,
                         std::vector<ModulatorZone>& outZones,
                         bool& masterEnable)
{
    if (!s_mapping)
    {
        masterEnable = false;
        outOverrides.clear();
        outZones.clear();
        return false;
    }

    constexpr uint32_t kMaxRetries = 10;
    for (uint32_t retry = 0; retry < kMaxRetries; ++retry)
    {
        const int64_t seq1 = s_mapping->header.seqlock;
        if (seq1 & 1)
        {
            YieldProcessor();
            continue;
        }

        MemoryBarrier();

        const uint64_t lastTick = s_mapping->header.lastUpdateTick;
        const uint64_t nowTick = GetTickCount64();
        if (lastTick == 0 || (nowTick > lastTick && (nowTick - lastTick) > 350))
        {
            // Watchdog heartbeat expired or client inactive: automatically disengage modulator!
            masterEnable = false;
            outOverrides.clear();
            outZones.clear();
            s_cachedMasterEnable = false;
            s_cachedUpdateTick = 0;
            s_cachedOverrides.clear();
            s_cachedZones.clear();
            return true;
        }

        if (seq1 == s_lastObservedSeqlock)
        {
            masterEnable = s_cachedMasterEnable;
            outOverrides = s_cachedOverrides;
            outZones = s_cachedZones;
            return true;
        }

        if (s_mapping->header.magic != ModulatorMagic || s_mapping->header.version != ModulatorVersion)
        {
            masterEnable = false;
            outOverrides.clear();
            outZones.clear();
            return false;
        }

        const bool currentMaster = (s_mapping->header.masterEnable != 0);

        const uint32_t oCount = std::min(s_mapping->header.overrideCount, MaxLightOverrides);
        std::vector<LightOverride> tempOverrides;
        tempOverrides.reserve(oCount);
        for (uint32_t i = 0; i < oCount; ++i)
        {
            if (s_mapping->overrides[i].enabled != 0)
            {
                tempOverrides.push_back(s_mapping->overrides[i]);
            }
        }

        const uint32_t zCount = std::min(s_mapping->header.zoneCount, MaxModulationZones);
        std::vector<ModulatorZone> tempZones;
        tempZones.reserve(zCount);
        for (uint32_t i = 0; i < zCount; ++i)
        {
            if (s_mapping->zones[i].enabled > 0.5f)
            {
                tempZones.push_back(s_mapping->zones[i]);
            }
        }

        MemoryBarrier();
        const int64_t seq2 = s_mapping->header.seqlock;
        if (seq1 == seq2)
        {
            s_lastObservedSeqlock = seq1;
            s_cachedUpdateTick = lastTick;
            s_cachedMasterEnable = currentMaster;
            s_cachedOverrides = std::move(tempOverrides);
            s_cachedZones = std::move(tempZones);

            masterEnable = s_cachedMasterEnable;
            outOverrides = s_cachedOverrides;
            outZones = s_cachedZones;
            return true;
        }
    }

    // A writer can die while the sequence is odd. Keep the last complete
    // snapshot only for its original watchdog window, never indefinitely.
    const uint64_t nowTick = GetTickCount64();
    if (!s_cachedUpdateTick || nowTick < s_cachedUpdateTick || nowTick - s_cachedUpdateTick > 350)
    {
        s_cachedMasterEnable = false;
        s_cachedUpdateTick = 0;
        s_cachedOverrides.clear();
        s_cachedZones.clear();
    }
    masterEnable = s_cachedMasterEnable;
    outOverrides = s_cachedOverrides;
    outZones = s_cachedZones;
    return true;
}

bool PollModulatorOverrides(std::vector<LightOverride>& outOverrides, bool& masterEnable)
{
    std::vector<ModulatorZone> unusedZones;
    if (!ReadSnapshot(outOverrides, unusedZones, masterEnable))
    {
        return false;
    }
    return masterEnable && !outOverrides.empty();
}

bool PollModulatorZones(std::vector<ModulatorZone>& outZones, bool& masterEnable)
{
    std::vector<LightOverride> unusedOverrides;
    if (!ReadSnapshot(unusedOverrides, outZones, masterEnable))
    {
        return false;
    }
    return masterEnable && !outZones.empty();
}

// Client Implementation:
bool ModulatorClient::Open(uint32_t pid)
{
    Close();

    const std::wstring name = GetMappingName(pid);
    handle_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!handle_) return false;

    mapping_ = static_cast<ModulatorMapping*>(MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ModulatorMapping)));
    if (!mapping_)
    {
        CloseHandle(handle_);
        handle_ = nullptr;
        return false;
    }

    std::memset(&localState_, 0, sizeof(localState_));
    localState_.header.magic = ModulatorMagic;
    localState_.header.version = ModulatorVersion;
    localState_.header.masterEnable = 1;
    return true;
}

void ModulatorClient::Close()
{
    if (mapping_)
    {
        UnmapViewOfFile(mapping_);
        mapping_ = nullptr;
    }
    if (handle_)
    {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}

bool ModulatorClient::SetOverride(uint32_t slot, uint32_t lightIndex, float r, float g, float b, float intensity, bool enabled)
{
    if (slot >= MaxLightOverrides) return false;
    localState_.overrides[slot].lightIndex = lightIndex;
    localState_.overrides[slot].rgb[0] = r;
    localState_.overrides[slot].rgb[1] = g;
    localState_.overrides[slot].rgb[2] = b;
    localState_.overrides[slot].colorW = intensity;
    localState_.overrides[slot].enabled = enabled ? 1 : 0;

    if (slot >= localState_.header.overrideCount)
    {
        localState_.header.overrideCount = slot + 1;
    }
    return true;
}

bool ModulatorClient::ClearAllOverrides()
{
    localState_.header.overrideCount = 0;
    std::memset(localState_.overrides, 0, sizeof(localState_.overrides));
    return true;
}

bool ModulatorClient::SetZone(uint32_t index, float targetX, float targetY, float targetZ,
                              float radius, float scaleR, float scaleG, float scaleB, bool enabled)
{
    if (index >= MaxModulationZones) return false;
    localState_.zones[index].targetWorldPos[0] = targetX;
    localState_.zones[index].targetWorldPos[1] = targetY;
    localState_.zones[index].targetWorldPos[2] = targetZ;
    localState_.zones[index].radius = radius;
    localState_.zones[index].scaleRgb[0] = scaleR;
    localState_.zones[index].scaleRgb[1] = scaleG;
    localState_.zones[index].scaleRgb[2] = scaleB;
    localState_.zones[index].enabled = enabled ? 1.0f : 0.0f;

    if (index >= localState_.header.zoneCount)
    {
        localState_.header.zoneCount = index + 1;
    }
    return true;
}

bool ModulatorClient::ClearAllZones()
{
    localState_.header.zoneCount = 0;
    std::memset(localState_.zones, 0, sizeof(localState_.zones));
    return true;
}

bool ModulatorClient::SetMasterEnable(bool enable)
{
    localState_.header.masterEnable = enable ? 1 : 0;
    return true;
}

bool ModulatorClient::Commit()
{
    if (!mapping_) return false;

    // Claim the writer sequence and keep it odd until every header field and
    // payload has been copied; an even sequence publishes the complete state.
    auto* sequence = reinterpret_cast<volatile LONG64*>(&mapping_->header.seqlock);
    const LONG64 previous = InterlockedCompareExchange64(sequence, 0, 0);
    if ((previous & 1) || InterlockedCompareExchange64(sequence, previous + 1, previous) != previous)
    {
        return false;
    }

    localState_.header.lastUpdateTick = GetTickCount64();
    constexpr size_t afterSequence = offsetof(ModulatorHeader, seqlock) + sizeof(ModulatorHeader::seqlock);
    std::memcpy(&mapping_->header, &localState_.header, offsetof(ModulatorHeader, seqlock));
    std::memcpy(reinterpret_cast<uint8_t*>(&mapping_->header) + afterSequence,
                reinterpret_cast<const uint8_t*>(&localState_.header) + afterSequence,
                sizeof(ModulatorHeader) - afterSequence);
    std::memcpy(mapping_->zones, localState_.zones, sizeof(localState_.zones));
    std::memcpy(mapping_->overrides, localState_.overrides, sizeof(localState_.overrides));

    InterlockedExchange64(sequence, previous + 2);
    return true;
}

} // namespace cdt::modulator

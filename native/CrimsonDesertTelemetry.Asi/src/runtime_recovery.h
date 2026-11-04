#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace cdt::recovery
{
// Native creates the per-process rendezvous. The out-of-process managed host
// performs discovery, using its existing PE/player/camera readers. Address data
// is independently checked against the loaded module before any hook can start.
class Handoff
{
public:
    ~Handoff();
    bool Initialize(const std::array<unsigned char,32>& executableHash, bool needed, std::uint32_t requestedFeatures);
    bool WaitAndApply(HANDLE stopEvent);
    void Complete(bool success);
private:
    HANDLE mapping_{};
    unsigned char* view_{};
    std::array<unsigned char,32> executableHash_{};
};

// Parses, validates and installs an immutable-for-this-process runtime contract.
// Only called on the instrument worker before hooks. Exposed for mutation tests.
bool ValidateAndApply(std::string_view payload, std::uint64_t moduleBase,
    std::uint32_t processId, std::uint64_t processStart,
    const std::array<unsigned char,32>& executableHash, std::string& failure);
}

#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace cdt::recovery
{
struct DiagnosticRequest
{
    bool force{};
    std::array<std::uint8_t,16> token{};
};
// Empty disables the test. A nonzero 32-hex token requests one cold recovery
// until the host claims it after live-world readiness. This reads metadata only.
bool ReadDiagnosticRequest(std::wstring_view token, DiagnosticRequest& request,
    std::string& failure, const std::filesystem::path& localAppDataForTest = {});
// Native creates the per-process rendezvous. The out-of-process managed host
// performs discovery, using its existing PE/player/camera readers. Address data
// is independently checked against the loaded module before any hook can start.
class Handoff
{
public:
    ~Handoff();
    bool Initialize(const std::array<unsigned char,32>& executableHash, bool needed, std::uint32_t requestedFeatures,
        const DiagnosticRequest& diagnostic = {});
    bool WaitAndApply(HANDLE stopEvent);
    void Complete(bool success);
private:
    HANDLE mapping_{};
    unsigned char* view_{};
    std::array<unsigned char,32> executableHash_{};
    bool diagnostic_{};
};

// Parses, validates and installs an immutable-for-this-process runtime contract.
// Only called on the instrument worker before hooks. Exposed for mutation tests.
bool ValidateAndApply(std::string_view payload, std::uint64_t moduleBase,
    std::uint32_t processId, std::uint64_t processStart,
    const std::array<unsigned char,32>& executableHash, std::string& failure);
}

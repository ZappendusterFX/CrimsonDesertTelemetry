#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cdt::overlay
{
using Clock = std::chrono::steady_clock;
struct Vec3 { float x{}, y{}, z{}; };
struct SourceVisibility
{
    std::string status = "unknown", reason = "not-reported";
    std::optional<double> attenuationFactor, volumeAgeMillisecondsAtCapture;
    bool physicsSampled{};
    int clearSamples{};
};
struct LightRecord
{
    // Sample-local renderer index, never a persistent physical-light identity.
    int sampleIndex{};
    Vec3 position, colorLinear;
    float luminanceLinear{};
    std::string kind;
    std::optional<Vec3> direction;
    std::optional<float> coneHalfAngleDegrees;
    std::optional<SourceVisibility> sourceVisibility;
    // Engine-input records only. False means the renderer did not select this light
    // in THIS view (e.g. behind the camera); it is not OFF and not occluded.
    bool rendererSelected = true;
    // Number of engine group members (e.g. a fire's flame particles); 0 = standalone.
    int memberCount{};
};
struct LightSummary
{
    std::string status = "not-reported", unavailableReason;
    std::optional<std::uint32_t> publishedRecords;
    std::optional<double> ageMilliseconds;
    // Only the current rendered feed owns records. View copies share immutable
    // storage; unavailable/malformed/newly missing feeds never retain it.
    std::shared_ptr<const std::vector<LightRecord>> records;
};
struct AmbientSummary
{
    std::string status = "not-reported", reason;
    std::optional<std::uint64_t> captureSequence;
    std::optional<std::uint32_t> frameNumber;
    std::optional<double> sourceAgeMilliseconds;
    std::optional<Vec3> upperHemisphereMeanWorking;
    std::optional<Vec3> upwardIrradianceOverPiWorking;
    std::optional<Vec3> inverseMatrixMean;
    std::optional<double> rec709MeanLuminanceEstimate;
    std::optional<double> cameraSkyVisibilityWorking;
    std::optional<std::uint32_t> visibilityFrameNumber;
    std::optional<double> visibilityAgeMilliseconds;
    std::optional<Vec3> localEnvironmentAmbientEstimateWorking;
    bool localEstimateStale{};
};
struct Sample
{
    std::string state = "waiting", build, orientationSource, schemaVersion;
    std::int64_t sequence{};
    double sourceAgeMs{};
    std::optional<Vec3> playerPosition, playerForward, playerUp;
    std::optional<Vec3> cameraPosition, cameraForward, cameraUp, cameraRight;
    std::optional<float> playerHeading, cameraHeading, pitch, fov;
    std::optional<float> aspectRatio, nearPlane;
    int consensus{}, validCopies{}, distinctStates{};
    std::int64_t captureUs{};
    bool rediscovered{};
    LightSummary authoredLights, renderedLights;
    // Current engine lights before view selection, same capture as renderedLights.
    LightSummary upstreamLights;
};
struct RecoveryStatus
{
    std::string state, title, detail;
    // Stage-based completion only. No invented percentage while waiting for a world.
    std::optional<int> progress;
};
struct View
{
    Sample sample;
    Clock::time_point received{};
    bool connected{}, hasSample{};
    std::string connection = "CONNECTING";
    std::string healthStatus, healthError;
    std::optional<RecoveryStatus> recovery;
    // Process-local bootstrap/native failures survive disconnected stream views.
    struct LocalFault { std::string source, title, detail; };
    std::vector<LocalFault> localFaults;
    double rateHz{};
    AmbientSummary ambient;
    Clock::time_point ambientReceived{};
    bool hasAmbient{};
};
struct Config
{
    bool enabled = false, visible = true, details = false, autoScale = true;
    bool notifications = false;
    bool huePanel = true; // optional protocol-1 CrimsonHue controller; no second renderer
    bool lightsExpected = false, renderedExpected = false, upstreamExpected = false;
    bool lightOverlay = false, lightOverlayVisible = true, radar3D = true;
    bool showAmbient = true, occlusionTest = false, hideOccluded = false;
    int notificationDurationMs = 6000;
    int toggleKey = 0x77, detailsKey = 0x78, corner = 0, staleMs = 1000;
    int lightToggleKey = 0x79, lightMaxMarkers = 512, lightMaxLabels = 6;
    int occlusionToggleKey = 0x7a;
    float scale = 1.0f, opacity = 0.92f;
    // SDR UI reference white for HDR10/scRGB output; does not alter API RGB.
    float hdrPaperWhiteNits = 200.0f;
    float lightRadius = 35.0f;
    unsigned short port = 27311;
};
struct Notice
{
    std::string title, detail;
    bool error = false;
    bool persistent = false;
    std::optional<int> progress;
};
class NoticeTracker
{
public:
    std::optional<Notice> Update(const View& view, const Config& config, Clock::time_point now);
private:
    std::string currentKey_, pendingKey_;
    std::optional<Notice> current_;
    Clock::time_point shownAt_{}, pendingAt_{};
    bool readyForScene_{};
};
// Reject malformed/oversize/incompatible data; never interpret missing vectors as zero.
Sample ParseSample(std::string_view json, std::chrono::system_clock::time_point now);
AmbientSummary ParseAmbient(std::string_view json);
// Optional health extension; old hosts omit it. Malformed status is rejected.
std::optional<RecoveryStatus> ParseRecoveryHealth(std::string_view json);
std::optional<float> Heading(Vec3 direction);
struct ScreenPoint { float x{}, y{}, depth{}; };
// World positions are already reconstructed with the capture-paired camera.
// Project with the newest envelope camera; leave viewport clipping to callers.
std::optional<ScreenPoint> ProjectWorld(Vec3 world, const Sample& sample, float width, float height);
struct CameraFrustum
{
    Vec3 apex, farCenter;
    // Camera-local top-left, top-right, bottom-right, bottom-left.
    std::array<Vec3, 4> farCorners;
};
// Schematic forward depth, not a physical visibility/range measurement. Preserve
// all three world dimensions, including camera pitch and roll.
std::optional<CameraFrustum> BuildCameraFrustum(const Sample& sample, float length);
bool NearbyForDetails(Vec3 a, Vec3 b, float maxDistance = .15f);
bool SpatialLightLess(const LightRecord& a, const LightRecord& b);
// Presentation only: complete-link proximity groups, never physical identities.
// Indices address the input span; at most its first 64 entries are considered.
// Every considered entry remains present exactly once, including singletons.
std::vector<std::vector<size_t>> GroupLightDetails(std::span<const LightRecord* const> records,
    float maxDistance = .15f);
double AgeMs(const View& view, Clock::time_point now);
bool IsLive(const View& view, Clock::time_point now, int staleMs);
bool RenderedLightsLive(const View& view, Clock::time_point now, int staleMs);
bool UpstreamLightsLive(const View& view, Clock::time_point now, int staleMs);
// The drawable set: the all-around engine input when fresh, otherwise the
// filtered renderer output, otherwise null. Both come from one capture.
const LightSummary* DisplayLights(const View& view, Clock::time_point now, int staleMs);
SourceVisibility CurrentSourceVisibility(const LightRecord& light, const View& view, Clock::time_point now);
bool HideOccludedLight(const LightRecord& light,const View& view,Clock::time_point now,bool hideOccluded);
void UpdateShortcutToggle(bool& value,bool& wasDown,int key,bool isDown,bool foreground);
std::string ShortcutLabel(int key);
struct SourceVisibilityCounts { size_t visible{}, blocked{}, unknown{}; };
// Counts current in-radius records, including off-screen/behind-camera records.
SourceVisibilityCounts CountSourceVisibility(const View& view, Clock::time_point now, float radius);
bool AmbientLive(const View& view, Clock::time_point now);
double AmbientAgeMs(const View& view, Clock::time_point now);
std::string LightFeedStatus(const View& view, Clock::time_point now, int staleMs);
std::string Status(const View& view, Clock::time_point now, int staleMs);
float HudNaturalHeight(const Config& config, bool details);
float HudScale(float width, float height, const Config& config, bool details);
}

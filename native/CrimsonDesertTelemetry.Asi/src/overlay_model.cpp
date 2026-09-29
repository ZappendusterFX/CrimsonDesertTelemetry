#include "overlay_model.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <numbers>
#include <limits>
#include <stdexcept>

namespace cdt::overlay
{
namespace
{
using Json = nlohmann::json;
constexpr size_t MaximumTelemetryMessageBytes = 4 * 1024 * 1024;
constexpr std::uint32_t MaximumRenderedRecords = 32768;
float Number(const Json& value)
{
    if (!value.is_number()) throw std::runtime_error("Invalid telemetry number");
    const float result = value.get<float>();
    if (!std::isfinite(result)) throw std::runtime_error("Non-finite telemetry");
    return result;
}
Vec3 Vector(const Json& value)
{
    return {Number(value.at("x")), Number(value.at("y")), Number(value.at("z"))};
}
Vec3 ArrayVector(const Json& value)
{
    if (!value.is_array() || value.size() != 3) throw std::runtime_error("Invalid RGB vector");
    return {Number(value.at(0)),Number(value.at(1)),Number(value.at(2))};
}
bool Finite(const Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
double Dot(const Vec3 a, const Vec3 b)
{
    return static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y + static_cast<double>(a.z) * b.z;
}
bool ValidCamera(const Sample& sample)
{
    if (!sample.cameraPosition || !sample.cameraForward || !sample.cameraRight || !sample.cameraUp ||
        !sample.fov || !sample.aspectRatio || !sample.nearPlane)
        return false;
    const auto forward = *sample.cameraForward, right = *sample.cameraRight, up = *sample.cameraUp;
    if (!Finite(*sample.cameraPosition) || !Finite(forward) || !Finite(right) || !Finite(up) ||
        !std::isfinite(*sample.fov) || *sample.fov <= 0 || *sample.fov >= 180 ||
        !std::isfinite(*sample.aspectRatio) || *sample.aspectRatio <= 0 ||
        !std::isfinite(*sample.nearPlane) || *sample.nearPlane <= 0)
        return false;
    const Vec3 cross{right.y * up.z - right.z * up.y, right.z * up.x - right.x * up.z,
        right.x * up.y - right.y * up.x};
    return std::abs(Dot(forward, forward) - 1.0) <= .02 && std::abs(Dot(right, right) - 1.0) <= .02 &&
        std::abs(Dot(up, up) - 1.0) <= .02 && std::abs(Dot(right, up)) <= .02 &&
        std::abs(Dot(right, forward)) <= .02 && std::abs(Dot(up, forward)) <= .02 &&
        std::abs(Dot(cross, forward) - 1.0) <= .03;
}
int CompareCoordinate(const float a, const float b)
{
    // Keep a strict weak ordering even for an invalid presentation input.
    if (std::isnan(a)) return std::isnan(b) ? 0 : 1;
    if (std::isnan(b)) return -1;
    return a < b ? -1 : (a > b ? 1 : 0);
}
int CompareSpatialVector(const Vec3 a, const Vec3 b)
{
    if (const int y = CompareCoordinate(a.y, b.y)) return y;
    if (const int x = CompareCoordinate(a.x, b.x)) return x;
    return CompareCoordinate(a.z, b.z);
}
Vec3 Direction(const Json& value)
{
    const auto result = Vector(value);
    const double length = std::sqrt(Dot(result, result));
    if (length < 0.9 || length > 1.1) throw std::runtime_error("Invalid direction");
    return result;
}
Vec3 LightVector(const Json& value)
{
    const auto result = Vector(value);
    if (std::abs(result.x) > 10000000 || std::abs(result.y) > 10000000 || std::abs(result.z) > 10000000)
        throw std::runtime_error("Invalid optional light vector");
    return result;
}
SourceVisibility ReadSourceVisibility(const Json& value, const Json& capture)
{
    try
    {
        SourceVisibility result;
        result.status=value.at("status").get<std::string>();
        if(value.value("method",std::string{})=="physics-ray-fan")
        {
            result.physicsSampled=true;
            // Preserve/validate the measured pose; movement does not invalidate
            // the latest raw sample. It is not claimed to be the current pose.
            (void)LightVector(value.at("referencePosition"));
            if(!value.at("lightCaptureSequence").is_number_integer()||value.at("lightCaptureSequence")<=0||
                value.at("lightCaptureSequence")>capture.at("captureSequence"))
                throw std::runtime_error("Invalid physics provenance");
            if(!value.at("volumeAgeMillisecondsAtCapture").is_null())
                result.volumeAgeMillisecondsAtCapture=Number(value.at("volumeAgeMillisecondsAtCapture"));
            if(result.status=="unknown")
            {
                result.reason=value.at("reason").get<std::string>();
                if(result.reason.empty()||!value.at("attenuationFactor").is_null())throw std::runtime_error("Invalid unknown physics");
                return result;
            }
            if((result.status!="clear"&&result.status!="blocked")||!value.at("reason").is_null()||
                value.at("sampleCount")!=9||!value.at("clearSampleCount").is_number_integer()||
                !result.volumeAgeMillisecondsAtCapture||*result.volumeAgeMillisecondsAtCapture<0||
                !value.at("contextFrame").is_number_integer())throw std::runtime_error("Invalid physics result");
            result.clearSamples=value.at("clearSampleCount").get<int>();
            result.attenuationFactor=Number(value.at("attenuationFactor"));
            if(result.clearSamples<0||result.clearSamples>9||
                (result.status=="blocked"?result.clearSamples!=0:result.clearSamples==0)||
                *result.attenuationFactor!=(result.status=="blocked"?0.0:1.0))throw std::runtime_error("Invalid physics verdict");
            result.reason.clear();
            return result;
        }
        const auto& sequence=value.at("lightCaptureSequence");
        const auto& pairedSequence=capture.at("captureSequence");
        if(!sequence.is_number_integer()||sequence<=0||!pairedSequence.is_number_integer()||
            sequence!=pairedSequence)throw std::runtime_error("Unpaired source visibility");
        const auto reference=LightVector(value.at("referencePosition"));
        const auto camera=LightVector(capture.at("camera").at("position"));
        if(reference.x!=camera.x||reference.y!=camera.y||reference.z!=camera.z)
            throw std::runtime_error("Mismatched visibility origin");
        if(!value.at("volumeAgeMillisecondsAtCapture").is_null())
        {
            result.volumeAgeMillisecondsAtCapture=Number(value.at("volumeAgeMillisecondsAtCapture"));
            if(*result.volumeAgeMillisecondsAtCapture<0)throw std::runtime_error("Invalid visibility age");
        }
        if(result.status=="unknown")
        {
            result.reason=value.at("reason").get<std::string>();
            if(result.reason.empty()||result.reason.size()>128||!value.at("attenuationFactor").is_null())
                throw std::runtime_error("Invalid unknown visibility");
        }
        else if(result.status=="clear"||result.status=="blocked")
        {
            const auto& volume=value.at("volumeSequence");
            const auto& frame=value.at("contextFrame");
            if(!volume.is_number_integer()||volume<=0||!frame.is_number_integer()||frame<0||
                frame>std::numeric_limits<std::uint32_t>::max()||!result.volumeAgeMillisecondsAtCapture||
                !value.at("reason").is_null())throw std::runtime_error("Invalid visibility provenance");
            result.attenuationFactor=Number(value.at("attenuationFactor"));
            const double closest=Number(value.at("closestApproach"));
            if(*result.attenuationFactor!=(result.status=="clear"?1.0:0.0)||
                (result.status=="clear"?closest<=0:closest>0))throw std::runtime_error("Invalid visibility verdict");
            result.reason.clear();
        }
        else throw std::runtime_error("Unknown visibility state");
        return result;
    }
    catch(const std::exception&)
    {
        SourceVisibility result;result.reason="invalid-metadata";return result;
    }
}
LightRecord ReadLightRecord(const Json& value, const Json& capture, const bool upstream)
{
    LightRecord result;
    if (upstream)
    {
        const auto& selected = value.at("rendererSelected");
        if (!selected.is_boolean()) throw std::runtime_error("Invalid renderer selection");
        result.rendererSelected = selected.get<bool>();
        const auto type = value.at("type").get<std::string>();
        if (type == "group")
        {
            const auto& members = value.at("memberCount");
            if (!members.is_number_integer() || members < 1 || members > 32767)
                throw std::runtime_error("Invalid group member count");
            result.memberCount = members.get<int>();
        }
        else if (type != "standalone" || value.contains("memberCount")) throw std::runtime_error("Invalid light type");
    }
    const auto& index = value.at("sampleIndex");
    if (!index.is_number_integer() || index < 0 || index >= MaximumRenderedRecords)
        throw std::runtime_error("Invalid optional light index");
    result.sampleIndex = index.get<int>();
    result.position = LightVector(value.at("position"));
    result.colorLinear = LightVector(value.at("colorLinear"));
    result.luminanceLinear = Number(value.at("luminanceLinear"));
    if (result.colorLinear.x < 0 || result.colorLinear.y < 0 || result.colorLinear.z < 0 ||
        result.luminanceLinear < 0)
        throw std::runtime_error("Invalid optional light color");
    if (value.contains("kind"))
    {
        result.kind = value.at("kind").get<std::string>();
        if (result.kind != "point" && result.kind != "spot")
            throw std::runtime_error("Invalid optional light kind");
    }
    if (value.contains("direction"))
    {
        result.direction = Direction(value.at("direction"));
        if (result.kind != "spot" || std::abs(Dot(*result.direction, *result.direction) - 1.0) > 0.01)
            throw std::runtime_error("Invalid optional spotlight direction");
    }
    if (value.contains("coneHalfAngleDegrees"))
    {
        result.coneHalfAngleDegrees = Number(value.at("coneHalfAngleDegrees"));
        if (result.kind != "spot" || *result.coneHalfAngleDegrees <= 0 || *result.coneHalfAngleDegrees > 90)
            throw std::runtime_error("Invalid optional spotlight cone");
    }
    if(value.contains("sourceVisibility"))
        result.sourceVisibility=ReadSourceVisibility(value.at("sourceVisibility"),capture);
    return result;
}
LightSummary ReadLights(const Json& value, std::uint32_t maximum, bool rendered = false, bool upstream = false)
{
    LightSummary result;
    try
    {
        if (!value.is_object()) throw std::runtime_error("Invalid optional light object");
        result.status = value.at("status").get<std::string>();
        if (result.status == "available")
        {
            const auto& sources = value.at("sources");
            if (!sources.is_array() || sources.size() > maximum)
                throw std::runtime_error("Invalid optional light count");
            result.publishedRecords = static_cast<std::uint32_t>(sources.size());
            if (value.contains("ageMilliseconds"))
            {
                const auto& age = value.at("ageMilliseconds");
                if (!age.is_number()) throw std::runtime_error("Invalid optional light age");
                result.ageMilliseconds = age.get<double>();
                if (!std::isfinite(*result.ageMilliseconds) || *result.ageMilliseconds < 0)
                    throw std::runtime_error("Invalid optional light age");
            }
            if (rendered)
            {
                if (!result.ageMilliseconds || *result.ageMilliseconds > 500)
                    throw std::runtime_error("Missing or stale rendered light age");
                auto records = std::make_shared<std::vector<LightRecord>>();
                records->reserve(sources.size());
                std::bitset<MaximumRenderedRecords> seen;
                for (const auto& source : sources)
                {
                    auto record = ReadLightRecord(source,value,upstream);
                    const auto index = static_cast<size_t>(record.sampleIndex);
                    if (seen.test(index)) throw std::runtime_error("Duplicate optional light index");
                    seen.set(index);
                    records->push_back(std::move(record));
                }
                result.records = std::move(records);
            }
        }
        else if (result.status == "unavailable")
        {
            result.unavailableReason = value.at("unavailableReason").get<std::string>();
            if (result.unavailableReason.empty() || result.unavailableReason.size() > 128)
                throw std::runtime_error("Invalid optional light reason");
            // No source count is reported for unavailable data; absent data is
            // not a zero count and says nothing about the lamps' ON/OFF state.
        }
        else throw std::runtime_error("Unknown optional light status");
    }
    catch (const std::exception&)
    {
        result = {};
        result.status = "invalid";
        result.unavailableReason = "invalid-light-summary";
    }
    return result;
}
double SourceAge(const std::string& text, const std::chrono::system_clock::time_point now)
{
    // The v1 contract emits UTC ISO 8601, with optional fractional seconds.
    std::tm tm{};
    int consumed{};
    if (sscanf_s(text.c_str(), "%d-%d-%dT%d:%d:%d%n", &tm.tm_year, &tm.tm_mon,
        &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &consumed) != 6)
        throw std::runtime_error("Invalid capture timestamp");
    if (tm.tm_year < 2020 || tm.tm_mon < 1 || tm.tm_mon > 12 || tm.tm_mday < 1 ||
        tm.tm_mday > 31 || tm.tm_hour > 23 || tm.tm_min > 59 || tm.tm_sec > 59)
        throw std::runtime_error("Invalid capture timestamp range");
    size_t offset = static_cast<size_t>(consumed);
    double fraction{}, place = 0.1;
    if (offset < text.size() && text[offset] == '.')
    {
        ++offset;
        const size_t first = offset;
        while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9')
        {
            fraction += static_cast<double>(text[offset++] - '0') * place;
            place *= 0.1;
        }
        if (offset == first) throw std::runtime_error("Invalid timestamp fraction");
    }
    const auto zone = text.substr(offset);
    if (zone != "Z" && zone != "+00:00") throw std::runtime_error("Non-UTC timestamp");
    tm.tm_year -= 1900;
    --tm.tm_mon;
    const auto seconds = _mkgmtime64(&tm);
    if (seconds < 0) throw std::runtime_error("Invalid capture time");
    const double age = std::chrono::duration<double, std::milli>(now.time_since_epoch()).count()
        - (static_cast<double>(seconds) + fraction) * 1000.0;
    if (age < -2000.0) throw std::runtime_error("Capture timestamp is in the future");
    return std::max(0.0, age);
}
}

std::optional<float> Heading(const Vec3 direction)
{
    if (std::hypot(direction.x, direction.z) < 0.1f) return std::nullopt;
    const float angle = std::atan2(direction.x, direction.z) * 180.0f / std::numbers::pi_v<float>;
    return std::fmod(angle + 360.0f, 360.0f);
}

std::optional<ScreenPoint> ProjectWorld(const Vec3 world, const Sample& sample, const float width, const float height)
{
    if (!Finite(world) || !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
        !ValidCamera(sample))
        return std::nullopt;
    const auto position = *sample.cameraPosition, forward = *sample.cameraForward,
        right = *sample.cameraRight, up = *sample.cameraUp;
    const Vec3 delta{world.x - position.x, world.y - position.y, world.z - position.z};
    if (!Finite(delta)) return std::nullopt;
    const double depth = Dot(delta, forward);
    if (depth <= *sample.nearPlane) return std::nullopt;
    const double halfHeight = std::tan(static_cast<double>(*sample.fov) * std::numbers::pi / 360.0) * depth;
    const double halfWidth = halfHeight * *sample.aspectRatio;
    if (!std::isfinite(halfHeight) || !std::isfinite(halfWidth) || halfHeight <= 0 || halfWidth <= 0)
        return std::nullopt;
    const ScreenPoint result{
        static_cast<float>((1.0 + Dot(delta, right) / halfWidth) * width * .5),
        static_cast<float>((1.0 - Dot(delta, up) / halfHeight) * height * .5), static_cast<float>(depth)};
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.depth)) return std::nullopt;
    return result;
}

std::optional<CameraFrustum> BuildCameraFrustum(const Sample& sample, const float length)
{
    if (!ValidCamera(sample) || !std::isfinite(length) || length <= *sample.nearPlane) return std::nullopt;
    const double halfHeight = std::tan(static_cast<double>(*sample.fov) * std::numbers::pi / 360.0) * length;
    const double halfWidth = halfHeight * *sample.aspectRatio;
    if (!std::isfinite(halfHeight) || !std::isfinite(halfWidth) || halfHeight <= 0 || halfWidth <= 0)
        return std::nullopt;
    const auto apex = *sample.cameraPosition, forward = *sample.cameraForward,
        right = *sample.cameraRight, up = *sample.cameraUp;
    const auto farPoint = [&](const double horizontal, const double vertical)
    {
        return Vec3{
            static_cast<float>(apex.x + static_cast<double>(forward.x) * length + right.x * horizontal + up.x * vertical),
            static_cast<float>(apex.y + static_cast<double>(forward.y) * length + right.y * horizontal + up.y * vertical),
            static_cast<float>(apex.z + static_cast<double>(forward.z) * length + right.z * horizontal + up.z * vertical)};
    };
    CameraFrustum result{apex, farPoint(0, 0),
        {farPoint(-halfWidth, halfHeight), farPoint(halfWidth, halfHeight),
         farPoint(halfWidth, -halfHeight), farPoint(-halfWidth, -halfHeight)}};
    if (!Finite(result.farCenter) || !std::all_of(result.farCorners.begin(), result.farCorners.end(), Finite))
        return std::nullopt;
    return result;
}

bool NearbyForDetails(const Vec3 a, const Vec3 b, const float maxDistance)
{
    if (!Finite(a) || !Finite(b) || !std::isfinite(maxDistance) || maxDistance <= 0) return false;
    const double x = static_cast<double>(a.x) - b.x, y = static_cast<double>(a.y) - b.y,
        z = static_cast<double>(a.z) - b.z;
    return x * x + y * y + z * z <= static_cast<double>(maxDistance) * maxDistance;
}

bool SpatialLightLess(const LightRecord& a, const LightRecord& b)
{
    if (const int position = CompareSpatialVector(a.position, b.position)) return position < 0;
    if (a.kind != b.kind) return a.kind < b.kind;
    if (a.direction.has_value() != b.direction.has_value()) return !a.direction.has_value();
    if (a.direction)
        if (const int direction = CompareSpatialVector(*a.direction, *b.direction)) return direction < 0;
    if (a.coneHalfAngleDegrees.has_value() != b.coneHalfAngleDegrees.has_value())
        return !a.coneHalfAngleDegrees.has_value();
    return a.coneHalfAngleDegrees && CompareCoordinate(*a.coneHalfAngleDegrees, *b.coneHalfAngleDegrees) < 0;
}

std::vector<std::vector<size_t>> GroupLightDetails(const std::span<const LightRecord* const> records,
    const float maxDistance)
{
    const size_t count = std::min(records.size(), size_t{64});
    std::vector<size_t> order;
    order.reserve(count);
    for (size_t index = 0; index < count; ++index) order.push_back(index);
    std::stable_sort(order.begin(), order.end(), [&](const size_t a, const size_t b)
    {
        if (!records[a] || !records[b]) return records[a] && !records[b];
        return SpatialLightLess(*records[a], *records[b]);
    });
    std::vector<std::vector<size_t>> groups;
    groups.reserve(count);
    for (const auto index : order)
    {
        const auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& members)
        {
            return records[index] && std::all_of(members.begin(), members.end(), [&](const size_t member)
            {
                return records[member] && NearbyForDetails(records[index]->position, records[member]->position, maxDistance);
            });
        });
        if (group == groups.end()) groups.push_back({index});
        else group->push_back(index);
    }
    return groups;
}

float HudNaturalHeight(const Config& config, const bool details)
{
    const float base = config.radar3D ? 550.f : 344.f;
    return base + (details ? 380.f : 0.f);
}

float HudScale(float width, float height, const Config& config, bool details)
{
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
        !std::isfinite(config.scale) || config.scale <= 0) return 0;
    const float resolution = config.autoScale ? std::max(1.0f, height / 1080.0f) : 1.0f;
    // Scale in physical render-target pixels: a 4K screen needs twice the 1080p size.
    // Keep lower-resolution displays readable; only shrink if the panel would clip.
    const float naturalHeight = HudNaturalHeight(config, details);
    return std::min({config.scale * resolution, width / 550.0f, height / (naturalHeight + 40.0f)});
}

Sample ParseSample(const std::string_view text, const std::chrono::system_clock::time_point now)
{
    if (text.empty() || text.size() > MaximumTelemetryMessageBytes) throw std::runtime_error("Invalid message size");
    const auto root = Json::parse(text, [](int depth, Json::parse_event_t, Json&)
    {
        if (depth > 24) throw std::runtime_error("Telemetry nesting limit exceeded");
        return true;
    });
    const auto schema = root.at("schemaVersion").get<std::string>();
    if (schema != "1.1" && schema != "1.2" && schema != "1.3" && schema != "1.4" && schema != "1.5" && schema != "1.6")
        throw std::runtime_error("Unsupported telemetry schema");
    const auto& axes = root.at("coordinateSystem");
    if (axes.at("upAxis") != "y" || axes.at("handedness") != "right" || axes.at("unit") != "game-unit")
        throw std::runtime_error("Unsupported coordinate system");
    Sample sample;
    sample.schemaVersion = schema;
    sample.sequence = root.at("sequence").get<std::int64_t>();
    sample.state = root.at("game").at("state").get<std::string>();
    sample.build = root.at("game").at("build").get<std::string>();
    if (sample.state.size() > 64 || sample.build.size() > 128 || sample.sequence < 0)
        throw std::runtime_error("Invalid sample metadata");
    sample.sourceAgeMs = SourceAge(root.at("capturedAt").get<std::string>(), now);
    if (root.contains("lights"))
    {
        const auto& lights = root.at("lights");
        sample.authoredLights = ReadLights(lights, 8192);
        if (lights.is_object() && lights.contains("rendered"))
            sample.renderedLights = ReadLights(lights.at("rendered"), MaximumRenderedRecords, true);
        if (lights.is_object() && lights.contains("upstream"))
            sample.upstreamLights = ReadLights(lights.at("upstream"), MaximumRenderedRecords, true, true);
    }
    // Non-playing samples must not accidentally show coordinates from an older state.
    if (sample.state != "playing") return sample;
    const auto& player = root.at("player");
    if (!player.is_null())
    {
        sample.playerPosition = Vector(player.at("position"));
        if (player.contains("orientation") && !player.at("orientation").is_null())
        {
            const auto& orientation = player.at("orientation");
            sample.orientationSource = orientation.at("source").get<std::string>();
            if (sample.orientationSource != "player-physics-root")
                throw std::runtime_error("Unknown orientation source");
            sample.playerForward = Direction(orientation.at("forward"));
            sample.playerUp = Direction(orientation.at("up"));
            sample.playerHeading = Heading(*sample.playerForward);
        }
    }
    const auto& camera = root.at("camera");
    if (!camera.is_null())
    {
        sample.cameraPosition = Vector(camera.at("position"));
        sample.cameraForward = Direction(camera.at("forward"));
        sample.cameraUp = Direction(camera.at("up"));
        sample.cameraRight = Direction(camera.at("right"));
        sample.cameraHeading = Heading(*sample.cameraForward);
        sample.pitch = std::asin(std::clamp(sample.cameraForward->y, -1.0f, 1.0f)) * 180.0f / std::numbers::pi_v<float>;
        sample.fov = Number(camera.at("verticalFovDegrees"));
        if (*sample.fov <= 0 || *sample.fov >= 180) throw std::runtime_error("Invalid FOV");
        // Legacy/minimal camera envelopes remain usable for the existing HUD.
        // World markers require explicit projection metadata via ProjectWorld.
        if (camera.contains("aspectRatio"))
        {
            sample.aspectRatio = Number(camera.at("aspectRatio"));
            if (*sample.aspectRatio <= 0) throw std::runtime_error("Invalid camera aspect ratio");
        }
        if (camera.contains("nearPlane"))
        {
            sample.nearPlane = Number(camera.at("nearPlane"));
            if (*sample.nearPlane <= 0) throw std::runtime_error("Invalid camera near plane");
        }
    }
    if (root.contains("quality") && !root.at("quality").is_null())
    {
        const auto& q = root.at("quality");
        sample.consensus = q.at("consensusCopies").get<int>();
        sample.validCopies = q.at("validCopies").get<int>();
        sample.distinctStates = q.at("distinctStates").get<int>();
        sample.captureUs = q.at("captureDurationMicroseconds").get<std::int64_t>();
        sample.rediscovered = q.at("rediscovered").get<bool>();
    }
    return sample;
}
AmbientSummary ParseAmbient(const std::string_view text)
{
    if(text.empty()||text.size()>65536)throw std::runtime_error("Invalid ambient message size");
    const auto root=Json::parse(text);
    if(root.at("schemaVersion")!="1.0")throw std::runtime_error("Unsupported ambient schema");
    AmbientSummary result;result.status=root.at("status").get<std::string>();
    if(result.status=="unavailable")
    {
        result.reason=root.at("reason").get<std::string>();
        if(result.reason.empty()||result.reason.size()>128)throw std::runtime_error("Invalid ambient reason");
        return result;
    }
    if(result.status!="available")throw std::runtime_error("Invalid ambient status");
    result.captureSequence=root.at("captureSequence").get<std::uint64_t>();
    result.frameNumber=root.at("frameNumber").get<std::uint32_t>();
    result.sourceAgeMilliseconds=root.at("ageMilliseconds").get<double>();
    if(!std::isfinite(*result.sourceAgeMilliseconds)||*result.sourceAgeMilliseconds<0)
        throw std::runtime_error("Invalid ambient age");
    const auto& sky=root.at("sky");
    result.upperHemisphereMeanWorking=ArrayVector(sky.at("upperHemisphereMeanWorking"));
    result.upwardIrradianceOverPiWorking=ArrayVector(sky.at("upwardIrradianceOverPiWorking"));
    result.inverseMatrixMean=ArrayVector(sky.at("inverseMatrixMean"));
    result.rec709MeanLuminanceEstimate=sky.at("rec709MeanLuminanceEstimate").get<double>();
    if(!std::isfinite(*result.rec709MeanLuminanceEstimate))throw std::runtime_error("Invalid ambient luminance");
    if(root.contains("visibility")&&!root.at("visibility").is_null())
    {
        const auto& value=root.at("visibility");
        result.cameraSkyVisibilityWorking=value.at("valueWorking").get<double>();
        result.visibilityFrameNumber=value.at("frameNumber").get<std::uint32_t>();
        result.visibilityAgeMilliseconds=value.at("ageMilliseconds").get<double>();
        if(!std::isfinite(*result.cameraSkyVisibilityWorking)||*result.cameraSkyVisibilityWorking<0||
            *result.cameraSkyVisibilityWorking>1||!std::isfinite(*result.visibilityAgeMilliseconds)||
            *result.visibilityAgeMilliseconds<0)throw std::runtime_error("Invalid sky visibility");
    }
    if(root.contains("localEnvironmentAmbientEstimateWorking")&&!root.at("localEnvironmentAmbientEstimateWorking").is_null())
    {
        const auto& local=root.at("localEnvironmentAmbientEstimateWorking");
        result.localEstimateStale=local.at("stale").get<bool>();
        if(local.at("available").get<bool>())
            result.localEnvironmentAmbientEstimateWorking=ArrayVector(local.at("rgbWorking"));
    }
    return result;
}
double AgeMs(const View& view, const Clock::time_point now)
{
    return view.sample.sourceAgeMs + std::max(0.0, std::chrono::duration<double, std::milli>(now - view.received).count());
}
bool IsLive(const View& view, const Clock::time_point now, const int staleMs)
{
    return view.connected && view.hasSample && AgeMs(view, now) <= staleMs && view.sample.state == "playing";
}
namespace
{
bool LightsLive(const View& view, const LightSummary& lights, const Clock::time_point now, const int staleMs)
{
    if (!IsLive(view, now, staleMs) || lights.status != "available" || !lights.records || !lights.ageMilliseconds ||
        !std::isfinite(*lights.ageMilliseconds) || *lights.ageMilliseconds < 0)
        return false;
    // Published render age ends at host decoding. Envelope age accounts for
    // transport and time in this client; its camera timestamp slightly predates
    // light decoding, so the resulting freshness bound is conservative.
    return *lights.ageMilliseconds + AgeMs(view, now) <= 500.0;
}
}
bool RenderedLightsLive(const View& view, const Clock::time_point now, const int staleMs)
{
    return LightsLive(view, view.sample.renderedLights, now, staleMs);
}
bool UpstreamLightsLive(const View& view, const Clock::time_point now, const int staleMs)
{
    return LightsLive(view, view.sample.upstreamLights, now, staleMs);
}
const LightSummary* DisplayLights(const View& view, const Clock::time_point now, const int staleMs)
{
    if (UpstreamLightsLive(view, now, staleMs)) return &view.sample.upstreamLights;
    if (RenderedLightsLive(view, now, staleMs)) return &view.sample.renderedLights;
    return nullptr;
}
namespace
{
const char* VisibilityExpiryReason(const SourceVisibility& visibility,const View& view,const Clock::time_point now)
{
    const auto* lights=DisplayLights(view,now,1000);
    if(!lights)return "stale-source";
    if(!visibility.volumeAgeMillisecondsAtCapture||!std::isfinite(*visibility.volumeAgeMillisecondsAtCapture)||
        *visibility.volumeAgeMillisecondsAtCapture<0)return "invalid-metadata";
    // Include transport age as well as time spent in this view. This is a
    // conservative bound; metadata itself remains frozen with its capture.
    // Physics age already starts at native measurement completion. Adding the
    // unrelated GPU readback age again would prematurely expire this sample.
    const double sourceAge=visibility.physicsSampled?0:*lights->ageMilliseconds;
    if(*visibility.volumeAgeMillisecondsAtCapture+sourceAge+AgeMs(view,now)>(visibility.physicsSampled?500:1500))
        return visibility.physicsSampled?"stale-physics":"stale-volume";
    return nullptr;
}
}
SourceVisibility CurrentSourceVisibility(const LightRecord& light,const View& view,const Clock::time_point now)
{
    auto result=light.sourceVisibility.value_or(SourceVisibility{});
    if(result.status!="clear"&&result.status!="blocked")return result;
    if(const char* reason=VisibilityExpiryReason(result,view,now))
    {
        result.status="unknown";result.reason=reason;result.attenuationFactor.reset();
    }
    return result;
}
SourceVisibilityCounts CountSourceVisibility(const View& view,const Clock::time_point now,const float radius)
{
    SourceVisibilityCounts counts;
    const auto* lights=DisplayLights(view,now,1000);
    if(!lights||!view.sample.playerPosition||!std::isfinite(radius)||radius<=0)return counts;
    const auto player=*view.sample.playerPosition;
    for(const auto& light:*lights->records)
    {
        const Vec3 delta{light.position.x-player.x,light.position.y-player.y,light.position.z-player.z};
        if(Dot(delta,delta)>double{radius}*radius)continue;
        const auto value=CurrentSourceVisibility(light,view,now);
        if(value.status=="clear")++counts.visible;
        else if(value.status=="blocked")++counts.blocked;
        else ++counts.unknown;
    }
    return counts;
}
bool HideOccludedLight(const LightRecord& light,const View& view,const Clock::time_point now,const bool hideOccluded)
{
    if(!hideOccluded)return false;
    // Filtered presentation is fail-closed: only a fresh measured clear may
    // appear. Check the original record without copying strings for each marker.
    const auto& visibility=light.sourceVisibility;
    return !visibility||visibility->status!="clear"||VisibilityExpiryReason(*visibility,view,now);
}
void UpdateShortcutToggle(bool& value,bool& wasDown,const int key,const bool isDown,const bool foreground)
{
    const bool down=key>0&&key<=255&&isDown;
    if(foreground&&down&&!wasDown)value=!value;
    wasDown=down;
}
std::string ShortcutLabel(const int key)
{
    if(key<=0||key>255)return "shortcut off";
    if(key>=0x70&&key<=0x87)return "F"+std::to_string(key-0x70+1);
    if((key>='A'&&key<='Z')||(key>='0'&&key<='9'))return std::string(1,static_cast<char>(key));
    return "VK "+std::to_string(key);
}
double AmbientAgeMs(const View& view,const Clock::time_point now)
{
    if(!view.hasAmbient||!view.ambient.sourceAgeMilliseconds)return std::numeric_limits<double>::infinity();
    return *view.ambient.sourceAgeMilliseconds+
        std::max(0.0,std::chrono::duration<double,std::milli>(now-view.ambientReceived).count());
}
bool AmbientLive(const View& view,const Clock::time_point now)
{
    return view.hasAmbient&&view.ambient.status=="available"&&AmbientAgeMs(view,now)<=1500.0;
}
std::string LightFeedStatus(const View& view, const Clock::time_point now, const int staleMs)
{
    if (!IsLive(view, now, staleMs)) return Status(view, now, staleMs);
    if (UpstreamLightsLive(view, now, staleMs)) return "LIVE ENGINE LIGHTS";
    const auto& lights = view.sample.renderedLights;
    if (lights.status == "not-reported") return "LIGHT FEED NOT REPORTED";
    if (lights.status == "invalid") return "INVALID LIGHT DATA";
    if (lights.status != "available") return "LIGHT DATA UNAVAILABLE";
    if (!RenderedLightsLive(view, now, staleMs)) return "STALE LIGHT DATA";
    // A configured all-around input that is not current must not look like success.
    return view.sample.upstreamLights.status == "not-reported" ? "LIVE RENDERED LIGHTS"
        : "RENDERED ONLY / ENGINE INPUT UNAVAILABLE";
}
std::string Status(const View& view, const Clock::time_point now, const int staleMs)
{
    if (!view.connected) return view.connection;
    if (!view.hasSample) return "WAITING FOR DATA";
    if (AgeMs(view, now) > staleMs) return "STALE DATA";
    if (view.sample.state == "playing") return "LIVE";
    if (view.sample.state == "unsupported") return "UNSUPPORTED BUILD";
    if (view.sample.state == "loading") return "LOADING";
    return "WAITING / DISCOVERING";
}

namespace
{
struct NoticeState
{
    std::string key;
    Notice notice;
    enum Kind { Silent, Waiting, Ready, Error } kind = Error;
};
NoticeState LightNotice(const LightSummary& light)
{
    const auto& reason = light.unavailableReason;
    if (light.status == "invalid")
        return {"lights-invalid", {"Light data is incompatible",
            "Install matching ASI and telemetry host versions, then restart Crimson Desert.", true}};
    if (reason == "unsupported-build")
        return {reason, {"Light capture needs an update",
            "Install a telemetry version compatible with this Crimson Desert build.", true}};
    if (reason == "legacy-plugin-conflict")
        return {reason, {"Two light plugins detected",
            "Disable the old CrimsonHueConsole package in your mod manager and restart.", true}};
    if (reason == "native-fault")
        return {reason, {"Light capture stopped",
            "Restart Crimson Desert; check the telemetry logs if this repeats.", true}};
    if (reason == "bridge-invalid")
        return {reason, {"Light bridge is incompatible",
            "Install matching ASI and telemetry host versions, then restart Crimson Desert.", true}};
    if (reason == "bridge-missing")
        return {reason, {"Light capture is not connected",
            "Check that CrimsonDesertTelemetry.asi is loaded and light capture is enabled.", true}, NoticeState::Waiting};
    if (reason == "input-refused" || reason == "input-invalid" || reason == "input-disabled")
        return {"lights-input-" + reason, {"All-around light input is unavailable",
            "The engine input did not verify for this build; only renderer-selected lights are shown. Check the native log.", true}};
    if (reason == "bridge-waiting" || reason == "bridge-stale" || reason == "bridge-changing" ||
        reason == "input-unavailable" || reason == "walk-unavailable" || reason == "player-unavailable" ||
        reason == "required-telemetry-unavailable" || reason == "game-stopped")
        return {"lights-waiting", {"Light capture has no fresh sample",
            "Restart Crimson Desert; check CrimsonDesertTelemetry.native.log if this continues.", true}, NoticeState::Waiting};
    return {"lights-unavailable", {"Light data is unavailable",
        "Check the telemetry logs for the light reader's failure reason.", true}};
}
NoticeState NextNotice(const View& view, const Config& config, Clock::time_point now)
{
    const bool live = IsLive(view, now, config.staleMs);
    if (!view.localFaults.empty())
    {
        const auto& fault = view.localFaults.front();
        return {"local-" + fault.source, {fault.title, fault.detail, true}};
    }
    if (!live && (view.healthStatus == "unsupported-build" || view.healthStatus == "error"))
        return {"health-" + view.healthStatus, {
            view.healthStatus == "unsupported-build" ? "This game build needs an update" : "Telemetry could not start",
            view.healthError.empty() ? "Check the telemetry logs and restart with matching plugin files."
                : view.healthError.substr(0, 512), true}};
    if (view.hasSample && view.sample.state == "unsupported")
        return {"unsupported", {"This game build needs an update",
            "Install a telemetry version compatible with this Crimson Desert build.", true}};
    // A concrete native/layout failure is actionable even during loading. A
    // missing or merely discovering feed is not a startup timeout or an error.
    for (const auto* light : {config.lightsExpected ? &view.sample.authoredLights : nullptr,
        config.lightsExpected && config.renderedExpected ? &view.sample.renderedLights : nullptr,
        config.lightsExpected && config.renderedExpected && config.upstreamExpected ? &view.sample.upstreamLights : nullptr})
    {
        if (!light || light->status == "available" || light->status == "not-reported") continue;
        const auto problem = LightNotice(*light);
        if (problem.kind == NoticeState::Error) return problem;
    }
    if (!view.connected)
    {
        if (view.connection == "INVALID / INCOMPATIBLE DATA")
            return {"connection-invalid", {"Telemetry data is incompatible",
                "Install matching ASI and telemetry host versions, then restart Crimson Desert.", true}};
        return {"connection-waiting", {"Telemetry connection was interrupted",
            "Check CrimsonDesertTelemetry.host.log; restart Crimson Desert if the host has stopped.", true}, NoticeState::Waiting};
    }
    if (!view.hasSample)
        return {"sample-waiting", {"Telemetry has no current sample",
            "Check CrimsonDesertTelemetry.host.log; restart Crimson Desert if this continues.", true}, NoticeState::Waiting};
    if (view.sample.state != "playing")
        return {"scene-waiting", {}, NoticeState::Silent};
    if (AgeMs(view, now) > config.staleMs)
        return {"telemetry-stale", {"Telemetry stopped updating",
            "Current values are unavailable. Check the telemetry logs or restart Crimson Desert.", true}, NoticeState::Waiting};
    if (!view.sample.cameraPosition || !view.sample.playerPosition)
        return {"pose-waiting", {"Player or camera data is unavailable",
            "Check CrimsonDesertTelemetry.host.log; restart Crimson Desert if this continues.", true}, NoticeState::Waiting};

    std::optional<NoticeState> waiting;
    for (const auto* light : {config.lightsExpected ? &view.sample.authoredLights : nullptr,
        config.lightsExpected && config.renderedExpected ? &view.sample.renderedLights : nullptr,
        config.lightsExpected && config.renderedExpected && config.upstreamExpected ? &view.sample.upstreamLights : nullptr})
    {
        if (!light) continue;
        if (light->status == "available") continue;
        if (light->status == "not-reported")
            return {"lights-missing", {"Requested light data is missing",
                "Install matching ASI and telemetry host files; check [Lights] settings and restart.", true}};
        auto notice = LightNotice(*light);
        if (notice.kind == NoticeState::Error) return notice;
        if (!waiting) waiting = std::move(notice);
    }
    if (waiting) return *waiting;
    if (config.lightsExpected && config.renderedExpected && !RenderedLightsLive(view, now, config.staleMs))
        return {"lights-waiting", {"Light capture has no fresh sample",
            "Restart Crimson Desert; check CrimsonDesertTelemetry.native.log if this continues.", true}, NoticeState::Waiting};
    const bool lights = config.lightsExpected && (view.sample.authoredLights.status == "available" ||
        (config.renderedExpected && view.sample.renderedLights.status == "available"));
    return {"ready", {"Telemetry is ready", lights ? "Player, camera and available light data are ready."
        : "Player and camera data are ready."}, NoticeState::Ready};
}
}

std::optional<Notice> NoticeTracker::Update(const View& view, const Config& config, Clock::time_point now)
{
    if (!config.notifications)
    {
        current_.reset(); currentKey_.clear(); pendingKey_.clear(); readyForScene_ = false;
        return std::nullopt;
    }
    const auto visible = [&]() -> std::optional<Notice>
    {
        return current_ && (current_->error || now - shownAt_ < std::chrono::milliseconds(
            std::clamp(config.notificationDurationMs, 5000, 10000))) ? current_ : std::nullopt;
    };
    // A confirmed non-playing state arms one new ready toast for the next
    // loaded scene. Connection jitter / sample counts never imply a reload.
    const bool sceneWaiting = (view.connected && view.hasSample && view.sample.state != "playing" &&
        view.sample.state != "unsupported") || (!IsLive(view, now, config.staleMs) &&
        (view.healthStatus == "loading" || view.healthStatus == "discovering" || view.healthStatus == "waiting"));
    if (sceneWaiting) readyForScene_ = false;
    auto next = NextNotice(view, config, now);
    if (currentKey_.starts_with("local-") && next.kind != NoticeState::Error)
    {
        // The owning local source explicitly cleared its fault. Do not keep
        // that resolved message during the connection-recovery grace period.
        current_.reset(); currentKey_.clear();
    }
    if (next.kind == NoticeState::Silent || (next.kind == NoticeState::Waiting && !readyForScene_))
    {
        pendingKey_.clear();
        // Concrete faults are retained by their source until it reports
        // recovery. Normal loading/discovery must not retain a stale toast.
        current_.reset(); currentKey_.clear();
        return std::nullopt;
    }
    if (next.kind == NoticeState::Waiting)
    {
        if (next.key != pendingKey_) { pendingKey_ = next.key; pendingAt_ = now; }
        if (now - pendingAt_ < std::chrono::seconds(1)) return visible();
    }
    else pendingKey_.clear();
    if (next.kind == NoticeState::Ready)
    {
        if (readyForScene_ && (!current_ || !current_->error)) return visible();
        readyForScene_ = true;
    }
    if (next.key == currentKey_)
    {
        // Updated diagnostics from the same source must not freeze old text.
        current_ = std::move(next.notice);
        return visible();
    }
    currentKey_ = std::move(next.key);
    current_ = std::move(next.notice);
    shownAt_ = now;
    return visible();
}
}

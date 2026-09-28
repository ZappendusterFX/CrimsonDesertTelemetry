#include "overlay.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace cdt::overlay;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Config ReadTestConfig(const char* contents)
{
    std::array<wchar_t, MAX_PATH> directory{}, filename{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
    Require(length > 0 && length < directory.size(), "Temporary directory lookup");
    Require(GetTempFileNameW(directory.data(), L"cdt", 0, filename.data()) != 0, "Temporary INI creation");
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{filename.data()};
    if (contents)
    {
        std::ofstream file(cleanup.path, std::ios::trunc);
        file << contents;
        file.close();
        Require(file.good(), "Temporary INI write");
    }
    else Require(std::filesystem::remove(cleanup.path), "Missing-INI fixture setup");
    return LoadConfig(cleanup.path);
}
void ServerPortConfigurationTests()
{
    struct Mode
    {
        const char* name;
        const char* ini;
        bool hud, notifications, markers;
    };
    const std::array<Mode, 4> modes{{
        {"HUD only", "[Overlay]\nEnabled=1\n", true, false, false},
        {"notifications only", "[Notifications]\nEnabled=1\n", false, true, false},
        {"markers only", "[LightOverlay]\nEnabled=1\n", false, false, true},
        {"all enabled", "[Overlay]\nEnabled=1\n[Notifications]\nEnabled=1\n[LightOverlay]\nEnabled=1\n", true, true, true},
    }};
    struct PortCase { int requested, expected; };
    const std::array<PortCase, 5> ports{{{-1, 1024}, {0, 1024}, {1024, 1024}, {65535, 65535}, {65536, 65535}}};
    for (const auto& mode : modes)
        for (const auto& port : ports)
        {
            const std::string ini = std::string(mode.ini) + "[Server]\nPort=" + std::to_string(port.requested) + "\n";
            const auto config = ReadTestConfig(ini.c_str());
            const std::string context = std::string(mode.name) + ", Port=" + std::to_string(port.requested);
            Require(config.port == port.expected, ("HUD client port must match bootstrap's signed bounds: " + context).c_str());
            Require(config.enabled == mode.hud && config.notifications == mode.notifications && config.lightOverlay == mode.markers,
                ("Port parsing must preserve independent feature switches: " + context).c_str());
        }
    std::cout << "PASS server port bounds across HUD, notifications, markers and combined modes\n";
}
void ProjectionTests()
{
    Sample sample;
    sample.cameraPosition = Vec3{10, 20, 30};
    sample.cameraForward = Vec3{0, 0, 1}; sample.cameraRight = Vec3{1, 0, 0}; sample.cameraUp = Vec3{0, 1, 0};
    sample.fov = 90.f; sample.aspectRatio = 2.f; sample.nearPlane = .1f;
    const auto close = [](float actual, float expected) { return std::abs(actual - expected) < .01f; };
    auto point = ProjectWorld({10, 20, 40}, sample, 1000, 500);
    Require(point && close(point->x, 500) && close(point->y, 250) && close(point->depth, 10),
        "World point ahead must project to viewport center with camera depth");
    point = ProjectWorld({20, 20, 40}, sample, 1000, 500);
    Require(point && close(point->x, 750) && close(point->y, 250), "Rightward world projection and camera aspect");
    point = ProjectWorld({10, 25, 40}, sample, 1000, 500);
    Require(point && close(point->x, 500) && close(point->y, 125), "Upward world projection must decrease screen Y");
    point = ProjectWorld({50, 20, 40}, sample, 1000, 500);
    Require(point && point->x > 1000, "Offscreen projection is left to caller clipping");
    Require(!ProjectWorld({10, 20, 29}, sample, 1000, 500), "Behind-camera lights must disappear");
    Require(!ProjectWorld({10, 20, 30.05f}, sample, 1000, 500), "Lights inside camera near plane must disappear");
    auto invalid = sample; invalid.cameraPosition.reset();
    Require(!ProjectWorld({10, 20, 40}, invalid, 1000, 500), "Missing camera must not use zero position");
    invalid = sample; invalid.cameraRight = invalid.cameraUp;
    Require(!ProjectWorld({10, 20, 40}, invalid, 1000, 500), "Nonorthogonal camera basis must fail projection");
    invalid = sample; invalid.cameraRight = Vec3{-1, 0, 0};
    Require(!ProjectWorld({10, 20, 40}, invalid, 1000, 500), "Mirrored camera basis must fail projection");
    invalid = sample; invalid.cameraForward = Vec3{0, 0, 2};
    Require(!ProjectWorld({10, 20, 40}, invalid, 1000, 500), "Nonunit camera basis must fail projection");
    for (const auto bad : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        invalid = sample; invalid.aspectRatio = bad;
        Require(!ProjectWorld({10, 20, 40}, invalid, 1000, 500), "Invalid camera aspect accepted");
        invalid = sample; invalid.nearPlane = bad;
        Require(!ProjectWorld({10, 20, 40}, invalid, 1000, 500), "Invalid camera near plane accepted");
        invalid = sample; invalid.fov = bad;
        Require(!ProjectWorld({10, 20, 40}, invalid, 1000, 500), "Invalid camera FOV accepted");
    }
    invalid = sample; invalid.fov = 180.f;
    Require(!ProjectWorld({10, 20, 40}, invalid, 1000, 500), "Degenerate FOV accepted");
    invalid = sample; invalid.aspectRatio.reset();
    Require(!ProjectWorld({10, 20, 40}, invalid, 1000, 500), "Missing aspect must not be guessed");
    invalid = sample; invalid.nearPlane.reset();
    Require(!ProjectWorld({10, 20, 40}, invalid, 1000, 500), "Missing near plane must not be guessed");
    Require(!ProjectWorld({std::numeric_limits<float>::infinity(), 20, 40}, sample, 1000, 500),
        "Nonfinite world point accepted");
    Require(!ProjectWorld({10, 20, 40}, sample, 0, 500), "Empty viewport accepted");
    Require(!ProjectWorld({10, 20, 40}, sample, 1000, std::numeric_limits<float>::quiet_NaN()),
        "Nonfinite viewport accepted");
    // Turn 90 degrees: +X becomes forward and -Z becomes screen right.
    sample.cameraForward = Vec3{1, 0, 0}; sample.cameraRight = Vec3{0, 0, -1};
    point = ProjectWorld({20, 25, 20}, sample, 1000, 500);
    Require(point && close(point->x, 750) && close(point->y, 125) && close(point->depth, 10),
        "Rotated-camera projection ignored the current envelope basis");
    // Pitch upward 45 degrees while retaining a right-handed orthogonal basis.
    const float diagonal = std::sqrt(.5f);
    sample.cameraForward = Vec3{0, diagonal, diagonal}; sample.cameraRight = Vec3{1, 0, 0};
    sample.cameraUp = Vec3{0, diagonal, -diagonal};
    point = ProjectWorld({10, 30, 40}, sample, 1000, 500);
    Require(point && close(point->x, 500) && close(point->y, 250), "Pitched camera projection ignored vertical basis");
    std::cout << "PASS world projection, rotated camera, near/behind rejection and invalid metadata\n";
}
void FrustumTests()
{
    Sample sample;
    sample.cameraPosition = Vec3{10, 20, 30};
    sample.cameraForward = Vec3{0, 0, 1}; sample.cameraRight = Vec3{1, 0, 0}; sample.cameraUp = Vec3{0, 1, 0};
    sample.fov = 90.f; sample.aspectRatio = 2.f; sample.nearPlane = .1f;
    const auto close = [](const float a, const float b) { return std::abs(a - b) < .001f; };
    const auto checkProjection = [&](const Sample& camera)
    {
        const auto geometry = BuildCameraFrustum(camera, 10.f);
        Require(geometry.has_value(), "Valid 3D frustum rejected");
        const std::array<ScreenPoint, 4> expected{{{0, 0, 10}, {1000, 0, 10}, {1000, 500, 10}, {0, 500, 10}}};
        for (size_t index = 0; index < expected.size(); ++index)
        {
            const auto point = ProjectWorld(geometry->farCorners[index], camera, 1000, 500);
            Require(point && close(point->x, expected[index].x) && close(point->y, expected[index].y) &&
                close(point->depth, 10), "3D frustum corners must agree with camera projection and forward depth");
        }
        const auto center = ProjectWorld(geometry->farCenter, camera, 1000, 500);
        Require(center && close(center->x, 500) && close(center->y, 250), "Frustum center must follow camera forward");
        return *geometry;
    };
    const auto level = checkProjection(sample);
    Require(close(level.apex.x, 10) && close(level.apex.y, 20) && close(level.apex.z, 30) &&
        close(level.farCenter.z, 40) && close(level.farCorners[0].x, -10) &&
        close(level.farCorners[0].y, 30) && close(level.farCorners[2].y, 10),
        "Level frustum must retain camera apex and full vertical and horizontal FOV");
    const float steep = std::sqrt(.75f);
    auto pitched = sample;
    pitched.cameraForward = Vec3{0, -steep, .5f}; pitched.cameraUp = Vec3{0, .5f, steep};
    const auto down = checkProjection(pitched);
    Require(close(down.farCenter.y, 20 - 10 * steep) && close(down.farCenter.z, 35) &&
        down.farCorners[0].y < down.apex.y && down.farCorners[2].z < down.apex.z &&
        down.farCorners[0].y - down.farCorners[2].y > 9.9f,
        "Downward 60-degree camera must tilt the whole 3D frustum, not flatten an elevated ground wedge");
    pitched.cameraForward = Vec3{0, steep, .5f}; pitched.cameraUp = Vec3{0, .5f, -steep};
    const auto up = checkProjection(pitched);
    Require(close(up.farCenter.y, 20 + 10 * steep) && up.farCorners[2].y > up.apex.y,
        "Upward camera must raise both center and far plane");
    auto rolled = sample;
    rolled.cameraRight = Vec3{0, 1, 0}; rolled.cameraUp = Vec3{-1, 0, 0};
    const auto roll = checkProjection(rolled);
    Require(close(roll.farCorners[0].x, 0) && close(roll.farCorners[0].y, 0) &&
        close(roll.farCorners[1].y, 40), "Camera roll must rotate frustum right/up geometry in world space");
    auto narrow = sample; narrow.fov = 60.f; narrow.aspectRatio = 1.f;
    const auto square = checkProjection(narrow);
    narrow.aspectRatio = 3.f;
    const auto wide = checkProjection(narrow);
    Require(close(wide.farCorners[1].x - wide.farCenter.x, 3 * (square.farCorners[1].x - square.farCenter.x)) &&
        close(wide.farCorners[0].y, square.farCorners[0].y) && square.farCorners[0].y < level.farCorners[0].y,
        "Frustum aspect must scale width only and narrower FOV must reduce far-plane extent");
    for (const auto bad : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        Require(!BuildCameraFrustum(sample, bad), "Invalid schematic frustum length accepted");
        auto invalid = sample; invalid.fov = bad;
        Require(!BuildCameraFrustum(invalid, 10), "Invalid frustum FOV accepted");
        invalid = sample; invalid.aspectRatio = bad;
        Require(!BuildCameraFrustum(invalid, 10), "Invalid frustum aspect accepted");
        invalid = sample; invalid.nearPlane = bad;
        Require(!BuildCameraFrustum(invalid, 10), "Invalid frustum near plane accepted");
    }
    Require(!BuildCameraFrustum(sample, .1f), "Frustum far plane must be beyond its near plane");
    auto invalid = sample; invalid.cameraPosition.reset();
    Require(!BuildCameraFrustum(invalid, 10), "Missing camera must not produce a zero-apex frustum");
    invalid = sample; invalid.cameraRight = sample.cameraUp;
    Require(!BuildCameraFrustum(invalid, 10), "Nonorthogonal frustum basis accepted");
    invalid = sample; invalid.cameraRight = Vec3{-1, 0, 0};
    Require(!BuildCameraFrustum(invalid, 10), "Mirrored frustum basis accepted");
    invalid = sample; invalid.cameraForward = Vec3{0, 0, 2};
    Require(!BuildCameraFrustum(invalid, 10), "Nonunit frustum basis accepted");
    invalid = sample; invalid.cameraUp = Vec3{0, std::numeric_limits<float>::quiet_NaN(), 0};
    Require(!BuildCameraFrustum(invalid, 10), "Nonfinite frustum basis accepted");
    invalid = sample; invalid.aspectRatio.reset();
    Require(!BuildCameraFrustum(invalid, 10), "Missing frustum aspect must not be guessed");
    invalid = sample; invalid.fov = 180.f;
    Require(!BuildCameraFrustum(invalid, 10), "Degenerate frustum FOV accepted");
    invalid = sample; invalid.aspectRatio = std::numeric_limits<float>::max();
    Require(!BuildCameraFrustum(invalid, std::numeric_limits<float>::max()), "Overflowing world frustum accepted");
    std::cout << "PASS full world-space camera frustum, pitch, roll, FOV/aspect and shared projection validation\n";
}

void LightDetailGroupingTests()
{
    std::vector<LightRecord> records(7);
    const std::array<Vec3, 7> positions{{{0, 0, 0}, {0, .04f, 0}, {1, 0, 0}, {1.1f, 0, 0},
        {1.2f, 0, 0}, {0, 0, 2}, {0, 1, 0}}};
    std::vector<const LightRecord*> input;
    for (size_t index = 0; index < records.size(); ++index)
    {
        records[index].position = positions[index]; records[index].sampleIndex = static_cast<int>(index);
        records[index].kind = "spot"; records[index].colorLinear = {1, .3f, .1f}; records[index].luminanceLinear = 1.f;
        input.push_back(&records[index]);
    }
    const auto groups = GroupLightDetails(input);
    Require(groups.size() == 5, "Nearby pair must merge while far/depth/height-separated records and a chain tail stay distinct");
    std::array<size_t, 7> seen{};
    bool nearbyPair = false, chainPair = false;
    for (const auto& group : groups)
    {
        for (const auto member : group)
        {
            Require(member < seen.size(), "Grouped index no longer addresses original raw record");
            ++seen[member];
            for (const auto other : group)
                Require(NearbyForDetails(input[member]->position, input[other]->position),
                    "Complete-link groups must not merge a proximity chain beyond maximum pair distance");
        }
        nearbyPair = nearbyPair || (group == std::vector<size_t>{0, 1});
        chainPair = chainPair || (group == std::vector<size_t>{2, 3});
    }
    Require(nearbyPair && chainPair && std::all_of(seen.begin(), seen.end(), [](const auto n) { return n == 1; }),
        "Grouping must retain every raw member exactly once, including singletons, in stable spatial order");
    const auto signature = [](const auto& members, const auto& pointers)
    {
        std::vector<std::vector<std::array<float, 3>>> result;
        for (const auto& group : members)
        {
            auto& output = result.emplace_back();
            for (const auto index : group)
            {
                const auto position = pointers[index]->position;
                output.push_back({position.x, position.y, position.z});
            }
        }
        return result;
    };
    const auto original = signature(groups, input);
    for (size_t iteration = 0; iteration < records.size(); ++iteration)
    {
        std::rotate(input.begin(), input.begin() + 1, input.end());
        for (size_t index = 0; index < records.size(); ++index)
        {
            records[index].sampleIndex = 100 - static_cast<int>(index + iteration);
            records[index].colorLinear = {static_cast<float>(index + iteration), 2, 3};
            records[index].luminanceLinear = static_cast<float>(100 - iteration - index);
        }
        Require(signature(GroupLightDetails(input), input) == original,
            "GPU-index/input-order or RGB/luminance changes must not reorder spatial detail groups");
    }
    auto equivalent = records.front(); equivalent.sampleIndex = -1; equivalent.luminanceLinear = 999.f;
    equivalent.colorLinear = {99, 55, 12};
    Require(!SpatialLightLess(equivalent, records.front()) && !SpatialLightLess(records.front(), equivalent),
        "Spatial ordering must not use color, brightness or GPU index as a tiebreaker");
    Require(!NearbyForDetails({0, 0, 0}, {0, 0, .151f}) &&
        !NearbyForDetails({0, 0, 0}, {0, .151f, 0}), "Detail proximity must use all world dimensions");
    Require(!NearbyForDetails({0, 0, 0}, {std::numeric_limits<float>::quiet_NaN(), 0, 0}),
        "Nonfinite positions must not merge");
    Require(GroupLightDetails(input, -1.f).size() == input.size(), "Invalid grouping distance must retain singles");
    Require(GroupLightDetails({}).empty(), "Empty detail selection must stay empty");
    std::vector<LightRecord> many(80);
    std::vector<const LightRecord*> manyPointers;
    for (auto& record : many) manyPointers.push_back(&record);
    const auto bounded = GroupLightDetails(manyPointers);
    Require(bounded.size() == 1 && bounded.front().size() == 64,
        "Detail grouping must be bounded to the first 64 selected raw records");
    for (const auto index : bounded.front()) Require(index < 64, "Grouping read beyond its selected-record cap");
    std::cout << "PASS bounded complete-link detail groups, raw-member preservation and index/color-independent ordering\n";
}
void SourceVisibilityTests(nlohmann::json json,std::chrono::system_clock::time_point now)
{
    using Json=nlohmann::json;
    auto point=Json::parse(R"({"sampleIndex":3,"position":{"x":1,"y":2,"z":13},
        "colorLinear":{"x":2.5,"y":0.75,"z":0.1},"luminanceLinear":1.25,"kind":"point"})");
    const Json cameraPosition={{"x",1},{"y",2},{"z",3}};
    const Json metadata={{"status","clear"},{"attenuationFactor",1},{"reason",nullptr},
        {"referencePosition",cameraPosition},{"lightCaptureSequence",7},{"volumeSequence",12},
        {"contextFrame",41},{"volumeAgeMillisecondsAtCapture",1200},{"closestApproach",.25}};
    point["sourceVisibility"]=metadata;
    auto blocked=point;blocked["sampleIndex"]=4;blocked["position"]["z"]=-7;
    blocked["sourceVisibility"]["status"]="blocked";blocked["sourceVisibility"]["attenuationFactor"]=0;
    blocked["sourceVisibility"]["closestApproach"]=-.03;
    auto legacy=point;legacy["sampleIndex"]=5;legacy["position"]["z"]=17;legacy.erase("sourceVisibility");
    json["schemaVersion"]="1.4";json["player"]["position"]=cameraPosition;
    json["camera"]["aspectRatio"]=2.0;json["camera"]["nearPlane"]=.1;
    json["lights"]={{"status","available"},{"sources",Json::array()},
        {"rendered",{{"status","available"},{"captureSequence",7},{"ageMilliseconds",125},
            {"camera",{{"position",cameraPosition}}},{"sources",Json::array({point,blocked,legacy})}}}};
    View view;view.sample=ParseSample(json.dump(),now);view.connected=true;view.hasSample=true;view.received=Clock::now();
    const auto& records=*view.sample.renderedLights.records;
    Require(records.size()==3&&records[0].sourceVisibility&&records[0].sourceVisibility->status=="clear"&&
        records[1].sourceVisibility->attenuationFactor==0&&!records[2].sourceVisibility,
        "Optional source visibility or legacy compatibility lost");
    Require(records[0].colorLinear.x==2.5f&&records[1].colorLinear.x==2.5f&&records[1].luminanceLinear==1.25f,
        "Visibility changed raw HUD source colors or removed blocked records");
    Require(CurrentSourceVisibility(records[0],view,view.received).status=="clear"&&
        CurrentSourceVisibility(records[1],view,view.received).status=="blocked"&&
        CurrentSourceVisibility(records[2],view,view.received).status=="unknown",
        "HUD visibility verdicts lost or legacy source invented a verdict");
    Require(!ProjectWorld(records[1].position,view.sample,1000,500),"Behind-camera visibility fixture must be off-screen");
    const auto counts=CountSourceVisibility(view,view.received,35);
    Require(counts.visible==1&&counts.blocked==1&&counts.unknown==1,
        "Visibility legend excluded the off-screen blocked source");
    Require(!HideOccludedLight(records[1],view,view.received,false)&&
        HideOccludedLight(records[1],view,view.received,true)&&
        !HideOccludedLight(records[0],view,view.received,true)&&HideOccludedLight(records[2],view,view.received,true),
        "Filtered presentation must show only a fresh clear verdict");
    Require(records.size()==3&&records[1].colorLinear.x==2.5f&&
        CountSourceVisibility(view,view.received,35).blocked==1,
        "Presentation hide mutated raw records or visibility counts");
    const auto nearbyCounts=CountSourceVisibility(view,view.received,12);
    Require(nearbyCounts.visible==1&&nearbyCounts.blocked==1&&nearbyCounts.unknown==0,"Visibility count ignored configured radius");
    Require(CurrentSourceVisibility(records[0],view,view.received+std::chrono::milliseconds(175)).status=="clear",
        "Exact 1500 ms visibility boundary rejected");
    const auto stale=CurrentSourceVisibility(records[1],view,view.received+std::chrono::milliseconds(176));
    Require(stale.status=="unknown"&&stale.reason=="stale-volume"&&!stale.attenuationFactor&&
        RenderedLightsLive(view,view.received+std::chrono::milliseconds(176),1000),
        "Expired visibility removed fresh raw data or retained a blocked contribution factor");
    Require(HideOccludedLight(records[1],view,view.received+std::chrono::milliseconds(176),true),
        "An expired blocker became visible without a fresh clear verdict");
    const auto expiredCounts=CountSourceVisibility(view,view.received+std::chrono::milliseconds(176),35);
    Require(expiredCounts.visible==0&&expiredCounts.blocked==0&&expiredCounts.unknown==3,
        "Legend retained expired visibility counts");
    auto delayed=view;delayed.sample.sourceAgeMs=176;
    Require(CurrentSourceVisibility(records[0],delayed,view.received).status=="unknown",
        "Visibility ignored transport age");
    auto checkInvalid=[&](Json changed)
    {
        const auto parsed=ParseSample(changed.dump(),now);
        Require(parsed.renderedLights.status=="available"&&parsed.renderedLights.records&&
            parsed.renderedLights.records->size()==3&&parsed.renderedLights.records->at(0).colorLinear.x==2.5f,
            "Malformed optional visibility discarded healthy raw HUD records");
        const auto& visibility=parsed.renderedLights.records->at(0).sourceVisibility;
        Require(visibility&&visibility->status=="unknown"&&visibility->reason=="invalid-metadata"&&
            !visibility->attenuationFactor,"Malformed visibility invented suppression or a known verdict");
    };
    for(const auto& bad: {Json(nullptr),Json(true),Json("bad"),Json(-1),Json(.5),Json(1e100)})
    {
        auto invalid=json;invalid["lights"]["rendered"]["sources"][0]["sourceVisibility"]["attenuationFactor"]=bad;
        checkInvalid(invalid);
    }
    for(const auto& bad: {Json(nullptr),Json(-1),Json(1e100)})
    {
        auto invalid=json;invalid["lights"]["rendered"]["sources"][0]["sourceVisibility"]["volumeAgeMillisecondsAtCapture"]=bad;
        checkInvalid(invalid);
    }
    auto invalid=json;invalid["lights"]["rendered"]["sources"][0]["sourceVisibility"]["lightCaptureSequence"]=8;checkInvalid(invalid);
    invalid=json;invalid["lights"]["rendered"]["sources"][0]["sourceVisibility"]["referencePosition"]["x"]=99;checkInvalid(invalid);
    invalid=json;invalid["lights"]["rendered"].erase("captureSequence");checkInvalid(invalid);
    invalid=json;invalid["lights"]["rendered"]["sources"][0]["sourceVisibility"]["status"]="unknown";checkInvalid(invalid);
    invalid=json;invalid["lights"]["rendered"]["sources"][0]["sourceVisibility"]=nullptr;checkInvalid(invalid);
    auto physics=json;
    auto& pm=physics["lights"]["rendered"]["sources"][0]["sourceVisibility"];
    pm=metadata;pm["method"]="physics-ray-fan";pm["sampleCount"]=9;pm["clearSampleCount"]=4;
    pm["volumeSequence"]=nullptr;pm["closestApproach"]=nullptr;pm["lightCaptureSequence"]=6;
    pm["volumeAgeMillisecondsAtCapture"]=100;
    auto physicsParsed=ParseSample(physics.dump(),now);
    Require(physicsParsed.renderedLights.records->at(0).sourceVisibility->physicsSampled&&
        physicsParsed.renderedLights.records->at(0).sourceVisibility->status=="clear",
        "Sampled visibility must preserve honest older measurement provenance");
    pm["status"]="blocked";pm["attenuationFactor"]=0;pm["clearSampleCount"]=0;
    View physicsView=view;physicsView.sample=ParseSample(physics.dump(),now);
    Require(HideOccludedLight(physicsView.sample.renderedLights.records->at(0),physicsView,physicsView.received,true),
        "Physics blocking not connected to HUD hide toggle");
    pm["referencePosition"]["x"]=1.3;
    physicsView.sample=ParseSample(physics.dump(),now);
    Require(HideOccludedLight(physicsView.sample.renderedLights.records->at(0),physicsView,physicsView.received,true),
        "HUD must use latest complete raw physics result without movement reset");
    pm["referencePosition"]=cameraPosition;pm["clearSampleCount"]=1;checkInvalid(physics);
    pm["clearSampleCount"]=0;pm["lightCaptureSequence"]=8;checkInvalid(physics);
    pm["lightCaptureSequence"]=6;pm["volumeAgeMillisecondsAtCapture"]=500;
    physicsView.sample=ParseSample(physics.dump(),now);
    Require(HideOccludedLight(physicsView.sample.renderedLights.records->at(0),physicsView,physicsView.received,true),
        "Physics age must not double-count unrelated GPU capture age");
    Require(HideOccludedLight(physicsView.sample.renderedLights.records->at(0),physicsView,physicsView.received+std::chrono::milliseconds(1),true),
        "Expired physics clear/blocked evidence must not become a visible unknown");
    pm["volumeAgeMillisecondsAtCapture"]=501;
    physicsView.sample=ParseSample(physics.dump(),now);
    Require(HideOccludedLight(physicsView.sample.renderedLights.records->at(0),physicsView,physicsView.received,true),
        "Stale physics evidence must not show in the filtered presentation");
    pm["status"]="unknown";pm["attenuationFactor"]=nullptr;
    pm["reason"]="waiting-for-physics";pm["volumeAgeMillisecondsAtCapture"]=nullptr;
    physicsView.sample=ParseSample(physics.dump(),now);
    const auto& provisional=physicsView.sample.renderedLights.records->at(0);
    Require(CurrentSourceVisibility(provisional,physicsView,physicsView.received).status=="unknown"&&
        HideOccludedLight(provisional,physicsView,physicsView.received,true)&&
        !HideOccludedLight(provisional,physicsView,physicsView.received,false),
        "Unknown must hide only from the filtered presentation, never raw data");
    pm["status"]="clear";pm["attenuationFactor"]=1;
    pm["reason"]=nullptr;pm["volumeAgeMillisecondsAtCapture"]=0;
    pm["clearSampleCount"]=1;
    physicsView.sample=ParseSample(physics.dump(),now);
    Require(!HideOccludedLight(physicsView.sample.renderedLights.records->at(0),physicsView,physicsView.received,true),
        "Fresh clear physics result must be visible in the filtered presentation");
    std::cout<<"PASS HUD source visibility metadata, physics estimates, hiding, freshness and malformed preservation\n";
}
void ShortcutTests()
{
    for(const int key:{119,120,121,122,72})
    {
        bool value=false,wasDown=false;
        UpdateShortcutToggle(value,wasDown,key,true,true);Require(value,"Shortcut did not toggle on press");
        UpdateShortcutToggle(value,wasDown,key,true,true);Require(value,"Held shortcut repeatedly toggled");
        UpdateShortcutToggle(value,wasDown,key,false,true);
        UpdateShortcutToggle(value,wasDown,key,true,true);Require(!value,"Shortcut did not reverse on second press");
        UpdateShortcutToggle(value,wasDown,key,false,true);
        UpdateShortcutToggle(value,wasDown,key,true,false);Require(!value,"Background shortcut changed a view");
        UpdateShortcutToggle(value,wasDown,key,true,true);Require(!value,"Focus arrival consumed a held background key");
        UpdateShortcutToggle(value,wasDown,key,false,true);
        UpdateShortcutToggle(value,wasDown,0,true,true);Require(!value&&!wasDown,"Disabled shortcut accepted a press");
    }
    Require(ShortcutLabel(0)=="shortcut off"&&ShortcutLabel(122)=="F11"&&ShortcutLabel(72)=="H",
        "Shortcut labels ignored disabled or remapped keys");
    std::cout<<"PASS F8-F11 edge toggles, background isolation, remapping and disabled shortcuts\n";
}
void RenderedLightTests(nlohmann::json json, std::chrono::system_clock::time_point now)
{
    using Json = nlohmann::json;
    const auto point = Json::parse(R"({"sampleIndex":3,"position":{"x":1,"y":2,"z":13},
        "colorLinear":{"x":2.5,"y":0.75,"z":0.1},"luminanceLinear":1.25,"kind":"point"})");
    auto spot = point;
    spot["sampleIndex"] = 9; spot["kind"] = "spot";
    spot["direction"] = {{"x", 0}, {"y", -1}, {"z", 0}}; spot["coneHalfAngleDegrees"] = 27;
    auto unknown = point; unknown["sampleIndex"] = 32767; unknown.erase("kind");
    json["schemaVersion"] = "1.4";
    json["camera"]["aspectRatio"] = 2.0; json["camera"]["nearPlane"] = .1;
    json["lights"] = {{"status", "available"}, {"sources", Json::array({point})},
        {"rendered", {{"status", "available"}, {"ageMilliseconds", 125}, {"sources", Json::array({point, spot, unknown})},
            {"camera", {{"position", {{"x", 1000}, {"y", 2000}, {"z", 3000}}}}}}}};
    auto sample = ParseSample(json.dump(), now);
    Require(sample.renderedLights.records && sample.renderedLights.records->size() == 3 &&
        sample.renderedLights.publishedRecords == 3 && !sample.authoredLights.records,
        "Only rendered contributions may enter immutable drawable records; never sum authored records");
    Require(sample.aspectRatio == 2.f && sample.nearPlane && std::abs(*sample.nearPlane - .1f) < .0001f,
        "Envelope camera projection metadata was not parsed");
    const auto& first = sample.renderedLights.records->at(0);
    Require(first.sampleIndex == 3 && first.position.z == 13 && first.colorLinear.x == 2.5f &&
        first.luminanceLinear == 1.25f && first.kind == "point" && !first.direction && !first.coneHalfAngleDegrees,
        "Rendered world position/HDR color must be preserved without applying paired camera translation again");
    const auto& second = sample.renderedLights.records->at(1);
    Require(second.direction && second.direction->y == -1 && second.coneHalfAngleDegrees == 27.f && second.kind == "spot",
        "Rendered spot emission direction and cone were not preserved");
    Require(sample.renderedLights.records->at(2).kind.empty(), "Unknown light kind must remain unknown");
    const auto projected = ProjectWorld(first.position, sample, 1000, 500);
    Require(projected && std::abs(projected->x - 500) < .01 && std::abs(projected->y - 250) < .01,
        "World lights must project with the latest envelope camera, not the old paired camera");
    View view; view.connected = true; view.hasSample = true; view.sample = sample;
    view.received = Clock::time_point{} + std::chrono::seconds(10);
    const View copy = view;
    Require(copy.sample.renderedLights.records == view.sample.renderedLights.records,
        "Rendering View copies must share immutable light storage");
    Require(RenderedLightsLive(view, view.received, 1000), "Fresh rendered data should be drawable");
    Require(RenderedLightsLive(view, view.received + std::chrono::milliseconds(375), 1000), "500ms freshness boundary");
    Require(!RenderedLightsLive(view, view.received + std::chrono::milliseconds(376), 1000), "Expired light data persisted");
    view.sample.sourceAgeMs = 200;
    Require(RenderedLightsLive(view, view.received + std::chrono::milliseconds(175), 1000), "Transport-age freshness boundary");
    Require(!RenderedLightsLive(view, view.received + std::chrono::milliseconds(176), 1000),
        "Transport delay must contribute to rendered staleness");
    Require(!RenderedLightsLive(view, view.received, 100), "Stale camera envelope must hide light markers");
    view.sample.sourceAgeMs = 0;
    view.connected = false;
    Require(!RenderedLightsLive(view, view.received, 1000), "Disconnected renderer must clear light markers");
    view.connected = true; view.sample.state = "loading";
    Require(!RenderedLightsLive(view, view.received, 1000), "Loading must hide light markers");
    const auto rejectFeed = [&](Json invalid)
    {
        const auto parsed = ParseSample(invalid.dump(), now);
        Require(parsed.playerPosition && parsed.cameraPosition && parsed.renderedLights.status == "invalid" &&
            !parsed.renderedLights.records && !parsed.renderedLights.publishedRecords,
            "Malformed rendered records must clear their feed while preserving core player/camera");
    };
    for (const auto& bad : {Json(nullptr), Json(true), Json("bad"), Json(1e100)})
    {
        auto invalid = json; invalid["lights"]["rendered"]["sources"][0]["position"]["x"] = bad; rejectFeed(invalid);
        invalid = json; invalid["lights"]["rendered"]["sources"][0]["colorLinear"]["x"] = bad; rejectFeed(invalid);
    }
    auto invalid = json; invalid["lights"]["rendered"]["sources"][0]["colorLinear"]["x"] = -1; rejectFeed(invalid);
    invalid = json; invalid["lights"]["rendered"]["sources"][0]["luminanceLinear"] = -1; rejectFeed(invalid);
    invalid = json; invalid["lights"]["rendered"]["sources"][0]["sampleIndex"] = 32768; rejectFeed(invalid);
    invalid = json; invalid["lights"]["rendered"]["sources"][0]["sampleIndex"] = 1.5; rejectFeed(invalid);
    invalid = json; invalid["lights"]["rendered"]["sources"][1]["sampleIndex"] = 3; rejectFeed(invalid);
    invalid = json; invalid["lights"]["rendered"]["sources"][1]["direction"]["y"] = 0; rejectFeed(invalid);
    invalid = json; invalid["lights"]["rendered"]["sources"][1]["coneHalfAngleDegrees"] = 91; rejectFeed(invalid);
    invalid = json; invalid["lights"]["rendered"]["sources"][0]["kind"] = "invented"; rejectFeed(invalid);
    invalid = json; invalid["lights"]["rendered"]["sources"][0].erase("luminanceLinear"); rejectFeed(invalid);
    invalid = json; invalid["lights"]["rendered"].erase("ageMilliseconds"); rejectFeed(invalid);
    invalid = json; invalid["lights"]["rendered"]["ageMilliseconds"] = 501; rejectFeed(invalid);
    invalid = json; invalid["lights"]["rendered"]["sources"] = Json(std::vector<std::nullptr_t>(32769)); rejectFeed(invalid);
    auto empty = json; empty["lights"]["rendered"]["sources"] = Json::array();
    view.sample = ParseSample(empty.dump(), now);
    Require(RenderedLightsLive(view, view.received, 1000) && view.sample.renderedLights.records->empty(),
        "Available zero-current-contribution sample must not be confused with missing data");
    json["lights"]["rendered"] = {{"status", "unavailable"}, {"unavailableReason", "bridge-changing"}};
    sample = ParseSample(json.dump(), now);
    Require(!sample.renderedLights.records && !sample.renderedLights.publishedRecords,
        "Unavailable rendered snapshot must not carry forward prior successful records");
    view.sample = sample;
    Require(!RenderedLightsLive(view, view.received, 1000), "Unavailable rendered snapshot must not remain drawable");
    json["lights"].erase("rendered");
    sample = ParseSample(json.dump(), now);
    Require(!sample.renderedLights.records && sample.renderedLights.status == "not-reported",
        "A newly missing feed must not carry forward records");
    json.erase("lights"); json["schemaVersion"] = "1.1";
    sample = ParseSample(json.dump(), now);
    Require(sample.playerPosition && !sample.renderedLights.records, "Schema1.1 core compatibility was lost");
    std::cout << "PASS bounded current rendered records, HDR/color/direction, transport freshness and invalid-feed isolation\n";
}
void UpstreamLightTests(nlohmann::json json,std::chrono::system_clock::time_point now)
{
    using Json=nlohmann::json;
    const Json cameraPosition={{"x",1},{"y",2},{"z",3}};
    auto group=Json::parse(R"({"sampleIndex":1360,"type":"group","memberCount":16,"position":{"x":1,"y":2,"z":13},
        "colorLinear":{"x":2.1,"y":0.65,"z":0.16},"luminanceLinear":0.93,"kind":"point",
        "rendererSelected":true,"renderedSampleIndex":4})");
    // Behind the camera and therefore absent from the filtered output: still current.
    auto behind=Json::parse(R"({"sampleIndex":1513,"type":"standalone","position":{"x":1,"y":2,"z":-7},
        "colorLinear":{"x":0.37,"y":0.11,"z":0.03},"luminanceLinear":0.16,"kind":"point","rendererSelected":false})");
    const Json rendered={{"sampleIndex",4},{"position",{{"x",1.2},{"y",2},{"z",13}}},
        {"colorLinear",{{"x",2.1},{"y",.65},{"z",.16}}},{"luminanceLinear",.93},{"kind","point"}};
    json["schemaVersion"]="1.6";json["player"]["position"]=cameraPosition;
    json["camera"]["aspectRatio"]=2.0;json["camera"]["nearPlane"]=.1;
    json["lights"]={{"status","available"},{"sources",Json::array()},
        {"rendered",{{"status","available"},{"captureSequence",7},{"ageMilliseconds",125},
            {"camera",{{"position",cameraPosition}}},{"sources",Json::array({rendered})}}},
        {"upstream",{{"status","available"},{"source","manylights-input"},{"captureSequence",7},
            {"ageMilliseconds",125},{"inputRecords",1770},{"sources",Json::array({group,behind})}}}};
    View view;view.sample=ParseSample(json.dump(),now);view.connected=true;view.hasSample=true;view.received=Clock::now();
    const auto& records=*view.sample.upstreamLights.records;
    Require(view.sample.schemaVersion=="1.6"&&records.size()==2&&records[0].memberCount==16&&records[0].rendererSelected&&
        records[1].memberCount==0&&!records[1].rendererSelected&&records[1].colorLinear.x==.37f,
        "Engine-input group/standalone and renderer selection were not preserved");
    Require(DisplayLights(view,view.received,1000)==&view.sample.upstreamLights,
        "Fresh all-around engine input must be the drawable light set");
    Require(!ProjectWorld(records[1].position,view.sample,1000,500),"Behind-camera fixture must be off-screen");
    const auto counts=CountSourceVisibility(view,view.received,35);
    Require(counts.unknown==2&&counts.visible==0&&counts.blocked==0,
        "Legend must count every current engine light, including the unselected one behind the camera");
    Require(LightFeedStatus(view,view.received,1000)=="LIVE ENGINE LIGHTS","Engine-input status label");
    // Stale or unavailable input falls back to the filtered output, labelled honestly.
    Require(DisplayLights(view,view.received+std::chrono::milliseconds(376),1000)==nullptr,
        "Expired input and output persisted");
    auto fallback=json;fallback["lights"]["upstream"]={{"status","unavailable"},{"source","manylights-input"},
        {"unavailableReason","input-unavailable"}};
    View fallbackView=view;fallbackView.sample=ParseSample(fallback.dump(),now);
    Require(DisplayLights(fallbackView,view.received,1000)==&fallbackView.sample.renderedLights&&
        fallbackView.sample.upstreamLights.unavailableReason=="input-unavailable"&&
        LightFeedStatus(fallbackView,view.received,1000)=="RENDERED ONLY / ENGINE INPUT UNAVAILABLE",
        "Unavailable input must fall back to filtered output without claiming all-around coverage");
    const auto reject=[&](Json invalid,const char* message)
    {
        const auto parsed=ParseSample(invalid.dump(),now);
        Require(parsed.upstreamLights.status=="invalid"&&!parsed.upstreamLights.records&&
            parsed.renderedLights.status=="available"&&parsed.renderedLights.records&&parsed.playerPosition,message);
    };
    auto invalid=json;invalid["lights"]["upstream"]["sources"][0].erase("memberCount");
    reject(invalid,"A group without member count was accepted");
    invalid=json;invalid["lights"]["upstream"]["sources"][1]["memberCount"]=3;
    reject(invalid,"A standalone record with members was accepted");
    invalid=json;invalid["lights"]["upstream"]["sources"][1].erase("rendererSelected");
    reject(invalid,"Missing renderer selection was accepted");
    invalid=json;invalid["lights"]["upstream"]["sources"][1]["rendererSelected"]=1;
    reject(invalid,"Numeric renderer selection was accepted");
    invalid=json;invalid["lights"]["upstream"]["sources"][1]["type"]="invented";
    reject(invalid,"Unknown light type was accepted");
    invalid=json;invalid["lights"]["upstream"]["ageMilliseconds"]=501;
    reject(invalid,"Stale engine input was accepted");
    // Notices: a refused input is an actionable error; a missing sample only waits.
    Config config;config.notifications=true;config.lightsExpected=true;config.renderedExpected=true;
    config.upstreamExpected=true;config.notificationDurationMs=6000;
    auto refused=fallback;refused["lights"]["upstream"]["unavailableReason"]="input-refused";
    View refusedView=fallbackView;refusedView.sample=ParseSample(refused.dump(),now);
    NoticeTracker tracker;
    const auto notice=tracker.Update(refusedView,config,view.received);
    Require(notice&&notice->error&&notice->title=="All-around light input is unavailable",
        "Refused engine input must be reported, not silently shown as complete coverage");
    NoticeTracker waiting;
    const auto transient=waiting.Update(fallbackView,config,view.received);
    Require(!transient||transient->title!="All-around light input is unavailable",
        "A single unavailable input sample must not raise the refusal error");
    std::cout<<"PASS HUD engine-input lights: preference, fallback, behind-camera counts, selection flags, isolation and notices\n";
}
void NoticeTests()
{
    Config config;
    config.notifications = true;
    config.lightsExpected = true; config.renderedExpected = true;
    config.notificationDurationMs = 6000;
    config.enabled = false;
    const auto start = Clock::time_point{} + std::chrono::seconds(10);
    View view;
    NoticeTracker tracker;
    auto notice = tracker.Update(view, config, start);
    Require(!notice && !tracker.Update(view, config, start + std::chrono::seconds(60)),
        "Normal startup must remain silent even after sixty seconds");
    view.connected = true;
    Require(!tracker.Update(view, config, start + std::chrono::seconds(120)),
        "Connected discovery without a sample must remain silent");
    view.connected = true; view.hasSample = true; view.sample.state = "loading";
    Require(!tracker.Update(view, config, start + std::chrono::seconds(121)) &&
        !tracker.Update(view, config, start + std::chrono::seconds(181)),
        "Long scene loading must never show a loading notice or timeout");
    view.sample.state = "playing";
    view.sample.playerPosition = Vec3{1, 2, 3}; view.sample.cameraPosition = Vec3{4, 5, 6};
    view.sample.authoredLights.status = "available"; view.sample.authoredLights.publishedRecords = 0;
    view.sample.renderedLights.status = "available"; view.sample.renderedLights.publishedRecords = 0;
    view.sample.renderedLights.records = std::make_shared<const std::vector<LightRecord>>();
    view.sample.renderedLights.ageMilliseconds = 0;
    const auto fresh = [&](Clock::time_point when)
    {
        view.received = when;
        ++view.sample.sequence;
        return tracker.Update(view, config, when);
    };
    view.sample.renderedLights.status = "unavailable";
    view.sample.renderedLights.unavailableReason = "bridge-waiting";
    Require(!fresh(start + std::chrono::seconds(182)) && !fresh(start + std::chrono::seconds(242)),
        "Initial light discovery must not become an arbitrary timeout");
    view.sample.renderedLights.status = "available";
    view.sample.renderedLights.unavailableReason.clear();
    notice = fresh(start + std::chrono::seconds(243));
    Require(notice && notice->title == "Telemetry is ready" && !notice->error,
        "Loading-to-ready success notice missing");
    view.sample.renderedLights.publishedRecords = 400;
    Require(fresh(start + std::chrono::milliseconds(248999)).has_value(), "Ready notice expired before six seconds");
    Require(!fresh(start + std::chrono::seconds(249)), "Ready notice did not expire at six seconds");
    Require(!fresh(start + std::chrono::seconds(250)), "Sequence/count changes retriggered the success notice");
    view.sample.renderedLights.status = "unavailable";
    view.sample.renderedLights.unavailableReason = "legacy-plugin-conflict";
    notice = fresh(start + std::chrono::seconds(251));
    Require(notice && notice->error && notice->detail.find("mod manager") != std::string::npos,
        "Legacy conflict must explain the corrective action");
    notice = fresh(start + std::chrono::seconds(281));
    Require(notice && notice->error, "Actionable error vanished before recovery");
    view.sample.renderedLights.status = "available";
    view.sample.renderedLights.unavailableReason.clear();
    notice = fresh(start + std::chrono::seconds(282));
    Require(notice && notice->title == "Telemetry is ready", "Recovery should emit one new ready notice");
    Require(!fresh(start + std::chrono::seconds(289)), "Recovery ready notice did not expire");
    // A brief interruption must neither replace the screen with a stale notice
    // nor cause another success toast when the next sample arrives.
    Require(!tracker.Update(view, config, start + std::chrono::milliseconds(291001)),
        "Transient stale data was not debounced");
    Require(!fresh(start + std::chrono::milliseconds(291500)), "Brief stale recovery retriggered ready");
    Require(!tracker.Update(view, config, start + std::chrono::seconds(294)), "Stale debounce did not start");
    notice = tracker.Update(view, config, start + std::chrono::seconds(295));
    Require(notice && notice->error && notice->title == "Telemetry stopped updating", "Persistent stale data was hidden");
    notice = fresh(start + std::chrono::seconds(296));
    Require(notice && notice->title == "Telemetry is ready", "Stale recovery was not reported");
    view.sample.state = "loading";
    Require(!fresh(start + std::chrono::seconds(297)) && !fresh(start + std::chrono::seconds(357)),
        "Reload must clear the old ready toast and stay silent");
    view.sample.state = "playing";
    notice = fresh(start + std::chrono::seconds(358));
    Require(notice && notice->title == "Telemetry is ready", "Second loaded scene did not report ready");
    config.notifications = false;
    Require(!fresh(start + std::chrono::seconds(359)), "Disabled notices must disappear");

    config.notifications = true;
    view.sample.renderedLights.status = "unavailable";
    view.sample.renderedLights.unavailableReason = "bridge-waiting";
    Require(!fresh(start + std::chrono::seconds(360)) && !fresh(start + std::chrono::seconds(420)),
        "Re-enabled initial discovery should stay silent without a proven fault");
    config.renderedExpected = false;
    view.sample.renderedLights.unavailableReason = "game-stopped";
    notice = fresh(start + std::chrono::seconds(421));
    Require(notice && notice->title == "Telemetry is ready" && !notice->error,
        "Intentionally disabled ManyLights must not become a capture error");

    Config healthConfig; healthConfig.notifications = true;
    View unhealthy; unhealthy.healthStatus = "unsupported-build";
    unhealthy.healthError = "Install the updated telemetry plugin.";
    NoticeTracker healthTracker;
    notice = healthTracker.Update(unhealthy, healthConfig, start);
    Require(notice && notice->error && notice->detail == unhealthy.healthError,
        "Unsupported build must be visible before any sample exists");
    unhealthy = View{}; unhealthy.hasSample = true; unhealthy.sample.state = "unsupported";
    NoticeTracker unsupportedSample;
    Require(unsupportedSample.Update(unhealthy, healthConfig, start)->error,
        "An unsupported sample must not be presented as loading");

    Publish(View{});
    SetLocalFault("bootstrap", "Telemetry host files are missing", "Reinstall the complete package.");
    View local;
    Require(TryRead(local) && !local.connected && local.localFaults.size() == 1,
        "A bootstrap fault must reach the overlay without a server");
    NoticeTracker localTracker;
    notice = localTracker.Update(local, healthConfig, start);
    Require(notice && notice->error && notice->title == "Telemetry host files are missing",
        "Missing-host bootstrap failure was not displayed immediately");
    Publish(View{});
    Require(TryRead(local) && local.localFaults.size() == 1 &&
        localTracker.Update(local, healthConfig, start + std::chrono::seconds(60))->error,
        "Reconnect publication or elapsed time cleared a local fault");
    healthConfig.notifications = false;
    Require(!localTracker.Update(local, healthConfig, start), "Explicit opt-out must suppress even bootstrap faults");
    healthConfig.notifications = true;
    ClearLocalFault("bootstrap");
    Require(TryRead(local) && local.localFaults.empty(), "Local fault recovery did not clear its source");
    Require(!localTracker.Update(local, healthConfig, start + std::chrono::seconds(61)),
        "A resolved local bootstrap failure remained on screen");
    View oldHost;
    oldHost.connected = true; oldHost.hasSample = true; oldHost.received = start;
    oldHost.sample.state = "playing"; oldHost.sample.schemaVersion = "1.1";
    oldHost.sample.playerPosition = Vec3{1, 2, 3}; oldHost.sample.cameraPosition = Vec3{4, 5, 6};
    Config expected; expected.notifications = true;
    NoticeTracker expectedTracker;
    notice = expectedTracker.Update(oldHost, expected, start);
    Require(notice && notice->title == "Telemetry is ready" && !notice->error,
        "An older host remains compatible when lights are not requested");
    expected.lightsExpected = true; expected.renderedExpected = true;
    notice = expectedTracker.Update(oldHost, expected, start);
    Require(notice && notice->error && notice->title == "Requested light data is missing",
        "An older host must not report ready when requested lights are missing");
    oldHost.sample.schemaVersion = "1.3";
    oldHost.sample.authoredLights.status = "available";
    notice = expectedTracker.Update(oldHost, expected, start);
    Require(notice && notice->error, "Missing requested rendered feed was hidden by authored light availability");
    expected.renderedExpected = false;
    notice = expectedTracker.Update(oldHost, expected, start);
    Require(notice && !notice->error && notice->title == "Telemetry is ready",
        "An intentionally unrequested rendered feed prevented readiness");
    oldHost.received = start;
    expected.notificationDurationMs = 1;
    NoticeTracker minimumDuration;
    Require(minimumDuration.Update(oldHost, expected, start).has_value(), "Ready duration fixture");
    oldHost.received = start + std::chrono::milliseconds(4999);
    Require(minimumDuration.Update(oldHost, expected, oldHost.received).has_value(), "Ready duration must clamp to at least five seconds");
    oldHost.received = start + std::chrono::seconds(5);
    Require(!minimumDuration.Update(oldHost, expected, oldHost.received), "Five-second minimum did not expire");
    expected.notificationDurationMs = 60000;
    NoticeTracker maximumDuration;
    Require(maximumDuration.Update(oldHost, expected, oldHost.received).has_value(), "Maximum ready duration fixture");
    oldHost.received = start + std::chrono::seconds(15);
    Require(!maximumDuration.Update(oldHost, expected, oldHost.received), "Ready duration must clamp to at most ten seconds");
    std::cout << "PASS notification lifecycle, error recovery, debounce and HUD independence\n";
}
void SnapshotFileTest(const char* filename)
{
    std::ifstream input(filename);
    Require(input.good(), "Recorded snapshot fixture could not be opened");
    std::string line;
    Require(static_cast<bool>(std::getline(input, line)), "Recorded snapshot fixture is empty");
    const auto wrapper = nlohmann::json::parse(line);
    const auto& json = wrapper.at("snapshot");
    const auto sample = ParseSample(json.dump(), std::chrono::system_clock::now());
    Require(sample.renderedLights.status == "available" && sample.renderedLights.records &&
        sample.renderedLights.records->size() == json.at("lights").at("rendered").at("sources").size(),
        "Actual recorded rendered sources were rejected or changed in count");
    size_t projected = 0, visible = 0;
    for (const auto& record : *sample.renderedLights.records)
    {
        const auto point = ProjectWorld(record.position, sample, 3840, 2160);
        if (!point) continue;
        ++projected;
        if (point->x >= 0 && point->x < 3840 && point->y >= 0 && point->y < 2160) ++visible;
    }
    Require(projected > 0 && visible > 0, "Recorded camera/light sample did not produce any visible projections");
    std::cout << "PASS recorded snapshot: " << sample.renderedLights.records->size() << " current contributions, "
        << projected << " in front of camera, " << visible << " within viewport\n";
}
int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string_view(argv[1]) == "--snapshot")
        {
            SnapshotFileTest(argv[2]);
            return 0;
        }
        Require(argc == 1, "Usage: overlay-tests [--snapshot recorded.jsonl]");
        Config config;
        Require(!config.enabled, "HUD must default to disabled");
        Require(!ReadTestConfig(nullptr).enabled, "Missing INI must disable HUD");
        Require(!ReadTestConfig("").enabled, "Empty INI must disable HUD");
        Require(!ReadTestConfig("[Server]\nEnabled=1\n").enabled, "Server enable must not enable HUD");
        Require(!ReadTestConfig("[Overlay]\nInitiallyVisible=1\n").enabled, "Visibility must not implicitly enable HUD");
        Require(!ReadTestConfig("[Overlay]\nEnabled=0\nInitiallyVisible=1\n").enabled, "Explicitly disabled HUD");
        const auto enabled = ReadTestConfig("[Server]\nPort=27329\n[Overlay]\nEnabled=1\n");
        Require(enabled.enabled && enabled.visible && enabled.port == 27329, "Explicit opt-in preserves HUD configuration");
        ServerPortConfigurationTests();
        const auto hidden = ReadTestConfig("[Overlay]\nEnabled=1\nInitiallyVisible=0\n");
        Require(enabled.hdrPaperWhiteNits == 200.f && config.hdrPaperWhiteNits == 200.f,
            "HDR UI paper white must have a safe 200 nit default");
        Require(ReadTestConfig("[Overlay]\nHdrPaperWhiteNits=250\n[Notifications]\nEnabled=1\n").hdrPaperWhiteNits == 250.f,
            "Status-only rendering must retain HDR brightness configuration");
        Require(ReadTestConfig("[Overlay]\nEnabled=1\nHdrPaperWhiteNits=0\n").hdrPaperWhiteNits == 80.f &&
            ReadTestConfig("[Overlay]\nEnabled=1\nHdrPaperWhiteNits=9999\n").hdrPaperWhiteNits == 500.f &&
            ReadTestConfig("[Overlay]\nEnabled=1\nHdrPaperWhiteNits=nan\n").hdrPaperWhiteNits == 200.f,
            "HDR brightness must clamp to 80-500 nits and reject nonfinite input");
        Require(hidden.enabled && !hidden.visible && hidden.toggleKey == 119, "Initially hidden HUD must remain toggleable");
        Require(!ReadTestConfig(nullptr).notifications && !ReadTestConfig("").notifications,
            "Missing/empty configuration must not silently install notification graphics hooks");
        const auto notices = ReadTestConfig("[Notifications]\nEnabled=1\nDurationMilliseconds=7000\n[Server]\nPort=27329\n[Lights]\nEnabled=1\nManyLights=0\n");
        Require(notices.notifications && !notices.enabled && notices.notificationDurationMs == 7000 &&
            notices.port == 27329 && notices.lightsExpected && !notices.renderedExpected,
            "Notification-only INI must load server/light expectations without enabling the full HUD");
        Require(ReadTestConfig("[Notifications]\nEnabled=1\nDurationMilliseconds=1\n").notificationDurationMs == 5000 &&
            ReadTestConfig("[Notifications]\nEnabled=1\nDurationMilliseconds=60000\n").notificationDurationMs == 10000,
            "Configured ready duration must remain between five and ten seconds");
        Require(!config.lightOverlay && config.lightOverlayVisible && config.radar3D && config.lightToggleKey == 121 &&
            config.showAmbient && !config.occlusionTest && !config.hideOccluded && config.occlusionToggleKey==122,
            "World markers must default off and retain independent visibility/hotkey defaults");
        const auto markers = ReadTestConfig("[Overlay]\nEnabled=0\nAutoScale=0\nScale=1.5\nRadar3D=0\n"
            "[Notifications]\nEnabled=0\n[LightOverlay]\nEnabled=1\nInitiallyVisible=0\nToggleKey=122\n"
            "MaxMarkers=200\nMaxLabels=4\nRadius=42.5\nOcclusionTest=1\nHideOccluded=1\nOcclusionToggleKey=72\n[Server]\nPort=27329\n");
        Require(markers.lightOverlay && !markers.enabled && !markers.notifications && !markers.lightOverlayVisible &&
            markers.lightToggleKey == 122 && markers.lightMaxMarkers == 200 && markers.lightMaxLabels == 4 &&
            markers.lightRadius == 42.5f && !markers.autoScale && markers.scale == 1.5f && !markers.radar3D &&
            markers.port == 27329,
            "World markers alone must retain client, scale and independent toggle configuration");
#if CDT_RESEARCH
        Require(markers.occlusionTest && markers.hideOccluded && markers.occlusionToggleKey==72,
            "Research builds must retain the occlusion tests and configurable hiding");
#else
        Require(!markers.occlusionTest && markers.hideOccluded && markers.occlusionToggleKey==72,
            "Release must ignore the legacy trace overlay while retaining source hiding");
#endif
        const auto largeMarkers = ReadTestConfig("[LightOverlay]\nEnabled=1\nMaxMarkers=99999\nMaxLabels=999\nRadius=99999\nToggleKey=999\n");
        Require(largeMarkers.lightMaxMarkers == 2048 && largeMarkers.lightMaxLabels == 16 &&
            largeMarkers.lightRadius == 500 && largeMarkers.lightToggleKey == 255, "Large marker settings must be bounded");
        const auto smallMarkers = ReadTestConfig("[LightOverlay]\nEnabled=1\nMaxMarkers=0\nMaxLabels=-1\nRadius=0\nToggleKey=-1\n");
        Require(smallMarkers.lightMaxMarkers == 1 && smallMarkers.lightMaxLabels == 0 && smallMarkers.lightRadius == 1 &&
            smallMarkers.lightToggleKey == 0, "Small/negative marker settings must be bounded");
        Require(ReadTestConfig("[LightOverlay]\nEnabled=1\nRadius=nan\n").lightRadius == 35,
            "Nonfinite marker radius must use the safe default");
        const auto remapped=ReadTestConfig("[Overlay]\nEnabled=1\nToggleKey=65\nDetailsKey=66\n"
            "[LightOverlay]\nEnabled=1\nToggleKey=67\nOcclusionToggleKey=68\n");
        Require(remapped.toggleKey==65&&remapped.detailsKey==66&&remapped.lightToggleKey==67&&remapped.occlusionToggleKey==68,
            "Offered view shortcuts must use their INI remapping");
        const auto disabledKeys=ReadTestConfig("[Overlay]\nEnabled=1\nToggleKey=0\nDetailsKey=0\n"
            "[LightOverlay]\nEnabled=1\nToggleKey=0\nOcclusionToggleKey=0\n");
        Require(disabledKeys.toggleKey==0&&disabledKeys.detailsKey==0&&disabledKeys.lightToggleKey==0&&disabledKeys.occlusionToggleKey==0,
            "All four view shortcuts must support disabling with zero");
        Require(ReadTestConfig("[LightOverlay]\nEnabled=1\nOcclusionToggleKey=999\n").occlusionToggleKey==255&&
            ReadTestConfig("[LightOverlay]\nEnabled=1\nOcclusionToggleKey=-1\n").occlusionToggleKey==0,
            "Occlusion shortcut must use the bounded virtual-key range");
        ShortcutTests();
        std::cout << "PASS HUD defaults off, missing configuration, explicit opt-in and hidden mode\n";
        Require(std::abs(HudScale(1920, 1080, config, false) - 1.0f) < 0.001f, "1080p HUD scale");
        Require(std::abs(HudScale(3840, 2160, config, false) - 2.0f) < 0.001f, "4K HUD scale");
        Require(std::abs(HudScale(2560, 1440, config, false) - 4.0f / 3.0f) < 0.001f, "1440p HUD scale");
        config.autoScale = false;
        Require(std::abs(HudScale(3840, 2160, config, false) - 1.0f) < 0.001f, "Manual HUD scale");
        config.autoScale = true;
        Require(HudScale(800, 480, config, true) * (HudNaturalHeight(config, true) + 40) <= 480.1f,
            "Diagnostics must fit small screens");
        for (const bool radar3D : {false, true})
            for (const bool details : {false, true})
                for (const auto display : {std::array<float, 2>{800, 480}, {1920, 1080}, {3840, 2160}})
                    for (const bool automatic : {false, true})
                    {
                        Config fit; fit.radar3D = radar3D; fit.autoScale = automatic; fit.scale = 3.f;
                        const float scale = HudScale(display[0], display[1], fit, details);
                        Require(scale > 0 && (HudNaturalHeight(fit, details) + 40) * scale <= display[1] + .01f &&
                            550 * scale <= display[0] + .01f,
                            "Both full-width radar and legacy HUD, normal/details and manual/auto scales must fit display");
                    }
        Require(HudNaturalHeight(config, false) == 550 && HudNaturalHeight(config, true) == 930,
            "3D radar layout must use the shared taller natural heights");
        Require(HudScale(0, 0, config, false) == 0, "Minimized surface");
        std::cout << "PASS HUD resolution scaling and panel bounds\n";
        const auto now = std::chrono::system_clock::from_time_t(1788091200); // 2026-08-30T12:00:00Z
        auto json = nlohmann::json::parse(R"({"schemaVersion":"1.1","sequence":1,"capturedAt":"2026-08-30T12:00:00+00:00",
            "game":{"build":"test","state":"playing"},"coordinateSystem":{"unit":"game-unit","handedness":"right","upAxis":"y"},
            "capabilities":["player.position","player.orientation","camera"],
            "player":{"position":{"x":123.0,"y":456.0,"z":789.0},"orientation":{"source":"player-physics-root",
                "forward":{"x":1,"y":0,"z":0},"up":{"x":0,"y":1,"z":0},"headingDegrees":90}},
            "camera":{"position":{"x":1,"y":2,"z":3},"forward":{"x":0,"y":0,"z":1},"up":{"x":0,"y":1,"z":0},
                "right":{"x":1,"y":0,"z":0},"verticalFovDegrees":60},
            "quality":{"consensusCopies":3,"validCopies":4,"distinctStates":1,"rediscovered":false,"captureDurationMicroseconds":120}})");
        auto sample = ParseSample(json.dump(), now);
        ProjectionTests();
        FrustumTests();
        LightDetailGroupingTests();
        SourceVisibilityTests(json, now);
        RenderedLightTests(json, now);
        UpstreamLightTests(json, now);
        Require(sample.playerPosition && sample.playerPosition->x == 123, "Player position");
        Require(sample.playerHeading && std::abs(*sample.playerHeading - 90) < 0.01, "Independent player heading");
        Require(sample.cameraHeading && std::abs(*sample.cameraHeading) < 0.01, "Independent camera heading");
        Require(!Heading({0, 1, 0}), "Vertical direction must not produce a heading");
        std::cout << "PASS overlay camera/player separation and vertical projection\n";
        for (const auto* version : {"1.2", "1.3", "1.4", "1.5", "1.6"})
        {
            json["schemaVersion"] = version;
            sample = ParseSample(json.dump(), now);
            Require(sample.playerPosition.has_value() && sample.schemaVersion == version,
                "Additive schema compatibility and displayed version");
        }
        json["lights"] = {{"status", "available"}, {"sources", nlohmann::json::array({{{"position", "not decoded by HUD"}}})},
            {"rendered", {{"status", "unavailable"}, {"unavailableReason", "bridge-waiting"}}}};
        sample = ParseSample(json.dump(), now);
        Require(sample.authoredLights.status == "available" && sample.authoredLights.publishedRecords == 1 &&
            sample.renderedLights.status == "unavailable" && sample.renderedLights.unavailableReason == "bridge-waiting" &&
            !sample.renderedLights.publishedRecords, "Optional light status/count summary");
        json["lights"]["rendered"] = {{"status", "available"}, {"sources", nlohmann::json::array()}, {"ageMilliseconds", 125}};
        json["lights"]["status"] = "broken";
        sample = ParseSample(json.dump(), now);
        Require(sample.cameraPosition && sample.authoredLights.status == "invalid" &&
            sample.renderedLights.status == "available" && sample.renderedLights.publishedRecords == 0 &&
            sample.renderedLights.ageMilliseconds == 125,
            "Malformed authored lights must not invalidate camera or rendered light summary");
        json["lights"]["status"] = "available";
        json["lights"]["rendered"]["sources"] = 17;
        sample = ParseSample(json.dump(), now);
        Require(sample.playerPosition && sample.cameraPosition && sample.authoredLights.status == "available" &&
            sample.renderedLights.status == "invalid" && !sample.renderedLights.publishedRecords,
            "Malformed rendered lights must fail independently");
        json.erase("lights");
        sample = ParseSample(json.dump(), now);
        Require(sample.authoredLights.status == "not-reported" && sample.renderedLights.status == "not-reported" &&
            !sample.authoredLights.publishedRecords, "Missing light module must not become an OFF/zero state");
        std::cout << "PASS additive schemas and independently optional light summaries\n";
        const auto ambientJson=nlohmann::json{{"schemaVersion","1.0"},{"status","available"},
            {"captureSequence",7},{"frameNumber",81},{"ageMilliseconds",25},
            {"sky",{{"upperHemisphereMeanWorking",{1.0,2.0,3.0}},
                {"upwardIrradianceOverPiWorking",{4.0,5.0,6.0}},
                {"inverseMatrixMean",{7.0,8.0,9.0}},{"rec709MeanLuminanceEstimate",8.1}}},
            {"visibility",{{"valueWorking",.25},{"frameNumber",77},{"ageMilliseconds",40}}},
            {"localEnvironmentAmbientEstimateWorking",{{"rgbWorking",{.25,.5,.75}},
                {"available",true},{"stale",false},{"basis","raw product"}}}};
        const auto ambient=ParseAmbient(ambientJson.dump());
        Require(ambient.status=="available"&&ambient.captureSequence==7&&
            ambient.upperHemisphereMeanWorking&&ambient.upperHemisphereMeanWorking->z==3&&
            ambient.cameraSkyVisibilityWorking==.25&&ambient.localEnvironmentAmbientEstimateWorking&&
            ambient.localEnvironmentAmbientEstimateWorking->y==.5f,"Ambient HUD fields were not preserved");
        View ambientView;ambientView.hasAmbient=true;ambientView.ambient=ambient;
        ambientView.ambientReceived=Clock::time_point{}+std::chrono::seconds(10);
        Require(AmbientLive(ambientView,ambientView.ambientReceived+std::chrono::milliseconds(1475))&&
            !AmbientLive(ambientView,ambientView.ambientReceived+std::chrono::milliseconds(1476)),
            "Ambient HUD must expire source plus client age at 1500 ms");
        const auto unavailable=ParseAmbient(nlohmann::json{{"schemaVersion","1.0"},
            {"status","unavailable"},{"reason","bridge-waiting"}}.dump());
        Require(unavailable.reason=="bridge-waiting"&&!unavailable.upperHemisphereMeanWorking,
            "Unavailable ambient invented a measurement");
        std::cout << "PASS ambient HUD parsing, independent freshness and unavailable state\n";
        json["schemaVersion"] = "1.1";
        View view;
        view.sample = sample; view.connected = true; view.hasSample = true; view.received = Clock::now();
        Require(IsLive(view, view.received, 1000), "Fresh data");
        Require(!IsLive(view, view.received + std::chrono::seconds(2), 1000), "Stale data");
        view.connected = false;
        Require(!IsLive(view, view.received, 1000), "Disconnected data");
        std::cout << "PASS overlay freshness and disconnect invalidation\n";
        json["game"]["state"] = "loading";
        sample = ParseSample(json.dump(), now);
        Require(!sample.playerPosition && !sample.cameraForward, "Loading must clear positions");
        json["game"]["state"] = "playing";
        json["player"]["orientation"] = nullptr;
        sample = ParseSample(json.dump(), now);
        Require(sample.playerPosition && !sample.playerHeading, "No invented orientation");
        std::cout << "PASS overlay loading and optional orientation\n";
        const auto reject = [&](nlohmann::json invalid)
        {
            bool rejected = false;
            try { ParseSample(invalid.dump(), now); } catch (...) { rejected = true; }
            Require(rejected, "Invalid sample accepted");
        };
        auto invalid = json; invalid["schemaVersion"] = "2.0"; reject(invalid);
        invalid = json; invalid["coordinateSystem"]["upAxis"] = "z"; reject(invalid);
        invalid = json; invalid["camera"]["forward"]["z"] = 50; reject(invalid);
        invalid = json; invalid["capturedAt"] = "tomorrow"; reject(invalid);
        invalid = json; invalid["capturedAt"] = "2027-08-30T12:00:00Z"; reject(invalid);
        std::cout << "PASS overlay malformed/schema/coordinate/timestamp rejection\n";
        json["capturedAt"] = "2026-08-30T11:59:58.5000000+00:00";
        sample = ParseSample(json.dump(), now);
        Require(std::abs(sample.sourceAgeMs - 1500) < 0.1, "Source timestamp age");
        std::cout << "PASS overlay source age includes time before receipt\n";
        NoticeTests();
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}

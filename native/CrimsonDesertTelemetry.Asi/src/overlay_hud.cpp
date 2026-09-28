#include "overlay.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <cmath>
#include <format>
#include <numbers>
#include <vector>

namespace cdt::overlay
{
namespace
{
constexpr ImU32 White = IM_COL32(228, 239, 244, 255);
constexpr ImU32 Muted = IM_COL32(174, 194, 207, 255);
constexpr ImU32 Cyan = IM_COL32(49, 221, 208, 255);
constexpr ImU32 Amber = IM_COL32(251, 188, 91, 255);
constexpr ImU32 Panel = IM_COL32(9, 20, 30, 240);
constexpr ImU32 Grid = IM_COL32(42, 67, 82, 190);

struct Rect
{
    ImVec2 min, max;
    bool Intersects(const Rect& other, float gap = 0) const
    {
        return min.x < other.max.x + gap && max.x + gap > other.min.x &&
            min.y < other.max.y + gap && max.y + gap > other.min.y;
    }
    bool Contains(ImVec2 p, float gap = 0) const
    {
        return p.x >= min.x - gap && p.x <= max.x + gap &&
            p.y >= min.y - gap && p.y <= max.y + gap;
    }
};

std::optional<Rect> HudBounds(const Config& config, ImVec2 display)
{
    if (!config.enabled || !config.visible) return std::nullopt;
    const float scale = HudScale(display.x, display.y, config, config.details);
    if (scale < .3f) return std::nullopt;
    const float width = 510 * scale, height = HudNaturalHeight(config,config.details) * scale;
    const float margin = 20 * scale;
    const ImVec2 origin((config.corner & 1) ? display.x - width - margin : margin,
        (config.corner & 2) ? display.y - height - margin : margin);
    return Rect{origin, ImVec2(origin.x + width, origin.y + height)};
}

ImU32 LightColor(Vec3 linear, int alpha = 255)
{
    // A chromatic HDR swatch: common-channel compression preserves linear ratios,
    // followed by the sRGB transfer curve. This is not the game's final scene color.
    const float peak = std::max({0.f, linear.x, linear.y, linear.z});
    const auto channel = [peak](float value)
    {
        const float c = std::max(0.f, value) / (1.f + peak);
        return c <= .0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.f / 2.4f) - .055f;
    };
    return ImGui::ColorConvertFloat4ToU32(ImVec4(channel(linear.x), channel(linear.y),
        channel(linear.z), static_cast<float>(alpha) / 255.f));
}

float DistanceSquared(Vec3 a, Vec3 b)
{
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x*x + y*y + z*z;
}
const char* VisibilityReasonText(const std::string& reason)
{
    if(reason=="waiting-for-physics"||reason=="physics-query-unavailable")return "waiting for physics";
    if(reason=="physics-budget-pending")return "queued for physics check";
    if(reason=="pending-near-recent-blocker")return "checking moving light";
    if(reason=="stale-physics")return "measurement expired";
    if(reason=="outside-physics-radius")return "outside configured radius";
    if(reason=="outside-physics-budget")return "outside sampled target set";
    if(reason=="physics-stopped-restart-required")return "physics stopped - restart required";
    if(reason=="stale-volume"||reason=="stale-source")return "waiting for fresh data";
    if(reason=="waiting-for-volume")return "waiting for data";
    if(reason=="disabled")return "disabled";
    if(reason=="uncovered")return "outside geometry coverage";
    if(reason=="outside-trace-radius")return "out of range";
    if(reason=="too-short")return "source too close";
    if(reason=="trace-budget-exceeded"||reason=="iteration-limit")return "processing limit";
    if(reason.starts_with("invalid"))return "invalid data";
    return "not available";
}

void ScreenArrow(ImDrawList* draw, ImVec2 from, ImVec2 to, ImU32 color, float scale,
    float thickness = 1.5f)
{
    const float dx = to.x - from.x, dy = to.y - from.y;
    const float length = std::sqrt(dx*dx + dy*dy);
    if (length < 2 * scale) return;
    const float ux = dx / length, uy = dy / length;
    const float head = std::min(6 * scale, length * .4f);
    draw->AddLine(from, to, color, thickness * scale);
    draw->AddLine(to, ImVec2(to.x - ux*head - uy*head*.55f,
        to.y - uy*head + ux*head*.55f), color, thickness * scale);
    draw->AddLine(to, ImVec2(to.x - ux*head + uy*head*.55f,
        to.y - uy*head - ux*head*.55f), color, thickness * scale);
}

std::string ShortcutAction(const int key,const std::string& action)
{
    return key?ShortcutLabel(key)+" "+action:action+" shortcut off";
}
std::string Angle(const std::optional<float>& angle)
{
    return angle ? std::format("{:.1f} deg", *angle) : "--";
}
std::string VectorText(const std::optional<Vec3>& vector, int precision = 2)
{
    return vector ? std::format("{:.{}f}   {:.{}f}   {:.{}f}", vector->x, precision,
        vector->y, precision, vector->z, precision) : "--   --   --";
}

void DrawRadar(ImDrawList* draw, const View& view, const Sample& sample,
    const Config& config, ImVec2 origin, float scale)
{
    const auto at = [&](float x, float y)
    {
        return ImVec2(origin.x + x*scale, origin.y + y*scale);
    };
    const auto text = [&](float x, float y, ImU32 color, const std::string& value, float size)
    {
        draw->AddText(ImGui::GetFont(), size*scale, at(x,y), color, value.c_str());
    };
    const float radius = std::clamp(config.lightRadius, 1.f, 500.f);
    const float unit = 205.f / radius;
    // One orthographic oblique projection for every vertex. XZ is the ground;
    // Y remains a true vertical coordinate for both lights and camera frustum.
    // Clip the completed drawing instead of distorting individual heights.
    const auto project = [&](Vec3 relative)
    {
        return at(255.f + (.8660254f*relative.x + .5f*relative.z)*unit,
            251.f + (.25f*relative.x - .4330127f*relative.z - .8660254f*relative.y)*unit);
    };
    const auto ground = [&](float x,float z) { return project(Vec3{x,0,z}); };
    const auto relative = [&](Vec3 world)
    {
        return Vec3{world.x-sample.playerPosition->x,world.y-sample.playerPosition->y,
            world.z-sample.playerPosition->z};
    };
    text(20,84,Cyan,"3D LIGHT RADAR",13);
    text(20,104,Muted,"Player center / XZ ground / Y height / fixed world axes",11);
    draw->PushClipRect(at(16,118),at(494,366),true);
    for (int ring = 1; ring <= 2; ++ring)
    {
        std::array<ImVec2, 64> points;
        for (size_t i = 0; i < points.size(); ++i)
        {
            const float a = static_cast<float>(i) * 2.f*std::numbers::pi_v<float> /
                static_cast<float>(points.size());
            points[i] = ground(std::sin(a)*radius*static_cast<float>(ring)*.5f,
                std::cos(a)*radius*static_cast<float>(ring)*.5f);
        }
        draw->AddPolyline(points.data(), static_cast<int>(points.size()), Grid,
            ImDrawFlags_Closed, scale);
    }
    for (int i = -3; i <= 3; ++i)
    {
        const float offset = static_cast<float>(i)*radius*.25f;
        const float edge = std::sqrt(radius*radius - offset*offset);
        draw->AddLine(ground(offset,-edge), ground(offset,edge), Grid, scale);
        draw->AddLine(ground(-edge,offset), ground(edge,offset), Grid, scale);
    }
    const ImVec2 center = ground(0,0);
    const auto now = Clock::now();
    const auto* lights = DisplayLights(view,now,config.staleMs);
    const bool lightLive = lights != nullptr;
    const bool engineInput = lights == &view.sample.upstreamLights;
    size_t inRange = 0, selectedInRange = 0;
    const float frustumLength = radius*.4f;
    const auto frustum = sample.playerPosition ? BuildCameraFrustum(sample,frustumLength) : std::nullopt;
    if (sample.playerPosition)
    {
        if (lightLive && lights->records)
        {
            struct Dot { ImVec2 base, tip; ImU32 color, halo; bool selected; };
            std::vector<Dot> dots;
            const size_t markerLimit = static_cast<size_t>(std::clamp(config.lightMaxMarkers,1,2048));
            dots.reserve(std::min(lights->records->size(),markerLimit));
            for (const auto& light : *lights->records)
            {
                if (DistanceSquared(light.position,*sample.playerPosition) > radius*radius) continue;
                ++inRange; if (light.rendererSelected) ++selectedInRange;
                if(HideOccludedLight(light,view,now,config.hideOccluded))continue;
                if (dots.size() >= markerLimit) continue;
                const auto delta = relative(light.position);
                const bool blocked=CurrentSourceVisibility(light,view,now).status=="blocked";
                dots.push_back({ground(delta.x,delta.z),project(delta),
                    LightColor(light.colorLinear,blocked?90:255),LightColor(light.colorLinear,blocked?15:40),
                    light.rendererSelected});
            }
            std::sort(dots.begin(),dots.end(),[](const Dot& a,const Dot& b){return a.base.y < b.base.y;});
            for (const auto& dot : dots)
            {
                draw->AddCircleFilled(dot.base,1.5f*scale,IM_COL32(138,163,176,100),8);
                draw->AddLine(dot.base,dot.tip,IM_COL32(140,174,190,85),scale);
                draw->AddCircleFilled(dot.tip,6*scale,dot.halo,16);
                if (dot.selected)
                {
                    draw->AddCircleFilled(dot.tip,3*scale,dot.color,12);
                    draw->AddCircle(dot.tip,3*scale,IM_COL32(224,239,245,170),12,.65f*scale);
                }
                // Hollow: a current engine light the renderer did not select in this view.
                else draw->AddCircle(dot.tip,3*scale,dot.color,12,1.5f*scale);
            }
        }
        // Draw the complete pitched/rolled frustum above the light dots. Its
        // depth is a display choice; its FOV, aspect and orientation are measured.
        if (frustum)
        {
            const auto delta = relative(frustum->apex);
            const auto camera = project(delta), base = ground(delta.x,delta.z);
            const ImU32 outline = IM_COL32(5,14,23,235);
            std::array<ImVec2,4> corners;
            for (size_t i = 0; i < corners.size(); ++i) corners[i] = project(relative(frustum->farCorners[i]));
            double signedArea = 0;
            for (size_t i = 0; i < corners.size(); ++i)
            {
                const auto next = (i+1)%corners.size();
                signedArea += static_cast<double>(corners[i].x)*corners[next].y -
                    static_cast<double>(corners[next].x)*corners[i].y;
            }
            // ImGui's antialiased convex fill expects clockwise screen winding.
            if (signedArea < 0) std::reverse(corners.begin(),corners.end());
            draw->AddConvexPolyFilled(corners.data(),static_cast<int>(corners.size()),IM_COL32(251,188,91,20));
            for (size_t i = 0; i < corners.size(); ++i)
            {
                const auto next = (i+1)%corners.size();
                draw->AddTriangleFilled(camera,corners[i],corners[next],IM_COL32(251,188,91,8));
            }
            for (size_t i = 0; i < corners.size(); ++i)
            {
                const auto next = (i+1)%corners.size();
                draw->AddLine(camera,corners[i],outline,3.5f*scale);
                draw->AddLine(corners[i],corners[next],outline,3.5f*scale);
                draw->AddLine(camera,corners[i],IM_COL32(251,188,91,210),1.5f*scale);
                draw->AddLine(corners[i],corners[next],Amber,1.5f*scale);
            }
            draw->AddLine(base,camera,outline,3*scale);
            draw->AddLine(base,camera,IM_COL32(251,188,91,185),scale);
            const auto tip = project(relative(frustum->farCenter));
            ScreenArrow(draw,camera,tip,outline,1.25f*scale,4);
            ScreenArrow(draw,camera,tip,Amber,1.25f*scale,2);
            draw->AddCircleFilled(camera,5.5f*scale,outline,16);
            draw->AddCircle(camera,4.5f*scale,Amber,16,1.8f*scale);
            draw->AddCircleFilled(camera,1.3f*scale,White,8);
        }
    }
    if (sample.playerHeading)
    {
        const float yaw = *sample.playerHeading * std::numbers::pi_v<float> / 180.f;
        const auto tip = ground(std::sin(yaw)*radius*.3f,std::cos(yaw)*radius*.3f);
        ScreenArrow(draw,center,tip,IM_COL32(5,14,23,245),1.3f*scale,4.5f);
        ScreenArrow(draw,center,tip,Cyan,1.3f*scale,2.2f);
    }
    draw->AddCircleFilled(center,5*scale,IM_COL32(9,20,30,255),16);
    draw->AddCircle(center,4*scale,Cyan,16,2*scale);
    const auto xAxis = ground(radius*.96f,0), zAxis = ground(0,radius*.96f);
    const auto yAxis = project(Vec3{0,radius*.3f,0});
    draw->AddLine(center,yAxis,IM_COL32(156,181,195,100),scale);
    draw->AddText(ImGui::GetFont(),11*scale,ImVec2(xAxis.x+7*scale,xAxis.y-5*scale),Muted,"+X");
    draw->AddText(ImGui::GetFont(),11*scale,ImVec2(zAxis.x+5*scale,zAxis.y-14*scale),Muted,"+Z");
    draw->AddText(ImGui::GetFont(),11*scale,ImVec2(yAxis.x+6*scale,yAxis.y-8*scale),Muted,"+Y");
    draw->PopClipRect();
    text(20,375,lightLive && sample.playerPosition ? Muted : Amber,
        !lightLive ? LightFeedStatus(view,now,config.staleMs) : !sample.playerPosition ? "Player position unavailable" :
        engineInput ? std::format("Radius {:.0f} gu / all engine lights / {} of {} renderer-selected (filled)",
            radius,selectedInRange,inRange)
        : std::format("Radius {:.0f} gu / filtered coverage (not 360 complete)",radius),11);
    const auto counts=CountSourceVisibility(view,now,radius);
    const auto hidden=config.hideOccluded?counts.blocked:0;
    text(20,394,lightLive ? Muted : Amber,lightLive
        ? std::format("Visible {} / blocked {} / unknown {} / {} hidden / ",counts.visible,counts.blocked,counts.unknown,hidden)+
            ShortcutAction(config.occlusionToggleKey,config.hideOccluded?"show blocked":"hide blocked")
        : "Source visibility unavailable",10);
}
}

void DrawHud(const View& view, const Config& config, const bool details)
{
    auto& io = ImGui::GetIO();
    const float naturalHeight = HudNaturalHeight(config,details);
    const float scale = HudScale(io.DisplaySize.x, io.DisplaySize.y, config, details);
    if (scale < 0.3f) return;
    const float width = 510 * scale, height = naturalHeight * scale;
    const float margin = 20 * scale;
    const ImVec2 origin((config.corner & 1) ? io.DisplaySize.x - width - margin : margin,
        (config.corner & 2) ? io.DisplaySize.y - height - margin : margin);
    auto* draw = ImGui::GetForegroundDrawList();
    const auto point = [&](float x, float y) { return ImVec2(origin.x + x * scale, origin.y + y * scale); };
    const auto text = [&](float x, float y, ImU32 color, const std::string& value, float size = 16)
    {
        draw->AddText(ImGui::GetFont(), size * scale, point(x, y), color, value.c_str());
    };
    draw->AddRectFilled(origin, point(510, naturalHeight),
        IM_COL32(9, 20, 30, static_cast<int>(255 * config.opacity)), 12 * scale);
    draw->AddRect(origin, point(510, naturalHeight), IM_COL32(47, 74, 90, 235), 12 * scale);
    draw->AddRectFilled(point(0, 16), point(3, 55), Cyan);
    text(20, 15, Muted, "CRIMSON DESERT", 13);
    text(20, 34, White, "TELEMETRY", 22);
    const auto now = Clock::now();
    const bool live = IsLive(view, now, config.staleMs);
    text(272, 24, live ? Cyan : Amber, Status(view, now, config.staleMs), 14);
    draw->AddLine(point(20, 70), point(490, 70), IM_COL32(40, 61, 76, 255));

    const Sample empty{};
    const auto& sample = live ? view.sample : empty;
    if (config.radar3D) DrawRadar(draw,view,sample,config,origin,scale);
    else
    {
    const ImVec2 center = point(121, 169);
    draw->AddCircle(center, 65 * scale, IM_COL32(60, 86, 102, 255), 64, scale);
    draw->AddCircle(center, 43 * scale, IM_COL32(28, 47, 61, 255), 64, scale);
    draw->AddLine(point(121, 104), point(121, 234), IM_COL32(35, 56, 71, 255));
    draw->AddLine(point(56, 169), point(186, 169), IM_COL32(35, 56, 71, 255));
    text(110, 83, Muted, "+Z", 13);
    text(196, 160, Muted, "+X", 13);
    text(110, 240, Muted, "-Z", 13);
    text(27, 160, Muted, "-X", 13);
    const auto arrow = [&](std::optional<float> heading, ImU32 color, bool filled, float length)
    {
        if (!heading) return;
        const float radians = *heading * std::numbers::pi_v<float> / 180.0f;
        const float dx = std::sin(radians), dy = -std::cos(radians);
        const auto p = [&](float forward, float sideways)
        {
            return ImVec2(center.x + (dx * forward - dy * sideways) * scale,
                center.y + (dy * forward + dx * sideways) * scale);
        };
        const ImVec2 tip = p(length, 0), left = p(-10, -10), right = p(-10, 10);
        if (filled) draw->AddTriangleFilled(tip, left, right, color);
        else draw->AddTriangle(tip, left, right, color, 2 * scale);
    };
    arrow(sample.cameraHeading, Amber, false, 59);
    arrow(sample.playerHeading, Cyan, true, 46);
    draw->AddCircleFilled(center, 3 * scale, White);
    }
    if (config.radar3D)
    {
        draw->AddLine(point(20,414),point(490,414),IM_COL32(40,61,76,255));
        text(20,427,Cyan,"PLAYER ROOT",12);
        text(145,427,Amber,"CAMERA YAW",12);
        text(270,427,Muted,"PITCH",12);
        text(395,427,Muted,"VERTICAL FOV",12);
        text(20,447,White,Angle(sample.playerHeading),21);
        text(145,447,White,Angle(sample.cameraHeading),21);
        text(270,447,White,Angle(sample.pitch),21);
        text(395,447,White,Angle(sample.fov),21);
        text(20,483,Muted,"PLAYER POSITION  X / Y / Z  [game units]",12);
        text(20,503,White,VectorText(sample.playerPosition),19);
        text(20,532,Muted,"World axes / player root orientation, not body pose",10);
    }
    else
    {
        text(250,87,Cyan,"PLAYER ROOT",13);
        text(250,106,White,Angle(sample.playerHeading),24);
        text(250,144,Amber,"CAMERA",13);
        text(250,163,White,Angle(sample.cameraHeading),24);
        text(250,204,Muted,"PITCH",12);
        text(370,204,Muted,"VERTICAL FOV",12);
        text(250,223,White,Angle(sample.pitch),17);
        text(370,223,White,Angle(sample.fov),17);
        draw->AddLine(point(20,265),point(490,265),IM_COL32(40,61,76,255));
        text(20,279,Muted,"PLAYER POSITION  X / Y / Z  [game units]",12);
        text(20,298,White,VectorText(sample.playerPosition),19);
        text(20,325,Muted,"World axes, not compass north. Root orientation, not body pose.",12);
    }

    if (!details) return;
    const float diagnostics = HudNaturalHeight(config,false);
    draw->AddLine(point(20,diagnostics+10),point(490,diagnostics+10),IM_COL32(40,61,76,255));
    text(20,diagnostics+24,White,"DIAGNOSTICS",14);
    text(20,diagnostics+51,Muted,std::format("Schema {}  |  sequence {}  |  {:.1f} received Hz",
        view.sample.schemaVersion, view.sample.sequence, live ? view.rateHz : 0.0), 13);
    text(20,diagnostics+72,Muted,view.hasSample ? std::format("Sample age {:.0f} ms  |  capture {} us (not latency)",
        AgeMs(view, now), view.sample.captureUs) : "Sample age --  |  capture --", 13);
    text(20,diagnostics+93,Muted,std::format("Camera sources {}/{}  |  states {}  |  rebound {}",
        sample.consensus, sample.validCopies, sample.distinctStates, sample.rediscovered ? "yes" : "no"), 13);
    text(20,diagnostics+114,Muted,"Player forward  " + VectorText(sample.playerForward,3),13);
    text(20,diagnostics+135,Muted,"Camera forward  " + VectorText(sample.cameraForward,3),13);
    text(20,diagnostics+156,Muted,"Player up       " + VectorText(sample.playerUp,3),13);
    text(20,diagnostics+177,Muted,"Build " + view.sample.build,13);
    text(20,diagnostics+198,Muted,std::string(GraphicsOutputLabel())+
        "  |  telemetry HUD  |  no mouse capture",13);
    const auto& input = sample.upstreamLights;
    text(20,diagnostics+226,Cyan,"Rendered lights: " + (live ? sample.renderedLights.status : "unavailable") +
        "  |  engine input: " + (!live ? std::string("unavailable") : input.status == "available" && input.publishedRecords
            ? std::format("{} in radius",*input.publishedRecords) : input.status),12);
    const bool ambientLive=config.showAmbient&&AmbientLive(view,now);
    text(20,diagnostics+251,Cyan,"AMBIENT / SKY EXPOSURE",12);
    text(20,diagnostics+272,ambientLive?White:Amber,config.showAmbient
        ? ambientLive?std::format("Global sky frame {}  |  age {:.0f} ms",
            view.ambient.frameNumber.value_or(0),AmbientAgeMs(view,now))
            : "Ambient unavailable: "+(view.hasAmbient
                ? view.ambient.status=="available"?"stale":""+view.ambient.reason
                : "waiting for /v1/ambient")
        : "Ambient HUD disabled",12);
    text(20,diagnostics+293,Muted,"Upper mean RGB  "+
        VectorText(ambientLive?view.ambient.upperHemisphereMeanWorking:std::nullopt,4),12);
    text(20,diagnostics+314,Muted,ambientLive
        ? std::format("Inverse mean RGB  {}  |  Rec.709 estimate {:.4f}",
            VectorText(view.ambient.inverseMatrixMean,4),view.ambient.rec709MeanLuminanceEstimate.value_or(0))
        : "Inverse mean RGB  --   --   --  |  Rec.709 estimate --",12);
    text(20,diagnostics+335,ambientLive&&view.ambient.cameraSkyVisibilityWorking?White:Amber,
        ambientLive&&view.ambient.cameraSkyVisibilityWorking
            ? std::format("Camera sky visibility {:.4f}  |  own age {:.0f} ms  |  frame {}",
                *view.ambient.cameraSkyVisibilityWorking,view.ambient.visibilityAgeMilliseconds.value_or(0),
                view.ambient.visibilityFrameNumber.value_or(0))
            : "Camera sky visibility unavailable",12);
    text(20,diagnostics+356,ambientLive&&view.ambient.localEnvironmentAmbientEstimateWorking?White:Amber,
        "Local estimate RGB  "+VectorText(ambientLive?view.ambient.localEnvironmentAmbientEstimateWorking:std::nullopt,4)+
        (ambientLive&&view.ambient.localEstimateStale?"  |  STALE":""),12);
}

void DrawLightOverlay(const View& view, const Config& config)
{
    if (!config.lightOverlay || !config.lightOverlayVisible) return;
    const auto display = ImGui::GetIO().DisplaySize;
    const float scale = std::min(std::max(1.f,display.y/1080.f),display.x/660.f);
    if (scale < .3f || display.y < 120*scale) return;
    auto* draw = ImGui::GetForegroundDrawList();
    auto* font = ImGui::GetFont();
    const auto hud = HudBounds(config,display);
    const float margin = 20*scale;
    const float radius = std::clamp(config.lightRadius,1.f,500.f);
    const auto now = Clock::now();
    const auto* lights = DisplayLights(view,now,config.staleMs);
    const bool live = lights != nullptr;
    const bool engineInput = lights == &view.sample.upstreamLights;
    const auto& sample = view.sample;
    struct Marker
    {
        const LightRecord* light;
        ScreenPoint screen;
        float distance, crosshairDistance;
    };
    std::vector<Marker> markers;
    const auto visibilityCounts=CountSourceVisibility(view,now,radius);
    const size_t inRange=visibilityCounts.visible+visibilityCounts.blocked+visibilityCounts.unknown;
    bool cameraReady = false;
    if (live && sample.cameraPosition && sample.cameraForward)
    {
        const float probeDistance = std::max(1.f,sample.nearPlane.value_or(0.f)*2.f);
        const Vec3 probe{sample.cameraPosition->x + sample.cameraForward->x*probeDistance,
            sample.cameraPosition->y + sample.cameraForward->y*probeDistance,
            sample.cameraPosition->z + sample.cameraForward->z*probeDistance};
        cameraReady = ProjectWorld(probe,sample,display.x,display.y).has_value();
    }
    size_t selectedInRange = 0;
    if (live && sample.playerPosition)
        for (const auto& light : *lights->records)
            if (light.rendererSelected && DistanceSquared(light.position,*sample.playerPosition) <= radius*radius)
                ++selectedInRange;
    if (live && sample.playerPosition && cameraReady && lights->records)
    {
        const size_t markerLimit = static_cast<size_t>(std::clamp(config.lightMaxMarkers,1,2048));
        markers.reserve(std::min(lights->records->size(),markerLimit));
        for (const auto& light : *lights->records)
        {
            const float distanceSquared = DistanceSquared(light.position,*sample.playerPosition);
            if (distanceSquared > radius*radius) continue;
            if(HideOccludedLight(light,view,now,config.hideOccluded))continue;
            const auto p = ProjectWorld(light.position,sample,display.x,display.y);
            if (!p || p->x < 8*scale || p->y < 8*scale ||
                p->x > display.x-8*scale || p->y > display.y-8*scale ||
                (hud && hud->Contains(ImVec2(p->x,p->y),10*scale))) continue;
            const float dx = p->x-display.x*.5f, dy = p->y-display.y*.5f;
            markers.push_back({&light,*p,std::sqrt(distanceSquared),dx*dx+dy*dy});
        }
        const size_t keep = std::min(markers.size(),markerLimit);
        std::partial_sort(markers.begin(),markers.begin()+static_cast<std::ptrdiff_t>(keep),markers.end(),
            [](const Marker& a,const Marker& b)
        {
            return a.crosshairDistance < b.crosshairDistance;
        });
        markers.resize(keep);
    }

    std::string headline;
    const char* feed = engineInput ? "ENGINE LIGHTS" : "RENDERED LIGHTS";
    if (!live)
    {
        headline = LightFeedStatus(view,now,config.staleMs);
        const auto& reason = !sample.renderedLights.unavailableReason.empty()
            ? sample.renderedLights.unavailableReason : sample.upstreamLights.unavailableReason;
        if (!reason.empty()) headline += "  /  " + reason.substr(0,52);
    }
    else if (!sample.playerPosition) headline = std::string(feed) + "  /  player position unavailable";
    else if (!cameraReady) headline = std::string(feed) + "  /  camera projection unavailable";
    else
    {
        const auto hidden=config.hideOccluded?visibilityCounts.blocked:0;
        headline=engineInput
            ? std::format("ENGINE LIGHTS  /  {} shown  /  {} renderer-selected  /  {} hidden  /  {} on screen",
                inRange-hidden,selectedInRange,hidden,markers.size())
            : std::format("RENDERED LIGHTS  /  {} shown  /  {} hidden  /  {} on screen",inRange-hidden,hidden,markers.size());
    }
    const std::string controls=std::format("{}  /  {}  /  radius {:.0f} gu",
        ShortcutAction(config.lightToggleKey,"lights"),
        ShortcutAction(config.occlusionToggleKey,config.hideOccluded?"show blocked":"hide blocked"),radius);
    const std::string visibilityLine=live&&sample.playerPosition
        ? std::format("SOURCE VISIBILITY  /  {} visible  /  {} blocked  /  {} unknown  /  includes off-screen",
            visibilityCounts.visible,visibilityCounts.blocked,visibilityCounts.unknown)
        : "SOURCE VISIBILITY  /  unavailable";
    const std::string caveat = "Sampled visibility is an estimate / pending near recent blockers stay hidden";
    const std::string coverage = engineInput
        ? "Includes lights behind the camera / hollow = not selected by the renderer here, not OFF" : "";
    float legendWidth = 0;
    for (const auto* line : std::array<const std::string*,5>{&headline,&controls,&visibilityLine,&caveat,&coverage})
        legendWidth = std::max(legendWidth,font->CalcTextSizeA(12*scale,FLT_MAX,0,line->c_str()).x);
    legendWidth = std::min(display.x-2*margin,legendWidth+28*scale);
    const float legendHeight = (engineInput ? 108 : 89)*scale;
    Rect legend{ImVec2(margin,display.y-margin-legendHeight),
        ImVec2(margin+legendWidth,display.y-margin)};
    if (hud && legend.Intersects(*hud,12*scale))
    {
        legend.min.x = display.x-margin-legendWidth;
        legend.max.x = display.x-margin;
        if (legend.Intersects(*hud,12*scale))
        {
            legend.max.y = std::max(legendHeight+margin,hud->min.y-12*scale);
            legend.min.y = legend.max.y-legendHeight;
        }
    }

    // Group only detail presentation, never the telemetry or physical identities.
    // A complete-link world-space bound prevents unrelated, depth-separated lights
    // or a chain of nearby sources from collapsing into one supposed object.
    std::vector<const LightRecord*> detailRecords;
    const size_t labelCandidates = std::min<size_t>(markers.size(),64);
    detailRecords.reserve(labelCandidates);
    for (size_t i = 0; i < labelCandidates; ++i) detailRecords.push_back(markers[i].light);
    auto detailGroups = GroupLightDetails(detailRecords);
    std::sort(detailGroups.begin(),detailGroups.end(),[](const auto& a,const auto& b)
    {
        return *std::min_element(a.begin(),a.end()) < *std::min_element(b.begin(),b.end());
    });
    const size_t wantedLabels = std::min(detailGroups.size(),
        static_cast<size_t>(std::clamp(config.lightMaxLabels,0,16)));
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto& marker = markers[i];
        const ImVec2 p(marker.screen.x,marker.screen.y);
        if (legend.Contains(p,10*scale)) continue;
        const auto& light = *marker.light;
        const bool selected = wantedLabels > 0 && !detailGroups.empty() &&
            std::find(detailGroups.front().begin(),detailGroups.front().end(),i) != detailGroups.front().end();
        const float ring = (selected ? 9.f : 6.f)*scale;
        const bool blocked=CurrentSourceVisibility(light,view,now).status=="blocked";
        draw->AddCircle(p,ring+3*scale,LightColor(light.colorLinear,blocked?16:42),24,5*scale);
        draw->AddCircle(p,ring,IM_COL32(4,12,19,235),24,4*scale);
        // Thin hollow ring: current engine light the renderer did not select in this view.
        draw->AddCircle(p,ring,LightColor(light.colorLinear,blocked?90:255),24,(light.rendererSelected?2.f:1.f)*scale);
        if(light.rendererSelected)draw->AddCircleFilled(p,1.4f*scale,blocked?Muted:White,8);
        if (selected)
        {
            draw->AddCircle(p,ring+4*scale,IM_COL32(230,244,247,170),32,scale);
            draw->AddLine(ImVec2(p.x-ring-8*scale,p.y),ImVec2(p.x-ring-3*scale,p.y),White,scale);
            draw->AddLine(ImVec2(p.x+ring+3*scale,p.y),ImVec2(p.x+ring+8*scale,p.y),White,scale);
        }
        if (light.kind == "spot" && light.direction)
        {
            const Vec3 end{light.position.x+light.direction->x,
                light.position.y+light.direction->y,light.position.z+light.direction->z};
            if (const auto projected = ProjectWorld(end,sample,display.x,display.y))
            {
                const float dx = projected->x-p.x, dy = projected->y-p.y;
                const float length = std::sqrt(dx*dx+dy*dy);
                if (length > .5f)
                {
                    const float ux = dx/length, uy = dy/length;
                    const ImVec2 from(p.x+ux*(ring+3*scale),p.y+uy*(ring+3*scale));
                    const ImVec2 to(p.x+ux*(ring+30*scale),p.y+uy*(ring+30*scale));
                    ScreenArrow(draw,from,to,IM_COL32(4,12,19,230),scale,3.5f);
                    ScreenArrow(draw,from,to,LightColor(light.colorLinear,225),scale);
                }
            }
        }
    }

    std::vector<Rect> occupied{legend};
    if (hud) occupied.push_back(*hud);
    // Keep labels clear of the reticle as well as each other and the optional HUD.
    occupied.push_back({ImVec2(display.x*.5f-22*scale,display.y*.5f-22*scale),
        ImVec2(display.x*.5f+22*scale,display.y*.5f+22*scale)});
    // Leave room beside the taller radar on small displays. Marker geometry and
    // the reticle exclusion keep their original scale; only detail cards shrink.
    const float detailScale = std::min(scale,display.x/1100.f);
    size_t labelCount = 0;
    for (const auto& group : detailGroups)
    {
        if (labelCount >= wantedLabels) break;
        const auto& marker = markers[group.front()];
        const auto& light = *marker.light;
        ImVec2 anchor{};
        float distance = 0;
        for (const size_t index : group)
        {
            anchor.x += markers[index].screen.x;
            anchor.y += markers[index].screen.y;
            distance += markers[index].distance;
        }
        const float count = static_cast<float>(group.size());
        anchor.x /= count; anchor.y /= count; distance /= count;
        if (legend.Contains(anchor,10*scale)) continue;
        struct DetailLine { std::string text; ImU32 color; };
        std::vector<DetailLine> lines;
        const bool multiple = group.size() > 1;
        lines.push_back({multiple ? std::format("LIGHT DETAILS  /  {} nearby {}",group.size(),engineInput?"lights":"contributions") :
            engineInput ? "LIGHT DETAILS  /  1 engine light" : "LIGHT DETAILS  /  1 contribution", White});
        lines.push_back({multiple ? std::format("{:.1f} gu away  /  spatial grouping, not object identity",distance) :
            std::format("{:.1f} gu away  /  current {} values",distance,engineInput?"engine":"renderer"),Muted});
        // Fixed spatial ordering is independent of frame-local GPU slots and RGB.
        // Keep each contribution, including its real pulse, separate and unfiltered.
        const size_t shown = std::min<size_t>(group.size(),4);
        for (size_t member = 0; member < shown; ++member)
        {
            const auto& value = *markers[group[member]].light;
            const std::string kind = value.kind == "spot" ? "SPOT" : value.kind == "point" ? "POINT" : "UNKNOWN KIND";
            auto title = std::format("{}  {}",member+1,kind);
            if (value.memberCount > 0) title += std::format("  /  GROUP of {}",value.memberCount);
            if (engineInput) title += value.rendererSelected ? "  /  SELECTED" : "  /  NOT SELECTED HERE";
            if (value.kind == "spot" && value.coneHalfAngleDegrees)
                title += std::format("  /  half cone {:.1f} deg",*value.coneHalfAngleDegrees);
            if (config.details) title += std::format("  /  GPU slot {}",value.sampleIndex);
            lines.push_back({std::move(title),LightColor(value.colorLinear)});
            lines.push_back({std::format("XYZ  {:.3f} / {:.3f} / {:.3f}",value.position.x,
                value.position.y,value.position.z),Muted});
            lines.push_back({std::format("Linear RGB  {:.4g} / {:.4g} / {:.4g}   |   L {:.4g}",
                value.colorLinear.x,value.colorLinear.y,value.colorLinear.z,value.luminanceLinear),White});
            const auto visibility=CurrentSourceVisibility(value,view,now);
            if(visibility.status=="clear")
                lines.push_back({visibility.physicsSampled ?
                    std::format("SAMPLED VISIBLE  /  {} of 9 paths clear",visibility.clearSamples) : "SOURCE VISIBLE  /  estimate",Cyan});
            else if(visibility.status=="blocked")
                lines.push_back({visibility.physicsSampled ? "SAMPLED BLOCKED  /  9 of 9 paths blocked" : "SOURCE BLOCKED  /  estimate",Amber});
            else
                lines.push_back({std::string("SOURCE UNKNOWN  /  ")+VisibilityReasonText(visibility.reason),Muted});
        }
        if (group.size() > shown)
            lines.push_back({std::format("+{} more contributions; all raw values remain in API",group.size()-shown),Muted});
        // The box size must not oscillate with the number of digits in live RGB.
        const float width = std::min(460*detailScale,display.x-2*margin);
        const float height = (20+17*static_cast<float>(lines.size()))*detailScale;
        if (width > display.x-2*margin || height > display.y-2*margin) continue;
        // Clear the 22px reticle half-width plus the 8px collision margin.
        // A smaller gap rejects every placement for the aimed-at center light.
        const float gap = 36*scale;
        const std::array<ImVec2,12> offsets{{
            {gap,-height*.5f},{-width-gap,-height*.5f},{gap,-height-gap},{gap,gap},
            {-width-gap,-height-gap},{-width-gap,gap},{-width*.5f,-height-gap},{-width*.5f,gap},
            {gap,-2*height-gap},{-width-gap,-2*height-gap},{gap,height+gap},{-width-gap,height+gap}}};
        std::optional<Rect> target;
        for (const auto offset : offsets)
        {
            const float x = std::clamp(anchor.x+offset.x,margin,display.x-margin-width);
            const float y = std::clamp(anchor.y+offset.y,margin,display.y-margin-height);
            const Rect candidate{ImVec2(x,y),ImVec2(x+width,y+height)};
            bool collision = false;
            for (const auto& taken : occupied)
                if (candidate.Intersects(taken,8*scale)) { collision = true; break; }
            if (collision) continue;
            // Labels must not conceal other source rings, including tightly packed lights.
            for (const auto& other : markers)
                if (candidate.Contains(ImVec2(other.screen.x,other.screen.y),11*scale))
                { collision = true; break; }
            if (!collision) { target = candidate; break; }
        }
        if (!target) continue;
        occupied.push_back(*target);
        const ImVec2 attach(std::clamp(anchor.x,target->min.x,target->max.x),
            std::clamp(anchor.y,target->min.y,target->max.y));
        draw->AddLine(anchor,attach,IM_COL32(8,18,27,230),3*scale);
        draw->AddLine(anchor,attach,LightColor(light.colorLinear,185),scale);
        draw->AddRectFilled(target->min,target->max,Panel,7*scale);
        draw->AddRect(target->min,target->max,LightColor(light.colorLinear,175),7*scale);
        draw->AddRectFilled(ImVec2(target->min.x,target->min.y+11*scale),
            ImVec2(target->min.x+3*scale,target->max.y-11*scale),LightColor(light.colorLinear),scale);
        draw->PushClipRect(ImVec2(target->min.x+10*detailScale,target->min.y),
            ImVec2(target->max.x-10*detailScale,target->max.y),true);
        for (size_t line = 0; line < lines.size(); ++line)
            draw->AddText(font,12*detailScale,ImVec2(target->min.x+13*detailScale,
                target->min.y+(10+17*static_cast<float>(line))*detailScale),lines[line].color,lines[line].text.c_str());
        draw->PopClipRect();
        ++labelCount;
    }

    draw->AddRectFilled(legend.min,legend.max,Panel,8*scale);
    draw->AddRect(legend.min,legend.max,live && cameraReady ? IM_COL32(49,221,208,110) : Amber,8*scale);
    draw->PushClipRect(legend.min,legend.max,true);
    const auto legendText = [&](float y,ImU32 color,const std::string& line)
    {
        draw->AddText(font,12*scale,ImVec2(legend.min.x+14*scale,legend.min.y+y*scale),color,line.c_str());
    };
    legendText(10,live && cameraReady && sample.playerPosition ? Cyan : Amber,headline);
    legendText(29,White,controls);
    legendText(48,Cyan,visibilityLine);
    legendText(67,Muted,caveat);
    if (engineInput) legendText(86,Muted,coverage);
    draw->PopClipRect();
}

void DrawNotice(const Notice& notice, const Config& config, bool hudVisible, bool details)
{
    const auto display = ImGui::GetIO().DisplaySize;
    const float scale = std::min(std::max(1.0f, display.y / 1080.0f), display.x / 660.0f);
    if (scale < .3f) return;
    const float margin = 20 * scale, width = 620 * scale;
    float x = margin, y = margin;
    auto* font = ImGui::GetFont();
    const float wrap = width - 36 * scale;
    const auto detailSize = font->CalcTextSizeA(15 * scale, wrap, wrap,
        notice.detail.c_str(), nullptr);
    const float height = 60 * scale + detailSize.y;
    // Default HUD uses top-left too. Place notices beside it when possible,
    // otherwise below. Bottom-left/right HUDs do not consume this origin.
    if (hudVisible && config.corner == 0)
    {
        const float hudScale = HudScale(display.x, display.y, config, details);
        const float beside = 550 * hudScale;
        if (beside + width + margin <= display.x) x = beside;
        else y = (HudNaturalHeight(config,details)+40) * hudScale;
    }
    y = std::min(y, std::max(margin, display.y - height - margin));
    auto* draw = ImGui::GetForegroundDrawList();
    const ImVec2 start(x,y), end(x+width,y+height);
    const ImU32 accent = notice.error ? IM_COL32(255,157,111,255) : Cyan;
    draw->AddRectFilled(start, end, IM_COL32(9,20,30,240), 8 * scale);
    draw->AddRect(start, end, accent, 8 * scale);
    draw->AddText(font, 13 * scale, ImVec2(x+18*scale,y+10*scale), Muted, "CRIMSON DESERT TELEMETRY");
    draw->AddText(font, 18 * scale, ImVec2(x+18*scale,y+29*scale), accent, notice.title.c_str());
    draw->AddText(font, 15 * scale, ImVec2(x+18*scale,y+55*scale), White, notice.detail.c_str(), nullptr, wrap);
}
}

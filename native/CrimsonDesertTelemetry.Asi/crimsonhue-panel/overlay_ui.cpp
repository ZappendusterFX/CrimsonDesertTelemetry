#include "overlay.h"
#include "overlay_shortcut.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace crimson::overlay {
namespace {
std::string testTab;
std::string Text(const Json& value, const char* key) { return value.value(key, std::string{}); }
void Wrap(const std::string& text) { ImGui::TextWrapped("%s", text.c_str()); }
void Action(const char* name) { Command({{"action", name}}); }
bool Button(const char* label, bool enabled = true) {
    ImGui::BeginDisabled(!enabled); const bool clicked = ImGui::Button(label); ImGui::EndDisabled(); return clicked;
}
void Room(const Json& view) {
    const ImVec2 size{std::max(200.0f, ImGui::GetContentRegionAvail().x), 260};
    const auto at = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("Room", size);
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 center{at.x + size.x / 2, at.y + size.y / 2};
    const float sx = size.x * .43f, sy = size.y * .35f;
    draw->AddRectFilled(at, {at.x + size.x, at.y + size.y}, IM_COL32(21, 25, 34, 255), 8);
    draw->AddLine({center.x - 55, center.y - sy}, {center.x + 55, center.y - sy}, IM_COL32(165, 180, 210, 255), 4);
    draw->AddText({center.x - 28, center.y - sy - 25}, IM_COL32(175, 185, 200, 255), "SCREEN");
    draw->AddCircleFilled(center, 5, IM_COL32(215, 220, 235, 255));
    if (view.value("leftRight", true)) draw->AddLine({center.x, center.y - sy + 10}, {center.x, center.y + sy}, IM_COL32(55, 70, 85, 255));
    if (view.value("frontRear", true)) draw->AddLine({center.x - sx, center.y}, {center.x + sx, center.y}, IM_COL32(55, 70, 85, 255));
    for (const auto& source : view.value("sources", Json::array())) {
        const auto direction = source.at("direction"); const auto fade = source.value("fade", 0.0f);
        if (fade > 0) draw->AddCircleFilled({center.x + direction.value("x", 0.0f) * sx * .7f,
            center.y - direction.value("y", 0.0f) * sy * .7f}, 2.0f, IM_COL32(155, 110, 90, static_cast<int>(fade * 255)));
    }
    for (const auto& washer : view.value("wallWashers", Json::array())) {
        if (!washer.value("enabled", false)) continue;
        for (const auto& segment : washer.at("segments")) {
            ImVec4 output{0, 0, 0, 1}; int count = 0;
            for (const auto& reference : segment.at("channels")) for (const auto& channel : view.value("channels", Json::array()))
                if (channel.at("id") == reference.at("id")) {
                    const auto& rgb = channel.at("rgb");
                    output.x += rgb.value("x", 0.0f); output.y += rgb.value("y", 0.0f); output.z += rgb.value("z", 0.0f); ++count;
                }
            if (count) { output.x /= count; output.y /= count; output.z /= count; }
            constexpr double radians = 3.14159265358979323846 / 180;
            const double yaw = washer.value("yaw", 0.0) + segment.value("yawOffset", 0.0);
            const double span = washer.value("horizontalSpan", 0.0);
            for (int height = -1; height <= 1; ++height) {
                const double elevation = std::clamp(segment.value("elevation", 0.0) + height * segment.value("verticalSpan", 0.0) / 2, -89.0, 89.0);
                ImVec2 previous{};
                for (int i = 0; i <= 30; ++i) {
                    const double angle = (yaw + span * (static_cast<double>(i) / 30 - .5)) * radians;
                    const ImVec2 point{center.x + static_cast<float>(std::sin(angle) * std::cos(elevation * radians)) * sx * .95f,
                        center.y - static_cast<float>(std::cos(angle) * std::cos(elevation * radians)) * sy * .95f};
                    if (i != 0) {
                        draw->AddLine(previous, point, IM_COL32(90, 155, 180, 160), 4);
                        if (std::max({output.x, output.y, output.z}) > .001f)
                            draw->AddLine(previous, point, ImGui::ColorConvertFloat4ToU32(output), 2);
                    }
                    previous = point;
                }
            }
        }
    }
    for (const auto& channel : view.value("channels", Json::array())) {
        const auto rgb = channel.at("rgb");
        const ImVec2 point{center.x + channel.value("x", 0.0f) * sx, center.y - channel.value("y", 0.0f) * sy};
        const auto color = ImGui::ColorConvertFloat4ToU32({rgb.value("x", 0.0f), rgb.value("y", 0.0f), rgb.value("z", 0.0f), 1});
        draw->AddCircleFilled(point, 10, color); draw->AddCircle(point, 10, IM_COL32(170, 180, 195, 255));
        const auto label = channel.value("label", std::string{});
        draw->AddText({point.x - 20, point.y + 13}, IM_COL32(200, 205, 220, 255), label.c_str());
    }
    if (view.value("demo", false)) draw->AddText({at.x + 12, at.y + size.y - 25}, IM_COL32(239, 150, 170, 255), "SIMULATED DATA - NO LAMP OUTPUT");
}
void WallWash(const Json& view) {
    Wrap("Map the illuminated wall as seen from your seat. Dots show Hue positions; blue traces show calibrated wall areas.");
    Wrap("Direction: 0 = screen, -90 = left, +90 = right, +/-180 = behind. Width is the visible wall span, not the lamp's beam angle.");
    Wrap("Demo previews your selected area without lamp output. Segment tests are separate: one channel, white at 10%, two seconds, then restore.");
    if (view.value("demo", false)) Wrap("Turn off Demo preview only before a physical segment test.");
    ImGui::BeginDisabled(view.value("canStop", false));
    bool preview = view.value("wallPreview", false);
    if (ImGui::Checkbox("Preview one light source (no lamp output)", &preview))
        Command({{"action", "wallPreview"}, {"key", "enabled"}, {"value", preview}});
    if (preview) {
        double yaw = view.value("previewYaw", 0.0), elevation = view.value("previewElevation", 0.0);
        const double yawMin = -180, yawMax = 180, elevationMin = -89, elevationMax = 89;
        if (ImGui::SliderScalar("Source direction", ImGuiDataType_Double, &yaw, &yawMin, &yawMax, "%.1f deg"))
            Command({{"action", "wallPreview"}, {"key", "yaw"}, {"value", yaw}});
        if (ImGui::SliderScalar("Source height", ImGuiDataType_Double, &elevation, &elevationMin, &elevationMax, "%.1f deg"))
            Command({{"action", "wallPreview"}, {"key", "elevation"}, {"value", elevation}});
    }
    ImGui::EndDisabled();
    const auto washers = view.value("wallWashers", Json::array());
    if (washers.empty()) Wrap("Pair the bridge and select an area first.");
    const bool editing = !view.value("identifying", false);
    for (const auto& washer : washers) {
        const auto service = Text(washer, "serviceId");
        ImGui::PushID(service.c_str());
        std::string title = Text(washer, "label") + "  / CH";
        for (const auto& segment : washer.at("segments")) for (const auto& channel : segment.at("channels"))
            title += " " + std::to_string(channel.value("id", 0));
        ImGui::SeparatorText(title.c_str());
        const auto set = [&](const char* key, const Json& value, int index = -1) {
            Command({{"action", "wallWash"}, {"serviceId", service}, {"key", key}, {"value", value}, {"index", index}});
        };
        const auto angle = [&](const char* label, const char* key, double value, double min, double max, int index = -1) {
            ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 135));
            if (ImGui::SliderScalar(label, ImGuiDataType_Double, &value, &min, &max, "%.1f deg")) set(key, value, index);
        };
        ImGui::BeginDisabled(!editing);
        bool enabled = washer.value("enabled", false);
        if (ImGui::Checkbox("Use wall area", &enabled)) set("enabled", enabled);
        if (Button("Start left")) { set("yaw", -60); set("span", 60); set("enabled", true); }
        ImGui::SameLine(); if (Button("Start right")) { set("yaw", 60); set("span", 60); set("enabled", true); }
        Wrap("Starting points only: adjust to the light actually visible on your walls.");
        angle("Direction", "yaw", washer.value("yaw", 0.0), -180, 180);
        angle("Wall width", "span", washer.value("horizontalSpan", 60.0), 0, 180);
        for (const auto& segment : washer.at("segments")) {
            const int index = segment.value("index", 0);
            ImGui::PushID(index);
            if (ImGui::TreeNode("Segment", "Segment %d", index)) {
                angle("Side offset", "offset", segment.value("yawOffset", 0.0), -180, 180, index);
                angle("Height angle", "elevation", segment.value("elevation", 0.0), -89, 89, index);
                angle("Height span", "verticalSpan", segment.value("verticalSpan", 0.0), 0, 90, index);
                Wrap("Height starts from Hue's coordinates. Use a segment test to check which wall patch it illuminates.");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (Button("Use imported Hue positions")) set("reset", nullptr);
        ImGui::EndDisabled();
        for (const auto& segment : washer.at("segments")) for (const auto& channel : segment.at("channels")) {
            const int id = channel.value("id", 0);
            const auto label = "Test CH " + std::to_string(id) + " (2 s, 10%)";
            if (Button(label.c_str(), view.value("canIdentify", false) && channel.value("canIdentify", false)))
                Command({{"action", "identify"}, {"channel", id}});
        }
        ImGui::PopID();
    }
    if (Button("Save room calibration", editing)) Action("save");
    Wrap("Stored only on this PC. Shared mixer presets do not change room calibration.");
}
void Controls(const Json& view, const char* group) {
    if (strcmp(group, "Space") == 0) {
        std::vector<float> curve; for (const auto& value : view.value("curve", Json::array())) curve.push_back(value.get<float>());
        ImGui::PlotLines("Distance fade", curve.data(), static_cast<int>(curve.size()), 0, nullptr, 0, 1, {0, 90});
        ImGui::Text("0 to %.2f game units", view.value("curveEnd", 35.0));
    }
    for (const auto& flag : view.value("switches", Json::array())) {
        if (flag.value("group", std::string{}) != group) continue;
        const auto key = Text(flag, "key"); bool value = flag.value("value", false);
        ImGui::PushID(key.c_str());
        if (ImGui::Checkbox(Text(flag, "title").c_str(), &value)) Command({{"action", "switch"}, {"key", key}, {"value", value}});
        Wrap(Text(flag, "description")); ImGui::PopID();
    }
    for (const auto& field : view.value("controls", Json::array())) {
        if (field.value("group", std::string{}) != group) continue;
        const auto key = Text(field, "key"); double value = field.value("value", 0.0);
        const double minimum = field.value("min", 0.0), maximum = field.value("max", 1.0);
        ImGui::PushID(key.c_str());
        ImGui::Text("%s %s", Text(field, "title").c_str(), Text(field, "unit").c_str());
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 170));
        bool changed = ImGui::SliderScalar("##slider", ImGuiDataType_Double, &value, &minimum, &maximum, "%.3f");
        ImGui::SameLine(); ImGui::SetNextItemWidth(95);
        // InputScalar's Enter commits exact values; arrows use each field's step.
        changed |= ImGui::InputScalar("##number", ImGuiDataType_Double, &value, nullptr, nullptr, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine(); if (ImGui::SmallButton("Reset")) { value = field.value("initial", 0.0); changed = true; }
        if (changed && std::isfinite(value) && value >= minimum && value <= maximum)
            Command({{"action", "set"}, {"key", key}, {"value", value}});
        Wrap(Text(field, "description")); ImGui::Spacing(); ImGui::PopID();
    }
    if (Button("Reset this tab")) Command({{"action", "reset"}, {"group", group}});
    ImGui::SameLine(); if (Button("Save settings")) Action("save");
}
void Setup(const Json& view) {
    static Shortcut chosen;
    static bool shortcutSeeded = false;
    if (!shortcutSeeded && view.value("ready", false)) { chosen = CurrentShortcut(); shortcutSeeded = true; }
    ImGui::SeparatorText("Connection");
    const bool connected = view.value("bridgeConnected", false);
    ImGui::TextColored(connected ? ImVec4(.19f, .87f, .82f, 1) : ImVec4(.98f, .74f, .36f, 1),
        "%s", connected ? "CONNECTED" : "NOT CONNECTED");
    Wrap(Text(view, "bridgeStatus"));
    if (!connected) {
        Wrap("1. Find your Hue Bridge at its local IP address.");
        Wrap("2. When found, press the round link button ON the bridge, then click Trust bridge & pair within 30 seconds.");
        Wrap("3. Wait for CONNECTED, then select your Entertainment area.");
    }
    static std::array<char, 2048> bridge{}, telemetry{};
    static bool seeded = false;
    if (!seeded && view.value("ready", false)) {
        strcpy_s(bridge.data(), bridge.size(), Text(view, "bridgeAddress").c_str());
        strcpy_s(telemetry.data(), telemetry.size(), Text(view, "telemetryAddress").c_str()); seeded = true;
    }
    const bool enabled = view.value("canSetup", false);
    ImGui::BeginDisabled(!enabled);
    ImGui::InputText("Bridge IPv4", bridge.data(), bridge.size());
    ImGui::EndDisabled();
    if (Button(connected ? "Reconnect bridge" : "Find bridge", enabled)) {
        Command({{"action", "connect"}, {"bridge", bridge.data()}, {"telemetry", telemetry.data()}});
    }

    if (view.value("pairing", false)) {
        ImGui::TextColored({.98f, .74f, .36f, 1}, "PRESS THE BUTTON ON YOUR HUE BRIDGE NOW");
        Wrap("Bridge found; pairing is still required. Press its round link button, then confirm below within 30 seconds.");
        if (ImGui::TreeNode("Bridge identity & certificate")) { Wrap(Text(view, "certificate")); ImGui::TreePop(); }
        if (Button("Trust bridge & pair", enabled)) Action("pair");
    }
    const auto connectionError = Text(view, "bridgeError");
    if (!connectionError.empty()) {
        ImGui::TextColored({.98f, .74f, .36f, 1}, "CONNECTION FAILED");
        Wrap(connectionError);
        if (ImGui::TreeNode("Connection details")) { Wrap(Text(view, "bridgeDiagnostic")); ImGui::TreePop(); }
    }
    auto selectedName = Text(view, "selectedArea");
    const auto areas = view.value("areas", Json::array());
    for (const auto& area : view.value("areas", Json::array())) if (Text(area, "id") == selectedName) { selectedName = Text(area, "name"); break; }
    if (!connected) selectedName = "Connect the bridge first";
    else if (areas.empty()) selectedName = "No Entertainment areas found";
    ImGui::BeginDisabled(!connected || areas.empty() || !enabled);
    if (ImGui::BeginCombo("Entertainment area", selectedName.c_str())) {
        for (const auto& area : areas) {
            ImGui::BeginDisabled(!enabled);
            if (ImGui::Selectable(Text(area, "name").c_str(), Text(area, "id") == Text(view, "selectedArea")))
                Command({{"action", "area"}, {"id", Text(area, "id")}});
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (connected && areas.empty()) Wrap("Connected, but no Entertainment area exists. Create one in the Hue app, then Refresh areas.");
    if (connected && !areas.empty()) ImGui::Text("%d Entertainment area(s) available", static_cast<int>(areas.size()));
    if (Button("Refresh areas", view.value("canRefresh", false) && enabled)) Action("refresh");
    Wrap(Text(view, "layout")); Wrap("Create and position channels in the Hue app. Imported positions are preserved.");
    if (ImGui::CollapsingHeader("Telemetry connection")) {
        ImGui::BeginDisabled(!enabled);
        ImGui::InputText("Telemetry WebSocket", telemetry.data(), telemetry.size());
        if (Button("Apply addresses")) Command({{"action", "setup"}, {"bridge", bridge.data()}, {"telemetry", telemetry.data()}});
        ImGui::SameLine(); if (Button("Reconnect telemetry")) Action("reconnect");
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Overlay shortcut")) {
        const auto& keys = ShortcutKeys();
        std::string keyName;
        for (const auto& [key, label] : keys) if (key == chosen.key) keyName = label;
        if (ImGui::BeginCombo("Key", keyName.c_str())) {
            for (const auto& [key, label] : keys) {
                if (ImGui::Selectable(label.c_str(), chosen.key == key)) chosen.key = key;
                if (chosen.key == key) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Ctrl", &chosen.control); ImGui::SameLine();
        ImGui::Checkbox("Shift", &chosen.shift); ImGui::SameLine(); ImGui::Checkbox("Alt", &chosen.alt);
        Wrap("Current: " + ShortcutLabel(CurrentShortcut()));
        if (Button("Save shortcut")) Command({{"action", "shortcut"}, {"key", chosen.key},
            {"control", chosen.control}, {"shift", chosen.shift}, {"alt", chosen.alt}});
        ImGui::SameLine(); if (Button("Default shortcut")) chosen = Shortcut{};
        Wrap("CDT uses F8-F11 even with modifiers. Choose another key to avoid triggering both mods.");
    }
}
void Presets(const Json& view) {
    static std::array<char, 2048> path{};
    static bool overwrite = false;
    static std::array<bool, 4> groups{true, true, true, false};
    ImGui::InputText("Preset path (.json)", path.data(), path.size());
    Wrap("Enter an absolute file path. Exports contain mixer controls only.");
    ImGui::Checkbox("Allow overwrite of this preset file", &overwrite);
    if (Button("Export preset")) Command({{"action", "export"}, {"path", path.data()}, {"overwrite", overwrite}});
    ImGui::SameLine(); if (Button("Preview import")) { groups = {true, true, true, false}; Command({{"action", "import"}, {"path", path.data()}}); }
    const auto name = Text(view, "presetName");
    if (!name.empty()) {
        ImGui::SeparatorText(name.c_str());
        const char* names[]{"Output", "Ambient", "Color", "Space"};
        int mask = 0;
        for (int i = 0; i < 4; ++i) { ImGui::Checkbox(names[i], &groups[i]); ImGui::SameLine(); if (groups[i]) mask |= 1 << i; }
        ImGui::NewLine();
        for (const auto& change : view.value("presetChanges", Json::array())) {
            const auto text = change.get<std::string>();
            for (int i = 0; i < 4; ++i) if (groups[i] && text.rfind(names[i], 0) == 0) Wrap(text);
        }
        if (Button("Apply selected", mask != 0)) Command({{"action", "applyImport"}, {"groups", mask}});
        ImGui::SameLine(); if (Button("Cancel import")) Action("cancelImport");
    }
}
}
void SelectTabForTest(const char* name) { testTab = name ? name : ""; }
void DrawUi(const Json& view, bool& visible, float scale) {
    ImGui::SetNextWindowSize({1040 * scale, 690 * scale}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({60 * scale, 40 * scale}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({720 * scale, 480 * scale}, ImGui::GetIO().DisplaySize);
    bool open = true;
    if (ImGui::Begin("CrimsonHue - live lighting mixer", &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored({49.f/255, 221.f/255, 208.f/255, 1}, "CRIMSONHUE  /  %s", ShortcutLabel(CurrentShortcut()).c_str());
        const bool bridgeConnected = view.value("bridgeConnected", false);
        ImGui::TextColored(bridgeConnected ? ImVec4(.19f, .87f, .82f, 1) : ImVec4(.98f, .74f, .36f, 1),
            "%s", bridgeConnected ? "HUE BRIDGE: CONNECTED" : "HUE BRIDGE: NOT CONNECTED - open Setup");
        Wrap(Text(view, "telemetry")); Wrap(Text(view, "capture")); Wrap(Text(view, "ambient"));
        if (!view.value("ready", false)) { Wrap("Starting CrimsonHue. Requires .NET 8 Desktop Runtime x64."); Wrap(GraphicsStatus()); }
        else {
            const float left = std::max(350 * scale, ImGui::GetContentRegionAvail().x * .47f);
            ImGui::BeginChild("Mixer", {left, -110 * scale}, ImGuiChildFlags_Borders);
            if (ImGui::BeginTabBar("MixerTabs")) {
                static bool previousWallPreview = false;
                const bool selectRoom = view.value("wallPreview", false) && !previousWallPreview;
                previousWallPreview = view.value("wallPreview", false);
                for (const auto* group : {"Output", "Ambient", "Color", "Space", "Room", "Setup", "Presets"}) {
                    const bool choose = (selectRoom && strcmp(group, "Room") == 0) || testTab == group;
                    if (ImGui::BeginTabItem(group, nullptr, choose ? ImGuiTabItemFlags_SetSelected : 0)) {
                        if (testTab == group) testTab.clear();
                        if (strcmp(group, "Setup") == 0) Setup(view);
                        else if (strcmp(group, "Room") == 0) WallWash(view);
                        else if (strcmp(group, "Presets") == 0) Presets(view);
                        else Controls(view, group);
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
            ImGui::EndChild(); ImGui::SameLine();
            ImGui::BeginChild("Monitor", {0, -110 * scale}, ImGuiChildFlags_Borders);
            bool demo = view.value("demo", false);
            ImGui::BeginDisabled(view.value("canStop", false) || view.value("smoke", false));
            if (ImGui::Checkbox("Demo preview only", &demo)) Command({{"action", "demo"}, {"value", demo}});
            ImGui::EndDisabled();
            if (ImGui::BeginTabBar("MonitorTabs")) {
                if (ImGui::BeginTabItem("Room & output")) {
                    Room(view); Wrap(Text(view, "boundary"));
                    for (const auto& channel : view.value("channels", Json::array())) {
                        const auto rgb = channel.at("rgb");
                        ImGui::Text("%s  CH %d  Z %.2f", Text(channel, "label").c_str(), channel.value("id", 0), channel.value("z", 0.0));
                        ImGui::Text("RGB %.4f / %.4f / %.4f", rgb.value("x", 0.0), rgb.value("y", 0.0), rgb.value("z", 0.0));
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("CDT raw input")) {
                    bool frozen = view.value("frozen", false);
                    if (ImGui::Checkbox("Freeze readout", &frozen)) Command({{"action", "freeze"}, {"value", frozen}});
                    Wrap(Text(view, "rawAmbient")); Wrap(Text(view, "rawSummary"));
                    const auto lights = view.value("rawLights", Json::array());
                    if (lights.is_array()) for (const auto& light : lights) {
                        ImGui::Text("%s gu  R %s  G %s  B %s", Text(light, "distance").c_str(), Text(light, "red").c_str(), Text(light, "green").c_str(), Text(light, "blue").c_str());
                        Wrap(Text(light, "position"));
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndChild();
            Wrap(Text(view, "stream"));
            if (Button("Start lighting sync", view.value("canStart", false))) Action("start");
            ImGui::SameLine(); if (Button("Stop", view.value("canStop", false))) Action("stop");
            ImGui::SameLine(); if (Button("Hide settings")) visible = false;
            Wrap(Text(view, "tuning"));
        }
        Wrap("Stop and wait for stopped status before leaving the game. Close stops sync; Hide keeps it running.");
    }
    ImGui::End();
    // Window close means stop + save; the explicit Hide button keeps sync armed.
    if (!open) { visible = false; Action("close"); }
}
}

#include "overlay.h"
#include "../crimsonhue-panel/overlay.h"
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <format>
#include <mutex>
#include <thread>
#include <utility>

namespace cdt::overlay
{
namespace
{
constexpr size_t MaximumTelemetryMessageBytes = 4 * 1024 * 1024;
struct Shared
{
    std::mutex mutex;
    View view;
    std::string healthStatus, healthError;
    std::vector<View::LocalFault> localFaults;
    AmbientSummary ambient;
    Clock::time_point ambientReceived{};
    bool hasAmbient{};
};
// Deliberately process-lifetime: callbacks/worker teardown must not run in DllMain.
Shared& SharedView() { static auto* shared = new Shared; return *shared; }
struct HttpHandle
{
    HINTERNET value{};
    explicit HttpHandle(HINTERNET handle) : value(handle) { }
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
};
float IniFloat(const std::filesystem::path& ini, const wchar_t* key, float fallback, float minimum, float maximum,
    const wchar_t* section = L"Overlay")
{
    std::array<wchar_t, 64> wide{};
    GetPrivateProfileStringW(section, key, L"", wide.data(), static_cast<DWORD>(wide.size()), ini.c_str());
    std::array<char, 64> value{};
    for (size_t i = 0; wide[i] && i < value.size() - 1; ++i)
    {
        if (wide[i] > 127) return fallback;
        value[i] = static_cast<char>(wide[i]);
    }
    float result{};
    const auto end = value.data() + strlen(value.data());
    const auto parsed = std::from_chars(value.data(), end, result);
    return parsed.ec == std::errc{} && parsed.ptr == end && std::isfinite(result)
        ? std::clamp(result, minimum, maximum) : fallback;
}
void Connect(const Config config, HANDLE stop)
{
    if (!config.enabled && !config.notifications && !config.lightOverlay) return;
    while (WaitForSingleObject(stop, 0) == WAIT_TIMEOUT)
    {
        View view;
        view.connection = "CONNECTING";
        Publish(view);
        try
        {
            HttpHandle session(WinHttpOpen(L"CrimsonDesertTelemetryOverlay/1.0.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!session.value) throw std::runtime_error("Session failed");
            WinHttpSetTimeouts(session.value, 1000, 1000, 1000, 1000);
            HttpHandle connection(WinHttpConnect(session.value, L"127.0.0.1", config.port, 0));
            HttpHandle request(WinHttpOpenRequest(connection.value, L"GET", L"/v1/stream", nullptr,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
            // Explicitly disable redirects: telemetry never leaves loopback.
            DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            if (!request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
                !WinHttpSetOption(request.value, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
                !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                !WinHttpReceiveResponse(request.value, nullptr)) throw std::runtime_error("Connection failed");
            DWORD code{}, codeSize = sizeof(code);
            if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeSize, WINHTTP_NO_HEADER_INDEX) || code != 101)
                throw std::runtime_error("No WebSocket upgrade");
            HttpHandle socket(WinHttpWebSocketCompleteUpgrade(request.value, 0));
            if (!socket.value) throw std::runtime_error("WebSocket failed");
            // Synchronous WebSocket receive has its own process-lifetime thread;
            // it never blocks rendering or the graphics-maintenance worker.
            view.connected = true;
            Publish(view);
            std::array<char, 8192> buffer{};
            std::string message;
            Clock::time_point previous{};
            while (WaitForSingleObject(stop, 0) == WAIT_TIMEOUT)
            {
                DWORD received{};
                WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
                const DWORD error = WinHttpWebSocketReceive(socket.value, buffer.data(),
                    static_cast<DWORD>(buffer.size()), &received, &type);
                if (error == ERROR_WINHTTP_TIMEOUT) continue;
                if (error != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
                if (type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE && type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
                    throw std::runtime_error("Non-text telemetry");
                if (message.size() + received > MaximumTelemetryMessageBytes)
                    throw std::runtime_error("Telemetry message too large");
                message.append(buffer.data(), received);
                if (type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) continue;
                try
                {
                    const auto now = Clock::now();
                    // Start receipt age before parsing a potentially large light
                    // payload; parsing time must not extend its freshness budget.
                    auto sample = ParseSample(message, std::chrono::system_clock::now());
                    // A repeated sample must not refresh its age, even if the peer resends it.
                    if (view.hasSample && sample.sequence <= view.sample.sequence)
                        throw std::runtime_error("Non-increasing sequence");
                    if (previous != Clock::time_point{})
                    {
                        const double interval = std::chrono::duration<double>(now - previous).count();
                        if (interval > 0) view.rateHz = view.rateHz == 0 ? 1 / interval : 0.9 * view.rateHz + 0.1 / interval;
                    }
                    previous = now;
                    view.sample = std::move(sample);
                    view.received = now;
                    view.hasSample = true;
                    ClearLocalFault("bootstrap");
                    ClearLocalFault("stream");
                    Publish(view);
                }
                catch (...)
                {
                    SetLocalFault("stream", "Telemetry data is incompatible",
                        "Install matching ASI and telemetry host versions, then restart Crimson Desert.");
                    view.hasSample = false;
                    view.connected = false;
                    view.connection = "INVALID / INCOMPATIBLE DATA";
                    Publish(view);
                    throw;
                }
                message.clear();
            }
        }
        catch (...) { }
        view.connected = false;
        if (view.connection != "INVALID / INCOMPATIBLE DATA") view.connection = "RECONNECTING";
        Publish(view);
        if (WaitForSingleObject(stop, 1000) != WAIT_TIMEOUT) break;
    }
}
struct WorkerArgs { Config config; HANDLE stop; std::filesystem::path directory; };
void PollHealth(unsigned short port)
{
    // This is a bounded diagnostic read on the maintenance worker, never on the
    // game's render thread. Unsupported builds do not produce stream snapshots.
    std::string status, error;
    try
    {
        HttpHandle session(WinHttpOpen(L"CrimsonDesertTelemetryStatus/1", WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session.value) throw std::runtime_error("Health session failed");
        WinHttpSetTimeouts(session.value, 500, 500, 500, 500);
        HttpHandle connection(WinHttpConnect(session.value, L"127.0.0.1", port, 0));
        HttpHandle request(WinHttpOpenRequest(connection.value, L"GET", L"/v1/health", nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)) ||
            !WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.value, nullptr)) throw std::runtime_error("Health unavailable");
        DWORD code{}, size = sizeof(code);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX) || code != 200)
            throw std::runtime_error("Health response failed");
        std::string body;
        std::array<char, 4096> bytes{};
        for (unsigned reads = 0; reads < 5; ++reads)
        {
            DWORD received{};
            if (!WinHttpReadData(request.value, bytes.data(), static_cast<DWORD>(bytes.size()), &received))
                throw std::runtime_error("Health read failed");
            if (!received) break;
            body.append(bytes.data(), received);
            if (body.size() > 16384) throw std::runtime_error("Health size limit");
        }
        const auto json = nlohmann::json::parse(body);
        status = json.at("status").get<std::string>();
        if (json.contains("error") && json["error"].is_string()) error = json["error"].get<std::string>();
        if (status.size() > 64) status = "error";
        if (error.size() > 300) error.resize(300);
    }
    catch (...) { status = "host-unreachable"; }
    auto& shared = SharedView();
    std::lock_guard guard(shared.mutex);
    // A lost health request is not proof that a diagnosed failure resolved.
    if (status == "host-unreachable" && (shared.healthStatus == "unsupported-build" || shared.healthStatus == "error")) return;
    if (status != "host-unreachable")
        std::erase_if(shared.localFaults, [](const auto& fault) { return fault.source == "bootstrap"; });
    shared.healthStatus = std::move(status);
    shared.healthError = std::move(error);
}
void PollAmbient(unsigned short port)
{
    AmbientSummary ambient; ambient.status="unavailable"; ambient.reason="host-unreachable";
    try
    {
        HttpHandle session(WinHttpOpen(L"CrimsonDesertTelemetryAmbientHud/1",WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
        if(!session.value)throw std::runtime_error("Ambient session failed");
        WinHttpSetTimeouts(session.value,500,500,500,500);
        HttpHandle connection(WinHttpConnect(session.value,L"127.0.0.1",port,0));
        HttpHandle request(WinHttpOpenRequest(connection.value,L"GET",L"/v1/ambient",nullptr,
            WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0));
        DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if(!request.value||!WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect))||
            !WinHttpSendRequest(request.value,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)||
            !WinHttpReceiveResponse(request.value,nullptr))throw std::runtime_error("Ambient unavailable");
        DWORD code{},size=sizeof(code);
        if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,&code,&size,WINHTTP_NO_HEADER_INDEX)||code!=200)
            throw std::runtime_error("Ambient response failed");
        std::string body;std::array<char,4096> bytes{};
        for(unsigned reads=0;reads<16;++reads)
        {
            DWORD received{};if(!WinHttpReadData(request.value,bytes.data(),static_cast<DWORD>(bytes.size()),&received))
                throw std::runtime_error("Ambient read failed");
            if(!received)break;body.append(bytes.data(),received);
            if(body.size()>65536)throw std::runtime_error("Ambient size limit");
        }
        ambient=ParseAmbient(body);
    }
    catch(...){ }
    auto& shared=SharedView();std::lock_guard guard(shared.mutex);
    shared.ambient=std::move(ambient);shared.ambientReceived=Clock::now();shared.hasAmbient=true;
}
DWORD WINAPI Worker(void* parameter)
{
    auto* args = static_cast<WorkerArgs*>(parameter);
    try
    {
        // The optional ASI can be loaded later in the loader's initial module list.
        // With every CDT view off, wait without hooks or HTTP/WebSocket clients.
        if (args->config.huePanel) crimson::overlay::RefreshBridge();
        while (!args->config.enabled && !args->config.notifications && !args->config.lightOverlay &&
            args->config.huePanel && !crimson::overlay::PanelAvailable()) {
            if (WaitForSingleObject(args->stop, 50) != WAIT_TIMEOUT) return 0;
            crimson::overlay::RefreshBridge();
        }
        const bool installed = InstallGraphics(args->config);
        std::ofstream log(args->directory / L"CrimsonDesertTelemetry.overlay.log", std::ios::app);
        log << "Overlay: " << (installed ? "graphics hooks installed" : "graphics hooks unavailable") << '\n';
        log.flush();
        if (!installed) return 1;
        std::thread([args] { try { Connect(args->config, args->stop); } catch (...) { } }).detach();
        if(args->config.enabled&&args->config.showAmbient)
            std::thread([args]
            {
                while(WaitForSingleObject(args->stop,0)==WAIT_TIMEOUT)
                {
                    PollAmbient(args->config.port);
                    if(WaitForSingleObject(args->stop,500)!=WAIT_TIMEOUT)break;
                }
            }).detach();
        std::string last;
        std::string lastNotice;
        NoticeTracker notices;
        Clock::time_point lastHealth{};
        while (WaitForSingleObject(args->stop, 50) == WAIT_TIMEOUT)
        {
            MaintainGraphics();
            if (args->config.notifications && Clock::now() - lastHealth >= std::chrono::seconds(2))
            {
                View current;
                TryRead(current);
                if (!IsLive(current, Clock::now(), args->config.staleMs)) PollHealth(args->config.port);
                else
                {
                    auto& shared = SharedView();
                    std::lock_guard guard(shared.mutex);
                    shared.healthStatus.clear(); shared.healthError.clear();
                }
                lastHealth = Clock::now();
            }
            const std::string status = GraphicsStatus();
            if (status != last)
            {
                log << status << '\n';
                log.flush();
                last = status;
            }
            View view;
            TryRead(view);
            const auto notice = notices.Update(view, args->config, Clock::now());
            const std::string noticeText = notice ? notice->title + ": " + notice->detail : "";
            if (!noticeText.empty() && noticeText != lastNotice)
            {
                log << "Status: " << noticeText << '\n'; log.flush();
            }
            lastNotice = noticeText;
        }
        if (args->config.huePanel) {
            crimson::overlay::RequestPanelStop();
            crimson::overlay::RefreshBridge();
        }
    }
    catch (...) { return 2; }
    return 0;
}
}
bool TryRead(View& view)
{
    auto& shared = SharedView();
    std::unique_lock lock(shared.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    view = shared.view;
    view.ambient=shared.ambient;view.ambientReceived=shared.ambientReceived;view.hasAmbient=shared.hasAmbient;
    view.healthStatus = shared.healthStatus;
    view.healthError = shared.healthError;
    view.localFaults.insert(view.localFaults.end(), shared.localFaults.begin(), shared.localFaults.end());
    return true;
}
void RunClient(Config config, HANDLE stopEvent) { Connect(config, stopEvent); }
void Publish(View view)
{
    auto& shared = SharedView();
    std::lock_guard lock(shared.mutex);
    shared.view = std::move(view);
}
void SetLocalFault(std::string_view source, std::string_view title, std::string_view detail) noexcept
{
    try
    {
        if (source.empty() || title.empty()) return;
        View::LocalFault fault{std::string(source.substr(0, 64)), std::string(title.substr(0, 128)),
            std::string(detail.substr(0, 300))};
        auto& shared = SharedView();
        std::lock_guard lock(shared.mutex);
        const auto existing = std::find_if(shared.localFaults.begin(), shared.localFaults.end(),
            [&](const auto& candidate) { return candidate.source == fault.source; });
        if (existing != shared.localFaults.end()) *existing = std::move(fault);
        else if (shared.localFaults.size() < 8) shared.localFaults.push_back(std::move(fault));
    }
    catch (...) { } // Diagnostics must never terminate the game process.
}
void ClearLocalFault(std::string_view source) noexcept
{
    try
    {
        auto& shared = SharedView();
        std::lock_guard lock(shared.mutex);
        std::erase_if(shared.localFaults, [&](const auto& fault) { return fault.source == source; });
    }
    catch (...) { }
}
Config LoadConfig(const std::filesystem::path& ini)
{
    Config config;
    const auto integer = [&](const wchar_t* key, int fallback) { return static_cast<int>(GetPrivateProfileIntW(L"Overlay", key, fallback, ini.c_str())); };
    config.enabled = integer(L"Enabled", config.enabled ? 1 : 0) != 0;
    const auto lightInteger = [&](const wchar_t* key, int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(L"LightOverlay", key, fallback, ini.c_str()));
    };
    config.lightOverlay = lightInteger(L"Enabled", 0) != 0;
    config.lightOverlayVisible = lightInteger(L"InitiallyVisible", 1) != 0;
    config.lightToggleKey = std::clamp(lightInteger(L"ToggleKey", 121), 0, 255);
    config.lightMaxMarkers = std::clamp(lightInteger(L"MaxMarkers", 512), 1, 2048);
    config.lightMaxLabels = std::clamp(lightInteger(L"MaxLabels", 6), 0, 16);
    config.lightRadius = IniFloat(ini, L"Radius", 35.0f, 1.0f, 500.0f, L"LightOverlay");
#if CDT_RESEARCH
    config.occlusionTest = lightInteger(L"OcclusionTest", 0) != 0;
#endif
    config.hideOccluded = lightInteger(L"HideOccluded", 0) != 0;
    config.occlusionToggleKey = std::clamp(lightInteger(L"OcclusionToggleKey", 122), 0, 255);
    config.radar3D = integer(L"Radar3D", 1) != 0;
    config.showAmbient = integer(L"ShowAmbient", 1) != 0;
    config.notifications = GetPrivateProfileIntW(L"Notifications", L"Enabled", 0, ini.c_str()) != 0;
    config.huePanel = GetPrivateProfileIntW(L"CrimsonHue", L"Enabled", 1, ini.c_str()) != 0;
    config.lightsExpected = GetPrivateProfileIntW(L"Lights", L"Enabled", 0, ini.c_str()) != 0;
    config.renderedExpected = config.lightsExpected && GetPrivateProfileIntW(L"Lights", L"ManyLights", 1, ini.c_str()) != 0;
    config.upstreamExpected = config.renderedExpected && GetPrivateProfileIntW(L"Lights", L"Upstream", 1, ini.c_str()) != 0;
    config.notificationDurationMs = static_cast<int>(std::clamp(
        GetPrivateProfileIntW(L"Notifications", L"DurationMilliseconds", 6000, ini.c_str()), 5000u, 10000u));
    if (!config.enabled && !config.notifications && !config.lightOverlay && !config.huePanel) return config;
    config.visible = integer(L"InitiallyVisible", 1) != 0;
    config.details = integer(L"ShowDetails", 0) != 0;
    config.autoScale = integer(L"AutoScale", 1) != 0;
    config.toggleKey = std::clamp(integer(L"ToggleKey", 119), 0, 255);
    config.detailsKey = std::clamp(integer(L"DetailsKey", 120), 0, 255);
    config.corner = std::clamp(integer(L"Corner", 0), 0, 3);
    config.staleMs = std::clamp(integer(L"StaleMilliseconds", 1000), 100, 10000);
    config.scale = IniFloat(ini, L"Scale", 1.0f, 0.5f, 3.0f);
    config.opacity = IniFloat(ini, L"Opacity", 0.92f, 0.2f, 1.0f);
    config.hdrPaperWhiteNits = IniFloat(ini, L"HdrPaperWhiteNits", 200.0f, 80.0f, 500.0f);
    const int requestedPort = static_cast<int>(GetPrivateProfileIntW(L"Server", L"Port", 27311, ini.c_str()));
    config.port = static_cast<unsigned short>(std::clamp(requestedPort, 1024, 65535));
    return config;
}
void Start(const HMODULE module, HANDLE stopEvent, const std::filesystem::path& directory) noexcept
{
    try
    {
        std::array<wchar_t, 32768> executable{};
        if (!GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())) ||
            _wcsicmp(std::filesystem::path(executable.data()).filename().c_str(), L"CrimsonDesert.exe") != 0) return;
        const auto config = LoadConfig(directory / L"CrimsonDesertTelemetry.ini");
        // Return before pinning the module, creating HUD workers, hooks or a client.
        if (!config.enabled && !config.notifications && !config.lightOverlay && !config.huePanel) return;
        const auto name = std::format(L"Local\\CrimsonDesertTelemetry.Overlay.{}", GetCurrentProcessId());
        HANDLE singleton = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!singleton) return;
        if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(singleton); return; }
        // A loaded graphics callback cannot be safely hot-unloaded by an ASI manager.
        HMODULE pinned{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(module), &pinned)) { CloseHandle(singleton); return; }
        auto* args = new WorkerArgs{config, stopEvent, directory};
        HANDLE thread = CreateThread(nullptr, 0, Worker, args, 0, nullptr);
        if (thread) CloseHandle(thread);
        else delete args;
        // Keep the named mutex handle for process lifetime, including initialization failure.
    }
    catch (...) { }
}
}

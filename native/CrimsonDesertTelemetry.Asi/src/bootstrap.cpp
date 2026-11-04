#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include "overlay.h"
#include "instruments.h"
#include "native_contract.generated.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
HMODULE g_module = nullptr;
HANDLE g_stopEvent = nullptr;

std::filesystem::path ModuleDirectory()
{
    std::vector<wchar_t> buffer(512);
    for (;;)
    {
        const DWORD length = GetModuleFileNameW(g_module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size() - 1)
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        buffer.resize(buffer.size() * 2);
    }
}

std::string Utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), count, nullptr, nullptr);
    return result;
}

void Log(const std::wstring& message)
{
    const auto directory = ModuleDirectory();
    if (directory.empty()) return;
    SYSTEMTIME time{};
    GetLocalTime(&time);
    const auto prefix = std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    const auto line = Utf8(prefix + message + L"\r\n");
    // Keep the detailed bootstrap log and a conventional <ASI stem>.log that
    // mod managers can discover without knowing our component-specific names.
    for (const auto* name : {L"CrimsonDesertTelemetry.bootstrap.log", L"CrimsonDesertTelemetry.log"})
    {
        const auto path = directory / name;
        HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        DWORD written = 0;
        WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        CloseHandle(file);
    }
}

std::filesystem::path FindDotnet()
{
    std::array<wchar_t, 32768> programFiles{};
    const DWORD length = GetEnvironmentVariableW(L"ProgramFiles", programFiles.data(),
        static_cast<DWORD>(programFiles.size()));
    if (length > 0 && length < programFiles.size())
    {
        const auto candidate = std::filesystem::path(programFiles.data()) / L"dotnet" / L"dotnet.exe";
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }

    std::array<wchar_t, 32768> resolved{};
    const DWORD found = SearchPathW(nullptr, L"dotnet.exe", nullptr,
        static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
    if (found > 0 && found < resolved.size()) return std::filesystem::path(resolved.data());
    return {};
}

bool IsServerReady(const unsigned short port)
{
    SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle == INVALID_SOCKET) return false;

    const DWORD timeoutMilliseconds = 500;
    setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeoutMilliseconds), sizeof(timeoutMilliseconds));
    setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO,
        reinterpret_cast<const char*>(&timeoutMilliseconds), sizeof(timeoutMilliseconds));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        closesocket(socketHandle);
        return false;
    }

    constexpr char request[] =
        "GET /v1/health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    if (send(socketHandle, request, static_cast<int>(sizeof(request) - 1), 0) == SOCKET_ERROR)
    {
        closesocket(socketHandle);
        return false;
    }

    std::array<char, 256> response{};
    const int received = recv(socketHandle, response.data(), static_cast<int>(response.size() - 1), 0);
    closesocket(socketHandle);
    if (received <= 0) return false;
    const std::string_view firstBytes(response.data(), static_cast<size_t>(received));
    return firstBytes.starts_with("HTTP/1.1 200") || firstBytes.starts_with("HTTP/1.0 200");
}

class BootstrapMutex
{
public:
    explicit BootstrapMutex(const unsigned short port)
    {
        const auto name = L"Local\\CrimsonDesertTelemetry.Bootstrap." + std::to_wstring(port);
        handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (handle_ == nullptr)
            throw std::runtime_error("Could not create the bootstrap mutex.");

        const DWORD result = WaitForSingleObject(handle_, 0);
        if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED)
            owns_ = true;
        else if (result == WAIT_TIMEOUT)
            return;
        else
        {
            CloseHandle(handle_);
            handle_ = nullptr;
            throw std::runtime_error("Could not acquire the bootstrap mutex.");
        }
    }

    BootstrapMutex(const BootstrapMutex&) = delete;
    BootstrapMutex& operator=(const BootstrapMutex&) = delete;

    ~BootstrapMutex()
    {
        if (owns_) ReleaseMutex(handle_);
        if (handle_ != nullptr) CloseHandle(handle_);
    }

    bool Owns() const noexcept { return owns_; }

private:
    HANDLE handle_ = nullptr;
    bool owns_ = false;
};

std::wstring Quote(const std::filesystem::path& value)
{
    return L"\"" + value.wstring() + L"\"";
}

bool ConfigureKillOnCloseJob(HANDLE job)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION information{};
    information.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    return SetInformationJobObject(job, JobObjectExtendedLimitInformation,
        &information, sizeof(information)) != FALSE;
}

std::filesystem::path CacheRuntimeConfig(const std::filesystem::path& source)
{
    // hostfxr normalizes --runtimeconfig to a .json filename. DMM scans those
    // filenames as mods, so materialize only this small metadata file in the
    // user's application cache, never in the game or manager library.
    HANDLE file = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot read runtime configuration.");
    const DWORD size = GetFileSize(file, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 65536)
    {
        CloseHandle(file);
        throw std::runtime_error("Invalid runtime configuration size.");
    }
    std::vector<unsigned char> bytes(size);
    DWORD read = 0;
    const bool readOk = ReadFile(file, bytes.data(), size, &read, nullptr) != FALSE && read == size;
    CloseHandle(file);
    if (!readOk) throw std::runtime_error("Cannot read complete runtime configuration.");

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot initialize runtime configuration hash.");
    std::array<unsigned char, 32> digest{};
    const auto hashStatus = BCryptHash(algorithm, nullptr, 0, bytes.data(), size,
        digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (hashStatus < 0) throw std::runtime_error("Cannot hash runtime configuration.");
    std::wstring key;
    for (const auto value : digest) key += std::format(L"{:02x}", value);

    std::array<wchar_t, 32768> localAppData{};
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData.data(),
        static_cast<DWORD>(localAppData.size()));
    if (length == 0 || length >= localAppData.size())
        throw std::runtime_error("LOCALAPPDATA is unavailable.");
    const auto cacheDirectory = std::filesystem::path(localAppData.data()) /
        L"CrimsonDesertTelemetry" / L"Runtime";
    std::filesystem::create_directories(cacheDirectory);
    const auto cached = cacheDirectory / (L"runtime-" + key + L".json");
    file = CreateFileW(cached.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() != ERROR_FILE_EXISTS)
            throw std::runtime_error("Cannot create cached runtime configuration.");
        // Do not trust a stale or altered cache file simply because its name matches.
        file = CreateFileW(cached.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot verify cached runtime configuration.");
        std::vector<unsigned char> existing(size);
        DWORD existingRead = 0;
        const bool matches = GetFileSize(file, nullptr) == size &&
            ReadFile(file, existing.data(), size, &existingRead, nullptr) &&
            existingRead == size && existing == bytes;
        CloseHandle(file);
        if (!matches) throw std::runtime_error("Cached runtime configuration differs from the package.");
    }
    else
    {
        DWORD written = 0;
        const bool writtenOk = WriteFile(file, bytes.data(), size, &written, nullptr) && written == size;
        CloseHandle(file);
        if (!writtenOk)
        {
            DeleteFileW(cached.c_str());
            throw std::runtime_error("Cannot write complete cached runtime configuration.");
        }
    }
    return cached;
}

DWORD RunBootstrap()
{
    const auto directory = ModuleDirectory();
    if (directory.empty()) return 1;
    const auto iniPath = directory / L"CrimsonDesertTelemetry.ini";
    const int enabled = GetPrivateProfileIntW(L"Server", L"Enabled", 1, iniPath.c_str());
    const int requestedPort = GetPrivateProfileIntW(L"Server", L"Port", 27311, iniPath.c_str());
    const int requestedRate = GetPrivateProfileIntW(L"Server", L"SampleRateHz", 60, iniPath.c_str());
    const unsigned short port = static_cast<unsigned short>(std::clamp(requestedPort, 1024, 65535));
    const int sampleRate = std::clamp(requestedRate, 1, 240);
    const bool lightsEnabled = GetPrivateProfileIntW(L"Lights", L"Enabled", 0, iniPath.c_str()) != 0;
#if CDT_RESEARCH
    const bool physicsVisibility = lightsEnabled &&
        GetPrivateProfileIntW(L"Experimental", L"PhysicsVisibility", 0, iniPath.c_str()) != 0;
    const bool researchSourceVisibility = lightsEnabled && !physicsVisibility &&
        GetPrivateProfileIntW(L"SourceVisibility", L"Enabled", 0, iniPath.c_str()) != 0;
#else
    // 2.2 production: [SourceVisibility] is the continuous physics ray fan.
    const bool physicsVisibility = lightsEnabled &&
        GetPrivateProfileIntW(L"Lights", L"ManyLights", 1, iniPath.c_str()) != 0 &&
        GetPrivateProfileIntW(L"SourceVisibility", L"Enabled", 0, iniPath.c_str()) != 0;
#endif
    const int nearbyRadius = std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"Lights", L"NearbyRadius", 100, iniPath.c_str())), 1, 100000);
    const int smoothingMs = std::clamp(static_cast<int>(GetPrivateProfileIntW(
        L"LightSmoothing", L"TimeConstantMilliseconds", 200, iniPath.c_str())), 0, 2000);
    std::array<wchar_t, 64> groupRadiusText{};
    GetPrivateProfileStringW(L"LightSmoothing", L"GroupRadius", L"0.15", groupRadiusText.data(),
        static_cast<DWORD>(groupRadiusText.size()), iniPath.c_str());
    wchar_t* groupEnd{};
    double groupRadius = std::wcstod(groupRadiusText.data(), &groupEnd);
    if (groupEnd == groupRadiusText.data() || *groupEnd || !std::isfinite(groupRadius) || groupRadius < .01 || groupRadius > 1)
    { groupRadius = .15; Log(L"Invalid LightSmoothing/GroupRadius; using 0.15 game units (decimal point required)."); }

    if (enabled == 0)
    {
        Log(L"Bootstrap disabled by CrimsonDesertTelemetry.ini.");
        return 0;
    }

    // Two ASI copies can be loaded by a mod manager or a stale installation.
    // Serialize the port check and host launch, then keep ownership for the
    // host lifetime so a second bootstrap cannot launch a competing server.
    BootstrapMutex bootstrapMutex(port);
    if (!bootstrapMutex.Owns())
    {
        Log(std::format(L"Another bootstrap instance owns port {}; skipping duplicate host launch.", port));
        return 0;
    }

    WSADATA sockets{};
    if (WSAStartup(MAKEWORD(2, 2), &sockets) != 0)
    {
        Log(L"Could not initialize Winsock.");
        cdt::overlay::SetLocalFault("bootstrap", "Telemetry networking could not start",
            "Restart Crimson Desert. Check CrimsonDesertTelemetry.bootstrap.log if this repeats.");
        return 2;
    }
    if (IsServerReady(port))
    {
        cdt::overlay::ClearLocalFault("bootstrap");
        Log(std::format(L"Telemetry server already available on 127.0.0.1:{}.", port));
        WSACleanup();
        return 0;
    }

    const auto dotnet = FindDotnet();
    if (dotnet.empty())
    {
        Log(L".NET 8 was not found. Install the .NET 8 ASP.NET Core Runtime (x64).");
        cdt::overlay::SetLocalFault("bootstrap", "Telemetry needs the .NET 8 runtime",
            "Install the .NET 8 ASP.NET Core Runtime (x64), then restart Crimson Desert.");
        WSACleanup();
        return 3;
    }
    const auto host = directory / L"crimson-desert-telemetry.dll";
    const auto dependencies = directory / L"crimson-desert-telemetry.deps.cfg";
    const auto runtimeConfig = directory / L"crimson-desert-telemetry.runtimeconfig.cfg";
    if (!std::filesystem::is_regular_file(host) ||
        !std::filesystem::is_regular_file(dependencies) ||
        !std::filesystem::is_regular_file(runtimeConfig))
    {
        Log(L"Missing host DLL or .deps.cfg/.runtimeconfig.cfg companion file; reinstall the complete package.");
        cdt::overlay::SetLocalFault("bootstrap", "Telemetry host files are missing",
            "Reinstall the complete CrimsonDesertTelemetry package, including DLL and CFG companion files.");
        WSACleanup();
        return 4;
    }
    std::filesystem::path cachedRuntimeConfig;
    try { cachedRuntimeConfig = CacheRuntimeConfig(runtimeConfig); }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        Log(L"Runtime configuration preparation failed: " + std::wstring(message.begin(), message.end()));
        cdt::overlay::SetLocalFault("bootstrap", "Telemetry runtime configuration failed",
            "Reinstall matching package files; check CrimsonDesertTelemetry.bootstrap.log for the configuration error.");
        WSACleanup();
        return 6;
    }

    const auto hostLogPath = directory / L"CrimsonDesertTelemetry.host.log";
    SECURITY_ATTRIBUTES inheritableHandles{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE hostLog = CreateFileW(hostLogPath.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &inheritableHandles, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hostLog == INVALID_HANDLE_VALUE) hostLog = nullptr;
    HANDLE hostInput = nullptr;
    if (hostLog != nullptr)
    {
        hostInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &inheritableHandles, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hostInput == INVALID_HANDLE_VALUE)
        {
            Log(L"Could not open an inheritable null input for telemetry host; starting without redirected output.");
            CloseHandle(hostLog);
            hostLog = nullptr;
            hostInput = nullptr;
        }
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (hostLog != nullptr)
    {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = hostLog;
        startup.hStdError = hostLog;
        startup.hStdInput = hostInput;
    }

    // Mod managers interpret loose .json files as game patches. Keep the .NET
    // dependency metadata as .cfg; hostfxr's runtime JSON lives outside the library.
    std::wstring commandLine = Quote(dotnet) + L" exec --depsfile " + Quote(dependencies) +
        L" --runtimeconfig " + Quote(cachedRuntimeConfig) + L" " + Quote(host) + L" serve " +
        std::to_wstring(port) + L" " + std::to_wstring(sampleRate);
    if (lightsEnabled)
        commandLine += L" --lights --light-radius " + std::to_wstring(nearbyRadius);
#if !CDT_RESEARCH
    if constexpr (cdt::native_contract::support::Available)
        if (lightsEnabled && GetPrivateProfileIntW(L"Lights", L"ManyLights", 1, iniPath.c_str()) != 0)
            commandLine += L" --native-recovery";
#endif
#if CDT_RESEARCH
    if (researchSourceVisibility) commandLine += L" --research-source-visibility";
#endif
    if (physicsVisibility)
    {
        // Reuse the exact HUD parser (including decimal/clamp/fallback rules),
        // even with both light views hidden. One INI setting owns both ranges.
        const float radius = cdt::overlay::LoadConfig(iniPath).lightRadius;
        commandLine += L" --physics-visibility --physics-visibility-radius " + std::format(L"{}", radius);
        Log(std::format(L"Physics visibility and HUD radius: {} game units around player; rays from camera.", radius));
    }
    commandLine += L" --light-smoothing-ms " + std::to_wstring(smoothingMs) +
        L" --light-group-radius " + std::format(L"{}", groupRadius);
    if (cdt::native_contract::ResearchContract)
    {
        commandLine += L" --private-exact-build";
        Log(L"PRIVATE UNVALIDATED exact-build integration test; not a supported release.");
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(dotnet.c_str(), mutableCommand.data(), nullptr, nullptr,
        hostLog != nullptr, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr,
        directory.c_str(), &startup, &process);
    const auto startError = started ? ERROR_SUCCESS : GetLastError();
    if (hostInput != nullptr) CloseHandle(hostInput);
    if (hostLog != nullptr) CloseHandle(hostLog);
    if (!started)
    {
        Log(std::format(L"Could not start telemetry host (Win32 error {}).", startError));
        cdt::overlay::SetLocalFault("bootstrap", "Telemetry host could not start",
            std::format("Win32 error {}. Check CrimsonDesertTelemetry.bootstrap.log and the .NET 8 ASP.NET Core Runtime (x64).", startError));
        WSACleanup();
        return 5;
    }
    CloseHandle(process.hThread);

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr && (!ConfigureKillOnCloseJob(job) || !AssignProcessToJobObject(job, process.hProcess)))
    {
        Log(std::format(L"Warning: could not attach host to lifetime job (Win32 error {}).", GetLastError()));
        CloseHandle(job);
        job = nullptr;
    }
    Log(std::format(L"Started telemetry host PID {} on 127.0.0.1:{} at {} Hz; engine lights {}.",
        process.dwProcessId, port, sampleRate, lightsEnabled ? L"enabled" : L"disabled"));

    const std::array<HANDLE, 2> waits{g_stopEvent, process.hProcess};
    const DWORD waitResult = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, INFINITE);
    if (waitResult == WAIT_OBJECT_0)
        Log(L"Game process is closing; stopping telemetry host.");
    else if (waitResult == WAIT_OBJECT_0 + 1)
    {
        DWORD exitCode = 0;
        GetExitCodeProcess(process.hProcess, &exitCode);
        Log(std::format(L"Telemetry host exited with code {}.", exitCode));
        cdt::overlay::SetLocalFault("bootstrap", "Telemetry host stopped",
            std::format("Exit code {}. Check CrimsonDesertTelemetry.host.log, then restart Crimson Desert.", exitCode));
    }

    if (job != nullptr) CloseHandle(job);
    CloseHandle(process.hProcess);
    WSACleanup();
    return 0;
}

DWORD WINAPI BootstrapThread(void*)
{
    try
    {
        cdt::overlay::Start(g_module, g_stopEvent, ModuleDirectory());
        return RunBootstrap();
    }
    catch (...)
    {
        // Configuration/filesystem errors in the optional bootstrap must not
        // escape the thread entry point and terminate the game process.
        try { Log(L"Bootstrap stopped after an unexpected configuration or filesystem error."); }
        catch (...) { }
        cdt::overlay::SetLocalFault("bootstrap", "Telemetry bootstrap failed",
            "Check CrimsonDesertTelemetry.bootstrap.log and reinstall matching package files, then restart.");
        return 7;
    }
}

DWORD WINAPI InstrumentsThread(void*)
{
    try { cdt::instruments::Run(g_stopEvent); }
    catch (...)
    {
        try { Log(L"Native instruments stopped after an unexpected error."); } catch (...) {}
        cdt::overlay::SetLocalFault("native", "Native telemetry stopped",
            "Restart Crimson Desert; check CrimsonDesertTelemetry.native.log if this repeats.");
    }
    return 0;
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (g_stopEvent == nullptr) return FALSE;
        // Sole process attach. Preserve early console/debug-owner capture before
        // asynchronous bootstrap; the imported proxy DllMain is not compiled.
        cdt::instruments::EarlyAttach(instance);
        HANDLE instruments = CreateThread(nullptr, 0, InstrumentsThread, nullptr, 0, nullptr);
        if (instruments) CloseHandle(instruments);
        HANDLE thread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
        if (thread == nullptr)
        {
            // Early hooks or the instrument worker may already exist. Returning
            // FALSE here unloads code underneath them; retain the ASI and let
            // its worker observe shutdown instead. The OS owns the event until
            // process exit, including any in-flight worker wait.
            SetEvent(g_stopEvent);
            return TRUE;
        }
        CloseHandle(thread);
    }
    else if (reason == DLL_PROCESS_DETACH && g_stopEvent != nullptr)
    {
        SetEvent(g_stopEvent);
    }
    return TRUE;
}

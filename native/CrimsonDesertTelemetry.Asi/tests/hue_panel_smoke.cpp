#include <windows.h>
#include "overlay.h"
#include "../crimsonhue-panel/overlay.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <nlohmann/json.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
using Json = nlohmann::json;
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void Check(HRESULT hr) { Require(SUCCEEDED(hr), "D3D12 smoke operation failed"); }
int wmain(int argc, wchar_t** argv) {
    if (argc != 3 && argc != 4) return 1;
    const bool setupPreview = argc == 4 && wcscmp(argv[3], L"--setup") == 0;
    const bool lightingPreview = argc == 4 && wcscmp(argv[3], L"--lighting") == 0;
    const bool advancedPreview = argc == 4 && wcscmp(argv[3], L"--advanced") == 0;
    const bool customPreview = argc == 4 && wcscmp(argv[3], L"--custom") == 0;
    const bool rawPreview = argc == 4 && wcscmp(argv[3], L"--raw") == 0;
    if (argc == 4 && !setupPreview && !lightingPreview && !advancedPreview && !customPreview && !rawPreview) return 1;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        const auto asi = std::filesystem::absolute(argv[1]);
        const auto png = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(png.parent_path());
        SetEnvironmentVariableW(L"CRIMSONHUE_ASI_SMOKE_PATH", png.c_str());
        SetEnvironmentVariableW(L"CRIMSONHUE_ASI_OVERLAY_SMOKE", L"1");
        Require(!crimson::overlay::PanelAvailable(), "Hue unexpectedly attached before module load");
        cdt::overlay::Config disabled;
        Require(!cdt::overlay::InstallGraphics(disabled), "All-off CDT installed hooks without a Hue module");
        const auto module = LoadLibraryExW(asi.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        Require(module != nullptr, "ASI load failed");
        using Status = int(__stdcall*)(); using Read = int(__stdcall*)(void*, int); using Send = int(__stdcall*)(const char*, int);
        
        const auto status = reinterpret_cast<Status>(GetProcAddress(module, "CrimsonHueStatus"));
        const auto stop = reinterpret_cast<Status>(GetProcAddress(module, "CrimsonHueStop"));
        const auto toggle = [] { crimson::overlay::Toggle(); };
        const auto read = reinterpret_cast<Read>(GetProcAddress(module, "CrimsonHueReadState"));
        const auto send = reinterpret_cast<Send>(GetProcAddress(module, "CrimsonHueSendCommand"));
        const auto frames = [] { return cdt::overlay::RenderedFrames(); };
        const auto graphics = [] { return cdt::overlay::GraphicsStatus(); };
        Require(status && stop && read && send, "Missing ASI exports");
        const auto start = GetTickCount64();
        while (status() == 0 && GetTickCount64() - start < 20000) Sleep(20);
        Require(status() == 1, "Managed engine not ready");

        auto State = [&] { std::vector<char> bytes(256 * 1024); const int size = read(bytes.data(), static_cast<int>(bytes.size()));
            Require(size > 0, "No UI snapshot"); return Json::parse(bytes.data(), bytes.data() + size); };
        auto Command = [&](Json command) { const auto text = command.dump(); Require(send(text.data(), static_cast<int>(text.size())) == 1, "Command not accepted"); };
        auto Value = [](const Json& snapshot, const char* key) {
            for (const auto& control : snapshot.at("controls")) if (control.at("key") == key) return control.at("value").get<double>();
            throw std::runtime_error("Missing effective mapping control");
        };
        auto state = State();
        Require(state.at("lighting").at("recommended") == true && state.at("lighting").at("hasCustom") == true &&
            state.at("lighting").at("response") == "Balanced", "New controller did not start with Recommended lighting");
        Require(state.at("assessment").at("code") == "demo" && !state.at("assessment").at("summary").get<std::string>().empty() &&
            state.at("assessment").at("warning").is_boolean(), "Missing user-facing lighting assessment");
        Require(state.value("bridgeConnected", false) && state.value("bridgePhase", std::string{}) == "connected" &&
            !state.at("areas").empty() && !state.at("areas")[0].at("name").get<std::string>().empty(),
            "Connected status or named Entertainment areas missing from controller snapshot");
        Require(state.at("shortcut").at("key") == 'H', "Default shortcut must use H instead of a function key");
        Command({{"action", "shortcut"}, {"key", 'H'}, {"control", false}, {"shift", false}, {"alt", true}});
        Sleep(150); state = State();
        Require(state.at("shortcut").at("key") == 'H' && state.at("shortcut").at("alt") == true, "Shortcut command did not reach controller");
        Command({{"action", "shortcut"}, {"key", VK_CONTROL}, {"control", false}, {"shift", false}, {"alt", false}});
        Sleep(100); Require(State().at("shortcut").at("key") == 'H', "Invalid shortcut overwrote saved binding");
        Command({{"action", "shortcut"}, {"key", 'H'}, {"control", true}, {"shift", true}, {"alt", false}});
        Require(state.value("smoke", false) && state.value("demo", false) && !state.value("windowVisible", true), "Engine showed a WPF window or enabled live mode");
        Require(state.at("controls").size() == 31 && state.at("switches").empty() && state.at("channels").size() == 6,
            "Missing mixer/layout controls or obsolete axis switches remain");
        Require(state.at("lighting").at("pointLightStrength") == 1 && state.at("lighting").at("spotLightStrength") == 1 &&
            state.at("lighting").at("playerLanternStrength") == .1 && state.at("lighting").at("lanternMatches") == 0,
            "Source weights or synthetic match count is invalid");
        Command({{"action", "lighting"}, {"key", "PointLightStrength"}, {"value", .4}});
        Command({{"action", "lighting"}, {"key", "SpotLightStrength"}, {"value", 10}});
        Command({{"action", "lighting"}, {"key", "PlayerLanternStrength"}, {"value", .05}});
        Sleep(150); state=State();
        Require(Value(state,"PointLightStrength")==40 && Value(state,"SpotLightStrength")==1000 && Value(state,"PlayerLanternStrength")==5,
            "Source weights did not reach the effective mapping");
        for(const auto& control: state.at("controls")) if(control.at("key")=="SpotLightStrength")
            Require(control.at("max")==1000,"Spotlight range not exposed by controller");
        Command({{"action", "lighting"}, {"key", "PointLightStrength"}, {"value", 1}});
        Command({{"action", "lighting"}, {"key", "SpotLightStrength"}, {"value", 1}});
        Command({{"action", "lighting"}, {"key", "PlayerLanternStrength"}, {"value", .1}});
        bool sphereDefault = false;
        for (const auto& value : state.at("controls")) if (value.at("key") == "SourceDiscRadiusDegrees")
            sphereDefault = value.at("value") == 30.0 && value.at("initial") == 30.0 && value.at("min") == 1.0 && value.at("max") == 180.0 && value.at("group") == "Space";
        Require(sphereDefault, "Spherical radius default or range missing from controller snapshot");
        Command({{"action", "lighting"}, {"key", "Brightness"}, {"value", .73}});
        Command({{"action", "lighting"}, {"key", "LocalAmount"}, {"value", 1.25}});
        Command({{"action", "lighting"}, {"key", "BackgroundAmount"}, {"value", .35}});
        Command({{"action", "lighting"}, {"key", "Response"}, {"value", 2}});
        Sleep(150); state = State();
        Require(state.at("lighting").at("brightness") == .73 && state.at("lighting").at("localAmount") == 1.25 &&
            state.at("lighting").at("backgroundAmount") == .35 && state.at("lighting").at("response") == "Calm",
            "Simple lighting commands did not update their preferences");
        Require(Value(state, "Brightness") == 73 && Value(state, "LocalStrength") == 125 && Value(state, "AmbientOutput") == 35 &&
            Value(state, "SmoothingMs") == 500 && Value(state, "AmbientSmoothingMs") == 1000,
            "Simple controls did not resolve to the effective mapping");
        const auto validLighting = state.at("lighting");
        Command({{"action", "lighting"}, {"key", "Brightness"}, {"value", 1.1}});
        Command({{"action", "lighting"}, {"key", "LocalAmount"}, {"value", 2.1}});
        Command({{"action", "lighting"}, {"key", "BackgroundAmount"}, {"value", 1.1}});
        Command({{"action", "lighting"}, {"key", "Response"}, {"value", .5}});
        Sleep(100); Require(State().at("lighting") == validLighting, "Invalid simple controls changed lighting preferences");
        Command({{"action", "set"}, {"key", "Brightness"}, {"value", 23.0}});
        Command({{"action", "set"}, {"key", "SourceDiscRadiusDegrees"}, {"value", 130.0}});
        Sleep(150); state = State();
        bool brightness = false, sphere = false;
        for (const auto& value : state.at("controls")) if (value.at("key") == "Brightness") brightness = value.at("value") == 23.0;
        for (const auto& value : state.at("controls")) if (value.at("key") == "SourceDiscRadiusDegrees") sphere = value.at("value") == 130.0;
        Require(brightness && sphere, "Overlay command did not reach live mapping");
        Require(state.at("lighting").at("recommended") == false, "Legacy detailed set command did not select Custom");
        const auto customControls = state.at("controls");
        Command({{"action", "profile"}, {"recommended", true}}); Sleep(100); state = State();
        Require(state.at("lighting").at("recommended") == true && Value(state, "Brightness") == 23 &&
            Value(state, "SourceDiscRadiusDegrees") == 30 && Value(state, "Spread") == 4 &&
            Value(state, "SourceDiscSoftness") == 20 && Value(state, "LocalStrength") == 125,
            "Recommended retained custom spatial tuning or changed the current brightness limit");
        Command({{"action", "profile"}, {"recommended", false}}); Sleep(100); state = State();
        Require(state.at("lighting").at("recommended") == false && state.at("controls") == customControls,
            "Returning to Custom discarded preserved detailed settings");
        Command({{"action", "lighting"}, {"key", "Brightness"}, {"value", .9}}); Sleep(100);
        Require(State().at("controls") == customControls, "Disabled simple controls changed Custom settings");
        Command({{"action", "set"}, {"key", "Brightness"}, {"value", 200.0}}); Sleep(100);
        state = State(); for (const auto& value : state.at("controls")) if (value.at("key") == "Brightness") Require(value.at("value") == 23.0, "Out-of-range command changed mapping");
        Command({{"action", "set"}, {"key", "SourceDiscRadiusDegrees"}, {"value", 181.0}}); Sleep(100);
        state = State(); for (const auto& value : state.at("controls")) if (value.at("key") == "SourceDiscRadiusDegrees")
            Require(value.at("value") == 130.0, "Out-of-range spherical radius changed mapping");
        Command({{"action", "reset"}, {"group", "Output"}}); Command({{"action", "reset"}, {"group", "Space"}}); Sleep(100);
        state = State(); for (const auto& value : state.at("controls")) if (value.at("key") == "SourceDiscRadiusDegrees")
            Require(value.at("value") == 30.0, "Space reset did not restore spherical radius default");
        Require(Value(state, "Spread") == 4 && Value(state, "SourceDiscSoftness") == 20,
            "Space reset did not restore the focused directional defaults");

        state = State(); Require(state.at("wallWashers").size() == 2, "Missing per-lamp wall calibration");
        for (const auto& washer : state.at("wallWashers")) {
            Require(washer.at("segmentCount") == 3 && washer.at("segments").size() == 3 &&
                washer.at("removedSegmentCount") == 0, "Imported Hue segment summary is incorrect");
            for (const auto& segment : washer.at("segments"))
                Require(segment.at("needsReview") == false, "Unchanged Hue segment unexpectedly needs review");
            const auto id = washer.at("serviceId");
            Command({{"action", "wallWash"}, {"serviceId", id}, {"key", "enabled"}, {"value", true}});
            Command({{"action", "wallWash"}, {"serviceId", id}, {"key", "yaw"}, {"value", id == "synthetic-left" ? -60 : 60}});
            Command({{"action", "wallWash"}, {"serviceId", id}, {"key", "span"}, {"value", 60}});
        }
        Command({{"action", "wallWash"}, {"serviceId", "synthetic-left"}, {"key", "elevation"}, {"index", 0}, {"value", -30}});
        Command({{"action", "wallPreview"}, {"key", "enabled"}, {"value", true}});
        Command({{"action", "wallPreview"}, {"key", "yaw"}, {"value", -75}});
        Sleep(200); state = State();
        Require(state.value("wallPreview", false) && !state.value("canIdentify", true), "Synthetic preview enabled physical testing");
        Require(state.at("wallWashers")[0].at("yaw") == -60 && state.at("wallWashers")[0].at("segments")[0].at("elevation") == -30,
            "Lamp or segment calibration command did not apply");
        const auto calibratedLeft = state.at("wallWashers")[0];
        Command({{"action", "wallWash"}, {"serviceId", "synthetic-left"}, {"key", "enabled"}, {"value", false}});
        Sleep(100); state = State();
        const auto pointLeft = state.at("wallWashers")[0];
        Require(pointLeft.at("enabled") == false && pointLeft.at("yaw") == calibratedLeft.at("yaw") &&
            pointLeft.at("horizontalSpan") == calibratedLeft.at("horizontalSpan") &&
            pointLeft.at("segments") == calibratedLeft.at("segments"), "Point mode discarded saved wall placement");
        Command({{"action", "wallWash"}, {"serviceId", "synthetic-left"}, {"key", "enabled"}, {"value", true}});
        Sleep(100); state = State();
        const auto restoredLeft = state.at("wallWashers")[0];
        Require(restoredLeft.at("enabled") == true && restoredLeft.at("yaw") == calibratedLeft.at("yaw") &&
            restoredLeft.at("horizontalSpan") == calibratedLeft.at("horizontalSpan") &&
            restoredLeft.at("segments") == calibratedLeft.at("segments"), "Wall mode did not restore saved placement");
        bool leftLit = false;
        for (const auto& channel : state.at("channels")) {
            const auto rgb = channel.at("rgb");
            if (channel.at("id").get<int>() < 3) leftLit |= rgb.at("x").get<double>() > 0;
            else Require(rgb.at("x") == 0 && rgb.at("y") == 0 && rgb.at("z") == 0, "Calibrated preview reached beyond its spherical radius");
        }
        Require(leftLit, "Outward wall preview received no light");
        Command({{"action", "wallWash"}, {"serviceId", "synthetic-left"}, {"key", "yaw"}, {"value", 400}});
        Command({{"action", "identify"}, {"channel", 0}});
        Sleep(100); state = State();
        Require(state.at("wallWashers")[0].at("yaw") == -60 && !state.value("canStop", true), "Invalid calibration or identify escaped smoke isolation");
        Command({{"action", "wallWash"}, {"serviceId", "synthetic-right"}, {"key", "reset"}});
        Sleep(100); Require(!State().at("wallWashers")[1].at("enabled").get<bool>(), "Reset did not restore Hue point mapping");
        Command({{"action", "wallWash"}, {"serviceId", "synthetic-right"}, {"key", "enabled"}, {"value", true}});
        Command({{"action", "wallWash"}, {"serviceId", "synthetic-right"}, {"key", "yaw"}, {"value", 60}});
        Command({{"action", "wallPreview"}, {"key", "yaw"}, {"value", 75}});
        Sleep(100); state = State();
        for (const auto& channel : state.at("channels")) if (channel.at("id").get<int>() < 3)
            Require(channel.at("rgb").at("x") == 0, "Right preview source leaked to left washer");
        Command({{"action", "wallPreview"}, {"key", "yaw"}, {"value", -75}});
        std::cout << "PASS Recommended controls/ranges, exact Custom restoration, assessment, segment summary, Point/Wall placement, spherical preview and physical-output isolation.\n";

        const auto protocol = reinterpret_cast<Status>(GetProcAddress(module, "CrimsonHueUiProtocol"));
        Require(protocol && protocol() == 1, "Incompatible Hue UI protocol");
        crimson::overlay::RefreshBridge();
        Require(crimson::overlay::PanelAvailable(), "CDT did not discover the headless controller");
        crimson::overlay::Command({{"action", "set"}, {"key", "Brightness"}, {"value", 37.0}});
        crimson::overlay::RefreshBridge(); Sleep(150); crimson::overlay::RefreshBridge();
        bool queued = false;
        state = State();
        for (const auto& value : state.at("controls")) if (value.at("key") == "Brightness") queued = value.at("value") == 37.0;
        Require(queued, "Shared panel queue did not reach the controller");
        for (int i = 0; i < 64; ++i)
            crimson::overlay::Command({{"action", "set"}, {"key", "Brightness"}, {"value", i}});
        crimson::overlay::Command({{"action", "set"}, {"key", "Brightness"}, {"value", 99}}); // full queue drops ordinary work
        crimson::overlay::Command({{"action", "stop"}}); // cleanup is retained even at capacity
        crimson::overlay::RefreshBridge(); Sleep(150); state = State();
        for (const auto& value : state.at("controls")) if (value.at("key") == "Brightness")
            Require(value.at("value") == 62.0, "Bounded queue or cleanup priority failed");
        if (!customPreview) {
            Command({{"action", "profile"}, {"recommended", true}}); Sleep(100); state = State();
            Require(state.at("lighting").at("recommended") == true, "Recommended did not resume before product UI preview");
        }
        if (lightingPreview || customPreview) crimson::overlay::SelectTabForTest("Lighting");
        else if (advancedPreview) crimson::overlay::SelectTabForTest("Advanced");
        else if (rawPreview) crimson::overlay::SelectTabForTest("CDT raw input");
        else if (!setupPreview) crimson::overlay::SelectTabForTest("Room");
        cdt::overlay::Config config; config.enabled = true; config.visible = true;
        if (setupPreview) {
            Command({{"action", "setup"}, {"bridge", "192.168.2.139"}, {"telemetry", "ws://127.0.0.1:27311/v1/stream"}});
            Sleep(150); state = State();
            Require(!state.value("bridgeConnected", true) && state.at("areas").empty(), "Unconnected setup advertised selectable areas");
            crimson::overlay::SelectTabForTest("Setup");
        }
        cdt::overlay::View telemetry; telemetry.connected = true; telemetry.hasSample = true;
        telemetry.received = cdt::overlay::Clock::now(); telemetry.sample.state = "playing";
        telemetry.sample.build = "synthetic"; telemetry.sample.sequence = 1;
        cdt::overlay::Publish(telemetry);
        Require(cdt::overlay::InstallGraphics(config), "CDT graphics hook install failed");
        // The hidden window never owns foreground input. Force its software
        // cursor anyway so cursor-atlas lifetime failures cannot escape this test.
        cdt::overlay::SetSoftwareCursorForTest(true);
        WNDCLASSW windowClass{}; windowClass.lpfnWndProc = DefWindowProcW; windowClass.hInstance = GetModuleHandleW(nullptr); windowClass.lpszClassName = L"CrimsonHue.OverlaySmoke";
        Require(RegisterClassW(&windowClass) != 0, "Window class failed");
        const auto window = CreateWindowW(windowClass.lpszClassName, L"Synthetic D3D12 overlay test", WS_OVERLAPPEDWINDOW,
            0, 0, 1440, 960, nullptr, nullptr, windowClass.hInstance, nullptr);
        Require(window != nullptr, "Synthetic window failed");
        ComPtr<IDXGIFactory4> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> adapter; Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        ComPtr<ID3D12Device> device; Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        ComPtr<ID3D12CommandQueue> queue; D3D12_COMMAND_QUEUE_DESC queueDesc{}; Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
        DXGI_SWAP_CHAIN_DESC1 description{}; description.Width = 1440; description.Height = 960;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM; description.BufferCount = 2;
        description.SampleDesc.Count = 1; description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> chain1; Check(factory->CreateSwapChainForHwnd(queue.Get(), window, &description, nullptr, nullptr, &chain1));
        ComPtr<IDXGISwapChain3> chain; Check(chain1.As(&chain));
        ComPtr<ID3D12Fence> fence; Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        UINT64 sequence = 0;
        auto Wait = [&] { Check(queue->Signal(fence.Get(), ++sequence)); HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            Require(event != nullptr, "Fence event failed"); Check(fence->SetEventOnCompletion(sequence, event));
            const auto result = WaitForSingleObject(event, 5000); CloseHandle(event); Require(result == WAIT_OBJECT_0, "GPU wait timed out"); };
        auto Pump = [&] { cdt::overlay::MaintainGraphics(); MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } };
        const auto graphicsStart = GetTickCount64();
        while (frames() < 5 && GetTickCount64() - graphicsStart < 15000) { Pump(); Check(chain->Present(0, 0)); Sleep(20); }
        std::cout << graphics() << '\n'; Require(frames() >= 5, "Overlay did not draw actual D3D12 frames"); Wait();

        // Copy the actual overlay-rendered backbuffer to CPU memory and encode
        // it with Windows WIC. This never captures the real game or desktop.
        ComPtr<ID3D12Resource> buffer; Check(chain->GetBuffer(chain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer)));
        // Present rotates the buffers; draw one more frame into this known one.
        Check(chain->Present(0, 0)); Wait();
        const auto resourceDesc = buffer->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 bytes{};
        device->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
        D3D12_RESOURCE_DESC stagingDesc{}; stagingDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        stagingDesc.Width = bytes; stagingDesc.Height = 1; stagingDesc.DepthOrArraySize = 1; stagingDesc.MipLevels = 1;
        stagingDesc.SampleDesc.Count = 1; stagingDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
        ComPtr<ID3D12Resource> staging; Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &stagingDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&staging)));
        ComPtr<ID3D12CommandAllocator> allocator; Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> list; Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffer.Get(); barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = buffer.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource = staging.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint = footprint;
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter); list->ResourceBarrier(1, &barrier); Check(list->Close());
        ID3D12CommandList* lists[]{list.Get()}; queue->ExecuteCommandLists(1, lists); Wait();
        void* mapped{}; Check(staging->Map(0, nullptr, &mapped));
        size_t lit = 0; const auto* pixels = static_cast<const unsigned char*>(mapped);
        for (UINT y = 0; y < description.Height; ++y) for (UINT x = 0; x < description.Width; ++x) {
            const auto* pixel = pixels + y * footprint.Footprint.RowPitch + x * 4;
            if (pixel[0] > 10 || pixel[1] > 10 || pixel[2] > 10) ++lit;
        }
        Require(lit > 10000, "Backbuffer contains no visible UI pixels");
        Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        ComPtr<IWICImagingFactory> imaging; Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&imaging)));
        ComPtr<IWICStream> stream; Check(imaging->CreateStream(&stream)); Check(stream->InitializeFromFilename(png.c_str(), GENERIC_WRITE));
        ComPtr<IWICBitmapEncoder> encoder; Check(imaging->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)); Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> image; ComPtr<IPropertyBag2> options; Check(encoder->CreateNewFrame(&image, &options)); Check(image->Initialize(options.Get()));
        Check(image->SetSize(description.Width, description.Height)); WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA; Check(image->SetPixelFormat(&format));
        Require(format == GUID_WICPixelFormat32bppBGRA, "Unexpected PNG encoder pixel format");
        std::vector<BYTE> bgra(pixels, pixels + bytes);
        for (UINT y = 0; y < description.Height; ++y) for (UINT x = 0; x < description.Width; ++x) {
            const auto index = y * footprint.Footprint.RowPitch + x * 4; std::swap(bgra[index], bgra[index + 2]); bgra[index + 3] = 255;
        }
        Check(image->WritePixels(description.Height, footprint.Footprint.RowPitch, static_cast<UINT>(bytes), bgra.data()));
        Check(image->Commit()); Check(encoder->Commit()); staging->Unmap(0, nullptr);
        const auto count = frames(); toggle(); cdt::overlay::SetSoftwareCursorForTest(false);
        Pump(); Check(chain->Present(0, 0)); Wait();
        Require(frames() > count, "Hiding Hue also hid the independent CDT HUD");
        cdt::overlay::SetVisibleForTest(false); const auto hiddenCount = frames();
        Pump(); Check(chain->Present(0, 0)); Wait(); Require(frames() == hiddenCount, "Hidden views still submitted a draw");
        toggle(); cdt::overlay::SetSoftwareCursorForTest(true);
        Pump(); Check(chain->Present(0, 0)); Wait(); Require(frames() > hiddenCount, "Hue-only reopen did not restore overlay draws");
        // Release our references and check real ResizeBuffers / renderer rebuild.
        buffer.Reset(); Check(chain->ResizeBuffers(2, 1280, 800, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
        const auto beforeResize = frames(); const auto resizeStart = GetTickCount64();
        while (frames() == beforeResize && GetTickCount64() - resizeStart < 5000) { Pump(); Check(chain->Present(0, 0)); Sleep(20); }
        Require(frames() > beforeResize, "Overlay did not rebuild after resize");
        Require(!State().value("windowVisible", true), "WPF became visible");
        crimson::overlay::RequestPanelStop(); crimson::overlay::RefreshBridge();
        stop(); const auto stopStart = GetTickCount64(); while (status() == 1 && GetTickCount64() - stopStart < 5000) { Pump(); Sleep(20); }
        Require(status() == 2, "Engine shutdown failed");
        std::cout << "PASS CDT single renderer + actual headless Hue ASI: simple lighting/assessment and preserved Custom, bounded command queue, 31 advanced controls, wall preview, D3D12 pixel readback, independent HUD/hide, Hue-only reopen, resize and shutdown; " << lit << " visible pixels.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}

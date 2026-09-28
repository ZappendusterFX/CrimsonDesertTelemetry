#include "overlay.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace cdt::overlay;
namespace
{
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D12 smoke operation failed"); }
void Require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct Gpu
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    UINT64 sequence{};
    void Wait()
    {
        Check(queue->Signal(fence.Get(), ++sequence));
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Require(event != nullptr, "Fence event");
        Check(fence->SetEventOnCompletion(sequence, event));
        const DWORD result = WaitForSingleObject(event, 5000);
        CloseHandle(event);
        Require(result == WAIT_OBJECT_0, "Smoke GPU fence timeout");
    }
    void Begin() { Wait(); Check(allocator->Reset()); Check(commands->Reset(allocator.Get(), nullptr)); }
    void Submit()
    {
        Check(commands->Close());
        ID3D12CommandList* list[] = {commands.Get()};
        queue->ExecuteCommandLists(1, list);
    }
    void Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commands->ResourceBarrier(1, &barrier);
    }
};
View Fixture()
{
    View view;
    view.connected = view.hasSample = true;
    view.received = Clock::now();
    view.rateHz = 60;
    auto& s = view.sample;
    s.sequence = 1234; s.build = "TEST FIXTURE - NOT LIVE GAME DATA"; s.state = "playing";
    s.playerPosition = Vec3{-11157.0f, 761.3f, -5969.5f};
    s.playerForward = Vec3{0.866025f, 0, 0.5f}; s.playerHeading = 60.0f;
    s.playerUp = Vec3{0, 1, 0};
    s.cameraForward = Vec3{-0.482963f, 0.258819f, 0.836516f}; s.cameraHeading = 330.0f;
    s.pitch = 15.0f; s.fov = 50.0f; s.consensus = 48; s.validCopies = 100; s.distinctStates = 7; s.captureUs = 510;
    return view;
}
View LightFixture(float aspect)
{
    auto view = Fixture();
    auto& sample = view.sample;
    sample.schemaVersion = "1.4";
    sample.orientationSource = "scene-constants-test-fixture";
    const Vec3 camera{-10528, 611, -4354};
    sample.cameraPosition = camera;
    sample.cameraForward = Vec3{0, 0, 1}; sample.cameraRight = Vec3{1, 0, 0}; sample.cameraUp = Vec3{0, 1, 0};
    sample.cameraHeading = 0.0f; sample.pitch = 0.0f; sample.fov = 60.0f;
    sample.aspectRatio = aspect; sample.nearPlane = 0.1f;
    sample.playerPosition = Vec3{camera.x, camera.y - 1, camera.z + 0.5f};
    sample.playerForward = Vec3{0, 0, 1}; sample.playerHeading = 0.0f;
    const auto position = [&](float x, float y, float z) { return Vec3{camera.x + x, camera.y + y, camera.z + z}; };
    // Five visible contributions, including a close pair with independent RGB,
    // one behind the camera and one before its near plane. Slots are not IDs.
    const std::vector<LightRecord> records{
        {0, position(0, 0, 10), {0.85f, 0.30f, 0.08f}, 0.40f, "point", std::nullopt, std::nullopt,
            SourceVisibility{"clear", "", 1.0, 20.0}},
        {1, position(-3, 2, 12), {1.20f, 0.65f, 0.12f}, 0.74f, "point", std::nullopt, std::nullopt,
            SourceVisibility{"blocked", "", 0.0, 20.0}},
        {2, position(4, 1, 14), {0.30f, 0.60f, 1.80f}, 0.63f, "spot", Vec3{0, -1, 0}, 27.0f},
        {3, position(-4, -2, -8), {0.85f, 0.05f, 1.20f}, 0.30f, "point", std::nullopt, std::nullopt,
            SourceVisibility{"blocked", "", 0.0, 20.0}},
        {4, position(0.01f, 0.005f, 0.02f), {0, 2, 0}, 1.40f, "point", std::nullopt, std::nullopt},
        {5, position(2, -2, 8), {0.06f, 0.15f, 0.85f}, 0.19f, "point", std::nullopt, std::nullopt},
        {6, position(0, .03f, 10), {2.4f, .75f, .19f}, 1.05f, "point", std::nullopt, std::nullopt}
    };
    sample.authoredLights.status = "available";
    sample.authoredLights.publishedRecords = 2u;
    sample.renderedLights.status = "available";
    sample.renderedLights.publishedRecords = static_cast<std::uint32_t>(records.size());
    sample.renderedLights.ageMilliseconds = 12;
    sample.renderedLights.records = std::make_shared<const std::vector<LightRecord>>(records);
    return view;
}
View PitchedLightFixture(float aspect)
{
    auto view = LightFixture(aspect);
    auto& sample = view.sample;
    sample.cameraPosition->y += 6;
    sample.cameraPosition->z -= 5;
    // Down40 degrees with15 degrees of roll; this cannot be represented by yaw.
    const float pitch = -40*std::numbers::pi_v<float>/180;
    const float roll = 15*std::numbers::pi_v<float>/180;
    const float sp = std::sin(pitch), cp = std::cos(pitch), sr = std::sin(roll), cr = std::cos(roll);
    sample.cameraForward = Vec3{0,sp,cp};
    sample.cameraRight = Vec3{cr,cp*sr,-sp*sr};
    sample.cameraUp = Vec3{-sr,cp*cr,-sp*cr};
    sample.pitch = -40.f;
    return view;
}
struct Pixels
{
    UINT width{}, height{};
    std::vector<unsigned char> bgra;
    // scRGB assertions use decoded linear FP16 values, never SDR byte casts.
    std::vector<std::array<float, 3>> linearRgb;
    size_t Changed(int left, int top, int right, int bottom) const
    {
        size_t count = 0;
        for (int y = std::max(0, top); y < std::min(static_cast<int>(height), bottom); ++y)
            for (int x = std::max(0, left); x < std::min(static_cast<int>(width), right); ++x)
            {
                const size_t index = static_cast<size_t>(y) * width + static_cast<size_t>(x);
                if (!linearRgb.empty())
                {
                    const auto& pixel = linearRgb[index];
                    if (std::abs(pixel[0] - .10f) + std::abs(pixel[1] - .15f) +
                        std::abs(pixel[2] - .21f) > .035f) ++count;
                    continue;
                }
                const auto* pixel = bgra.data() + index * 4;
                // The UNORM clear is RGBA(0.10,0.15,0.21,1). Allow rounding.
                if (std::abs(static_cast<int>(pixel[0]) - 54) + std::abs(static_cast<int>(pixel[1]) - 38) +
                    std::abs(static_cast<int>(pixel[2]) - 26) > 9) ++count;
            }
        return count;
    }
    size_t Different(const Pixels& other, int left, int top, int right, int bottom) const
    {
        Require(width == other.width && height == other.height, "Pixel comparison dimensions differ");
        Require(linearRgb.empty() == other.linearRgb.empty(), "Pixel comparison color spaces differ");
        size_t count = 0;
        for (int y = std::max(0, top); y < std::min(static_cast<int>(height), bottom); ++y)
            for (int x = std::max(0, left); x < std::min(static_cast<int>(width), right); ++x)
            {
                const size_t index = static_cast<size_t>(y) * width + static_cast<size_t>(x);
                if (!linearRgb.empty())
                {
                    const auto& a = linearRgb[index];
                    const auto& b = other.linearRgb[index];
                    if (std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]) > .08f) ++count;
                    continue;
                }
                const size_t offset = index * 4;
                if (std::abs(static_cast<int>(bgra[offset]) - other.bgra[offset]) +
                    std::abs(static_cast<int>(bgra[offset + 1]) - other.bgra[offset + 1]) +
                    std::abs(static_cast<int>(bgra[offset + 2]) - other.bgra[offset + 2]) > 20) ++count;
            }
        return count;
    }
    size_t AboveSdrWhite(int left, int top, int right, int bottom) const
    {
        Require(!linearRgb.empty(), "Expected linear scRGB readback");
        size_t count = 0;
        for (int y = std::max(0, top); y < std::min(static_cast<int>(height), bottom); ++y)
            for (int x = std::max(0, left); x < std::min(static_cast<int>(width), right); ++x)
            {
                const auto& pixel = linearRgb[static_cast<size_t>(y) * width + static_cast<size_t>(x)];
                if (pixel[0] > 1.1f && pixel[1] > 1.1f && pixel[2] > 1.1f) ++count;
            }
        return count;
    }
    size_t Around(float x, float y, float radius) const
    {
        return Changed(static_cast<int>(std::floor(x - radius)), static_cast<int>(std::floor(y - radius)),
            static_cast<int>(std::ceil(x + radius)), static_cast<int>(std::ceil(y + radius)));
    }
};
void RequireScRgbUi(const Pixels& image, const Config& config, bool noticesOnly)
{
    const float width = static_cast<float>(image.width), height = static_cast<float>(image.height);
    const float scale = noticesOnly ? std::min(std::max(1.f, height / 1080.f), width / 660.f) :
        HudScale(width, height, config, true);
    // These regions contain font glyphs, with no bright panel borders or radar
    // primitives. 200-nit UI white is 2.5 scRGB units (80 nits per unit), so the
    // light text must exceed 1.0; plain SDR rendering into FP16 would fail.
    const int left = static_cast<int>((noticesOnly ? 36.f : 40.f) * scale);
    const int top = static_cast<int>((noticesOnly ? 73.f : 54.f) * scale);
    const int right = static_cast<int>((noticesOnly ? 620.f : 260.f) * scale);
    const int bottom = static_cast<int>((noticesOnly ? 110.f : 79.f) * scale);
    Require(image.AboveSdrWhite(left, top, right, bottom) > 20,
        "scRGB ImGui font glyphs missing, incorrectly encoded, or clipped to SDR white");
    Require(image.Changed(static_cast<int>(image.width) - 20, static_cast<int>(image.height) - 20,
        static_cast<int>(image.width), static_cast<int>(image.height)) == 0,
        "scRGB composition changed pixels outside the UI");
}
void RequireLightPixels(const Pixels& image, const Config& config, bool hudVisible)
{
    const float width = static_cast<float>(image.width), height = static_cast<float>(image.height);
    // Independently calculated pinhole projection for the fixture's vertical60°
    // FOV. Deliberately do not call ProjectWorld: this is a renderer integration
    // check, not a second assertion using the implementation's own result.
    const float focal = height / (2 * std::tan(std::numbers::pi_v<float> / 6));
    const float scale = HudScale(width, height, config, true);
    const float radius = 13 * scale;
    Require(image.Around(width / 2 + focal * 4 / 14, height / 2 - focal / 14, radius) > 10,
        "Projected blue spot marker missing");
    Require(image.Around(width / 2 + focal * 2 / 8, height / 2 + focal * 2 / 8, radius) > 10,
        "Projected lower light marker missing");
    if (!hudVisible)
    {
        Require(image.Around(width / 2, height / 2, radius) > 10, "Projected central light marker missing");
        Require(image.Around(width / 2 - focal * 3 / 12, height / 2 - focal * 2 / 12, radius) > 10,
            "Projected elevated light marker missing");
    }
    const float spotX = width / 2 + focal * 4 / 14, spotY = height / 2 - focal / 14;
    Require(image.Changed(static_cast<int>(spotX - 4 * scale), static_cast<int>(spotY + 16 * scale),
        static_cast<int>(spotX + 4 * scale), static_cast<int>(spotY + 40 * scale)) > 4,
        "Downward spot direction arrow missing");
    if (hudVisible)
        Require(image.Changed(static_cast<int>(36 * scale), static_cast<int>(138 * scale),
            static_cast<int>(514 * scale), static_cast<int>(386 * scale)) > 100,
            "Combined light HUD radar area was not rendered");
}
void RequireNoLightPixels(const Pixels& image)
{
    const float width = static_cast<float>(image.width), height = static_cast<float>(image.height);
    const float focal = height / (2 * std::tan(std::numbers::pi_v<float> / 6));
    Require(image.Around(width / 2, height / 2, 14) == 0 &&
        image.Around(width / 2 + focal * 4 / 14, height / 2 - focal / 14, 14) == 0 &&
        image.Around(width / 2 + focal * 2 / 8, height / 2 + focal * 2 / 8, 14) == 0,
        "Missing/stale/hidden light data retained fullscreen ghosts");
}
float DecodeHalf(std::uint16_t bits)
{
    const int exponent = (bits >> 10) & 31;
    const int fraction = bits & 1023;
    if (exponent == 31) return fraction ? std::numeric_limits<float>::quiet_NaN() :
        (bits & 0x8000 ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity());
    const float magnitude = exponent ? std::ldexp(1.f + static_cast<float>(fraction) / 1024.f, exponent - 15) :
        std::ldexp(static_cast<float>(fraction), -24);
    return bits & 0x8000 ? -magnitude : magnitude;
}
Pixels SaveBuffer(Gpu& gpu, ID3D12Resource* buffer, const wchar_t* path)
{
    const auto desc = buffer->GetDesc();
    const bool scRgb = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    Require(scRgb || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM, "Unsupported smoke readback format");
    Require(!scRgb || !path, "scRGB smoke has no SDR BMP output; inspect its linear pixel assertions instead");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 total{};
    gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc{};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = total; readbackDesc.Height = 1; readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1; readbackDesc.SampleDesc.Count = 1; readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    Check(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
    gpu.Begin();
    gpu.Transition(buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = buffer; source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    gpu.Transition(buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    gpu.Submit(); gpu.Wait();
    unsigned char* mapped{};
    D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
    Check(readback->Map(0, &range, reinterpret_cast<void**>(&mapped)));
    const auto width = static_cast<UINT>(desc.Width);
    const size_t pixelCount = static_cast<size_t>(width) * desc.Height;
    std::vector<unsigned char> pixels(scRgb ? 0 : pixelCount * 4);
    std::vector<std::array<float, 3>> linearRgb(scRgb ? pixelCount : 0);
    const UINT pixelBytes = scRgb ? 8u : 4u;
    for (UINT y = 0; y < desc.Height; ++y)
        for (UINT x = 0; x < width; ++x)
        {
            const auto input = mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch + x * pixelBytes;
            if (scRgb)
            {
                std::array<std::uint16_t, 4> half{};
                std::memcpy(half.data(), input, sizeof(half));
                auto& output = linearRgb[static_cast<size_t>(y) * width + x];
                for (size_t channel = 0; channel < 4; ++channel)
                {
                    const float decoded = DecodeHalf(half[channel]);
                    Require(std::isfinite(decoded), "scRGB output contains non-finite pixels");
                    if (channel < 3) output[channel] = decoded;
                }
                continue;
            }
            const auto output = pixels.data() + (static_cast<size_t>(y) * width + x) * 4;
            output[0] = input[2]; output[1] = input[1]; output[2] = input[0]; output[3] = 255;
        }
    D3D12_RANGE noWrite{}; readback->Unmap(0, &noWrite);
    if (path)
    {
        BITMAPFILEHEADER file{}; file.bfType = 0x4D42; file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
        file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
        BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = static_cast<LONG>(width);
        info.biHeight = -static_cast<LONG>(desc.Height); info.biPlanes = 1; info.biBitCount = 32;
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(&file), sizeof(file));
        stream.write(reinterpret_cast<const char*>(&info), sizeof(info));
        stream.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        Require(stream.good(), "Could not save smoke image");
    }
    return Pixels{width, desc.Height, std::move(pixels), std::move(linearRgb)};
}
}
int wmain(int argc, wchar_t** argv)
{
    try
    {
        Config config; config.details = true;
        Require(!InstallGraphics(config), "Disabled HUD installed graphics hooks");
        const bool allOn = argc > 1 && (_wcsicmp(argv[1], L"--all-ui") == 0 || _wcsicmp(argv[1], L"--scrgb-all-ui") == 0);
        const bool scRgbNotices = argc > 1 && _wcsicmp(argv[1], L"--scrgb-notices") == 0;
        const bool scRgb = scRgbNotices || (argc > 1 && (_wcsicmp(argv[1], L"--scrgb") == 0 || _wcsicmp(argv[1], L"--scrgb-all-ui") == 0));
        const bool noticesOnly = scRgbNotices || (argc > 1 && _wcsicmp(argv[1], L"--notices") == 0);
        const bool lightsOnly = argc > 1 && _wcsicmp(argv[1], L"--lights-only") == 0;
        const bool engineLights = argc > 1 && _wcsicmp(argv[1], L"--engine-lights") == 0;
        const bool lightsMode = allOn || lightsOnly || engineLights || (argc > 1 && _wcsicmp(argv[1], L"--lights") == 0);
        config.enabled = !noticesOnly && !lightsOnly;
        config.notifications = allOn || noticesOnly;
        config.lightOverlay = lightsMode;
        // Regression control: InitiallyVisible=0 must still allow the runtime
        // F10 state to enable drawing; the immutable startup config stays false.
        config.lightOverlayVisible = allOn || !lightsMode;
        config.radar3D = true;
        const int imageArgument = noticesOnly || lightsMode || scRgb ? 2 : 1;
        Require(!scRgb || argc == 2, "scRGB modes accept no BMP paths; they validate synthetic linear FP16 pixels");
        Require(InstallGraphics(config), "Graphics hooks not installed");
        WNDCLASSW windowClass{}; windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = GetModuleHandleW(nullptr); windowClass.lpszClassName = L"CDTGraphicsSmoke";
        Require(RegisterClassW(&windowClass) != 0, "Window registration");
        // Hidden throughout: no focus changes or interference with the user's desktop.
        HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"CDT automated render test",
            WS_OVERLAPPEDWINDOW, 0, 0, 1000, 760, nullptr, nullptr, windowClass.hInstance, nullptr);
        Require(window != nullptr, "Hidden window creation");
        ComPtr<IDXGIFactory4> factory;
        Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> adapter;
        Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        Gpu gpu;
        Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu.device)));
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        Check(gpu.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gpu.queue)));
        Check(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gpu.allocator)));
        Check(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpu.allocator.Get(), nullptr, IID_PPV_ARGS(&gpu.commands)));
        Check(gpu.commands->Close());
        Check(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu.fence)));
        DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = 960; desc.Height = 720;
        const DXGI_FORMAT swapchainFormat = scRgb ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Format = swapchainFormat; desc.BufferCount = 3;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> initialChain;
        Check(factory->CreateSwapChainForHwnd(gpu.queue.Get(), window, &desc, nullptr, nullptr, &initialChain));
        initialChain.Reset();
        // NVIDIA Streamline may replace the base chain inside its outer factory
        // wrapper. A pending telemetry reference must not block that nested create.
        ComPtr<IDXGISwapChain1> chain1;
        Check(factory->CreateSwapChainForHwnd(gpu.queue.Get(), window, &desc, nullptr, nullptr, &chain1));
        ComPtr<IDXGISwapChain3> chain; Check(chain1.As(&chain));
        // FP16 defaults to G10/P709 scRGB. Do not require display HDR support or
        // SetColorSpace1 success on this hidden software-rendered swapchain.
        // Production deliberately leaves the newly returned swapchain untouched
        // while graphics wrappers finish their post-create association work.
        MaintainGraphics();
        Require(std::string(GraphicsStatus()).find("waiting for graphics wrappers") != std::string::npos,
            "Swapchain was tracked before graphics wrappers could finish");
        Sleep(550);
        MaintainGraphics();
        std::cout << GraphicsStatus() << '\n';
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{}; heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heapDesc.NumDescriptors = 1;
        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        Check(gpu.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap)));
        const auto frame = [&](bool present1, const wchar_t* screenshot, const View* supplied = nullptr, bool inspect = false)
        {
            DXGI_SWAP_CHAIN_DESC1 current{}; Check(chain->GetDesc1(&current));
            auto view = supplied ? *supplied : lightsMode ?
                LightFixture(static_cast<float>(current.Width) / static_cast<float>(current.Height)) : Fixture();
            if (allOn)
            {
                // Each Present represents a new synthetic capture. CPU conversion
                // of the preceding 4K FP16 readback must not age this next sample.
                view.received = Clock::now();
                view.sample.sequence = static_cast<std::int64_t>(RenderedFrames()) + 1;
            }
            if (noticesOnly && !supplied) view.sample.cameraPosition = view.sample.playerPosition;
            Publish(std::move(view));
            ComPtr<ID3D12Resource> buffer;
            Check(chain->GetBuffer(chain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer)));
            const auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
            gpu.device->CreateRenderTargetView(buffer.Get(), nullptr, rtv);
            gpu.Begin();
            gpu.Transition(buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const float color[] = {0.10f, 0.15f, 0.21f, 1};
            gpu.commands->ClearRenderTargetView(rtv, color, 0, nullptr);
            gpu.Transition(buffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            gpu.Submit();
            DXGI_PRESENT_PARAMETERS parameters{};
            Check(present1 ? chain->Present1(0, 0, &parameters) : chain->Present(0, 0));
            gpu.Wait();
            if (screenshot || inspect) return SaveBuffer(gpu, buffer.Get(), screenshot);
            return Pixels{};
        };
        // Also replace a chain after the overlay has accepted it. This models a
        // runtime display/HDR transition while keeping the HWND unchanged.
        chain.Reset();
        chain1.Reset();
        Check(factory->CreateSwapChainForHwnd(gpu.queue.Get(), window, &desc, nullptr, nullptr, &chain1));
        Check(chain1.As(&chain));
        MaintainGraphics();
        Require(std::string(GraphicsStatus()).find("waiting for graphics wrappers") != std::string::npos,
            "Replacement swapchain was tracked before graphics wrappers could finish");
        Sleep(550);
        MaintainGraphics();
        Require(std::string(GraphicsStatus()).find("Overlay ready") != std::string::npos,
            "HUD did not adopt the replacement swapchain");
        if (engineLights)
        {
            // Engine-input view: the renderer's selection plus current lights it did
            // not select (hollow), including sources behind the camera.
            SetVisibleForTest(true); SetLightVisibleForTest(true);
            const auto engineView = [&](float aspect)
            {
                auto view = LightFixture(aspect);
                view.sample.schemaVersion = "1.6";
                auto records = *view.sample.renderedLights.records;
                records[0].memberCount = 16; records[6].memberCount = 16;
                records[3].rendererSelected = false; records[5].rendererSelected = false;
                const auto camera = *view.sample.cameraPosition;
                for (int n = 0; n < 4; ++n)
                {
                    LightRecord behind{100 + n, Vec3{camera.x - 9 + 6 * n, camera.y + 1 - n * .5f, camera.z - 14 - 3 * n},
                        Vec3{1.6f - .3f * n, .5f, .12f + .2f * n}, .7f, "point"};
                    behind.rendererSelected = false; behind.memberCount = n % 2 ? 0 : 16;
                    records.push_back(behind);
                }
                view.sample.upstreamLights = view.sample.renderedLights;
                view.sample.upstreamLights.publishedRecords = static_cast<std::uint32_t>(records.size());
                view.sample.upstreamLights.records = std::make_shared<const std::vector<LightRecord>>(std::move(records));
                return view;
            };
            const auto engine = engineView(960.f / 720.f);
            const auto engineImage = frame(false, argc > 2 ? argv[2] : nullptr, &engine, true);
            const auto renderedImage = frame(false, nullptr, nullptr, true);
            const float scale = HudScale(960, 720, config, true);
            Require(engineImage.Different(renderedImage, static_cast<int>(36 * scale), static_cast<int>(138 * scale),
                static_cast<int>(514 * scale), static_cast<int>(386 * scale)) > 20,
                "Radar ignored current engine lights the renderer did not select");
            Require(DisplayLights(engine, Clock::now(), config.staleMs) == &engine.sample.upstreamLights,
                "Engine-input fixture was not the drawable set");
            Check(chain->ResizeBuffers(3, 3840, 2160, swapchainFormat, 0));
            MaintainGraphics();
            const auto large = engineView(3840.f / 2160.f);
            frame(false, argc > 3 ? argv[3] : nullptr, &large, true);
            Check(gpu.device->GetDeviceRemovedReason());
            std::cout << "PASS engine-input radar/markers: renderer-selected filled, unselected hollow, behind-camera lights drawn; synthetic WARP only\n";
            DestroyWindow(window);
            return 0;
        }
        if (allOn)
        {
            // Exercise actual Present/Present1 with all three graphics features
            // active together. Compare independent pixels, not just draw counts.
            for (const auto size : {std::array<UINT,2>{960,720}, {1920,1080}, {3840,2160}})
            {
                std::cout << "Combined UI " << size[0] << 'x' << size[1] << '\n';
                Check(chain->ResizeBuffers(3, size[0], size[1], swapchainFormat, 0));
                MaintainGraphics();
                auto view = LightFixture(static_cast<float>(size[0]) / size[1]);
                view.localFaults.push_back({"all-ui", "Synthetic combined-feature test", "Notification A"});
                const auto combined = frame(false, nullptr, &view, true);
                Require(RenderedFrames() > 0, "Combined graphics path did not submit a frame after resize");
                RequireLightPixels(combined, config, true);
                if (scRgb) RequireScRgbUi(combined, config, false);
                view.localFaults.front().detail = "Notification B: changed while HUD and markers stay active";
                const auto changedNotice = frame(true, nullptr, &view, true);
                Require(combined.Different(changedNotice, 0, 0, size[0], size[1]) > 20,
                    "Combined graphics path did not update its notification");
                SetVisibleForTest(false);
                const auto noHud = frame(false, nullptr, &view, true);
                Require(changedNotice.Different(noHud, 0, 0, size[0], size[1]) > 100,
                    "HUD toggle had no effect while markers and notifications were active");
                SetLightVisibleForTest(false);
                const auto noticeOnlyImage = frame(true, nullptr, &view, true);
                Require(noHud.Different(noticeOnlyImage, 0, 0, size[0], size[1]) > 100,
                    "Marker toggle had no effect while notifications were active");
                Require(noticeOnlyImage.Changed(20,20,640,160) > 100,
                    "Notification disappeared when both other views were hidden");
                SetVisibleForTest(true);
                SetLightVisibleForTest(true);
                const auto restored = frame(false, nullptr, &view, true);
                RequireLightPixels(restored, config, true);
                Require(view.sample.renderedLights.records->size() == 7,
                    "Presentation toggles changed the source records");
            }
            Check(gpu.device->GetDeviceRemovedReason());
            std::cout << "PASS combined HUD/markers/notifications, independent toggles and resize at 720p/1080p/2160p; synthetic WARP only\n";
            DestroyWindow(window);
            return 0;
        }
        if (noticesOnly)
        {
            View waiting;
            const auto startupImage = frame(false, nullptr, &waiting, true);
            Require(startupImage.Changed(0, 0, 960, 720) == 0 && RenderedFrames() == 0,
                "Normal notification-only startup produced visible loading UI");
            waiting.connected = waiting.hasSample = true;
            waiting.sample.state = "loading"; waiting.received = Clock::now();
            const auto loadingImage = frame(false, nullptr, &waiting, true);
            Require(loadingImage.Changed(0, 0, 960, 720) == 0 && RenderedFrames() == 0,
                "Normal loading submitted a notification draw");
        }
        if (lightsMode)
        {
            SetVisibleForTest(false);
            RequireNoLightPixels(frame(false, nullptr, nullptr, true));
            Require(RenderedFrames() == 0, "Initially hidden light overlay submitted a draw");
            SetLightVisibleForTest(true);
            SetVisibleForTest(!lightsOnly);
        }
        Pixels referenceLights;
        for (int i = 0; i < 8; ++i)
        {
            const auto image = frame(i % 2 == 0, i == 7 && argc > imageArgument ? argv[imageArgument] : nullptr,
                nullptr, (lightsMode || scRgb) && i == 7);
            if (scRgb && i == 7) RequireScRgbUi(image, config, noticesOnly);
            if (lightsMode && i == 7)
            {
                RequireLightPixels(image, config, !lightsOnly);
                referenceLights = image;
            }
        }
        Require(RenderedFrames() == 8, "Present/Present1 did not render exactly once per frame");
        std::cout << "PASS D3D12 Present/Present1 and multi-frame resource reuse\n";
        if (lightsMode && !lightsOnly)
        {
            auto flat = LightFixture(960.0f / 720.0f);
            auto records = *flat.sample.renderedLights.records;
            for (auto& record : records) record.position.y = flat.sample.playerPosition->y;
            flat.sample.renderedLights.records = std::make_shared<const std::vector<LightRecord>>(std::move(records));
            const auto flattenedImage = frame(false, nullptr, &flat, true);
            const float scale = HudScale(960, 720, config, true);
            Require(referenceLights.Different(flattenedImage, static_cast<int>(36 * scale), static_cast<int>(138 * scale),
                static_cast<int>(514 * scale), static_cast<int>(386 * scale)) > 10,
                "Light radar ignored record height changes");
            std::cout << "PASS 3D light radar responds to changing light elevations\n";
        }
        const auto before = RenderedFrames();
        Check(chain->Present(0, DXGI_PRESENT_TEST));
        Require(RenderedFrames() == before, "Test-only present changed output");
        SetVisibleForTest(false);
        const auto hiddenHud = frame(false, nullptr, nullptr, lightsMode || scRgb);
        Require(RenderedFrames() == before + (noticesOnly || lightsMode ? 1 : 0),
            "Independent notices/light markers did not survive hiding the full HUD");
        if (scRgb)
        {
            if (noticesOnly) RequireScRgbUi(hiddenHud, config, true);
            else Require(hiddenHud.Changed(0, 0, 960, 720) == 0, "Hidden scRGB HUD left visible pixels");
        }
        if (lightsMode)
        {
            RequireLightPixels(hiddenHud, config, false);
            // Runtime hide state must reach both draw passes even though the
            // startup config remains HideOccluded=0. Do not mutate the feed.
            auto occludedOnly=LightFixture(960.0f/720.0f);
            std::vector<LightRecord> oneBlocked{occludedOnly.sample.renderedLights.records->at(1)};
            occludedOnly.sample.renderedLights.records=std::make_shared<const std::vector<LightRecord>>(oneBlocked);
            occludedOnly.sample.renderedLights.publishedRecords=1u;
            const float hideFocal=720/(2*std::tan(std::numbers::pi_v<float>/6));
            const float blockedX=480-hideFocal*3/12,blockedY=360-hideFocal*2/12;
            Require(frame(false,nullptr,&occludedOnly,true).Around(blockedX,blockedY,14)>10,
                "Fresh blocked fixture must start visible when hide is off");
            SetHideOccludedForTest(true);
            const auto suppressed=frame(false,nullptr,&occludedOnly,true);
            Require(suppressed.Around(blockedX,blockedY,14)==0&&suppressed.Changed(0,0,960,600)==0,
                "Hide option left a blocked marker or its detail card visible");
            Require(occludedOnly.sample.renderedLights.records->size()==1&&
                occludedOnly.sample.renderedLights.records->front().colorLinear.x==1.20f&&
                CountSourceVisibility(occludedOnly,Clock::now(),35).blocked==1,
                "HUD hide altered raw records or raw visibility counts");
            for(const bool staleVisibility:{false,true})
            {
                auto unknown=occludedOnly;
                auto unknownRecords=oneBlocked;
                unknownRecords.front().sourceVisibility=staleVisibility
                    ? SourceVisibility{"blocked","",0.0,2000.0}
                    : SourceVisibility{"unknown","uncovered",std::nullopt,std::nullopt};
                unknown.sample.renderedLights.records=std::make_shared<const std::vector<LightRecord>>(unknownRecords);
                unknown.received=Clock::now();
                Require(frame(false,nullptr,&unknown,true).Around(blockedX,blockedY,14)==0,
                    "Hide option exposed an unknown or stale visibility result");
            }
            SetHideOccludedForTest(false);occludedOnly.received=Clock::now();
            Require(frame(false,nullptr,&occludedOnly,true).Around(blockedX,blockedY,14)>10,
                "Showing blocked sources again did not restore marker geometry");
            if(!lightsOnly)
            {
                SetVisibleForTest(true);
                const auto shownRadar=frame(false,nullptr,nullptr,true);
                SetHideOccludedForTest(true);
                const auto hiddenRadar=frame(false,nullptr,nullptr,true);
                const float radarScale=HudScale(960,720,config,true);
                Require(shownRadar.Different(hiddenRadar,static_cast<int>(36*radarScale),static_cast<int>(138*radarScale),
                    static_cast<int>(514*radarScale),static_cast<int>(386*radarScale))>10,
                    "Runtime hide state did not remove blocked radar markers");
                SetHideOccludedForTest(false);SetVisibleForTest(false);
            }
            std::cout<<"PASS runtime blocked-source hiding/restoration, radar propagation and unknown/raw preservation\n";
            // Isolate each excluded source with the HUD hidden. A legitimate
            // detail card in the mixed scene can otherwise occupy its would-be
            // projection, making a blank-pixel clipping assertion invalid.
            for (const size_t excludedIndex : std::array<size_t,2>{3,4})
            {
                auto excludedOnly = LightFixture(960.0f/720.0f);
                const std::vector<LightRecord> excludedRecord{
                    excludedOnly.sample.renderedLights.records->at(excludedIndex)};
                excludedOnly.sample.renderedLights.records =
                    std::make_shared<const std::vector<LightRecord>>(excludedRecord);
                excludedOnly.sample.renderedLights.publishedRecords = 1u;
                const auto beforeExcluded = RenderedFrames();
                const auto excludedImage = frame(false,nullptr,&excludedOnly,true);
                Require(RenderedFrames() == beforeExcluded+1,"Clipping fixture did not render its active light overlay");
                const float excludedWidth = static_cast<float>(excludedImage.width);
                const float excludedHeight = static_cast<float>(excludedImage.height);
                const float focal = excludedHeight/(2*std::tan(std::numbers::pi_v<float>/6));
                Require(excludedImage.Around(excludedWidth/2+focal/2,excludedHeight/2-focal/4,12) == 0,
                    "An isolated behind-camera or near-plane light produced its screen ghost");
                Require(excludedImage.Changed(0,0,static_cast<int>(excludedImage.width),
                    static_cast<int>(excludedImage.height)-120) == 0,
                    "An excluded-only fixture painted a marker or detail card outside the status legend");
            }
            auto centerOnly = LightFixture(960.0f / 720.0f);
            const std::vector<LightRecord> oneRecord{centerOnly.sample.renderedLights.records->front()};
            centerOnly.sample.renderedLights.records = std::make_shared<const std::vector<LightRecord>>(oneRecord);
            centerOnly.sample.renderedLights.publishedRecords = 1u;
            const auto centerImage = frame(false, nullptr, &centerOnly, true);
            const float centerX = static_cast<float>(centerImage.width) / 2;
            const float centerY = static_cast<float>(centerImage.height) / 2;
            // Allow either right-side or upper placement on compact surfaces.
            // Source rings and their short connector do not reach this region.
            Require(centerImage.Changed(static_cast<int>(centerX + 50), static_cast<int>(centerY - 180),
                static_cast<int>(centerX + 430), static_cast<int>(centerY + 180)) > 100,
                "Selected central light detail card was blocked by the reticle");
            auto grouped = LightFixture(960.0f / 720.0f);
            std::vector<LightRecord> pair{grouped.sample.renderedLights.records->front(),
                grouped.sample.renderedLights.records->back()};
            grouped.sample.renderedLights.records = std::make_shared<const std::vector<LightRecord>>(pair);
            grouped.sample.renderedLights.publishedRecords = 2u;
            const auto groupedImage = frame(false,nullptr,&grouped,true);
            pair[1].colorLinear = Vec3{7.7f,1.7f,.7f}; pair[1].luminanceLinear = 2.9f;
            grouped.sample.renderedLights.records = std::make_shared<const std::vector<LightRecord>>(pair);
            grouped.received = Clock::now();
            const auto changedSecondRow = frame(false,nullptr,&grouped,true);
            Require(groupedImage.Different(changedSecondRow,static_cast<int>(centerX+50),static_cast<int>(centerY-180),
                static_cast<int>(centerX+430),static_cast<int>(centerY+180)) > 5,
                "Grouped central detail card did not render the second contribution's own RGB");
            auto stale = LightFixture(960.0f / 720.0f);
            stale.sample.renderedLights.ageMilliseconds = 650;
            RequireNoLightPixels(frame(false, nullptr, &stale, true));
            auto missing = LightFixture(960.0f / 720.0f);
            missing.sample.renderedLights = LightSummary{};
            RequireNoLightPixels(frame(false, nullptr, &missing, true));
            SetLightVisibleForTest(false);
            const auto beforeOff = RenderedFrames();
            RequireNoLightPixels(frame(false, nullptr, nullptr, true));
            Require(RenderedFrames() == beforeOff, "All UI hidden still submitted an overlay draw");
            SetLightVisibleForTest(true);
            RequireLightPixels(frame(false, nullptr, nullptr, true), config, false);
            std::cout << "PASS projected lights, spot arrow, depth clipping, stale/missing clearing and independent light visibility\n";
        }
        const auto afterHidden = RenderedFrames();
        SetVisibleForTest(!lightsOnly && !noticesOnly);
        Check(chain->ResizeBuffers(2, 1280, 800, swapchainFormat, 0));
        MaintainGraphics(); const auto resizedImage = frame(false, nullptr, nullptr, scRgb);
        Require(RenderedFrames() == afterHidden + 1, "HUD/notifications did not resume after ResizeBuffers");
        if (scRgb) RequireScRgbUi(resizedImage, config, noticesOnly);
        const UINT masks[] = {0, 0}; IUnknown* queues[] = {gpu.queue.Get(), gpu.queue.Get()};
        Check(chain->ResizeBuffers1(2, 960, 720, swapchainFormat, 0, masks, queues));
        MaintainGraphics(); const auto resizedImage1 = frame(true, nullptr, nullptr, scRgb);
        Require(RenderedFrames() == afterHidden + 2, "HUD/notifications did not resume after ResizeBuffers1");
        if (scRgb) RequireScRgbUi(resizedImage1, config, noticesOnly);
        std::cout << "PASS visibility, test-only present, ResizeBuffers and ResizeBuffers1\n";
        Check(chain->ResizeBuffers(2, 3840, 2160, swapchainFormat, 0));
        MaintainGraphics();
        const auto largeImage = frame(false, argc > imageArgument + 1 ? argv[imageArgument + 1] : nullptr, nullptr, lightsMode || scRgb);
        Require(RenderedFrames() == afterHidden + 3, "4K HUD/notifications did not render after resize");
        if (lightsMode) RequireLightPixels(largeImage, config, !lightsOnly);
        if (scRgb) RequireScRgbUi(largeImage, config, noticesOnly);
        std::cout << "PASS 4K HUD rendering after resize with resolution-scaled font atlas\n";
        if (lightsMode && !lightsOnly)
        {
            auto pitched = PitchedLightFixture(3840.0f/2160.0f);
            const auto pitchedImage = frame(false,argc > imageArgument+2 ? argv[imageArgument+2] : nullptr,&pitched,true);
            auto level = pitched;
            level.sample.cameraForward = Vec3{0,0,1}; level.sample.cameraRight = Vec3{1,0,0};
            level.sample.cameraUp = Vec3{0,1,0}; level.sample.pitch = 0.f;
            level.received = Clock::now();
            const auto levelImage = frame(false,nullptr,&level,true);
            const float radarScale = HudScale(3840,2160,config,true);
            Require(pitchedImage.Different(levelImage,static_cast<int>(36*radarScale),static_cast<int>(138*radarScale),
                static_cast<int>(514*radarScale),static_cast<int>(386*radarScale)) > 100,
                "Camera radar frustum ignored pitch and roll while yaw was unchanged");
            auto missingBasis = pitched; missingBasis.sample.cameraRight.reset(); missingBasis.received = Clock::now();
            const auto missingImage = frame(false,nullptr,&missingBasis,true);
            Require(pitchedImage.Different(missingImage,static_cast<int>(36*radarScale),static_cast<int>(138*radarScale),
                static_cast<int>(514*radarScale),static_cast<int>(386*radarScale)) > 100,
                "Camera radar frustum was guessed without a valid camera basis");
            Check(chain->ResizeBuffers(2,800,480,DXGI_FORMAT_R8G8B8A8_UNORM,0));
            MaintainGraphics();
            const auto compactImage = frame(false,argc > imageArgument+3 ? argv[imageArgument+3] : nullptr,nullptr,true);
            Require(compactImage.Changed(440,190,760,310) > 100,
                "Compact combined surface lost its grouped light detail card");
            std::cout << "PASS full-width radar camera pitch/roll, missing-basis clearing and grouped contribution RGB\n";
        }
        if (noticesOnly)
        {
            View waiting;
            SetLocalFault("bootstrap", "Telemetry host files are missing", "Reinstall the complete CrimsonDesertTelemetry package.");
            const auto localFault = frame(false, nullptr, &waiting, true);
            Require(localFault.Changed(0, 0, 3840, 2160) > 100,
                "Local bootstrap failure produced no on-screen notice without a server");
            ClearLocalFault("bootstrap");
            const auto cleared = frame(false, nullptr, &waiting, true);
            Require(cleared.Changed(0, 0, 3840, 2160) == 0,
                "Resolved local failure left a stale notification on screen");
            waiting.healthStatus = "unsupported-build";
            waiting.healthError = "Install a telemetry version for this game build.";
            // The raster fixture publishes its modeled diagnostic directly;
            // process-local reporting is exercised above through the real API.
            waiting.localFaults.push_back({"native", "This game build needs an update", waiting.healthError});
            const auto unsupported = frame(false, nullptr, &waiting, true);
            Require(unsupported.Changed(0, 0, 3840, 2160) > 100,
                "Unsupported build before any playing sample produced no on-screen error");
            std::cout << "PASS silent startup/loading and local bootstrap/unsupported errors without a host\n";
        }
        if (scRgb && !noticesOnly)
        {
            // Keep the same swapchain and never call SetColorSpace1: its default
            // encoding must follow each resized buffer format, including the
            // first submitted UI frame after the worker rebuilds its pipeline.
            const auto beforeTransition = RenderedFrames();
            Check(chain->ResizeBuffers(2, 960, 720, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
            MaintainGraphics();
            Require(std::strcmp(GraphicsOutputLabel(), "D3D12 / SDR") == 0 &&
                std::strstr(GraphicsStatus(), "Overlay ready: D3D12 / SDR") != nullptr,
                "FP16-to-UNORM resize did not restore SDR output label/status");
            const auto sdrTransition = frame(false, nullptr, nullptr, true);
            Require(RenderedFrames() == beforeTransition + 1 && !sdrTransition.bgra.empty() &&
                sdrTransition.linearRgb.empty() && sdrTransition.Changed(0, 0, 960, 720) > 100,
                "First SDR frame after FP16 resize did not render visible UI");
            Check(chain->ResizeBuffers1(2, 960, 720, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, masks, queues));
            MaintainGraphics();
            Require(std::strcmp(GraphicsOutputLabel(), "D3D12 / scRGB") == 0 &&
                std::strstr(GraphicsStatus(), "Overlay ready: D3D12 / scRGB") != nullptr,
                "UNORM-to-FP16 resize did not restore scRGB output label/status");
            const auto hdrTransition = frame(true, nullptr, nullptr, true);
            Require(RenderedFrames() == beforeTransition + 2,
                "First scRGB frame after SDR resize did not render exactly once");
            RequireScRgbUi(hdrTransition, config, false);
            std::cout << "PASS same-swapchain scRGB-to-SDR-to-scRGB default-color transitions and first-frame UI\n";
        }
        Check(gpu.device->GetDeviceRemovedReason());
        if (scRgb) std::cout << "PASS synthetic D3D12/WARP scRGB Present/font/visibility/resize integration; no HDR display or live game tested\n";
        DestroyWindow(window);
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}

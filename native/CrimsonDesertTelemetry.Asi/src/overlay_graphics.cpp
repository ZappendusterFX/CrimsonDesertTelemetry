#include "overlay.h"
#include "overlay_hdr.h"
#include "presentation_queue.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <memory>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace cdt::overlay
{
namespace
{
using Microsoft::WRL::ComPtr;
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Graphics operation failed"); }
bool SameObject(IUnknown* first, IUnknown* second)
{
    if (!first || !second) return false;
    ComPtr<IUnknown> a, b;
    return SUCCEEDED(first->QueryInterface(IID_PPV_ARGS(&a))) &&
        SUCCEEDED(second->QueryInterface(IID_PPV_ARGS(&b))) && a.Get() == b.Get();
}
struct ContextScope
{
    ImGuiContext* previous;
    explicit ContextScope(ImGuiContext* context) : previous(ImGui::GetCurrentContext()) { ImGui::SetCurrentContext(context); }
    ~ContextScope() { ImGui::SetCurrentContext(previous); }
};
struct Frame
{
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    UINT64 fenceValue{};
};
struct Renderer
{
    ComPtr<IDXGISwapChain3> chain;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12DescriptorHeap> rtvHeap, srvHeap;
    ComPtr<ID3D12Fence> fence;
    std::vector<ComPtr<ID3D12Resource>> buffers;
    std::vector<Frame> frames;
    UINT rtvSize{}, width{}, height{};
    DXGI_FORMAT outputFormat = DXGI_FORMAT_UNKNOWN;
    hdr::OutputMode outputMode = hdr::OutputMode::Unsupported;
    std::unique_ptr<hdr::Compositor> compositor;
    UINT64 signalValue{}, draws{};
    ImGuiContext* context{};
    bool backend{}, fault{};
    Clock::time_point previousFrame = Clock::now();
    View latest;
    NoticeTracker notices;

    // Worker thread only. Font upload / shader compilation are never done in Present.
    void Initialize(IDXGISwapChain3* swapchain, ID3D12CommandQueue* commandQueue, const Config& config,
        DXGI_COLOR_SPACE_TYPE colorSpace)
    {
        chain = swapchain;
        queue = commandQueue;
        Check(chain->GetDevice(IID_PPV_ARGS(&device)));
        ComPtr<ID3D12Device> queueDevice;
        Check(queue->GetDevice(IID_PPV_ARGS(&queueDevice)));
        if (!SameObject(device.Get(), queueDevice.Get()) || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
            throw std::runtime_error("Swapchain queue mismatch");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        Check(chain->GetDesc1(&desc));
        outputFormat = desc.Format;
        outputMode = hdr::ResolveOutput(desc.Format, colorSpace);
        if (desc.BufferCount < 2 || desc.BufferCount > 8 || desc.SampleDesc.Count != 1 || desc.Stereo ||
            outputMode == hdr::OutputMode::Unsupported)
            throw std::runtime_error("Unsupported overlay swapchain format/color space");
        width = desc.Width; height = desc.Height;
        if (outputMode != hdr::OutputMode::Sdr)
        {
            compositor = std::make_unique<hdr::Compositor>();
            if (!compositor->Initialize(device.Get(), width, height, desc.Format))
                throw std::runtime_error("HDR compositor initialization failed");
        }
        frames.resize(desc.BufferCount);
        buffers.resize(desc.BufferCount);
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap.NumDescriptors = desc.BufferCount;
        Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvHeap)));
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 1;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&srvHeap)));
        rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < desc.BufferCount; ++i)
        {
            Check(chain->GetBuffer(i, IID_PPV_ARGS(&buffers[i])));
            device->CreateRenderTargetView(buffers[i].Get(), nullptr, rtv);
            rtv.ptr += rtvSize;
            Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frames[i].allocator)));
            Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames[i].allocator.Get(),
                nullptr, IID_PPV_ARGS(&frames[i].commands)));
            Check(frames[i].commands->Close());
        }
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        const auto saved = ImGui::GetCurrentContext();
        context = ImGui::CreateContext();
        ImGui::SetCurrentContext(saved);
        ContextScope scope(context);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
        io.BackendPlatformName = "cdt_passive_win32";
        // Read the locally installed Windows UI font on this worker, not in Present.
        // No system font is redistributed; embedded ProggyClean is the fallback.
        std::array<wchar_t, MAX_PATH> windows{};
        std::vector<char> fontBytes;
        if (GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size())))
        {
            std::ifstream file(std::filesystem::path(windows.data()) / L"Fonts" / L"segoeui.ttf", std::ios::binary | std::ios::ate);
            if (file && file.tellg() > 0 && file.tellg() < 16 * 1024 * 1024)
            {
                fontBytes.resize(static_cast<size_t>(file.tellg()));
                file.seekg(0);
                if (!file.read(fontBytes.data(), static_cast<std::streamsize>(fontBytes.size()))) fontBytes.clear();
            }
        }
        ImFontConfig font{};
        const float fontPixels = 24.0f * HudScale(static_cast<float>(width), static_cast<float>(height), config, false);
        font.SizePixels = fontPixels;
        if (!fontBytes.empty())
        {
            font.FontDataOwnedByAtlas = false;
            io.Fonts->AddFontFromMemoryTTF(fontBytes.data(), static_cast<int>(fontBytes.size()), fontPixels, &font);
        }
        else io.Fonts->AddFontDefault(&font);
        ImGui_ImplDX12_InitInfo info{};
        info.Device = device.Get();
        info.CommandQueue = queue.Get();
        info.NumFramesInFlight = static_cast<int>(frames.size());
        info.RTVFormat = compositor ? DXGI_FORMAT_R16G16B16A16_FLOAT : desc.Format;
        info.SrvDescriptorHeap = srvHeap.Get();
        info.UserData = this;
        info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* init, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
        {
            const auto* renderer = static_cast<Renderer*>(init->UserData);
            *cpu = renderer->srvHeap->GetCPUDescriptorHandleForHeapStart();
            *gpu = renderer->srvHeap->GetGPUDescriptorHandleForHeapStart();
        };
        info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) { };
        backend = ImGui_ImplDX12_Init(&info);
        if (!backend || !ImGui_ImplDX12_CreateDeviceObjects()) throw std::runtime_error("ImGui initialization failed");
        // The atlas has been uploaded; release its pointer to temporary input bytes.
        io.Fonts->ClearInputData();
    }
    bool Idle() const
    {
        return !fence || fence->GetCompletedValue() >= signalValue;
    }
    bool WaitIdle() const
    {
        if (Idle()) return true;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) return false;
        const HRESULT result = fence->SetEventOnCompletion(signalValue, event);
        const bool done = SUCCEEDED(result) && WaitForSingleObject(event, 2000) == WAIT_OBJECT_0;
        CloseHandle(event);
        return done && Idle();
    }
    ~Renderer()
    {
        // Owner guarantees idle or a removed device before destruction.
        if (context)
        {
            ContextScope scope(context);
            if (backend) ImGui_ImplDX12_Shutdown();
            ImGui::DestroyContext(context);
        }
    }
    bool Draw(const Config& config, bool details, bool hudVisible, bool lightVisible)
    {
        if (fault || !context) return false;
        if (FAILED(device->GetDeviceRemovedReason())) { fault = true; return false; }
        auto& frame = frames[draws % frames.size()];
        // ImGui's upload-buffer ring advances once per submitted draw, not per game frame.
        // Skip a HUD frame instead of waiting for a busy GPU on the game's render thread.
        if (fence->GetCompletedValue() < frame.fenceValue) return false;
        const UINT backBuffer = chain->GetCurrentBackBufferIndex();
        if (backBuffer >= buffers.size()) return false;
        TryRead(latest);
        const auto notice = notices.Update(latest, config, Clock::now());
        if (!hudVisible && !lightVisible && !notice) return false;
        ContextScope scope(context);
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        const auto now = Clock::now();
        io.DeltaTime = std::clamp(std::chrono::duration<float>(now - previousFrame).count(), 0.001f, 0.25f);
        previousFrame = now;
        ImGui_ImplDX12_NewFrame();
        ImGui::NewFrame();
        // Both passes share this frame/backbuffer. Light markers are independent
        // of the corner HUD; draw that panel afterwards to preserve readability.
        if (lightVisible)
        {
            auto drawingConfig = config;
            drawingConfig.visible = hudVisible;
            drawingConfig.details = details;
            drawingConfig.lightOverlayVisible = lightVisible;
            DrawLightOverlay(latest, drawingConfig);
        }
        if (hudVisible) DrawHud(latest, config, details);
        if (notice) DrawNotice(*notice, config, hudVisible, details);
        ImGui::Render();
        Check(frame.allocator->Reset());
        Check(frame.commands->Reset(frame.allocator.Get(), nullptr));
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffers[backBuffer].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        auto* commands = frame.commands.Get();
        auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(backBuffer) * rtvSize;
        if (compositor) compositor->BeginOverlay(commands);
        else
        {
            commands->ResourceBarrier(1, &barrier);
            commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        }
        ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
        commands->SetDescriptorHeaps(1, heaps);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commands);
        if (compositor)
        {
            if (!compositor->Composite(commands, buffers[backBuffer].Get(), rtv, outputMode, config.hdrPaperWhiteNits))
                throw std::runtime_error("HDR compositor rejected the frame");
        }
        else
        {
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            commands->ResourceBarrier(1, &barrier);
        }
        Check(commands->Close());
        ID3D12CommandList* lists[] = {commands};
        queue->ExecuteCommandLists(1, lists);
        ++signalValue;
        if (FAILED(queue->Signal(fence.Get(), signalValue)))
        {
            // Do not recycle or free resources when submission completion is unknown.
            signalValue = UINT64_MAX;
            fault = true;
            return false;
        }
        frame.fenceValue = signalValue;
        ++draws;
        return true;
    }
};

using CreateChain = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateHwnd = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using Resize = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using Resize1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);
using ColorSpace = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, DXGI_COLOR_SPACE_TYPE);
constexpr ULONGLONG GraphicsWrapperSettleMs = 500;
struct State
{
    struct PendingSwapchain
    {
        ComPtr<IUnknown> device;
        ComPtr<IDXGISwapChain> chain;
        HWND window{};
        ULONGLONG readyAt{};
    };
    std::mutex mutex, hooksMutex;
    Config config;
    std::unique_ptr<Renderer> renderer;
    ComPtr<IDXGISwapChain3> candidate;
    ComPtr<ID3D12CommandQueue> queue;
    HWND window{};
    bool visible{}, details{}, lightVisible{}, toggleDown{}, detailsDown{}, lightToggleDown{}, resizing{}, failed{};
    bool hideOccluded{}, occlusionToggleDown{};
    bool changingColorSpace{}, explicitColorSpace{};
    DXGI_COLOR_SPACE_TYPE outputColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    std::atomic<const char*> outputLabel{"D3D12 / waiting"};
    std::atomic<const char*> status{"Waiting for the game's D3D12 swapchain. Restart the game if attached late."};
    std::atomic<unsigned long long> rendered{};
    CreateChain createChain{}; CreateHwnd createHwnd{};
    Present present{}; Present1 present1{}; Resize resize{}; Resize1 resize1{}; ColorSpace colorSpace{};
    std::array<void*, 7> targets{};
    std::vector<PendingSwapchain> pendingSwapchains;
};
State& Data() { static auto* state = new State; return *state; }
void SetReadyStatus(State& state, hdr::OutputMode mode)
{
    if (mode == hdr::OutputMode::Hdr10)
    {
        state.outputLabel = "D3D12 / HDR10";
        state.status = "Overlay ready: D3D12 / HDR10 (PQ/BT.2020).";
    }
    else if (mode == hdr::OutputMode::ScRgb)
    {
        state.outputLabel = "D3D12 / scRGB";
        state.status = "Overlay ready: D3D12 / scRGB (linear FP16).";
    }
    else
    {
        state.outputLabel = "D3D12 / SDR";
        state.status = "Overlay ready: D3D12 / SDR.";
    }
}
template<class T> bool Hook(void* target, void* replacement, T& original, size_t slot)
{
    auto& state = Data();
    if (state.targets[slot]) return state.targets[slot] == target;
    if (MH_CreateHook(target, replacement, reinterpret_cast<void**>(&original)) != MH_OK) return false;
    if (MH_EnableHook(target) != MH_OK)
    {
        MH_RemoveHook(target);
        original = nullptr;
        return false;
    }
    state.targets[slot] = target;
    return true;
}
bool Draw(IDXGISwapChain* chain, UINT flags) noexcept
{
    if (flags & (DXGI_PRESENT_TEST | DXGI_PRESENT_DO_NOT_WAIT)) return false;
    auto& state = Data();
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock() || state.resizing || state.changingColorSpace || !SameObject(chain, state.candidate.Get())) return false;
    try
    {
        const bool foreground = GetForegroundWindow() == GetAncestor(state.window, GA_ROOT);
        const bool toggle = state.config.enabled && state.config.toggleKey && (GetAsyncKeyState(state.config.toggleKey) & 0x8000);
        const bool detail = state.config.enabled && state.config.detailsKey && (GetAsyncKeyState(state.config.detailsKey) & 0x8000);
        const bool lightToggle = state.config.lightOverlay && state.config.lightToggleKey &&
            (GetAsyncKeyState(state.config.lightToggleKey) & 0x8000);
        const bool occlusionToggle=(state.config.enabled||state.config.lightOverlay)&&state.config.occlusionToggleKey&&
            (GetAsyncKeyState(state.config.occlusionToggleKey)&0x8000);
        UpdateShortcutToggle(state.visible,state.toggleDown,state.config.toggleKey,toggle,foreground);
        UpdateShortcutToggle(state.details,state.detailsDown,state.config.detailsKey,detail,foreground);
        UpdateShortcutToggle(state.lightVisible,state.lightToggleDown,state.config.lightToggleKey,lightToggle,foreground);
        UpdateShortcutToggle(state.hideOccluded,state.occlusionToggleDown,state.config.occlusionToggleKey,occlusionToggle,foreground);
        const bool hudVisible = state.config.enabled && state.visible;
        const bool lightVisible = state.config.lightOverlay && state.lightVisible;
        if ((!hudVisible && !lightVisible && !state.config.notifications) || !state.renderer) return false;
        // Color-space changes are rebuilt on the worker after our GPU fence, not
        // in Present. Never draw an SDR/PQ pipeline into the newly selected space.
        if (hdr::ResolveOutput(state.renderer->outputFormat, state.outputColorSpace) != state.renderer->outputMode) return false;
        auto drawingConfig=state.config;drawingConfig.hideOccluded=state.hideOccluded;
        if (state.renderer->Draw(drawingConfig, state.details, hudVisible, lightVisible)) { ++state.rendered; return true; }
    }
    catch (...)
    {
        if (state.renderer) state.renderer->fault = true;
        state.status = "Overlay drawing disabled after a graphics error; telemetry continues.";
    }
    return false;
}
thread_local bool presenting = false, resizing = false;
struct RecursionScope { bool& flag; explicit RecursionScope(bool& value) : flag(value) { flag = true; } ~RecursionScope() { flag = false; } };
HRESULT STDMETHODCALLTYPE OnPresent(IDXGISwapChain* chain, UINT sync, UINT flags)
{
    if (presenting) return Data().present(chain, sync, flags);
    RecursionScope scope(presenting);
    Draw(chain, flags);
    return Data().present(chain, sync, flags);
}
HRESULT STDMETHODCALLTYPE OnPresent1(IDXGISwapChain1* chain, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters)
{
    if (presenting) return Data().present1(chain, sync, flags, parameters);
    RecursionScope scope(presenting);
    const bool drawn = Draw(chain, flags);
    // A HUD touches pixels outside the game's dirty rectangles. Request a full present.
    const DXGI_PRESENT_PARAMETERS full{};
    return Data().present1(chain, sync, flags, drawn ? &full : parameters);
}
bool BeforeResize(IDXGISwapChain* chain) noexcept
{
    auto& state = Data();
    try
    {
        std::lock_guard lock(state.mutex);
        if (!SameObject(chain, state.candidate.Get())) return false;
        state.resizing = true;
        if (state.renderer && !state.renderer->WaitIdle())
        {
            state.status = "GPU timeout during resize; HUD disabled. Restart recommended.";
            state.renderer->fault = true;
            // Keep potentially in-flight resources alive. DXGI may reject this resize,
            // which is preferable to releasing memory still used by the GPU.
            return true;
        }
        state.renderer.reset();
        state.failed = false;
        return true;
    }
    catch (...) { return false; }
}
void AfterResize(bool tracked) noexcept
{
    if (!tracked) return;
    auto& state = Data();
    try
    {
        std::lock_guard lock(state.mutex);
        state.resizing = false;
        if (!state.explicitColorSpace && state.candidate)
        {
            DXGI_SWAP_CHAIN_DESC1 desc{};
            if (SUCCEEDED(state.candidate->GetDesc1(&desc))) state.outputColorSpace = hdr::DefaultColorSpace(desc.Format);
        }
    }
    catch (...) { }
}
HRESULT STDMETHODCALLTYPE OnResize(IDXGISwapChain* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    if (resizing) return Data().resize(chain, count, width, height, format, flags);
    RecursionScope scope(resizing);
    const bool tracked = BeforeResize(chain);
    const HRESULT result = Data().resize(chain, count, width, height, format, flags);
    AfterResize(tracked);
    return result;
}
HRESULT STDMETHODCALLTYPE OnResize1(IDXGISwapChain3* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format,
    UINT flags, const UINT* masks, IUnknown* const* queues)
{
    if (resizing) return Data().resize1(chain, count, width, height, format, flags, masks, queues);
    RecursionScope scope(resizing);
    const bool tracked = BeforeResize(chain);
    const HRESULT result = Data().resize1(chain, count, width, height, format, flags, masks, queues);
    if (tracked && SUCCEEDED(result) && queues)
    {
        // Per-buffer / multi-queue presentation is not supported; never guess a queue.
        auto& state = Data();
        std::lock_guard lock(state.mutex);
        DXGI_SWAP_CHAIN_DESC1 desc{};
        bool valid = SUCCEEDED(chain->GetDesc1(&desc));
        for (UINT i = 0; valid && i < desc.BufferCount; ++i) valid = SameObject(queues[i], state.queue.Get());
        if (!valid) { state.failed = true; state.status = "Overlay disabled: presentation queue changed."; }
    }
    AfterResize(tracked);
    return result;
}
HRESULT STDMETHODCALLTYPE OnColorSpace(IDXGISwapChain3* chain, DXGI_COLOR_SPACE_TYPE color)
{
    auto& state = Data();
    bool tracked{};
    {
        std::lock_guard lock(state.mutex);
        tracked = SameObject(chain, state.candidate.Get());
        if (tracked) state.changingColorSpace = true;
    }
    const HRESULT result = Data().colorSpace(chain, color);
    if (tracked)
    {
        std::lock_guard lock(state.mutex);
        if (SameObject(chain, state.candidate.Get()))
        {
            state.changingColorSpace = false;
            if (SUCCEEDED(result))
            {
                state.explicitColorSpace = true;
                if (state.outputColorSpace != color)
                {
                    state.outputColorSpace = color;
                    state.failed = false;
                    state.status = "Display color space changed; adapting the overlay.";
                }
            }
        }
    }
    return result;
}
void Track(IUnknown* suppliedDevice, IDXGISwapChain* chain) noexcept
{
    try
    {
        if (!suppliedDevice || !chain) return;
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<IDXGISwapChain3> chain3;
        if (FAILED(suppliedDevice->QueryInterface(IID_PPV_ARGS(&queue))) || FAILED(chain->QueryInterface(IID_PPV_ARGS(&chain3)))) return;
        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(chain->GetDesc(&desc)) || !desc.OutputWindow) return;
        DWORD process{};
        GetWindowThreadProcessId(desc.OutputWindow, &process);
        RECT client{};
        if (process != GetCurrentProcessId() || !GetClientRect(desc.OutputWindow, &client) || client.right < 320 || client.bottom < 200) return;
        auto& state = Data();
        {
            std::lock_guard lock(state.hooksMutex);
            auto** table = *reinterpret_cast<void***>(chain3.Get());
            if (!Hook(table[8], reinterpret_cast<void*>(&OnPresent), state.present, 2) ||
                !Hook(table[22], reinterpret_cast<void*>(&OnPresent1), state.present1, 3) ||
                !Hook(table[13], reinterpret_cast<void*>(&OnResize), state.resize, 4) ||
                !Hook(table[39], reinterpret_cast<void*>(&OnResize1), state.resize1, 5) ||
                !Hook(table[38], reinterpret_cast<void*>(&OnColorSpace), state.colorSpace, 6))
            {
                state.status = "Unsupported swapchain hook implementation; telemetry continues without HUD.";
                return;
            }
        }
        std::lock_guard lock(state.mutex);
        if (SameObject(state.candidate.Get(), chain3.Get())) return;
        if (state.renderer && !state.renderer->WaitIdle()) return;
        state.renderer.reset();
        state.candidate = chain3;
        state.queue = queue;
        render::presentation_queue::Publish(queue.Get());
        state.window = desc.OutputWindow;
        state.failed = false;
        state.changingColorSpace = state.explicitColorSpace = false;
        // Floating-point swapchains default to linear scRGB, not gamma SDR.
        // Follow the game's successful SetColorSpace1 calls after creation.
        state.outputColorSpace = hdr::DefaultColorSpace(desc.BufferDesc.Format);
        state.outputLabel = "D3D12 / initializing";
        state.status = "Game D3D12 swapchain and its presentation queue captured; initializing HUD.";
    }
    catch (...) { Data().status = "Swapchain tracking failed; telemetry continues."; }
}
void BeforeSwapchainCreate(HWND window) noexcept
{
    if (!window) return;
    try
    {
        auto& state = Data();
        std::lock_guard lock(state.mutex);
        state.pendingSwapchains.erase(std::remove_if(state.pendingSwapchains.begin(), state.pendingSwapchains.end(),
            [window](const auto& pending) { return pending.window == window; }), state.pendingSwapchains.end());
        if (state.window != window) return;
        if (state.renderer && !state.renderer->WaitIdle())
        {
            state.status = "GPU timeout before swapchain replacement; HUD disabled. Restart recommended.";
            state.renderer->fault = true;
            return;
        }
        // A flip-model HWND can own only one swapchain. Release every reference
        // held by the overlay before DXGI or a graphics wrapper replaces it.
        state.renderer.reset();
        state.candidate.Reset();
        state.queue.Reset();
        state.window = nullptr;
        state.resizing = false;
        state.failed = false;
        state.changingColorSpace = state.explicitColorSpace = false;
        state.outputLabel = "D3D12 / waiting";
        state.status = "Existing HUD swapchain released for replacement.";
    }
    catch (...) { Data().status = "Swapchain replacement preparation failed; telemetry continues."; }
}
void QueueTrack(IUnknown* suppliedDevice, IDXGISwapChain* chain, HWND window) noexcept
{
    try
    {
        if (!suppliedDevice || !chain) return;
        auto& state = Data();
        std::lock_guard lock(state.mutex);
        // This callback runs inside the real DXGI factory call. NVIDIA Streamline
        // still has to associate the returned swapchain with its command queue
        // after DXGI returns. Patching the swapchain vtable here races that work
        // and can make Streamline fail its link with E_ACCESSDENIED. Retain the
        // COM objects only; the worker installs presentation hooks after the
        // wrapper stack has completed. BeforeSwapchainCreate drops this pending
        // reference if a wrapper replaces the chain for the same HWND first.
        if (state.pendingSwapchains.size() == 8) state.pendingSwapchains.erase(state.pendingSwapchains.begin());
        state.pendingSwapchains.push_back({suppliedDevice, chain, window, GetTickCount64() + GraphicsWrapperSettleMs});
        state.status = "Game D3D12 swapchain created; waiting for graphics wrappers to finish.";
    }
    catch (...) { Data().status = "Swapchain handoff failed; telemetry continues."; }
}
void ProcessPendingSwapchains() noexcept
{
    try
    {
        std::vector<State::PendingSwapchain> ready;
        auto& state = Data();
        {
            std::lock_guard lock(state.mutex);
            const ULONGLONG now = GetTickCount64();
            auto firstPending = std::stable_partition(state.pendingSwapchains.begin(), state.pendingSwapchains.end(),
                [now](const auto& pending) { return pending.readyAt <= now; });
            ready.insert(ready.end(), std::make_move_iterator(state.pendingSwapchains.begin()),
                std::make_move_iterator(firstPending));
            state.pendingSwapchains.erase(state.pendingSwapchains.begin(), firstPending);
        }
        for (const auto& pending : ready) Track(pending.device.Get(), pending.chain.Get());
    }
    catch (...) { Data().status = "Deferred swapchain tracking failed; telemetry continues."; }
}
HRESULT STDMETHODCALLTYPE OnCreateChain(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** chain)
{
    const HWND window = desc ? desc->OutputWindow : nullptr;
    BeforeSwapchainCreate(window);
    const HRESULT result = Data().createChain(factory, device, desc, chain);
    if (SUCCEEDED(result) && chain) QueueTrack(device, *chain, window);
    return result;
}
HRESULT STDMETHODCALLTYPE OnCreateHwnd(IDXGIFactory2* factory, IUnknown* device, HWND window, const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* output, IDXGISwapChain1** chain)
{
    BeforeSwapchainCreate(window);
    const HRESULT result = Data().createHwnd(factory, device, window, desc, fullscreen, output, chain);
    if (SUCCEEDED(result) && chain) QueueTrack(device, *chain, window);
    return result;
}
}
bool InstallGraphics(const Config& config) noexcept
{
    if (!config.enabled && !config.notifications && !config.lightOverlay) return false;
    try
    {
        auto& state = Data();
        state.config = config;
        state.visible = config.visible;
        state.details = config.details;
        state.lightVisible = config.lightOverlayVisible;
        state.hideOccluded = config.hideOccluded;
        const auto hookInit = MH_Initialize();
        if (hookInit != MH_OK && hookInit != MH_ERROR_ALREADY_INITIALIZED) return false;
        ComPtr<IDXGIFactory2> factory;
        Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        auto** table = *reinterpret_cast<void***>(factory.Get());
        return Hook(table[10], reinterpret_cast<void*>(&OnCreateChain), state.createChain, 0) &&
            Hook(table[15], reinterpret_cast<void*>(&OnCreateHwnd), state.createHwnd, 1);
    }
    catch (...) { return false; }
}
void MaintainGraphics() noexcept
{
    auto& state = Data();
    try
    {
        ProcessPendingSwapchains();
        std::lock_guard lock(state.mutex);
        if (!state.candidate || state.resizing || state.changingColorSpace || state.failed) return;
        DXGI_SWAP_CHAIN_DESC1 desc{};
        if (FAILED(state.candidate->GetDesc1(&desc))) return;
        const auto mode = hdr::ResolveOutput(desc.Format, state.outputColorSpace);
        if (mode == hdr::OutputMode::Unsupported)
        {
            state.outputLabel = "D3D12 / unsupported color space";
            state.status = "Unsupported display format/color space; overlays paused, telemetry continues.";
            return;
        }
        if (state.renderer)
        {
            if (state.renderer->outputMode == mode && state.renderer->outputFormat == desc.Format)
            {
                if (!state.renderer->fault) SetReadyStatus(state, mode);
                return;
            }
            // Existing GPU work owns these resources until its fence completes.
            // Do not block Present or free in-flight uploads when HDR is toggled.
            if (!state.renderer->Idle()) return;
            state.renderer.reset();
        }
        auto renderer = std::make_unique<Renderer>();
        try
        {
            renderer->Initialize(state.candidate.Get(), state.queue.Get(), state.config, state.outputColorSpace);
            state.renderer = std::move(renderer);
            SetReadyStatus(state, mode);
        }
        catch (...)
        {
            state.failed = true;
            state.status = "Overlay graphics initialization failed; see overlay log and D3D12 output settings. Telemetry continues.";
        }
    }
    catch (...) { state.status = "Overlay maintenance stopped after an error."; }
}
const char* GraphicsStatus() noexcept { return Data().status.load(); }
const char* GraphicsOutputLabel() noexcept { return Data().outputLabel.load(); }
unsigned long long RenderedFrames() noexcept { return Data().rendered.load(); }
void SetVisibleForTest(bool visible) noexcept
{
    auto& state = Data();
    std::lock_guard lock(state.mutex);
    state.visible = visible;
}
void SetLightVisibleForTest(bool visible) noexcept
{
    auto& state = Data();
    std::lock_guard lock(state.mutex);
    state.lightVisible = visible;
}
void SetHideOccludedForTest(bool hidden) noexcept
{
    auto& state=Data();
    std::lock_guard lock(state.mutex);
    state.hideOccluded=hidden;
}
}

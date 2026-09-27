#include "render_capture.h"
#include "manylights_pair.h"
#include "submission_observer.h"
#include "ambient_probe.h"
#include "render_bridge.h"
#include "sky_bridge.h"
#include "device_identity.h"
#include "native_contract.generated.h"
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

namespace cdt::render
{
void InitializeCaptureForTest(uint64_t base);
const char* CapturePhaseForTest();
void CaptureFilter(uint64_t outer, uint64_t command, uint64_t counterOuter, uint64_t owner);
bool InitializeAmbientForTest(uint64_t base, const wchar_t* directory);
uint32_t AmbientSamplesForTest();
void CaptureAmbient(uint64_t sky, uint64_t command, uint64_t path);
void EnableSkyForTest();
bool InitializePairForTest(const wchar_t* directory);
bool InitializeUpstreamForTest();
}
// The smoke executable links only memory/log support from imported research.
namespace cdt::instruments { bool OwnsCodeAddress(uint64_t) { return false; } }

namespace
{
namespace contract = cdt::native_contract;
using Microsoft::WRL::ComPtr;
void Check(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; ExitProcess(1); } }
void Hr(HRESULT value, const char* message) { if (FAILED(value)) { std::cerr << std::hex << value << ' '; Check(false,message); } }
class ReShadeLikeDeviceProxy final : public IUnknown
{
public:
    ReShadeLikeDeviceProxy(ID3D12Device* original, bool malformed = false)
        : original_(original), malformed_(malformed) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown)
        {
            *out = this;
            AddRef();
            return S_OK;
        }
        if (iid == cdt::render::device_identity::ReShadeUnwrappedObject)
        {
            if (malformed_) { *out = this; AddRef(); }
            else { *out = original_; original_->AddRef(); }
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs_; }

private:
    ID3D12Device* original_;
    bool malformed_;
    std::atomic<ULONG> refs_{1};
};

void CheckReShadeDeviceIdentity(ID3D12Device* device)
{
    using cdt::render::device_identity::CanonicalDeviceIdentity;
    using cdt::render::device_identity::ReShadeUnwrappedObject;
    ComPtr<ID3D12Device> original;
    const HRESULT unwrap = device->QueryInterface(ReShadeUnwrappedObject,
        reinterpret_cast<void**>(original.GetAddressOf()));
    if (unwrap == E_NOINTERFACE) original = device;
    else Hr(unwrap, "unwrap test device");

    ComPtr<IUnknown> nativeIdentity;
    Hr(CanonicalDeviceIdentity(device, nativeIdentity.GetAddressOf()), "canonical native device");
    ReShadeLikeDeviceProxy proxy(original.Get());
    ComPtr<IUnknown> proxyIdentity;
    Hr(proxy.QueryInterface(IID_PPV_ARGS(&proxyIdentity)), "proxy identity");
    Check(proxyIdentity.Get() != nativeIdentity.Get(), "proxy did not have a distinct COM identity");
    ComPtr<IUnknown> normalized;
    Hr(CanonicalDeviceIdentity(&proxy, normalized.GetAddressOf()), "canonical proxy device");
    Check(normalized.Get() == nativeIdentity.Get(), "same native device behind proxy was rejected");

    ReShadeLikeDeviceProxy malformed(original.Get(), true);
    IUnknown* refused{};
    Check(FAILED(CanonicalDeviceIdentity(&malformed, &refused)) && !refused,
        "malformed device proxy was accepted");
}
std::atomic<unsigned> observedBegins{},observedEnds{},invalidSubmissions{};
void ObserveSubmission(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists,bool after)
{
    if(!queue||!count||!lists)++invalidSubmissions;
    if(after)++observedEnds;else ++observedBegins;
}
void CheckSubmissions()
{
    Check(observedBegins>0&&observedBegins==observedEnds&&invalidSubmissions==0,
        "passive submission observer missing/unbalanced or arguments changed");
    cdt::render::submissionObserver=nullptr;
}
void SignalCaptureReady()
{
    const auto name=L"Local\\CrimsonDesertTelemetry.CaptureReady."+std::to_wstring(GetCurrentProcessId());
    HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str());
    Check(event!=nullptr,"open native playable-world gate");
    Check(SetEvent(event)!=FALSE,"signal native playable-world gate");
    CloseHandle(event);
    cdt::render::PollCapture();
}
template<class T, size_t N> void Put(std::array<uint8_t,N>& data, size_t offset, T value)
{ memcpy(data.data()+offset,&value,sizeof(value)); }

void CheckCapture(bool value, const cdt::render::Mapping* bridge, ID3D12Device* device, const char* message)
{
    if (value) return;
    std::cerr << message << ": phase=" << cdt::render::CapturePhaseForTest()
        << ", state=" << static_cast<uint32_t>(bridge->header.state)
        << ", sequence=" << bridge->header.sampleSequence << ", frame=" << bridge->header.frameNumber
        << ", capture-error=0x" << std::hex << cdt::render::CaptureFailureCode()
        << ", bridge-error=0x" << bridge->header.error
        << ", device-removed=0x" << device->GetDeviceRemovedReason() << std::dec << '\n';
    ExitProcess(1);
}

void WaitForSample(const cdt::render::Mapping* bridge, ID3D12Device* device,
    uint64_t sequence, uint32_t frame, const char* message)
{
    // Let the production five-second capture timeout report a fault first.
    // A fixed iteration count depended on the machine's Sleep granularity.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(6);
    while (bridge->header.sampleSequence<sequence && std::chrono::steady_clock::now()<deadline)
    {
        cdt::render::PollCapture();
        if (cdt::render::CaptureFailureCode()!=0) break;
        if (bridge->header.sampleSequence<sequence) Sleep(5);
    }
    CheckCapture(bridge->header.state==cdt::render::Status::Active &&
        bridge->header.sampleSequence==sequence && bridge->header.frameNumber==frame, bridge, device, message);
}
}
int main(int argc, char** argv)
{
    using namespace cdt::render;
    submissionObserver=ObserveSubmission;
    const bool rejectCounterDevice=argc==2 && std::string(argv[1])=="--counter-device";
    const bool reshadeComputeTest=argc==2 && std::string(argv[1])=="--compute-reshade";
    const bool computeTest=reshadeComputeTest || (argc==2 && std::string(argv[1])=="--compute");
    const bool ambientTest=argc==2 && std::string(argv[1])=="--ambient";
    const bool mixedTest=argc==2 && (std::string(argv[1])=="--sky-shared" || std::string(argv[1])=="--sky-first");
    const bool skyFirst=argc==2 && std::string(argv[1])=="--sky-first";
    const bool pairTest=argc==2 && std::string(argv[1])=="--manylights-pair";
    const bool upstreamTest=argc==2 && std::string(argv[1])=="--upstream";
    ComPtr<ID3D12Debug> debug;
    if((pairTest||upstreamTest) && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory; Hr(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"factory");
    ComPtr<IDXGIAdapter> warp; Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"WARP");
    ComPtr<ID3D12Device> device; Hr(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
    CheckReShadeDeviceIdentity(device.Get());
    const auto captureListType=computeTest ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_DIRECT;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type=captureListType;
    ComPtr<ID3D12CommandQueue> queue; Hr(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)),"queue");
    ComPtr<ID3D12CommandAllocator> allocator, unrelatedAllocator;
    Hr(device->CreateCommandAllocator(captureListType,IID_PPV_ARGS(&allocator)),"allocator");
    Hr(device->CreateCommandAllocator(captureListType,IID_PPV_ARGS(&unrelatedAllocator)),"other allocator");
    ComPtr<ID3D12GraphicsCommandList> list, unrelated;
    Hr(device->CreateCommandList(0,captureListType,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
    Hr(device->CreateCommandList(0,captureListType,unrelatedAllocator.Get(),nullptr,IID_PPV_ARGS(&unrelated)),"other list");
    Hr(unrelated->Close(),"other close");
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width=ambientTest ? AmbientBytes : LightBytes; desc.Height=1;
    desc.DepthOrArraySize=1; desc.MipLevels=1; desc.SampleDesc.Count=1;
    desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT; heap.CreationNodeMask=heap.VisibleNodeMask=1;
    ComPtr<ID3D12Resource> source, source2, upload, counter, counter2, counterUpload, shortCounter;
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&source)),"source");
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&source2)),"second source");
    if (reshadeComputeTest)
    {
        ComPtr<ID3D12Device> unwrapped;
        Hr(device->QueryInterface(cdt::render::device_identity::ReShadeUnwrappedObject,
            reinterpret_cast<void**>(unwrapped.GetAddressOf())), "ReShade proxy missing");
        ComPtr<IUnknown> resourceDevice, listDevice;
        Hr(source->GetDevice(IID_PPV_ARGS(&resourceDevice)), "ReShade resource device");
        Hr(list->GetDevice(IID_PPV_ARGS(&listDevice)), "ReShade compute list device");
        Check(resourceDevice.Get() != listDevice.Get(),
            "ReShade proxy/native device identity contrast was not reproduced");
    }
    desc.Width=CounterBytes*2;
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&counter)),"counter");
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&counter2)),"second counter");
    desc.Width=CounterBytes/2;
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&shortCounter)),"undersized counter");
    heap.Type=D3D12_HEAP_TYPE_UPLOAD; desc.Flags=D3D12_RESOURCE_FLAG_NONE;
    desc.Width=LightBytes;
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"upload");
    desc.Width=CounterBytes*2;
    Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&counterUpload)),"counter upload");
    void* mapped{}; const D3D12_RANGE noReads{0,0}; Hr(upload->Map(0,&noReads,&mapped),"upload map");
    memset(mapped,0,LightBytes);
    constexpr std::array<float,8> light{1,2,3,3.14159265f,0.9f,0.3f,0.1f,0.005f};
    memcpy(mapped,light.data(),sizeof(light)); upload->Unmap(0,nullptr);
    std::array<uint32_t,CounterBytes/sizeof(uint32_t)> counterData{}, counterData2{};
    for (uint32_t i=0;i<counterData.size();++i) { counterData[i]=i*17+3; counterData2[i]=i*23+7; }
    counterData[1]=counterData2[1]=1; // One valid light; capture transports bytes without interpreting the count.
    Hr(counterUpload->Map(0,&noReads,&mapped),"counter upload map");
    memcpy(mapped,counterData.data(),CounterBytes);
    memcpy(static_cast<uint8_t*>(mapped)+CounterBytes,counterData2.data(),CounterBytes);
    counterUpload->Unmap(0,nullptr);
    list->CopyBufferRegion(source.Get(),0,upload.Get(),0,ambientTest ? AmbientBytes : LightBytes);
    list->CopyBufferRegion(source2.Get(),0,upload.Get(),0,ambientTest ? AmbientBytes : LightBytes);
    list->CopyBufferRegion(counter.Get(),0,counterUpload.Get(),0,CounterBytes);
    list->CopyBufferRegion(counter2.Get(),0,counterUpload.Get(),CounterBytes,CounterBytes);
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=source.Get(); barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST; barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    list->ResourceBarrier(1,&barrier);
    barrier.Transition.pResource=source2.Get(); list->ResourceBarrier(1,&barrier);
    barrier.Transition.pResource=counter.Get(); list->ResourceBarrier(1,&barrier);
    barrier.Transition.pResource=counter2.Get(); list->ResourceBarrier(1,&barrier);

    std::array<uint8_t,SceneBytes> constants{};
    Put(constants,0xAC0,6360000.0f); Put(constants,0x20,uint32_t{100});
    Put(constants,0x30,std::array<float,4>{3840,2160,1.0f/3840,1.0f/2160});
    Put(constants,0x80,std::array<float,4>{-10528,611,-4354,0}); Put(constants,0x90,std::array<float,4>{0,0,1,0});
    std::array<uint8_t,0x500> root{};
    Put(root,0x428,reinterpret_cast<uint64_t>(constants.data()));
    const auto scenePage=contract::SceneGlobalRva&~uint64_t{0xFFF};
    const auto fakeModuleBytes=(contract::SceneGlobalRva+sizeof(uint64_t)+0xFFF)&~uint64_t{0xFFF};
    auto* fakeBase=static_cast<uint8_t*>(VirtualAlloc(nullptr,static_cast<SIZE_T>(fakeModuleBytes),MEM_RESERVE,PAGE_NOACCESS));
    Check(fakeBase!=nullptr,"reserve fake module");
    Check(VirtualAlloc(fakeBase+scenePage,0x1000,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"commit fake root");
    const auto rootPtr=reinterpret_cast<uint64_t>(root.data()); memcpy(fakeBase+contract::SceneGlobalRva,&rootPtr,8);
    std::array<uint8_t,0x200> inner{}; std::array<uint8_t,0x38> outer{};
    Put(inner,0xC0,RecordStride); Put(inner,0xC4,RecordCount); Put(inner,0x168,reinterpret_cast<uint64_t>(source.Get()));
    Put(outer,0x30,reinterpret_cast<uint64_t>(inner.data()));
    std::array<uint8_t,0x200> counterInner{}; std::array<uint8_t,0x38> counterOuter{};
    Put(counterInner,0xC0,uint32_t{4}); Put(counterInner,0xC4,uint32_t{128});
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(counter.Get()));
    Put(counterOuter,0x30,reinterpret_cast<uint64_t>(counterInner.data()));
    std::array<uint8_t,0x900> owner{};
    std::array<uint8_t,16> holder{}; std::array<uint8_t,0x808> command{};
    Put(holder,8,reinterpret_cast<uint64_t>(list.Get())); Put(command,0x800,reinterpret_cast<uint64_t>(holder.data()));
    Check(OpenBridge(upstreamTest),"bridge");
    const auto mappingName=L"Local\\CrimsonDesertTelemetry.Render."+std::to_wstring(GetCurrentProcessId());
    HANDLE mapHandle=OpenFileMappingW(FILE_MAP_READ,FALSE,mappingName.c_str());
    const auto* bridge=static_cast<const Mapping*>(MapViewOfFile(mapHandle,FILE_MAP_READ,0,0,
        upstreamTest ? InputMappingBytes : MappingBytes));
    Check(bridge!=nullptr,"read bridge");
    const auto validateDebugLayer=[&]
    {
        ComPtr<ID3D12InfoQueue> info;
        if(!debug || FAILED(device.As(&info))) return;
        for(UINT64 i=0;i<info->GetNumStoredMessages();++i)
        {
            SIZE_T bytes{}; info->GetMessage(i,nullptr,&bytes); std::vector<uint8_t> data(bytes);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data()); Hr(info->GetMessage(i,message,&bytes),"debug message");
            if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR)
            { std::cerr<<message->pDescription<<'\n'; Check(false,"D3D12 validation error"); }
        }
    };
    if(upstreamTest)
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};
        Hr(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12,&options,sizeof(options)),"enhanced barriers query");
        Check(options.EnhancedBarriersSupported,"enhanced barriers required for upstream test");
        Check(bridge->header.version==InputVersion && bridge->header.totalBytes==InputMappingBytes &&
            bridge->header.inputBytes==InputBytes && bridge->header.inputState==InputState::Unavailable,"input bridge ABI");
        const auto directory=std::filesystem::absolute(L"upstream-smoke-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Check(std::filesystem::create_directory(directory),"upstream directory");
        Check(InitializePairForTest(directory.c_str()),"pair control beside continuous input");
        Check(InitializeUpstreamForTest(),"upstream control");
        Check(!InitializeUpstreamForTest(),"duplicate upstream setup accepted");
        std::array<uint8_t,0x200> inputInner{}; std::array<uint8_t,0x38> inputOuter{};
        Put(inputInner,0xC0,RecordStride); Put(inputInner,0xC4,RecordCount);
        Put(inputInner,0x168,reinterpret_cast<uint64_t>(source2.Get()));
        Put(inputOuter,0x30,reinterpret_cast<uint64_t>(inputInner.data()));
        Put(owner,PairInputOwnerOffset,reinterpret_cast<uint64_t>(inputOuter.data()));
        // A distinctive input tail proves the full capacity is transported and that
        // input bytes never enter the filtered output block.
        barrier.Transition.pResource=source2.Get();
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(1,&barrier);
        list->CopyBufferRegion(source2.Get(),LightBytes-RecordStride,counterUpload.Get(),0,RecordStride);
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1,&barrier);
        InitializeCaptureForTest(reinterpret_cast<uint64_t>(fakeBase));
        const auto capture=[&] { CaptureFilter(reinterpret_cast<uint64_t>(outer.data()),reinterpret_cast<uint64_t>(command.data()),
            reinterpret_cast<uint64_t>(counterOuter.data()),reinterpret_cast<uint64_t>(owner.data())); };
        const auto eventName=L"Local\\CrimsonDesertTelemetry.ManyLightsPair."+std::to_wstring(GetCurrentProcessId());
        HANDLE request=OpenEventW(EVENT_MODIFY_STATE,FALSE,eventName.c_str()); Check(request!=nullptr,"pair event");
        const auto files=[&] { size_t count=0; for(const auto& entry:std::filesystem::directory_iterator(directory)) { (void)entry; ++count; } return count; };
        SignalCaptureReady(); capture(); PollCapture();
        CheckCapture(std::strcmp(CapturePhaseForTest(),"ready (no copy recorded)")==0,bridge,device.Get(),"upstream prepare");
        ComPtr<ID3D12Fence> gate; Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"upstream gate");
        ID3D12CommandList* lists[]{list.Get()};
        // Sample 1 has an invalid input layout and must keep only the filtered output.
        // It also executes the legacy-state test setup: buffers decay to COMMON at
        // the submission boundary, as the game's enhanced-barrier input always is.
        // Sample 2 recovers; sample 3 additionally serves one explicit pair save.
        for(uint32_t n=0;n<3;++n)
        {
            if(n>0) { Hr(allocator->Reset(),"upstream allocator reset"); Hr(list->Reset(allocator.Get(),nullptr),"upstream list reset"); }
            Put(constants,0x20,uint32_t{100+n});
            Put(inputInner,0xC0,n==0 ? uint32_t{4} : RecordStride);
            if(n==2) { Check(SetEvent(request)!=FALSE,"pair request"); PollCapture(); }
            capture();
            Hr(list->Close(),"upstream close"); Hr(queue->Wait(gate.Get(),n+1),"upstream GPU block");
            queue->ExecuteCommandLists(1,lists); PollCapture();
            Check(bridge->header.sampleSequence==n,"upstream published before fence");
            Hr(gate->Signal(n+1),"upstream GPU release");
            WaitForSample(bridge,device.Get(),n+1,100+n,"upstream sample failed");
            Check(memcmp(bridge->lights,light.data(),sizeof(light))==0 && bridge->lights[LightBytes-RecordStride]==0 &&
                memcmp(bridge->counters,counterData.data(),CounterBytes)==0,"input polluted filtered output/counter");
            if(n==0)
            {
                Check(bridge->header.inputState==InputState::Unavailable && bridge->header.inputResource==0,
                    "invalid input layout was published as paired");
                continue;
            }
            const auto* block=InputBlock(bridge);
            Check(bridge->header.inputState==InputState::Paired &&
                bridge->header.inputResource==reinterpret_cast<uint64_t>(source2.Get()) &&
                memcmp(block,light.data(),sizeof(light))==0 &&
                memcmp(block+LightBytes-RecordStride,counterData.data(),RecordStride)==0,"paired input bytes/provenance");
        }
        Check(files()==1,"pair request beside continuous input did not save exactly one file");
        CheckSubmissions(); StopCapture(); CloseHandle(request);
        validateDebugLayer();
        Check(device->GetDeviceRemovedReason()==S_OK,"upstream device lost");
        UnmapViewOfFile(bridge); CloseHandle(mapHandle); VirtualFree(fakeBase,0,MEM_RELEASE);
        std::cout<<"ManyLights INPUT stream: v4 bridge, every-sample paired input on the same list/fence, invalid layout isolated to the input, recovery, pair save beside continuous input and D3D12 validation passed.\n";
        return 0;
    }
    if(pairTest)
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};
        Hr(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12,&options,sizeof(options)),"enhanced barriers query");
        Check(options.EnhancedBarriersSupported,"enhanced barriers required for pair test");
        const auto directory=std::filesystem::absolute(L"manylights-pair-smoke-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Check(std::filesystem::create_directory(directory),"pair directory");
        Check(InitializePairForTest(directory.c_str()),"pair control");
        Check(!InitializePairForTest(directory.c_str()),"duplicate pair setup accepted");
        std::array<uint8_t,0x200> inputInner{}; std::array<uint8_t,0x38> inputOuter{};
        Put(inputInner,0xC0,RecordStride); Put(inputInner,0xC4,RecordCount);
        Put(inputInner,0x168,reinterpret_cast<uint64_t>(source2.Get()));
        Put(inputOuter,0x30,reinterpret_cast<uint64_t>(inputInner.data()));
        Put(owner,PairInputOwnerOffset,reinterpret_cast<uint64_t>(inputOuter.data()));
        // A distinctive input-only tail must survive the copy without entering
        // the public output. No claim that this synthetic payload is a light.
        barrier.Transition.pResource=source2.Get();
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(1,&barrier);
        list->CopyBufferRegion(source2.Get(),LightBytes-RecordStride,counterUpload.Get(),0,RecordStride);
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1,&barrier);
        InitializeCaptureForTest(reinterpret_cast<uint64_t>(fakeBase));
        const auto capture=[&] { CaptureFilter(reinterpret_cast<uint64_t>(outer.data()),reinterpret_cast<uint64_t>(command.data()),
            reinterpret_cast<uint64_t>(counterOuter.data()),reinterpret_cast<uint64_t>(owner.data())); };
        const auto eventName=L"Local\\CrimsonDesertTelemetry.ManyLightsPair."+std::to_wstring(GetCurrentProcessId());
        HANDLE request=OpenEventW(EVENT_MODIFY_STATE,FALSE,eventName.c_str()); Check(request!=nullptr,"pair event");
        const auto start=[&] { Check(SetEvent(request)!=FALSE,"pair request"); PollCapture(); };
        const auto files=[&] { std::vector<std::filesystem::path> result;
            for(const auto& entry:std::filesystem::directory_iterator(directory)) result.push_back(entry.path()); return result; };
        start(); capture(); PollCapture();
        Check(files().empty() && std::strcmp(CapturePhaseForTest(),"discover (no source recorded)")==0,"pair captured while loading");
        SignalCaptureReady(); capture(); PollCapture();
        Check(files().empty(),"pair wrote without request");
        // Invalid input layout consumes the request, not the normal stream.
        Put(inputInner,0xC0,uint32_t{4}); start(); capture(); Hr(list->Close(),"invalid input close");
        ID3D12CommandList* lists[]{list.Get()}; queue->ExecuteCommandLists(1,lists);
        WaitForSample(bridge,device.Get(),1,100,"invalid pair broke raw output"); Check(files().empty(),"invalid input saved");
        Put(inputInner,0xC0,RecordStride);
        ComPtr<ID3D12Fence> gate; Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"pair gate");
        for(uint32_t n=0;n<2;++n)
        {
            Hr(allocator->Reset(),"pair allocator reset"); Hr(list->Reset(allocator.Get(),nullptr),"pair list reset");
            Put(constants,0x20,uint32_t{101+n}); Put(owner,0x8F8,n);
            start(); capture(); Put(owner,0x8F8,uint32_t{99});
            start(); // busy must not queue an additional capture
            Hr(list->Close(),"pair close"); Hr(queue->Wait(gate.Get(),n+1),"pair GPU block");
            queue->ExecuteCommandLists(1,lists); PollCapture();
            Check(files().size()==n && bridge->header.sampleSequence==n+1,"pair published before fence");
            Hr(gate->Signal(n+1),"pair GPU release");
            WaitForSample(bridge,device.Get(),n+2,101+n,"pair sample failed");
            Check(files().size()==n+1,"pair file missing or busy request queued");
            Check(memcmp(bridge->lights,light.data(),sizeof(light))==0 && bridge->lights[LightBytes-RecordStride]==0,
                "input polluted public output");
            const auto suffix=L"-"+std::to_wstring(n+1)+L".bin";
            std::filesystem::path path;
            for(const auto& f:files()) if(f.filename().wstring().ends_with(suffix)) path=f;
            Check(!path.empty() && std::filesystem::file_size(path)==sizeof(ManyLightsPairHeader)+SceneBytes+PairCopyBytes,"pair file size");
            std::ifstream file(path,std::ios::binary); ManyLightsPairHeader header{};
            file.read(reinterpret_cast<char*>(&header),sizeof(header));
            std::vector<uint8_t> payload(SceneBytes+PairCopyBytes); file.read(reinterpret_cast<char*>(payload.data()),payload.size());
            Check(file.good() && header.magic==0x50445443 && header.flags==15 && header.frame==101+n && header.bank==n &&
                header.input==reinterpret_cast<uint64_t>(source2.Get()) && header.output==reinterpret_cast<uint64_t>(source.Get()) &&
                header.fenceValue==n+2 && header.processStart!=0,"pair provenance");
            Check(memcmp(payload.data(),constants.data(),SceneBytes)==0 &&
                memcmp(payload.data()+SceneBytes,bridge->lights,LightBytes)==0 &&
                memcmp(payload.data()+SceneBytes+LightBytes,counterData.data(),CounterBytes)==0 &&
                memcmp(payload.data()+SceneBytes+PairInputOffset,light.data(),sizeof(light))==0 &&
                memcmp(payload.data()+SceneBytes+PairCopyBytes-RecordStride,counterData.data(),RecordStride)==0,"pair bytes mismatch");
        }
        CheckSubmissions(); StopCapture(); start(); Check(files().size()==2,"stopped pair rearmed"); CloseHandle(request);
        validateDebugLayer();
        Check(device->GetDeviceRemovedReason()==S_OK,"pair device lost");
        std::cout<<"ManyLights pair: explicit bounded requests, invalid layout isolation, SRV round-trip, full input/output/counter/scene bytes, blocked GPU fences, repeated requests and unchanged public feed passed.\n";
        return 0;
    }
    if (mixedTest)
    {
        Check(cdt::sky::OpenBridge(),"sky bridge");
        Check(!cdt::sky::OpenBridge(),"duplicate sky bridge takeover");
        const auto name=L"Local\\CrimsonDesertTelemetry.Sky."+std::to_wstring(GetCurrentProcessId());
        HANDLE skyHandle=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());
        const auto* skyMap=static_cast<const cdt::sky::Mapping*>(MapViewOfFile(skyHandle,FILE_MAP_READ,0,0,sizeof(cdt::sky::Mapping)));
        Check(skyMap!=nullptr,"sky mapping view");
        // A distinct source payload catches cross-feed buffer interpretation.
        std::array<uint8_t,AmbientBytes> skyBytes{}; skyBytes.fill(0x3F);
        Hr(upload->Map(0,&noReads,&mapped),"sky upload map");
        memcpy(static_cast<uint8_t*>(mapped)+2048,skyBytes.data(),skyBytes.size()); upload->Unmap(0,nullptr);
        heap.Type=D3D12_HEAP_TYPE_DEFAULT; desc.Width=AmbientBytes; desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ComPtr<ID3D12Resource> skySource;
        Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&skySource)),"sky source");
        list->CopyBufferRegion(skySource.Get(),0,upload.Get(),2048,AmbientBytes);
        barrier.Transition.pResource=skySource.Get(); list->ResourceBarrier(1,&barrier);
        std::array<uint8_t,0x200> skyInner{}; std::array<uint8_t,0x38> skyOuter{}; std::array<uint8_t,0xA0> skyOwner{};
        Put(skyInner,0xC0,uint32_t{16}); Put(skyInner,0xC4,uint32_t{64});
        Put(skyInner,0x168,reinterpret_cast<uint64_t>(skySource.Get()));
        Put(skyOuter,0x30,reinterpret_cast<uint64_t>(skyInner.data())); Put(skyOwner,0x98,reinterpret_cast<uint64_t>(skyOuter.data()));
        InitializeCaptureForTest(reinterpret_cast<uint64_t>(fakeBase)); EnableSkyForTest();
        Check(!CaptureReady(), "loading world opened the SDF acquisition gate");
        const auto capture=[&](bool sky) {
            if(sky) CaptureAmbient(reinterpret_cast<uint64_t>(skyOwner.data()),reinterpret_cast<uint64_t>(command.data()),0);
            else CaptureFilter(reinterpret_cast<uint64_t>(outer.data()),reinterpret_cast<uint64_t>(command.data()),
                reinterpret_cast<uint64_t>(counterOuter.data()),reinterpret_cast<uint64_t>(owner.data()));
        };
        capture(skyFirst); PollCapture();
        Check(std::strcmp(CapturePhaseForTest(),"discover (no source recorded)")==0,
            "capture armed before playable-world signal");
        SignalCaptureReady();
        CaptureAmbient(reinterpret_cast<uint64_t>(skyOwner.data()),reinterpret_cast<uint64_t>(command.data()),1);
        Check(std::strcmp(CapturePhaseForTest(),"discover (no source recorded)")==0,"unvalidated B entered public stream");
        capture(skyFirst); PollCapture(); Sleep(510); // Sky cadence applies to discovery too.
        Check(CaptureReady(), "playable-world signal did not open the SDF acquisition gate");
        ComPtr<ID3D12CommandQueue> computeQueue;
        D3D12_COMMAND_QUEUE_DESC cq{}; cq.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
        Hr(device->CreateCommandQueue(&cq,IID_PPV_ARGS(&computeQueue)),"mixed compute queue");
        ComPtr<ID3D12CommandAllocator> ca; ComPtr<ID3D12GraphicsCommandList> cl;
        Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,IID_PPV_ARGS(&ca)),"mixed compute allocator");
        Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COMPUTE,ca.Get(),nullptr,IID_PPV_ARGS(&cl)),"mixed compute list");
        ComPtr<ID3D12Fence> gate; Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"mixed gate");
        uint64_t lightSequence=0,skySequence=0;
        for(uint32_t n=0;n<4;++n)
        {
            const bool sky=(n%2==0)==skyFirst;
            const bool compute=sky && n>0;
            if(n>0 && !compute) { Hr(allocator->Reset(),"mixed reset allocator"); Hr(list->Reset(allocator.Get(),nullptr),"mixed reset list"); }
            auto* targetList=compute ? cl.Get() : list.Get(); auto* targetQueue=compute ? computeQueue.Get() : queue.Get();
            Put(holder,8,reinterpret_cast<uint64_t>(targetList)); Put(constants,0x20,uint32_t{100+n});
            Sleep(510); capture(sky);
            Check(std::strcmp(CapturePhaseForTest(),"recorded (not submitted)")==0,"mixed record failed");
            capture(!sky); // In-flight copy must not be overwritten by the other feed.
            Hr(targetList->Close(),"mixed close");
            ID3D12CommandList* other[]{unrelated.Get()}; queue->ExecuteCommandLists(1,other);
            Hr(targetQueue->Wait(gate.Get(),n+1),"mixed GPU block");
            ID3D12CommandList* submission[]{targetList}; targetQueue->ExecuteCommandLists(1,submission);
            for(int k=0;k<5;++k) { PollCapture(); Sleep(5); }
            Check(bridge->header.sampleSequence==lightSequence && skyMap->header.sampleSequence==skySequence,"mixed premature publication");
            Hr(gate->Signal(n+1),"mixed GPU release");
            if(sky) ++skySequence; else ++lightSequence;
            const auto deadline=GetTickCount64()+5000;
            while((bridge->header.sampleSequence<lightSequence || skyMap->header.sampleSequence<skySequence) && GetTickCount64()<deadline) { PollCapture(); Sleep(5); }
            Check(bridge->header.sampleSequence==lightSequence && skyMap->header.sampleSequence==skySequence && !CaptureFailureCode(),"mixed stream sequence/fence failure");
            if(sky)
                Check(skyMap->header.flags==7 && skyMap->header.producerRva==AmbientHookRvas[0] &&
                    skyMap->header.frameNumber==100+n && memcmp(skyMap->data,skyBytes.data(),AmbientBytes)==0,"sky payload/provenance mismatch");
            else Check(bridge->header.flags==15 && bridge->header.frameNumber==100+n &&
                memcmp(bridge->lights,light.data(),sizeof(light))==0 && memcmp(bridge->counters,counterData.data(),CounterBytes)==0,"lights polluted by sky");
            if(compute && n<3) { Hr(ca->Reset(),"mixed compute reset allocator"); Hr(cl->Reset(ca.Get(),nullptr),"mixed compute reset list"); }
        }
        CheckSubmissions();StopCapture();
        Check(bridge->header.state==Status::Stopped && skyMap->header.state==Status::Stopped,"mixed stop retained active data");
        std::cout<<"Shared sky/ManyLights pipeline: either discovery order, direct/compute submission, blocked GPU, no cross-feed overwrite, recurring publication and stop passed.\n";
        return 0;
    }
    if (ambientTest)
    {
        const auto directory = std::filesystem::absolute(L"ambient-smoke-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        Check(std::filesystem::create_directory(directory), "fresh ambient test directory");
        Check(InitializeAmbientForTest(reinterpret_cast<uint64_t>(fakeBase), directory.c_str()), "ambient control initialization");
        const auto eventName = L"Local\\CrimsonDesertTelemetry.AmbientProbe." + std::to_wstring(GetCurrentProcessId());
        HANDLE request = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName.c_str());
        Check(request != nullptr, "open actual named start event");
        const auto start = [&] { Check(SetEvent(request) != FALSE, "signal ambient start"); PollCapture(); };
        const auto fileCount = [&] {
            size_t count = 0; for (const auto& entry : std::filesystem::directory_iterator(directory)) if (entry.is_regular_file()) ++count;
            return count;
        };
        const auto runFile = [&](unsigned run) {
            const auto suffix = L"-" + std::to_wstring(run) + L".bin";
            for (const auto& entry : std::filesystem::directory_iterator(directory))
                if (entry.path().filename().wstring().ends_with(suffix)) return entry.path();
            Check(false, "missing ambient run file"); return std::filesystem::path{};
        };
        std::array<uint8_t,0xA0> sky{};
        Put(sky,0x98,reinterpret_cast<uint64_t>(outer.data()));
        std::array<uint8_t,0x698> renderer{};
        std::array<uint8_t,0x118> exposure{};
        std::array<uint8_t,0x38> exposureOuter{};
        std::array<uint8_t,0x170> exposureInner{};
        Put(sky,0x10,reinterpret_cast<uint64_t>(renderer.data()));
        Put(renderer,0x668,reinterpret_cast<uint64_t>(sky.data()));
        Put(renderer,0x690,reinterpret_cast<uint64_t>(exposure.data()));
        Put(exposure,0x10,reinterpret_cast<uint64_t>(renderer.data()));
        Put(exposure,0xC0,reinterpret_cast<uint64_t>(exposureOuter.data()));
        Put(exposure,0xD0,reinterpret_cast<uint64_t>(counterOuter.data()));
        Put(exposureOuter,0x30,reinterpret_cast<uint64_t>(exposureInner.data()));
        Put(exposureInner,0x168,reinterpret_cast<uint64_t>(counter.Get()));
        Put(exposureInner,0xC0,uint32_t{4}); Put(exposureInner,0xC4,uint32_t{32}); Put(exposureInner,0xAE,uint8_t{2});
        Put(exposure,0xD8,0.08f); Put(exposure,0xF0,uint32_t{0xFFFefef5});
        const auto probe = [&](uint64_t path) {
            CaptureAmbient(reinterpret_cast<uint64_t>(sky.data()),reinterpret_cast<uint64_t>(command.data()),path);
        };
        // Realistic valid renderer activity during loading must consume no
        // budget, perform no discovery/copy and not even create an output file.
        Put(inner,0xC0,uint32_t{16}); Put(inner,0xC4,uint32_t{64});
        for (int n=0;n<50;++n) { probe(n%2); PollCapture(); }
        Check(fileCount()==0 && std::strcmp(CapturePhaseForTest(),"stopped")==0,"capture started without explicit request");
        start();
        Check(fileCount()==1,"explicit start did not create exactly one output");
        const auto output = runFile(1);
        Put(inner,0xC0,RecordStride); Put(inner,0xC4,RecordCount);
        // Wrong ManyLights layout is not accepted as ambient; short/no-UAV
        // resources and unknown producer indices cannot advance discovery.
        probe(0); PollCapture();
        Check(std::strcmp(CapturePhaseForTest(),"discover (no source recorded)")==0,"wrong stride/count accepted");
        Put(inner,0xC0,uint32_t{16}); Put(inner,0xC4,uint32_t{64});
        Put(inner,0x168,reinterpret_cast<uint64_t>(shortCounter.Get())); probe(0);
        Put(inner,0x168,reinterpret_cast<uint64_t>(upload.Get())); probe(0);
        Put(inner,0x168,reinterpret_cast<uint64_t>(source.Get())); probe(2);
        Check(std::strcmp(CapturePhaseForTest(),"discover (no source recorded)")==0,"invalid ambient source/path accepted");
        probe(0); PollCapture();
        Check(std::strcmp(CapturePhaseForTest(),"ready (no copy recorded)")==0,"ambient prepare");
        probe(0);
        Check(std::strcmp(CapturePhaseForTest(),"recorded (not submitted)")==0,"ambient record");
        Put(exposure,0xD8,9.0f); // Worker must not resample cache AFTER GPU completion.
        Hr(list->Close(),"ambient close");
        ID3D12CommandList* others[]{unrelated.Get()}; queue->ExecuteCommandLists(1,others); PollCapture();
        Check(AmbientSamplesForTest()==0 && std::filesystem::file_size(output)==0,"unrelated list saved ambient");
        ComPtr<ID3D12Fence> gate; Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"ambient gate");
        Hr(queue->Wait(gate.Get(),1),"ambient wait");
        ID3D12CommandList* lists[]{list.Get()}; queue->ExecuteCommandLists(1,lists);
        start(); // Busy request is discarded, never a deferred run or reset.
        for(int n=0;n<20;++n) { PollCapture(); Sleep(5); }
        Check(AmbientSamplesForTest()==0 && std::filesystem::file_size(output)==0,"ambient saved before fence");
        Hr(gate->Signal(1),"ambient release");
        const auto waitSample = [&](uint32_t expected) {
            const auto deadline=GetTickCount64()+6000;
            while(AmbientSamplesForTest()<expected && GetTickCount64()<deadline) { PollCapture(); Sleep(5); }
            Check(AmbientSamplesForTest()==expected && CaptureFailureCode()==0,"ambient sample failed");
        };
        waitSample(1);
        Hr(allocator->Reset(),"ambient reset allocator"); Hr(list->Reset(allocator.Get(),nullptr),"ambient reset list");
        probe(1); Check(std::strcmp(CapturePhaseForTest(),"ready (no copy recorded)")==0,"duplicate frame captured");
        Put(constants,0x20,uint32_t{101}); Put(inner,0x168,reinterpret_cast<uint64_t>(source2.Get()));
        Put(renderer,0x690,uint64_t{0}); // Ambient survives an unavailable exposure cache.
        probe(1); Hr(list->Close(),"ambient second close"); queue->ExecuteCommandLists(1,lists); waitSample(2);
        Check(std::strcmp(CapturePhaseForTest(),"stopped")==0,"ambient limit did not stop capture");
        for(int n=0;n<10;++n) { probe(0); PollCapture(); }
        Check(fileCount()==1 && AmbientSamplesForTest()==2,"busy request was queued or completed run restarted itself");
        Check(bridge->header.sampleSequence==0,"ambient bytes leaked into light bridge");
        std::ifstream file(output,std::ios::binary);
        constexpr size_t recordBytes=sizeof(AmbientRecordHeader)+SceneBytes+AmbientBytes+sizeof(ExposureCacheContext);
        Check(std::filesystem::file_size(output)==recordBytes*2,"ambient file size");
        for(uint32_t n=0;n<2;++n)
        {
            AmbientRecordHeader header{}; std::array<uint8_t,SceneBytes> recordedScene{}; std::array<uint8_t,AmbientBytes> recordedAmbient{};
            file.read(reinterpret_cast<char*>(&header),sizeof(header));
            file.read(reinterpret_cast<char*>(recordedScene.data()),recordedScene.size());
            file.read(reinterpret_cast<char*>(recordedAmbient.data()),recordedAmbient.size());
            ExposureCacheContext cache{}; file.read(reinterpret_cast<char*>(&cache),sizeof(cache));
            Check(file.good() && header.magic==0x41445443 && header.version==2 && header.recordBytes==recordBytes && header.flags==7 &&
                header.sequence==n+1 && header.frame==100+n && header.producerRva==AmbientHookRvas[n] &&
                header.resource==reinterpret_cast<uint64_t>(n ? source2.Get() : source.Get()) &&
                header.sky==reinterpret_cast<uint64_t>(sky.data()) && header.outer==reinterpret_cast<uint64_t>(outer.data()),"ambient provenance");
            uint32_t sceneFrame{}; memcpy(&sceneFrame,recordedScene.data()+0x20,4);
            Check(sceneFrame==header.frame && memcmp(recordedAmbient.data(),light.data(),sizeof(light))==0,"ambient bytes/paired scene");
            float cacheValue{}; memcpy(&cacheValue,cache.before.data(),4);
            Check(cache.magic==0x58455443 && cache.bytes==224 && cache.flags==(n ? 0u : 31u) &&
                cache.beginTick>=header.capturedTick && cache.endTick>=cache.beginTick,"exposure appendix controls");
            Check(n ? cache.owner==0 : (cacheValue==0.08f && cache.before==cache.after),"cache not frozen with ambient recording / missing cache stale reuse");
        }
        file.close();
        // Second measurement, same device and process. Fence values must not
        // reset (an old completed fence would otherwise accept unfinished GPU work).
        start();
        const auto secondOutput=runFile(2);
        Check(fileCount()==2 && AmbientSamplesForTest()==0,"new run did not reset only its sample budget");
        Hr(allocator->Reset(),"ambient second run allocator"); Hr(list->Reset(allocator.Get(),nullptr),"ambient second run list");
        Put(constants,0x20,uint32_t{102}); probe(0); Hr(list->Close(),"ambient second run close");
        Hr(queue->Wait(gate.Get(),2),"ambient second run GPU gate"); queue->ExecuteCommandLists(1,lists);
        for(int n=0;n<20;++n) { PollCapture(); Sleep(5); }
        Check(AmbientSamplesForTest()==0 && std::filesystem::file_size(secondOutput)==0,"second run reused old completed fence");
        Hr(gate->Signal(2),"ambient second run release"); waitSample(1);
        Hr(allocator->Reset(),"ambient second run final allocator"); Hr(list->Reset(allocator.Get(),nullptr),"ambient second run final list");
        Put(constants,0x20,uint32_t{103}); probe(1); Hr(list->Close(),"ambient second run final close");
        queue->ExecuteCommandLists(1,lists); waitSample(2);
        Check(std::filesystem::file_size(secondOutput)==recordBytes*2 && std::filesystem::file_size(output)==recordBytes*2,"run isolation/file preservation");
        std::ifstream secondFile(secondOutput,std::ios::binary); AmbientRecordHeader secondHeader{};
        secondFile.read(reinterpret_cast<char*>(&secondHeader),sizeof(secondHeader)); secondFile.close();
        Check(secondHeader.sequence==1 && secondHeader.frame==102 && bridge->header.sampleSequence==0,"second run provenance/API isolation");
        // A source/list environment failure must permanently refuse rearming.
        start();
        ComPtr<ID3D12CommandAllocator> badAllocator; ComPtr<ID3D12GraphicsCommandList> badList;
        Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,IID_PPV_ARGS(&badAllocator)),"ambient incompatible allocator");
        Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_COMPUTE,badAllocator.Get(),nullptr,IID_PPV_ARGS(&badList)),"ambient incompatible list");
        Put(holder,8,reinterpret_cast<uint64_t>(badList.Get())); Put(constants,0x20,uint32_t{104}); probe(0); PollCapture();
        Check(CaptureFailureCode()==ERROR_INVALID_HANDLE,"ambient changed queue not rejected");
        start();
        Check(fileCount()==3 && std::strcmp(CapturePhaseForTest(),"stopped")==0,"faulted ambient run was rearmed");
        CheckSubmissions();StopCapture(); start(); Check(fileCount()==3,"explicit stop rearmed"); CloseHandle(request);
        UnmapViewOfFile(bridge); CloseHandle(mapHandle); VirtualFree(fakeBase,0,MEM_RELEASE);
        std::cout<<"Ambient WARP: explicit named-event start, idle ignores loading, repeated bounded runs preserve fence ordering/files, busy requests discarded, fault/stop prevent restart; resource guards, both producers, exact bytes/provenance and no API leak.\n";
        return 0;
    }
    InitializeCaptureForTest(reinterpret_cast<uint64_t>(fakeBase));
    Check(!CaptureReady(), "loading world opened the SDF acquisition gate");
    const auto capture=[&] {
        CaptureFilter(reinterpret_cast<uint64_t>(outer.data()),reinterpret_cast<uint64_t>(command.data()),
            reinterpret_cast<uint64_t>(counterOuter.data()),reinterpret_cast<uint64_t>(owner.data()));
    };
    capture(); PollCapture();
    Check(std::strcmp(CapturePhaseForTest(),"discover (no source recorded)")==0,
        "capture armed before playable-world signal");
    SignalCaptureReady();
    PollCapture();
    Check(CaptureReady(), "playable-world signal did not open the SDF acquisition gate");
    // A malformed/missing/undersized counter cannot silently downgrade a pair
    // to the former light-only capture. These calls occur before discovery.
    CaptureFilter(reinterpret_cast<uint64_t>(outer.data()),reinterpret_cast<uint64_t>(command.data()),0,0);
    Put(counterInner,0x168,uint64_t{0}); capture(); PollCapture();
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(shortCounter.Get()));
    capture(); PollCapture();
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(counterUpload.Get())); capture(); PollCapture();
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(source.Get())); capture(); PollCapture();
    Check(bridge->header.sampleSequence==0,"invalid counter published a light-only sample");
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(counter.Get())); capture();
    PollCapture(); // prepare readback and install real same-device submission hook
    CheckCapture(std::strcmp(CapturePhaseForTest(),"ready (no copy recorded)")==0,
        bridge,device.Get(),"capture preparation did not become ready");
    capture();
    CheckCapture(std::strcmp(CapturePhaseForTest(),"recorded (not submitted)")==0,
        bridge,device.Get(),"capture step did not record a GPU copy");
    Put(owner,0x8F8,uint32_t{99}); // Publication must keep the index captured BEFORE GPU execution.
    Hr(list->Close(),"target close");
    ID3D12CommandList* unrelatedLists[]{unrelated.Get()}; queue->ExecuteCommandLists(1,unrelatedLists);
    PollCapture(); Check(bridge->header.sampleSequence==0,"unrelated list falsely completed sample");

    // Hold the GPU behind an independent fence. The target submission is real,
    // but its copy cannot complete. A sleep-based reader would publish garbage.
    ComPtr<ID3D12Fence> gate; Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"gate");
    Hr(queue->Wait(gate.Get(),1),"gate wait");
    ID3D12CommandList* lists[]{list.Get()}; queue->ExecuteCommandLists(1,lists);
    for (int i=0;i<30;++i) { PollCapture(); Sleep(5); }
    Check(bridge->header.sampleSequence==0,"readback published before GPU fence completion");
    Hr(gate->Signal(1),"release gate");
    WaitForSample(bridge,device.Get(),1,100,"completed sample not published");
    Check(bridge->header.flags==15 && memcmp(bridge->lights,light.data(),sizeof(light))==0,"wrong light data/flags");
    Check(memcmp(bridge->scene,constants.data(),SceneBytes)==0,"camera not paired");
    Check(memcmp(bridge->counters,counterData.data(),CounterBytes)==0,"counter bytes not paired with completed light copy");
    Check(bridge->header.outputResource==reinterpret_cast<uint64_t>(source.Get()) &&
        bridge->header.counterResource==reinterpret_cast<uint64_t>(counter.Get()) &&
        bridge->header.owner==reinterpret_cast<uint64_t>(owner.data()) && bridge->header.bufferIndex==0,
        "first pair identity was resolved again after capture");

    Hr(allocator->Reset(),"reuse allocator"); Hr(list->Reset(allocator.Get(),nullptr),"reuse list");
    Put(constants,0x20,uint32_t{101}); Put(constants,0x80,-10527.0f);
    Put(inner,0x168,reinterpret_cast<uint64_t>(source2.Get()));
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(counter2.Get())); Put(owner,0x8F8,uint32_t{1});
    capture(); Put(owner,0x8F8,uint32_t{98});
    Hr(list->Close(),"second close"); queue->ExecuteCommandLists(1,lists);
    WaitForSample(bridge,device.Get(),2,101,"recurring capture failed");
    Check(memcmp(bridge->scene,constants.data(),SceneBytes)==0,"second camera not paired");
    Check(memcmp(bridge->counters,counterData2.data(),CounterBytes)==0 &&
        bridge->header.outputResource==reinterpret_cast<uint64_t>(source2.Get()) &&
        bridge->header.counterResource==reinterpret_cast<uint64_t>(counter2.Get()) && bridge->header.bufferIndex==1,
        "second resource bank reused the first counter or publication-time index");
    Put(constants,0x20,uint32_t{102}); Put(counterInner,0x168,uint64_t{0});
    capture(); PollCapture();
    Check(bridge->header.sampleSequence==2 && bridge->header.frameNumber==101,
        "failed counter resolution republished the previous pair as a new frame");
    Put(counterInner,0x168,reinterpret_cast<uint64_t>(counter2.Get()));
    // Resource/list environment changes must fail before recording barriers
    // against resources prepared for another queue type.
    ComPtr<ID3D12CommandAllocator> computeAllocator;
    ComPtr<ID3D12GraphicsCommandList> computeList;
    const auto incompatibleListType=computeTest ? D3D12_COMMAND_LIST_TYPE_DIRECT : D3D12_COMMAND_LIST_TYPE_COMPUTE;
    Hr(device->CreateCommandAllocator(incompatibleListType,IID_PPV_ARGS(&computeAllocator)),"incompatible allocator");
    Hr(device->CreateCommandList(0,incompatibleListType,computeAllocator.Get(),nullptr,IID_PPV_ARGS(&computeList)),"incompatible list");
    ComPtr<ID3D12Device> otherDevice; ComPtr<ID3D12Resource> foreignCounter;
    if (rejectCounterDevice)
    {
        // D3D12CreateDevice on the same WARP adapter returns the existing
        // device; that is not a negative control. This optional local case
        // requires a separate hardware adapter, unlike the portable WARP test.
        for (UINT adapterIndex=0; !otherDevice; ++adapterIndex)
        {
            ComPtr<IDXGIAdapter1> candidate;
            if (factory->EnumAdapters1(adapterIndex,&candidate)==DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 adapterDesc{};
            if (FAILED(candidate->GetDesc1(&adapterDesc)) || (adapterDesc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)!=0) continue;
            D3D12CreateDevice(candidate.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&otherDevice));
        }
        Check(otherDevice && otherDevice.Get()!=device.Get(),"--counter-device requires a distinct hardware adapter");
        heap.Type=D3D12_HEAP_TYPE_DEFAULT; desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Hr(otherDevice->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,IID_PPV_ARGS(&foreignCounter)),"foreign counter");
        Put(counterInner,0x168,reinterpret_cast<uint64_t>(foreignCounter.Get()));
    }
    else Put(holder,8,reinterpret_cast<uint64_t>(computeList.Get()));
    capture();
    PollCapture();
    CheckCapture(bridge->header.state==Status::Fault && bridge->header.error==ERROR_INVALID_HANDLE && bridge->header.sampleSequence==2,
        bridge,device.Get(),"changed queue type/counter device not refused before copy");
    CheckSubmissions();StopCapture(); Check(bridge->header.state==Status::Stopped,"stop retained active result");
    UnmapViewOfFile(bridge); CloseHandle(mapHandle); VirtualFree(fakeBase,0,MEM_RELEASE);
    std::cout<<"Real D3D12/WARP: invalid counters refused; unrelated submission ignored; blocked GPU withheld; exact fence published paired scene/lights/256 counter bytes; alternating resource identities frozen at capture; "
        <<(rejectCounterDevice ? "counter-device" : "queue-type")<<" rejection and stop passed.\n";
}

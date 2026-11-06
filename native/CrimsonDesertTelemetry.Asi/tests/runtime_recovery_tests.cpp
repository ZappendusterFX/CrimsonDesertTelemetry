#include "runtime_recovery.h"
#include "runtime_contract.generated.h"
#include "recovery_roles.h"
#include "ambient_probe.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using Json=nlohmann::json;
namespace contract=cdt::native_contract;
constexpr std::uint64_t Delta=0x10000, Vtable=0x60FC000;
constexpr std::uint64_t CollectorStore=0x200014;
void Check(bool condition,const char* reason)
{ if(!condition) {std::cerr<<reason<<'\n';ExitProcess(1);} }
std::string Hex(std::span<const unsigned char> bytes)
{
    constexpr char chars[]="0123456789ABCDEF";std::string value;
    for(auto b:bytes) {value+=chars[b>>4];value+=chars[b&15];}return value;
}
template<class T> void Put(void* destination,size_t offset,const T& value)
{ std::memcpy(static_cast<unsigned char*>(destination)+offset,&value,sizeof(value)); }
void DiagnosticRequestTests()
{
    using cdt::recovery::ReadDiagnosticRequest;
    cdt::recovery::DiagnosticRequest diagnostic;
    std::string error;
    Check(ReadDiagnosticRequest(L"",diagnostic,error,L"relative")&&!diagnostic.force&&error.empty(),"disabled diagnostic touched storage");
    for(const auto invalid:{L"0",L"0123456789abcdef0123456789abcdeff00",L"0123456789abcdef0123456789abcdefg",
        L"../3456789abcdef0123456789abcdef0",L"00000000000000000000000000000000"})
        Check(!ReadDiagnosticRequest(invalid,diagnostic,error)&&!diagnostic.force&&!error.empty(),"malformed diagnostic token accepted");
    constexpr auto token=L"0123456789abcdef0123456789abcdef";
    Check(!ReadDiagnosticRequest(token,diagnostic,error,L"relative")&&!diagnostic.force&&!error.empty(),"relative diagnostic storage accepted");
    std::array<wchar_t,MAX_PATH> temp{},unique{};
    Check(GetTempPathW(static_cast<DWORD>(temp.size()),temp.data())>0,"read diagnostic test temp path");
    Check(GetTempFileNameW(temp.data(),L"CDT",0,unique.data())!=0,"reserve isolated diagnostic test directory");
    Check(DeleteFileW(unique.data())&&CreateDirectoryW(unique.data(),nullptr),"create isolated diagnostic test directory");
    const std::filesystem::path root=unique.data();
    const auto product=root/L"CrimsonDesertTelemetry",directory=product/L"RecoveryTests";
    const auto marker=directory/L"0123456789ABCDEF0123456789ABCDEF.done";
    Check(ReadDiagnosticRequest(token,diagnostic,error,root)&&diagnostic.force&&error.empty(),"fresh diagnostic did not arm");
    const std::array<std::uint8_t,16> bytes{0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
    Check(diagnostic.token==bytes,"diagnostic token bytes differ");
    Check(GetFileAttributesW(product.c_str())==INVALID_FILE_ATTRIBUTES,"native arming created marker storage before world readiness");
    Check(ReadDiagnosticRequest(token,diagnostic,error,root)&&diagnostic.force,"re-reading armed token consumed it");
    Check(CreateDirectoryW(product.c_str(),nullptr)&&CreateDirectoryW(directory.c_str(),nullptr),"create diagnostic marker fixture");
    const auto file=CreateFileW(marker.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(file!=INVALID_HANDLE_VALUE,"create consumed diagnostic fixture");CloseHandle(file);
    Check(ReadDiagnosticRequest(token,diagnostic,error,root)&&!diagnostic.force&&error.empty(),"consumed token forced recovery again");
    Check(ReadDiagnosticRequest(L"0123456789ABCDEF0123456789ABCDEF",diagnostic,error,root)&&!diagnostic.force,"token casing bypassed one-shot marker");
    Check(DeleteFileW(marker.c_str())&&CreateDirectoryW(marker.c_str(),nullptr),"create invalid marker fixture");
    Check(!ReadDiagnosticRequest(token,diagnostic,error,root)&&!diagnostic.force&&!error.empty(),"directory was accepted as diagnostic marker");
    Check(RemoveDirectoryW(marker.c_str()),"remove invalid marker fixture");
    Check(ReadDiagnosticRequest(token,diagnostic,error,root)&&diagnostic.force,"unclaimed token did not stay armed");
    Check(RemoveDirectoryW(directory.c_str())&&RemoveDirectoryW(product.c_str())&&RemoveDirectoryW(root.c_str()),"remove isolated diagnostic test directories");
}
struct Fixture
{
    static constexpr std::uint32_t Size=0x7800000;
    unsigned char* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,Size,MEM_RESERVE,PAGE_NOACCESS));
    std::array<unsigned char,0x1000> root{};
    std::array<unsigned char,contract::SceneBytes> scene{};
    Json json;
    std::uint64_t helper{};
    std::array<unsigned char,32> digest=contract::ExecutableSha256;
    Fixture()
    {
        Check(image!=nullptr,"reserve fixture");Commit(0,0x1000);
        auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
        auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+0x80);nt->Signature=IMAGE_NT_SIGNATURE;
        nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;nt->FileHeader.NumberOfSections=3;
        nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt->OptionalHeader.SizeOfImage=Size;
        auto* sections=IMAGE_FIRST_SECTION(nt);
        sections[0].VirtualAddress=0x1000;sections[0].Misc.VirtualSize=0x6000000-0x1000;
        sections[0].Characteristics=IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_EXECUTE;
        sections[1].VirtualAddress=0x6000000;sections[1].Misc.VirtualSize=0x1000000;sections[1].Characteristics=IMAGE_SCN_MEM_READ;
        sections[2].VirtualAddress=0x7000000;sections[2].Misc.VirtualSize=0x800000;sections[2].Characteristics=IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE;
        digest[0]^=0x55;
        json={{"processId",42u},{"processStartFileTime",123456789ULL},{"executableSha256",Hex(digest)},
            {"templateSha256",Hex(contract::ExecutableSha256)},{"contractId",std::string(contract::ContractId)},
            {"sceneGlobalRva",contract::SceneGlobalRva+Delta},{"sceneVtableRva",Vtable},
            {"hookRva",contract::HookRva+Delta},{"skyProducerRva",contract::SkyProducerRva+Delta},
            {"hookSignature",Hex(contract::HookSignature)},{"contexts",Json::array()}};
        Write(contract::HookRva+Delta,contract::HookSignature);
        for(const auto& context:contract::ContextSignatures)
        {
            Write(context.rva+Delta,context.bytes);
            json["contexts"].push_back({{"rva",context.rva+Delta},{"bytes",Hex(context.bytes)}});
        }
        json["support"]["anchors"]=Json::array();
        for(const auto& anchor:contract::support::RecoveryAnchors)
        {
            Write(anchor.rva+Delta,anchor.bytes);
            json["support"]["anchors"].push_back({{"name",std::string(anchor.name)},{"rva",anchor.rva+Delta},{"bytes",Hex(anchor.bytes)}});
        }
        namespace support=contract::support;
        const std::array<unsigned char,6> ambientDispatch{0xFF,0x90,0x38,0x03,0,0};
        Write(support::AmbientHookARva+Delta-6,ambientDispatch);
        Write(support::AmbientHookBRva+Delta-6,ambientDispatch);
        json["support"]["values"]={{"AmbientSourceOffset",support::AmbientSourceOffset},{"RendererGiOffset",support::RendererGiOffset},
            {"RendererSkyOffset",support::RendererSkyOffset},{"RendererExposureOffset",support::RendererExposureOffset},
            {"PhysicsCollectorVtableRva",support::PhysicsCollectorVtableRva+Delta},{"PhysicsQueryProfile",support::PhysicsQueryProfile},
            {"UpstreamInputRva",support::UpstreamInputRva+Delta},{"UpstreamBinderRva",support::UpstreamBinderRva+Delta},
            {"ExposureReturnRva",support::ExposureReturnRva+Delta}};
        const std::array<unsigned char,15> ray{0x48,0x8B,0x01,0x4D,0x8B,0xC7,0x49,0x8B,0xD4,0xFF,0x90,0xE8,0x01,0,0};
        const std::array<unsigned char,34> shape{0x48,0x8B,0x01,0x4C,0x8B,0x90,0xF0,0x01,0,0,0x48,0x8B,0x85,0xB0,0,0,0,0x48,0x89,0x44,0x24,0x20,0x4D,0x8B,0xCF,0x4D,0x8B,0xC4,0x49,0x8B,0xD5,0x41,0xFF,0xD2};
        Write(support::PhysicsRayCallerRva+Delta-ray.size(),ray);
        Write(support::PhysicsShapeCallerRva+Delta-shape.size(),shape);
        std::int32_t call{};std::memcpy(&call,contract::HookSignature.data()+9,4);
        helper=static_cast<std::uint64_t>(static_cast<std::int64_t>(contract::HookRva+Delta+13)+call);
        constexpr std::uint32_t directory=0x6001000;
        const auto exposure=static_cast<std::uint32_t>(support::ExposureDispatchRva+Delta-0x80);
        std::array<RUNTIME_FUNCTION,5> functions{{
            {exposure,exposure+0x180,0},
            {static_cast<DWORD>(helper),static_cast<DWORD>(helper)+0x100,0},
            {static_cast<DWORD>(support::PhysicsRayRva+Delta),static_cast<DWORD>(support::PhysicsRayRva+Delta)+0x100,0},
            {static_cast<DWORD>(support::PhysicsShapeRva+Delta),static_cast<DWORD>(support::PhysicsShapeRva+Delta)+0x100,0},
            {static_cast<DWORD>(support::PhysicsRayRva+Delta+0x2000),static_cast<DWORD>(support::PhysicsRayRva+Delta+0x2100),0}}};
        std::sort(functions.begin(),functions.end(),[](auto a,auto b){return a.BeginAddress<b.BeginAddress;});
        Commit(directory,sizeof(functions));std::memcpy(image+directory,functions.data(),sizeof(functions));
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION]={directory,sizeof(functions)};
        nt->OptionalHeader.NumberOfRvaAndSizes=IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        const auto role=[&](std::uint32_t entry,std::uint32_t label,std::string_view name) {
            Commit(entry,0x180);Commit(label,name.size()+1);std::memcpy(image+label,name.data(),name.size());
            const std::array<unsigned char,3> lea{0x48,0x8D,0x15};std::memcpy(image+entry+0x40,lea.data(),lea.size());
            const auto displacement=static_cast<std::int32_t>(label-entry-0x47);Put(image,entry+0x43,displacement);
        };
        role(exposure,0x60FE000,"AdaptExposure");
        role(static_cast<std::uint32_t>(support::PhysicsRayRva+Delta),0x60FE100,"TtWorldCastRay");
        role(static_cast<std::uint32_t>(support::PhysicsShapeRva+Delta),0x60FE200,"TtWorldCastShape");
        Write(support::PhysicsRayRva+Delta+0x2000,support::PhysicsRayBytes);
        const auto collector=support::PhysicsCollectorVtableRva+Delta;Commit(collector,80);
        std::array<std::uint64_t,9> slots{};slots.fill(Base()+contract::HookRva+Delta);
        slots[1]=Base()+CollectorStore-0x14;
        slots[5]=Base()+support::PhysicsCollectorMethodRva+Delta;std::memcpy(image+collector,slots.data(),sizeof(slots));
        Put(image,collector+sizeof(slots),Base()+helper); // Neighboring table.
        const std::array<unsigned char,15> store{0x48,0x8D,0x05,0,0,0,0,0x48,0x89,0x01,0xE8,0,0,0,0};
        Write(CollectorStore,store);
        Put(image,CollectorStore+3,static_cast<std::int32_t>(collector)-static_cast<std::int32_t>(CollectorStore+7));
        Commit(Vtable,24);Commit(contract::SceneGlobalRva+Delta,8);
        Put(image,contract::SceneGlobalRva+Delta,reinterpret_cast<std::uint64_t>(root.data()));
        Put(root.data(),0,Base()+Vtable);Put(root.data(),contract::SceneRootPointerOffset,reinterpret_cast<std::uint64_t>(scene.data()));
        Put(scene.data(),contract::EarthRadiusOffset,contract::EarthRadius);
        Put(scene.data(),contract::ScreenOffset,std::array<float,4>{1920,1080,1.0f/1920,1.0f/1080});
        Put(scene.data(),contract::DirectionOffset,std::array<float,4>{0,0,1,0});
    }
    ~Fixture(){VirtualFree(image,0,MEM_RELEASE);}
    std::uint64_t Base()const{return reinterpret_cast<std::uint64_t>(image);}
    void Commit(std::uint64_t rva,size_t count)
    {
        const auto begin=rva&~std::uint64_t{0xfff},end=(rva+count+0xfff)&~std::uint64_t{0xfff};
        Check(end<=Size&&VirtualAlloc(image+begin,static_cast<SIZE_T>(end-begin),MEM_COMMIT,PAGE_READWRITE),"commit fixture");
    }
    void Write(std::uint64_t rva,std::span<const unsigned char> bytes)
    {Commit(rva,bytes.size());std::memcpy(image+rva,bytes.data(),bytes.size());}
    bool Apply(const Json& value,std::string& error)
    {return cdt::recovery::ValidateAndApply(value.dump(),Base(),42,123456789,digest,error);}
    void Reject(std::function<void(Json&)> mutate,const char* label)
    {
        auto value=json;mutate(value);std::string error;
        Check(!Apply(value,error)&&!error.empty(),label);
        Check(!cdt::runtime_contract::Recovered&&cdt::runtime_contract::HookRva==contract::HookRva,"rejected data partially changed contract");
    }
};
}
int wmain(int argc,wchar_t** argv)
{
    DiagnosticRequestTests();
    if(!contract::support::Available) {std::cout<<"PASS archived contract does not enable runtime recovery\n";return 0;}
    if(argc>1)
    {
        // SEC_IMAGE_NO_EXECUTE is a read-only disk preflight: no process attach,
        // loader initialization, callbacks, executable mapping or game input.
        const auto file=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"open optional EXE");
        const auto mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY|SEC_IMAGE_NO_EXECUTE,0,0,nullptr);
        Check(mapping!=nullptr,"map optional EXE without execution");
        const auto* view=static_cast<const unsigned char*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0));
        Check(view!=nullptr,"view optional EXE");
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(view);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(view+dos->e_lfanew);
        const auto base=reinterpret_cast<std::uint64_t>(view),size=static_cast<std::uint64_t>(nt->OptionalHeader.SizeOfImage);
        Check(size<=UINT32_MAX,"optional EXE bounded image");
        Check(cdt::recovery::roles::VerifyRole(base,static_cast<std::uint32_t>(size),contract::support::PhysicsRayRva,"TtWorldCastRay",true),"real EXE ray role");
        Check(cdt::recovery::roles::VerifyRole(base,static_cast<std::uint32_t>(size),contract::support::PhysicsShapeRva,"TtWorldCastShape",true),"real EXE shape role");
        Check(cdt::recovery::roles::VerifyRole(base,static_cast<std::uint32_t>(size),contract::support::ExposureDispatchRva,"AdaptExposure",false),"real EXE exposure role");
        std::int32_t relative{};std::memcpy(&relative,view+contract::HookRva+9,4);
        const auto helper=static_cast<std::uint64_t>(static_cast<std::int64_t>(contract::HookRva)+13+relative);
        Check(cdt::recovery::roles::IsFunctionEntry(base,static_cast<std::uint32_t>(size),helper),"real EXE shared helper entry");
        UnmapViewOfFile(view);CloseHandle(mapping);CloseHandle(file);
        std::cout<<"PASS actual EXE named physics/exposure roles and common helper unwind entry (disk only)\n";
    }
    Fixture f;
    f.Reject([](Json& j){j["processId"]=43u;},"wrong PID accepted");
    f.Reject([](Json& j){j["processStartFileTime"]=123456788u;},"reused PID accepted");
    f.Reject([](Json& j){j["executableSha256"]=std::string(64,'0');},"wrong EXE accepted");
    f.Reject([](Json& j){j["templateSha256"]=std::string(64,'0');},"wrong template accepted");
    f.Reject([](Json& j){j["hookRva"]=-1;},"negative RVA accepted");
    f.Reject([](Json& j){j["hookRva"]=0x7000000u;},"data section hook accepted");
    f.Reject([](Json& j){j["contexts"][5]["rva"]=contract::ContextSignatures[5].rva+Delta+1;},"misaligned context accepted");
    f.Reject([](Json& j){j["contexts"].erase(0);},"missing context accepted");
    f.Reject([](Json& j){j["support"]["values"]["PhysicsQueryProfile"]=0x40000016u;},"foreign profile accepted");
    f.Reject([](Json& j){j["support"]["values"]["RendererGiOffset"]=0x660u;},"layout change accepted");
    f.Reject([](Json& j){j["support"]["values"]["ExposureReturnRva"]=0x3000000u;},"incoherent return accepted");
    f.Reject([](Json& j){j["support"]["anchors"][1]=j["support"]["anchors"][0];},"duplicate anchor accepted");
    f.Reject([](Json& j){j["support"]["anchors"][0]["bytes"]="90";},"changed hook accepted");
    f.Reject([](Json& j){j["sceneVtableRva"]=Vtable+8;},"wrong scene type accepted");
    f.Reject([](Json& j){j["support"]["anchors"][6]["rva"]=contract::support::PhysicsRayRva+Delta+0x2000;},"unrelated identical physics prologue accepted");
    const auto corrupt=[&](std::uint64_t address,const char* label) {
        f.image[address]^=1;f.Reject([](Json&){},label);f.image[address]^=1;
    };
    corrupt(contract::support::PhysicsRayCallerRva+Delta-1,"changed natural caller accepted");
    corrupt(contract::support::PhysicsCollectorVtableRva+Delta+5*8,"changed collector accepted");
    corrupt(CollectorStore+3,"collector storing a different table accepted");
    corrupt(contract::HookRva+Delta,"changed live code accepted");
    corrupt(contract::support::AmbientHookARva+Delta-1,"changed ambient dispatch accepted");
    Put(f.scene.data(),contract::EarthRadiusOffset,0.0f);f.Reject([](Json&){},"invalid in-game scene accepted");
    Put(f.scene.data(),contract::EarthRadiusOffset,contract::EarthRadius);
    // Moving a relative target is allowed only inside its reviewed operand. The
    // changed bytes must still agree with loaded code and remain executable.
    auto hook=contract::HookSignature;
    const auto hookDisplacement=static_cast<std::int32_t>(f.helper+0x1000-(contract::HookRva+Delta+13));
    Put(hook.data(),9,hookDisplacement);
    f.Write(contract::HookRva+Delta,hook);f.json["hookSignature"]=Hex(hook);
    // A different callee in only one caller must be rejected, even though it
    // is executable. The common-role invariant binds all four dispatch sites.
    f.Reject([](Json&){},"incoherent shared helper accepted");
    auto ambient=contract::support::AmbientHookABytes;
    const auto moveCall=[&](auto& bytes,std::uint64_t rva,size_t opcode,size_t anchorIndex) {
        const auto displacement=static_cast<std::int32_t>(f.helper+0x1000-(rva+opcode+5));
        Put(bytes.data(),opcode+1,displacement);f.Write(rva,bytes);
        f.json["support"]["anchors"][anchorIndex]["bytes"]=Hex(bytes);
    };
    moveCall(ambient,contract::support::AmbientHookARva+Delta,3,0);
    auto ambientB=contract::support::AmbientHookBBytes;
    moveCall(ambientB,contract::support::AmbientHookBRva+Delta,3,1);
    auto exposureBytes=contract::support::ExposureDispatchBytes;
    moveCall(exposureBytes,contract::support::ExposureDispatchRva+Delta,21,8);
    auto* functions=reinterpret_cast<RUNTIME_FUNCTION*>(f.image+0x6001000);
    for(size_t i=0;i<5;++i) if(functions[i].BeginAddress==f.helper)
    {functions[i].BeginAddress+=0x1000;functions[i].EndAddress+=0x1000;}
    std::string error;
    Check(f.Apply(f.json,error),error.c_str());
    Check(cdt::runtime_contract::Recovered&&cdt::runtime_contract::HookRva==contract::HookRva+Delta&&
        cdt::runtime_contract::SceneGlobalRva==contract::SceneGlobalRva+Delta&&
        cdt::render::AmbientHookRvas()[0]==contract::SkyProducerRva+Delta&&
        cdt::render::AmbientSignatureA()==ambient&&cdt::runtime_contract::HookSignature==hook&&
        cdt::runtime_contract::support::PhysicsRayRva==contract::support::PhysicsRayRva+Delta&&
        cdt::runtime_contract::support::ExposureReturnRva==contract::support::ExposureReturnRva+Delta,
        "successful recovery did not publish all runtime addresses");
    Check(!f.Apply(f.json,error),"second activation was accepted");
    // No graphics hooks or game writes: exercise the actual IPC object/header
    // and cancellation using this test process, then a submitted invalid message.
    const auto name=L"Local\\CrimsonDesertTelemetry.Recovery."+std::to_wstring(GetCurrentProcessId());
    const auto header=[&](std::uint32_t expected,const cdt::recovery::DiagnosticRequest& diagnostic=cdt::recovery::DiagnosticRequest{}) {
        const auto mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());Check(mapping!=nullptr,"open native-created handshake");
        auto* view=static_cast<unsigned char*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,65536));Check(view!=nullptr,"map handshake");
        std::uint32_t magic{},state{},features{},version{},diagnosticFlag{};std::memcpy(&magic,view,4);std::memcpy(&state,view+12,4);std::memcpy(&features,view+224,4);
        std::memcpy(&version,view+4,4);std::memcpy(&diagnosticFlag,view+232,4);
        Check(magic==0x52544443&&state==expected&&features==15,"wrong handshake header/state/features");
        Check(version==(diagnostic.force?2u:1u)&&diagnosticFlag==(diagnostic.force?1u:0u),"wrong diagnostic handshake version/flag");
        const auto token=diagnostic.force?diagnostic.token:std::array<std::uint8_t,16>{};
        Check(std::equal(token.begin(),token.end(),view+240),"wrong diagnostic handshake token");
        return std::pair{mapping,view};
    };
    {
        cdt::recovery::Handoff handoff;Check(handoff.Initialize(f.digest,false,15),"exact handoff initialization");
        const auto [mapping,view]=header(5);UnmapViewOfFile(view);CloseHandle(mapping);
    }
    {
        cdt::recovery::Handoff handoff;Check(handoff.Initialize(f.digest,true,15),"recovery handoff initialization");
        const auto [mapping,view]=header(1);
        const auto stop=CreateEventW(nullptr,TRUE,TRUE,nullptr);Check(stop!=nullptr,"stop event");
        Check(!handoff.WaitAndApply(stop),"cancelled waiting activated recovery");CloseHandle(stop);
        const std::string invalid="{}";std::memcpy(view+256,invalid.data(),invalid.size());Put(view,16,static_cast<std::uint32_t>(invalid.size()));
        InterlockedExchange(reinterpret_cast<volatile LONG*>(view+12),2);
        const auto running=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        Check(!handoff.WaitAndApply(running),"invalid submission activated recovery");CloseHandle(running);
        Check(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(view+12),0,0)==4,"rejected handoff did not publish terminal state");
        UnmapViewOfFile(view);CloseHandle(mapping);
    }
    {
        cdt::recovery::DiagnosticRequest diagnostic;diagnostic.force=true;
        cdt::recovery::Handoff invalid;Check(!invalid.Initialize(f.digest,true,15,diagnostic),"empty forced token was published");
        diagnostic.token[0]=0xA5;diagnostic.token[15]=0xEF;
        Check(!invalid.Initialize(f.digest,false,15,diagnostic),"diagnostic was allowed to skip recovery");
        cdt::recovery::Handoff handoff;Check(handoff.Initialize(f.digest,true,15,diagnostic),"diagnostic handoff initialization");
        const auto [mapping,view]=header(1,diagnostic);
        const auto stop=CreateEventW(nullptr,TRUE,TRUE,nullptr);Check(stop!=nullptr,"diagnostic stop event");
        Check(!handoff.WaitAndApply(stop),"cancelled diagnostic activated recovery");CloseHandle(stop);
        UnmapViewOfFile(view);CloseHandle(mapping);
    }
    {
        // Consumed token metadata must not leak into an ordinary recovery after
        // another game update: normal hash/context validation remains active.
        cdt::recovery::DiagnosticRequest consumed;consumed.token[0]=0xA5;
        cdt::recovery::Handoff handoff;Check(handoff.Initialize(f.digest,true,15,consumed),"ordinary recovery after consumed diagnostic");
        const auto [mapping,view]=header(1);UnmapViewOfFile(view);CloseHandle(mapping);
    }
    std::cout<<"PASS complete shifted contract, identity/layout/code/caller/collector/scene rejection, one-time activation and cold-test handshake\n";
}

#include "runtime_recovery.h"
#include "runtime_contract.generated.h"
#include "recovery_roles.h"
#include "render_bridge.h"
#include "overlay.h"
#include "console/common.h"
#include "console/mem.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

namespace cdt::recovery
{
namespace
{
using Json = nlohmann::json;
constexpr std::uint32_t Capacity = 65536, HeaderBytes = 256, Magic = 0x52544443;
std::string Hex(std::span<const unsigned char> bytes)
{
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result; result.reserve(bytes.size()*2);
    for (const auto b : bytes) { result += digits[b >> 4]; result += digits[b & 15]; }
    return result;
}
std::uint64_t StartTime()
{
    FILETIME born{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &born, &exit, &kernel, &user)) return 0;
    return (std::uint64_t{born.dwHighDateTime} << 32) | born.dwLowDateTime;
}
template<class T> T Get(const unsigned char* p, size_t offset)
{ T value{}; std::memcpy(&value, p+offset, sizeof(value)); return value; }
template<class T> void Put(unsigned char* p, size_t offset, T value)
{ std::memcpy(p+offset, &value, sizeof(value)); }
std::uint32_t State(unsigned char* p)
{ return static_cast<std::uint32_t>(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(p+12),0,0)); }
void SetState(unsigned char* p, std::uint32_t state)
{ InterlockedExchange(reinterpret_cast<volatile LONG*>(p+12),static_cast<LONG>(state)); }
std::uint64_t Number(const Json& object, const char* name)
{
    const auto& value=object.at(name);
    if (!value.is_number_unsigned()) throw std::runtime_error(std::string("Non-integer field: ")+name);
    return value.get<std::uint64_t>();
}
std::vector<std::uint8_t> Bytes(std::string_view text)
{
    std::vector<std::uint8_t> bytes;
    int high=-1;
    for (char c:text)
    {
        if(c==' ') { if(high>=0) throw std::runtime_error("Split hex byte"); continue; }
        const int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
        if(n<0) throw std::runtime_error("Invalid hex byte");
        if(high<0) high=n; else { bytes.push_back(static_cast<std::uint8_t>((high<<4)|n)); high=-1; }
        if(bytes.size()>128) throw std::runtime_error("Oversize code signature");
    }
    if(high>=0||bytes.empty()) throw std::runtime_error("Incomplete code signature");
    return bytes;
}
bool PatternMatches(std::span<const std::uint8_t> bytes, std::string_view pattern)
{
    size_t index=0;
    for(size_t i=0;i<pattern.size();)
    {
        if(pattern[i]==' ') {++i;continue;}
        if(i+2>pattern.size()||index>=bytes.size()) return false;
        const auto token=pattern.substr(i,2);
        if(token!="??" && Bytes(token)[0]!=bytes[index]) return false;
        ++index;i+=2;
    }
    return index==bytes.size();
}
struct Image
{
    std::uint64_t base{};
    std::uint32_t size{};
    std::vector<IMAGE_SECTION_HEADER> sections;
    explicit Image(std::uint64_t moduleBase):base(moduleBase)
    {
        IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
        if(!ch::mem::SafeRead(reinterpret_cast<void*>(base),&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||
            dos.e_lfanew<0||dos.e_lfanew>0x100000||
            !ch::mem::SafeRead(reinterpret_cast<void*>(base+dos.e_lfanew),&nt,sizeof(nt))||
            nt.Signature!=IMAGE_NT_SIGNATURE||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||
            nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.FileHeader.NumberOfSections==0||
            nt.FileHeader.NumberOfSections>96||nt.OptionalHeader.SizeOfImage>0x40000000)
            throw std::runtime_error("Loaded PE headers are invalid");
        size=nt.OptionalHeader.SizeOfImage;
        sections.resize(nt.FileHeader.NumberOfSections);
        const auto address=base+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
        if(!ch::mem::SafeRead(reinterpret_cast<void*>(address),sections.data(),sections.size()*sizeof(IMAGE_SECTION_HEADER)))
            throw std::runtime_error("Loaded PE sections are unreadable");
    }
    bool In(std::uint64_t rva,size_t length,DWORD required,DWORD prohibited=0) const
    {
        if(!rva||rva>size||length>size-rva) return false;
        return std::any_of(sections.begin(),sections.end(),[&](const auto& s){
            return (s.Characteristics&required)==required&&!(s.Characteristics&prohibited)&&
                rva>=s.VirtualAddress&&rva+length<=std::uint64_t{s.VirtualAddress}+s.Misc.VirtualSize;
        });
    }
    bool Exact(std::uint64_t rva,std::span<const std::uint8_t> bytes) const
    {
        std::array<std::uint8_t,128> live{};
        return bytes.size()<=live.size()&&In(rva,bytes.size(),IMAGE_SCN_MEM_EXECUTE)&&
            ch::mem::SafeRead(reinterpret_cast<void*>(base+rva),live.data(),bytes.size())&&
            std::equal(bytes.begin(),bytes.end(),live.begin());
    }
};
struct Candidate
{
    std::uint64_t scene{},sceneVtable{},hook{},sky{};
    std::vector<std::uint8_t> hookBytes;
    struct Context { std::uint64_t rva{};std::vector<std::uint8_t> bytes; };
    std::vector<Context> contexts;
    std::map<std::string,Context> support;
    std::map<std::string,std::uint32_t> values;
};
template<class T> void CopyBytes(T& destination,const std::vector<std::uint8_t>& source)
{ std::copy(source.begin(),source.end(),destination.begin()); }
Json Parse(std::string_view payload)
{
    if(payload.empty()||payload.size()>Capacity-HeaderBytes) throw std::runtime_error("Invalid recovery payload length");
    return Json::parse(payload,[](int depth,Json::parse_event_t,Json&) {
        if(depth>12) throw std::runtime_error("Recovery JSON is too deeply nested");
        return true;
    });
}
}

bool ReadDiagnosticRequest(std::wstring_view token,DiagnosticRequest& request,std::string& failure,
    const std::filesystem::path& localAppDataForTest)
{
    request={};failure.clear();
    if(token.empty()) return true;
    if(token.size()!=32) {failure="RecoveryTestOnce must be an empty value or a nonzero 32-digit hexadecimal token.";return false;}
    std::wstring normalized;normalized.reserve(32);
    constexpr wchar_t digits[]=L"0123456789ABCDEF";
    for(size_t i=0;i<token.size();++i)
    {
        const auto c=token[i];
        const int value=c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:c>=L'A'&&c<=L'F'?c-L'A'+10:-1;
        if(value<0) {failure="RecoveryTestOnce contains a non-hexadecimal character.";return false;}
        normalized+=digits[value];
        request.token[i/2]=static_cast<std::uint8_t>((request.token[i/2]<<4)|value);
    }
    if(std::all_of(request.token.begin(),request.token.end(),[](auto value){return value==0;}))
    {failure="RecoveryTestOnce must not be the all-zero token.";return false;}
    try
    {
        auto directory=localAppDataForTest;
        if(directory.empty())
        {
            std::array<wchar_t,32768> local{};
            const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local.data(),static_cast<DWORD>(local.size()));
            if(length==0||length>=local.size()) {failure="LOCALAPPDATA is unavailable for the one-time recovery test.";return false;}
            directory=std::filesystem::path(local.data());
        }
        if(!directory.is_absolute()) {failure="The recovery-test storage path is not absolute.";return false;}
        const auto marker=directory/L"CrimsonDesertTelemetry"/L"RecoveryTests"/(normalized+L".done");
        const auto attributes=GetFileAttributesW(marker.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES)
        {
            if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))
            {failure="The recovery-test marker is not a regular file.";return false;}
            return true;
        }
        const auto error=GetLastError();
        if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)
        {failure="The one-time recovery-test marker could not be checked (Windows error "+std::to_string(error)+").";return false;}
        request.force=true;
        return true;
    }
    catch(const std::exception& error) {failure=std::string("Recovery-test storage could not be checked: ")+error.what();return false;}
}

bool ValidateAndApply(std::string_view payload,std::uint64_t moduleBase,std::uint32_t processId,
    std::uint64_t processStart,const std::array<unsigned char,32>& executableHash,std::string& failure)
{
    try
    {
        if constexpr(!native_contract::support::Available)
            throw std::runtime_error("Recovery is unavailable for this archived contract");
        if(runtime_contract::Recovered)
            throw std::runtime_error("Recovery is already activated");
        const auto json=Parse(payload);
        if(Number(json,"processId")!=processId||Number(json,"processStartFileTime")!=processStart||
            json.at("executableSha256")!=Hex(executableHash)||
            json.at("templateSha256")!=Hex(native_contract::ExecutableSha256)||
            json.at("contractId").get<std::string>()!=native_contract::ContractId)
            throw std::runtime_error("Recovery process, executable or template identity differs");
        const Image image(moduleBase);
        Candidate c;
        c.scene=Number(json,"sceneGlobalRva");c.sceneVtable=Number(json,"sceneVtableRva");
        c.hook=Number(json,"hookRva");c.sky=Number(json,"skyProducerRva");
        c.hookBytes=Bytes(json.at("hookSignature").get<std::string>());
        if(c.hookBytes.size()!=native_contract::HookSignature.size()||
            !PatternMatches(c.hookBytes,"48 8B 5C 24 50 48 8B CB E8 ?? ?? ?? ?? 48 8B 03 48 8B CB FF 90 18 02 00 00")||
            !image.Exact(c.hook,c.hookBytes)) throw std::runtime_error("ManyLights hook context differs");
        std::int32_t relative{};std::memcpy(&relative,c.hookBytes.data()+9,4);
        const auto called=static_cast<std::int64_t>(c.hook)+13+relative;
        if(called<=0||!image.In(static_cast<std::uint64_t>(called),1,IMAGE_SCN_MEM_EXECUTE))
            throw std::runtime_error("ManyLights relative call leaves executable image");
        const auto& contexts=json.at("contexts");
        if(!contexts.is_array()||contexts.size()!=native_contract::ContextSignatures.size())
            throw std::runtime_error("ManyLights provenance contexts are incomplete");
        for(size_t i=0;i<contexts.size();++i)
        {
            Candidate::Context item{Number(contexts[i],"rva"),Bytes(contexts[i].at("bytes").get<std::string>())};
            const auto original=native_contract::ContextSignatures[i];
            if(item.bytes.size()!=original.bytes.size()||!std::equal(item.bytes.begin(),item.bytes.end(),original.bytes.begin())||
                !image.Exact(item.rva,item.bytes)) throw std::runtime_error("ManyLights register/binding layout changed");
            // The reviewed function's register lifetimes and control-flow distances
            // are one contract. The separate command Dispatch helper is index4.
            if(i!=4 && static_cast<std::int64_t>(item.rva)-static_cast<std::int64_t>(c.hook)!=
                static_cast<std::int64_t>(original.rva)-static_cast<std::int64_t>(native_contract::HookRva))
                throw std::runtime_error("ManyLights context relationship changed");
            c.contexts.push_back(std::move(item));
        }
        const auto& support=json.at("support");const auto& anchors=support.at("anchors");
        if(!anchors.is_array()||anchors.size()!=native_contract::support::RecoveryAnchors.size())
            throw std::runtime_error("Acquisition anchors are incomplete");
        for(const auto& anchor:anchors)
        {
            const auto name=anchor.at("name").get<std::string>();
            const auto original=std::find_if(native_contract::support::RecoveryAnchors.begin(),native_contract::support::RecoveryAnchors.end(),
                [&](const auto& a){return a.name==name;});
            if(original==native_contract::support::RecoveryAnchors.end()||c.support.contains(name))
                throw std::runtime_error("Unknown or duplicate acquisition anchor");
            Candidate::Context item{Number(anchor,"rva"),Bytes(anchor.at("bytes").get<std::string>())};
            if(item.bytes.size()!=original->bytes.size()||!PatternMatches(item.bytes,original->pattern)||!image.Exact(item.rva,item.bytes))
                throw std::runtime_error("Acquisition guard differs: "+name);
            c.support.emplace(name,std::move(item));
        }
        const auto& values=support.at("values");
        if(!values.is_object()||values.size()!=9) throw std::runtime_error("Acquisition values are incomplete");
        for(const char* name:{"AmbientSourceOffset","RendererGiOffset","RendererSkyOffset","RendererExposureOffset",
            "PhysicsCollectorVtableRva","PhysicsQueryProfile","UpstreamInputRva","UpstreamBinderRva","ExposureReturnRva"})
        {
            const auto value=Number(values,name);
            if(value>0x7fffffff) throw std::runtime_error("Acquisition value is outside bounds");
            c.values.emplace(name,static_cast<std::uint32_t>(value));
        }
        namespace original=native_contract::support;
        for(const auto& [name,expected]:std::array<std::pair<const char*,std::uint32_t>,5>{{
            {"AmbientSourceOffset",original::AmbientSourceOffset},{"RendererGiOffset",original::RendererGiOffset},
            {"RendererSkyOffset",original::RendererSkyOffset},{"RendererExposureOffset",original::RendererExposureOffset},
            {"PhysicsQueryProfile",original::PhysicsQueryProfile}}})
            if(c.values.at(name)!=expected) throw std::runtime_error("Recovery cannot change reviewed layout or physics profile");
        if(c.sky!=c.support.at("AmbientHookA").rva||c.values.at("UpstreamInputRva")!=c.contexts[5].rva||
            c.values.at("UpstreamBinderRva")!=c.contexts[6].rva||
            c.values.at("ExposureReturnRva")!=c.support.at("ExposureDispatch").rva+18||
            c.contexts[4].rva!=c.support.at("SpatialDispatch").rva+
                native_contract::ContextSignatures[4].rva-original::SpatialDispatchRva)
            throw std::runtime_error("Derived acquisition addresses disagree");
        const auto relativeMatches=[&](const char* a,const char* b,std::uint64_t originalA,std::uint64_t originalB) {
            return static_cast<std::int64_t>(c.support.at(a).rva)-static_cast<std::int64_t>(c.support.at(b).rva)==
                static_cast<std::int64_t>(originalA)-static_cast<std::int64_t>(originalB);
        };
        if(!relativeMatches("AmbientHookA","AmbientSourceA",original::AmbientHookARva,original::AmbientSourceARva)||
            !relativeMatches("AmbientHookB","AmbientSourceB",original::AmbientHookBRva,original::AmbientSourceBRva)||
            !relativeMatches("AmbientSourceB","AmbientSourceA",original::AmbientSourceBRva,original::AmbientSourceARva)||
            !relativeMatches("PhysicsShapeCaller","PhysicsRayCaller",original::PhysicsShapeCallerRva,original::PhysicsRayCallerRva))
            throw std::runtime_error("Acquisition family relationships changed");
        const auto callTarget=[&](const Candidate::Context& context,size_t offset) {
            if(offset+5>context.bytes.size()||context.bytes[offset]!=0xE8) throw std::runtime_error("Expected direct call is missing");
            std::int32_t displacement{};std::memcpy(&displacement,context.bytes.data()+offset+1,4);
            return static_cast<std::int64_t>(context.rva)+static_cast<std::int64_t>(offset)+5+displacement;
        };
        if(callTarget(c.support.at("AmbientHookA"),3)!=called||callTarget(c.support.at("AmbientHookB"),3)!=called||
            callTarget(c.support.at("ExposureDispatch"),21)!=called||
            !roles::IsFunctionEntry(moduleBase,image.size,static_cast<std::uint64_t>(called)))
            throw std::runtime_error("Shared renderer helper provenance changed");
        if(!roles::VerifyRole(moduleBase,image.size,c.support.at("PhysicsRay").rva,"TtWorldCastRay",true)||
            !roles::VerifyRole(moduleBase,image.size,c.support.at("PhysicsShape").rva,"TtWorldCastShape",true)||
            !roles::VerifyRole(moduleBase,image.size,c.support.at("ExposureDispatch").rva,"AdaptExposure",false))
            throw std::runtime_error("Named physics or exposure function role differs");
        const std::array<std::uint8_t,6> ambientDispatch{0xFF,0x90,0x38,0x03,0,0};
        if(!image.Exact(c.support.at("AmbientHookA").rva-6,ambientDispatch)||
            !image.Exact(c.support.at("AmbientHookB").rva-6,ambientDispatch))
            throw std::runtime_error("Ambient dispatch ABI changed");
        const std::array<std::uint8_t,15> rayCaller{0x48,0x8B,0x01,0x4D,0x8B,0xC7,0x49,0x8B,0xD4,0xFF,0x90,0xE8,0x01,0,0};
        const std::array<std::uint8_t,34> shapeCaller{0x48,0x8B,0x01,0x4C,0x8B,0x90,0xF0,0x01,0,0,0x48,0x8B,0x85,0xB0,0,0,0,0x48,0x89,0x44,0x24,0x20,0x4D,0x8B,0xCF,0x4D,0x8B,0xC4,0x49,0x8B,0xD5,0x41,0xFF,0xD2};
        if(!image.Exact(c.support.at("PhysicsRayCaller").rva-rayCaller.size(),rayCaller)||
            !image.Exact(c.support.at("PhysicsShapeCaller").rva-shapeCaller.size(),shapeCaller))
            throw std::runtime_error("Natural physics caller ABI changed");
        const auto collector=c.values.at("PhysicsCollectorVtableRva");std::array<std::uint64_t,12> slots{};
        if(collector%8||!image.In(collector,sizeof(slots),IMAGE_SCN_MEM_READ,IMAGE_SCN_MEM_WRITE|IMAGE_SCN_MEM_EXECUTE)||
            !ch::mem::SafeRead(reinterpret_cast<void*>(moduleBase+collector),slots.data(),sizeof(slots))||
            slots[5]!=moduleBase+c.support.at("PhysicsCollectorMethod").rva||slots[0]!=slots[9]||slots[3]!=slots[7]||slots[6]!=slots[8]||
            !std::all_of(slots.begin(),slots.end(),[&](auto address){return address>moduleBase&&image.In(address-moduleBase,1,IMAGE_SCN_MEM_EXECUTE);}))
            throw std::runtime_error("Physics collector shape differs");
        std::uint64_t scene{},vtable{},data{};std::array<std::uint8_t,native_contract::SceneBytes> sceneBytes{};
        if(!image.In(c.scene,8,IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE,IMAGE_SCN_MEM_EXECUTE)||
            !image.In(c.sceneVtable,24,IMAGE_SCN_MEM_READ,IMAGE_SCN_MEM_WRITE|IMAGE_SCN_MEM_EXECUTE)||
            !ch::mem::SafeRead(reinterpret_cast<void*>(moduleBase+c.scene),&scene,8)||!scene||
            !ch::mem::SafeRead(reinterpret_cast<void*>(scene),&vtable,8)||vtable!=moduleBase+c.sceneVtable||
            !ch::mem::SafeRead(reinterpret_cast<void*>(scene+native_contract::SceneRootPointerOffset),&data,8)||!data||
            !ch::mem::SafeRead(reinterpret_cast<void*>(data),sceneBytes.data(),sceneBytes.size())||!render::ValidateScene(sceneBytes.data()))
            throw std::runtime_error("Playable scene type or constants are no longer valid");
        // All checks above are non-mutating. Publish the complete contract only now;
        // no hooks/consumers exist until the owning worker returns from this method.
        runtime_contract::SceneGlobalRva=c.scene;runtime_contract::HookRva=c.hook;
        runtime_contract::SkyProducerRva=static_cast<std::uint32_t>(c.sky);
        CopyBytes(runtime_contract::HookSignature,c.hookBytes);
        for(size_t i=0;i<c.contexts.size();++i) runtime_contract::ContextSignatures[i].rva=c.contexts[i].rva;
        // Context bytes are exact (no relative operands), so only RVAs change.
#define CDT_APPLY_ANCHOR(Name) runtime_contract::support::Name##Rva=c.support.at(#Name).rva; CopyBytes(runtime_contract::support::Name##Bytes,c.support.at(#Name).bytes)
        // Support-less archived targets compile but never reach this path.
        CDT_APPLY_ANCHOR(AmbientHookA);CDT_APPLY_ANCHOR(AmbientHookB);
        CDT_APPLY_ANCHOR(AmbientSourceA);CDT_APPLY_ANCHOR(AmbientSourceB);
        CDT_APPLY_ANCHOR(SpatialDispatch);CDT_APPLY_ANCHOR(PhysicsShape);CDT_APPLY_ANCHOR(PhysicsRay);
        CDT_APPLY_ANCHOR(PhysicsCollectorMethod);CDT_APPLY_ANCHOR(ExposureDispatch);
        CDT_APPLY_ANCHOR(PhysicsShapeCaller);CDT_APPLY_ANCHOR(PhysicsRayCaller);
#undef CDT_APPLY_ANCHOR
#define CDT_APPLY_VALUE(Name) runtime_contract::support::Name=c.values.at(#Name)
        CDT_APPLY_VALUE(AmbientSourceOffset);CDT_APPLY_VALUE(RendererGiOffset);CDT_APPLY_VALUE(RendererSkyOffset);
        CDT_APPLY_VALUE(RendererExposureOffset);CDT_APPLY_VALUE(PhysicsCollectorVtableRva);CDT_APPLY_VALUE(PhysicsQueryProfile);
        CDT_APPLY_VALUE(UpstreamInputRva);CDT_APPLY_VALUE(UpstreamBinderRva);CDT_APPLY_VALUE(ExposureReturnRva);
#undef CDT_APPLY_VALUE
        for(size_t i=0;i<native_contract::support::RecoveryAnchors.size();++i)
            runtime_contract::support::Contexts[i].rva=c.support.at(std::string(native_contract::support::RecoveryAnchors[i].name)).rva;
        runtime_contract::ExecutableSha256=executableHash;
        runtime_contract::Recovered=true;
        return true;
    }
    catch(const std::exception& error) {failure=error.what();return false;}
    catch(...) {failure="Unknown recovery validation failure";return false;}
}

Handoff::~Handoff()
{
    if(view_ && State(view_)==2) SetState(view_,4);
    if(view_) UnmapViewOfFile(view_);
    if(mapping_) CloseHandle(mapping_);
}
bool Handoff::Initialize(const std::array<unsigned char,32>& executableHash,bool needed,std::uint32_t requestedFeatures,
    const DiagnosticRequest& diagnostic)
{
    if(diagnostic.force&&(!needed||std::all_of(diagnostic.token.begin(),diagnostic.token.end(),[](auto value){return value==0;}))) return false;
    if(!native_contract::support::Available) return !needed;
    const auto name=L"Local\\CrimsonDesertTelemetry.Recovery."+std::to_wstring(GetCurrentProcessId());
    mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,Capacity,name.c_str());
    if(!mapping_||GetLastError()==ERROR_ALREADY_EXISTS) return false;
    view_=static_cast<unsigned char*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,Capacity));
    if(!view_) return false;
    const auto born=StartTime();if(!born) return false;
    std::memset(view_,0,Capacity);Put(view_,0,Magic);Put(view_,4,diagnostic.force?2u:1u);Put(view_,8,Capacity);
    Put(view_,20,GetCurrentProcessId());Put(view_,24,born);
    Put(view_,224,requestedFeatures&15u);
    diagnostic_=diagnostic.force;
    if(diagnostic_)
    {
        Put(view_,232,1u);
        std::memcpy(view_+240,diagnostic.token.data(),diagnostic.token.size());
    }
    executableHash_=executableHash;
    const auto baseline=Hex(native_contract::ExecutableSha256),actual=Hex(executableHash);
    std::memcpy(view_+32,baseline.data(),64);std::memcpy(view_+96,actual.data(),64);
    std::memcpy(view_+160,native_contract::ContractId.data(),std::min(size_t{63},native_contract::ContractId.size()));
    SetState(view_,needed?1u:5u);
    return true;
}
bool Handoff::WaitAndApply(HANDLE stopEvent)
{
    if(!view_) return false;
    const auto initialState=State(view_);
    if(initialState!=1&&initialState!=2&&initialState!=4) return false;
    overlay::SetRecoveryStatus("waiting-for-world",diagnostic_?"One-time recovery test":"Game update detected",
        diagnostic_?"Waiting for a loaded game before testing the complete recovery path. All validation remains active.":
            "Waiting for a loaded game before checking compatibility.",-1);
    const auto started=GetTickCount64();std::uint64_t acknowledgedAt{};bool cleared=false;
    while(WaitForSingleObject(stopEvent,250)==WAIT_TIMEOUT)
    {
        const auto state=State(view_);
        if(state==1)
        {
            const auto now=GetTickCount64();
            const auto acknowledged=InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(view_+228),0,0)==1;
            if(acknowledged&&!acknowledgedAt) acknowledgedAt=now;
            if(!cleared&&acknowledgedAt&&now-acknowledgedAt>=2000)
            {overlay::ClearRecoveryStatus();cleared=true;}
            if(!acknowledged&&now-started>=30000)
            {
                SetState(view_,4);
                overlay::SetRecoveryStatus("failed",diagnostic_?"Recovery test host did not respond":"Recovery host did not respond",
                    "Install the complete matching telemetry package and restart the game. Native game hooks remain disabled.",-1);
                ch::Log("Runtime recovery handshake timed out before host acknowledgement; no native game hooks installed.");
                return false;
            }
            continue;
        }
        if(state==4)
        {
            std::string detail=diagnostic_?"The one-time recovery test could not verify the loaded game. Native hooks remain disabled.":
                "Automatic recovery could not verify this game update. Native hooks remain disabled.";
            try {
                const auto length=Get<std::uint32_t>(view_,16);
                if(length&&length<=Capacity-HeaderBytes) {
                    const auto error=Parse(std::string_view(reinterpret_cast<char*>(view_+HeaderBytes),length)).value("error",std::string{});
                    if(!error.empty()) detail=error.substr(0,512);
                }
            }catch(...){}
            overlay::SetRecoveryStatus("failed",diagnostic_?"Recovery test stopped":"Game update requires a mod update",detail,-1);
            ch::Log("Runtime recovery refused: %s",detail.c_str());return false;
        }
        if(state!=2) {SetState(view_,4);return false;}
        const auto length=Get<std::uint32_t>(view_,16);
        std::string payload;
        if(length&&length<=Capacity-HeaderBytes)
            payload.assign(reinterpret_cast<char*>(view_+HeaderBytes),length);
        MemoryBarrier();
        if(State(view_)!=2||length!=Get<std::uint32_t>(view_,16)||payload.empty()||
            std::memcmp(payload.data(),view_+HeaderBytes,payload.size())!=0)
        {SetState(view_,4);return false;}
        overlay::SetRecoveryStatus("activating",diagnostic_?"Validating the recovery test":"Checking recovered addresses",
            "Verifying the loaded game code and scene before starting telemetry.",90);
        std::string failure;
        if(!ValidateAndApply(payload,reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr)),GetCurrentProcessId(),StartTime(),executableHash_,failure))
        {
            SetState(view_,4);overlay::SetRecoveryStatus("failed",diagnostic_?"Recovery test validation failed":"Game update could not be verified",failure,-1);
            ch::Log("Runtime recovery validation refused before hooks: %s",failure.c_str());return false;
        }
        ch::Log("Runtime recovery accepted complete guarded address contract; starting existing capture paths.");
        return true;
    }
    return false;
}
void Handoff::Complete(bool success)
{
    if(!view_||State(view_)!=2) return;
    SetState(view_,success?3u:4u);
    if(success) overlay::ClearRecoveryStatus();
    else overlay::SetRecoveryStatus("failed",diagnostic_?"Recovery test could not start telemetry":"Recovered telemetry could not start",
        "Address checks passed, but a required native data source could not initialize. Check the native log.",-1);
}
}

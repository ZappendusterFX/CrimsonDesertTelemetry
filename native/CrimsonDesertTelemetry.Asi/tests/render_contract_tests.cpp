#include "render_capture.h"
#include "ambient_probe.h"
#include "native_contract.generated.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>

extern "C" void* CdtFilterTrampoline;
namespace cdt::instruments { bool OwnsCodeAddress(uint64_t) { return false; } }
namespace
{
namespace contract = cdt::native_contract;
using cdt::render::PreflightFailure;
void Check(bool condition, const char* reason)
{
    if (!condition) { std::cerr << reason << '\n'; ExitProcess(1); }
}
struct Image
{
    static constexpr uint32_t Size = static_cast<uint32_t>(std::max<uint64_t>(0x7000000,
        (contract::SceneGlobalRva + sizeof(uint64_t) + 0xFFF) & ~uint64_t{0xFFF}));
    uint8_t* data = static_cast<uint8_t*>(VirtualAlloc(nullptr, Size, MEM_RESERVE, PAGE_NOACCESS));
    IMAGE_DOS_HEADER* dos{};
    IMAGE_NT_HEADERS64* nt{};
    IMAGE_SECTION_HEADER* sections{};
    Image()
    {
        Check(data != nullptr, "Reserve isolated PE fixture");
        Commit(0, 0x1000);
        dos = reinterpret_cast<IMAGE_DOS_HEADER*>(data);
        dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x80;
        nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(data + dos->e_lfanew);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
        nt->FileHeader.NumberOfSections = 2;
        nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
        nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt->OptionalHeader.SizeOfImage = Size;
        nt->OptionalHeader.SizeOfHeaders = 0x400;
        sections = IMAGE_FIRST_SECTION(nt);
        const auto codeStart = static_cast<DWORD>(contract::ContextSignatures.front().rva & ~uint64_t{0xFFF});
        const auto codeEnd = static_cast<DWORD>((contract::HookRva + contract::HookSignature.size() + 0xFFF) & ~uint64_t{0xFFF});
        sections[0].VirtualAddress = codeStart; sections[0].Misc.VirtualSize = codeEnd - codeStart;
        sections[0].Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
        sections[1].VirtualAddress = static_cast<DWORD>(contract::SceneGlobalRva & ~uint64_t{0xFFF});
        sections[1].Misc.VirtualSize = 0x1000;
        sections[1].Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
        Commit(contract::HookRva, contract::HookSignature.size());
        memcpy(data + contract::HookRva, contract::HookSignature.data(), contract::HookSignature.size());
        for (const auto& context : contract::ContextSignatures)
        {
            Commit(context.rva, context.bytes.size());
            memcpy(data + context.rva, context.bytes.data(), context.bytes.size());
        }
    }
    ~Image() { VirtualFree(data, 0, MEM_RELEASE); }
    void Commit(uint64_t rva, size_t size)
    {
        const auto start = rva & ~uint64_t{0xFFF};
        const auto length = ((rva + size + 0xFFF) & ~uint64_t{0xFFF}) - start;
        Check(start + length <= Size && VirtualAlloc(data + start, static_cast<SIZE_T>(length), MEM_COMMIT,
            PAGE_READWRITE) != nullptr, "Commit bounded fixture pages");
    }
    uint64_t Base() const { return reinterpret_cast<uint64_t>(data); }
    void Reject(PreflightFailure expected, uint32_t context = UINT32_MAX)
    {
        const auto result = cdt::render::CheckCapturePreflight(Base());
        Check(result.failure == expected && result.contextIndex == context, "Incorrect preflight failure diagnostic");
        std::array<uint8_t, contract::HookSignature.size()> before{};
        memcpy(before.data(), data + contract::HookRva, before.size());
        // Exercise production StartCapture too, not a test-only initializer.
        Check(!cdt::render::StartCapture(Base(), 20), "Rejected image installed a production hook");
        Check(CdtFilterTrampoline == nullptr && !cdt::render::OwnsCodeAddress(Base() + contract::HookRva),
            "Preflight rejection reached hook installation");
        Check(memcmp(before.data(), data + contract::HookRva, before.size()) == 0,
            "Preflight rejection altered fixture code");
    }
};

void CheckFileSignatures(const wchar_t* path, bool ambient)
{
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    Check(file.good(), "Open optional current game executable");
    IMAGE_DOS_HEADER dos{}; file.read(reinterpret_cast<char*>(&dos), sizeof(dos));
    Check(file.good() && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0, "Current file DOS header");
    file.seekg(dos.e_lfanew);
    IMAGE_NT_HEADERS64 nt{}; file.read(reinterpret_cast<char*>(&nt), sizeof(nt));
    Check(file.good() && nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.NumberOfSections <= 96 &&
        nt.FileHeader.SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER64), "Current file PE header");
    std::vector<IMAGE_SECTION_HEADER> sections(nt.FileHeader.NumberOfSections);
    file.read(reinterpret_cast<char*>(sections.data()), static_cast<std::streamsize>(sections.size() * sizeof(sections[0])));
    Check(file.good(), "Current file section table");
    const auto verify = [&](uint64_t rva, std::span<const uint8_t> signature)
    {
        for (const auto& section : sections)
        {
            if (rva < section.VirtualAddress || rva - section.VirtualAddress >= section.SizeOfRawData) continue;
            const auto offset = rva - section.VirtualAddress;
            Check(signature.size() <= section.SizeOfRawData - offset, "Signature crosses raw section");
            file.seekg(static_cast<std::streamoff>(section.PointerToRawData + offset));
            std::vector<uint8_t> actual(signature.size());
            file.read(reinterpret_cast<char*>(actual.data()), static_cast<std::streamsize>(actual.size()));
            Check(file.good() && std::equal(actual.begin(), actual.end(), signature.begin()),
                "Generated hook/context signature differs from current executable");
            return;
        }
        Check(false, "Generated signature RVA missing from current executable");
    };
    verify(contract::HookRva, contract::HookSignature);
    for (const auto& context : contract::ContextSignatures) verify(context.rva, context.bytes);
    if (ambient)
    {
        verify(cdt::render::AmbientHookRvas[0], cdt::render::AmbientSignatureA);
        verify(cdt::render::AmbientHookRvas[1], cdt::render::AmbientSignatureB);
    }
    std::cout << "PASS generated hook and all caller contexts against current EXE file (no process access)"
        << (ambient ? ", including ambient hook bytes\n" : "; ambient deliberately not checked\n");
}
}

int wmain(int argc, wchar_t** argv)
{
    using namespace cdt::render;
    Check(CheckCapturePreflight(0).failure == PreflightFailure::MissingImage && !StartCapture(0, 20),
        "Missing module did not fail before hook installation");
    Check(CheckCapturePreflight(1).failure == PreflightFailure::UnreadableImage && !StartCapture(1, 20),
        "Unreadable module did not fail before hook installation");
    Check(CheckCapturePreflight(UINT64_MAX - 4).failure == PreflightFailure::MalformedImage,
        "Overflowing module address accepted");
    Image image;
    Check(static_cast<bool>(CheckCapturePreflight(image.Base())), "Valid generated contract fixture rejected");
    image.dos->e_magic = 0; image.Reject(PreflightFailure::MalformedImage); image.dos->e_magic = IMAGE_DOS_SIGNATURE;
    image.nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    image.Reject(PreflightFailure::MalformedImage); image.nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    image.nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(contract::HookRva + 0x1000);
    image.Reject(PreflightFailure::SceneGlobalOutsideImage); image.nt->OptionalHeader.SizeOfImage = Image::Size;
    image.sections[0].Characteristics &= ~IMAGE_SCN_MEM_EXECUTE;
    image.Reject(PreflightFailure::HookOutsideExecutableSection); image.sections[0].Characteristics |= IMAGE_SCN_MEM_EXECUTE;
    image.data[contract::HookRva] ^= 1;
    image.Reject(PreflightFailure::HookSignatureMismatch); image.data[contract::HookRva] ^= 1;
    for (uint32_t i = 0; i < contract::ContextSignatures.size(); ++i)
    {
        const auto rva = contract::ContextSignatures[i].rva;
        image.data[rva] ^= 1; image.Reject(PreflightFailure::ContextSignatureMismatch, i); image.data[rva] ^= 1;
    }
    const auto section = image.sections[0];
    image.sections[0].VirtualAddress = static_cast<DWORD>(contract::HookRva);
    image.sections[0].Misc.VirtualSize = static_cast<DWORD>(contract::HookSignature.size());
    image.Reject(PreflightFailure::ContextOutsideExecutableSection, 0); image.sections[0] = section;
    Check(static_cast<bool>(CheckCapturePreflight(image.Base())) && CdtFilterTrampoline == nullptr,
        "Read-only valid preflight unexpectedly installed a hook");
    Check(!CheckAmbientPreflight(image.Base()),"missing ambient hook context accepted");
    image.sections[0].VirtualAddress=0x3800000;
    image.sections[0].Misc.VirtualSize=static_cast<DWORD>(contract::HookRva+0x1000-0x3800000);
    for(unsigned n=0;n<2;++n)
    {
        image.Commit(AmbientHookRvas[n]-6,21);
        const auto& sig=n ? AmbientSignatureB : AmbientSignatureA;
        memcpy(image.data+AmbientHookRvas[n],sig.data(),sig.size());
        memcpy(image.data+AmbientHookRvas[n]-6,AmbientDispatchSignature.data(),AmbientDispatchSignature.size());
    }
    image.Commit(AmbientSourceRvas[0],7); image.Commit(AmbientSourceRvas[1],7);
    const std::array<uint8_t,7> sourceA{0x48,0x8B,0xAF,0x98,0,0,0}, sourceB{0x48,0x8B,0x9D,0x98,0,0,0};
    memcpy(image.data+AmbientSourceRvas[0],sourceA.data(),7); memcpy(image.data+AmbientSourceRvas[1],sourceB.data(),7);
    Check(CheckAmbientPreflight(image.Base()) == !CameraRecoveryDiagnostic,
        "Ambient preflight disagrees with the supported/private-diagnostic contract");
    for(auto rva : {AmbientHookRvas[0],AmbientHookRvas[1],AmbientHookRvas[0]-6,AmbientHookRvas[1]-6,
                    AmbientSourceRvas[0],AmbientSourceRvas[1]})
    {
        image.data[rva]^=1; Check(!CheckAmbientPreflight(image.Base()),"changed ambient context accepted"); image.data[rva]^=1;
    }
    image.sections[0]=section;
    Check(!CheckAmbientPreflight(image.Base()),"ambient hooks outside executable section accepted");
    if (argc == 2 || argc == 3)
    {
        const bool ambient = argc == 3 && wcscmp(argv[2], L"--ambient") == 0;
        Check(argc == 2 || ambient, "Unknown file-signature test option");
        CheckFileSignatures(argv[1], ambient);
    }
    std::cout << "PASS production preflight and StartCapture rejection: missing/unreadable/malformed image, "
        "bounds, executable sections, hook bytes and each caller context; no hook installed\n";
}

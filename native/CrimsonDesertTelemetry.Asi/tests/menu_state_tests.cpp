#include "menu_state.h"
#include <windows.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
using namespace cdt::menu_state;
using U64 = std::uint64_t;
using U32 = std::uint32_t;
void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class T> void Put(void* at, const T& value) { std::memcpy(at, &value, sizeof(value)); }
template<class T> U64 Address(T* p) { return reinterpret_cast<U64>(p); }
void Hex(std::uint8_t* at, std::string_view bytes)
{
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
    for (std::size_t i = 0; i < bytes.size(); i += 2)
        *at++ = static_cast<std::uint8_t>(16 * digit(bytes[i]) + digit(bytes[i + 1]));
}
void Rel(std::uint8_t* base, U32 next, U32 target)
{
    Put(base + next - 4, static_cast<std::int32_t>(target) - static_cast<std::int32_t>(next));
}

struct Ui
{
    std::array<std::uint8_t, 0xB8> app{};
    std::array<std::uint8_t, 0xA40> manager{};
    std::array<std::uint8_t, 0x50> event{};
    std::array<std::uint8_t, 0x90> owner{};
    std::array<std::uint8_t, 0xB8> context{};
    std::array<std::uint8_t, 0xA8> entry{};
    std::array<std::uint8_t, 0x30> holder{};
    std::array<std::uint8_t, 0x280> control{};
    std::array<U64, 2> root{}, secondRoot{}, indexedEntries{}, vector{};
    std::array<std::array<U64, 4>, 2> subscriptions{};

    Ui(std::uint8_t* global, U64 callback, U64 vtable)
    {
        Put(global, Address(app.data()));
        Put(app.data() + 0xB0, Address(manager.data()));
        Put(manager.data() + 0xA38, Address(event.data()));
        Put(event.data() + 0x40, Address(subscriptions.data()));
        Put(event.data() + 0x48, U32{1});
        Put(event.data() + 0x4C, U32{2});
        subscriptions[0] = {Address(root.data()), callback, 0, 0};
        subscriptions[1] = {Address(secondRoot.data()), callback, 0, 0};
        root = secondRoot = {vtable, Address(owner.data())};
        Put(owner.data() + 0x8A, std::uint16_t{1});
        Put(owner.data() + 0x58, Address(context.data()));
        Put(context.data() + 0xB0, Address(manager.data()));
        Put(manager.data() + 0xB8, Address(vector.data()));
        vector = {Address(indexedEntries.data()), 2};
        indexedEntries[1] = Address(entry.data());
        Put(entry.data() + 0xA0, Address(holder.data()));
        Put(holder.data() + 0x28, Address(control.data()));
        Put(control.data() + 0x120, Address(entry.data()));
        Flags(0x0001);
    }
    void Flags(std::uint16_t flags) { Put(control.data() + 0x27C, flags); }
};

void RegistryCode(std::uint8_t* base, U32 start, U32 global, U32 callback)
{
    Hex(base + start, "4C8BE1488B05000000004C8BB8B00000004C897D40498BBF380A000033DB84D20F8400000000"
        "48894DC0488D1500000000488955C848895DD066895DD88B4F488B474C");
    Rel(base, start + 10, global);
    Rel(base, start + 38, start + 0x500);
    Rel(base, start + 49, callback);
    Hex(base + start + 0x130, "8B474848C1E00548034740C5FC1045C0C5FC1100FF4748");
    Hex(base + start + 0x500, "8B47488BF385C0746C4C8B4740488D150000000066660F1F840000000000448BF649C1E6054B8D0C304C3B217506483B51087408");
    Rel(base, start + 0x500 + 20, callback);
}

void Synthetic(U32 shift)
{
    constexpr U32 Size = 0xD000;
    std::vector<std::uint8_t> image(Size);
    auto* base = image.data();
    IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 0x80;
    Put(base, dos);
    IMAGE_NT_HEADERS64 nt{}; nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64; nt.FileHeader.NumberOfSections = 3;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC; nt.OptionalHeader.SizeOfImage = Size;
    nt.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION] = {0x9000, sizeof(RUNTIME_FUNCTION)};
    Put(base + dos.e_lfanew, nt);
    std::array<IMAGE_SECTION_HEADER, 3> sections{};
    sections[0].VirtualAddress = 0x1000; sections[0].Misc.VirtualSize = 0x8000;
    sections[0].Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    sections[1].VirtualAddress = 0x9000; sections[1].Misc.VirtualSize = 0x2000;
    sections[1].Characteristics = IMAGE_SCN_MEM_READ;
    sections[2].VirtualAddress = 0xB000; sections[2].Misc.VirtualSize = 0x2000;
    sections[2].Characteristics = IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_READ;
    Put(base + dos.e_lfanew + sizeof(nt), sections);
    const U32 start = 0x2000 + shift, callback = 0x3700 + shift, getter = 0x4400 + shift, handler = 0x5500 + shift;
    const U32 global = 0xB800 + shift, locator = 0x9100 + shift, vtable = 0x9A00 + shift;
    const U32 descriptor = 0xB200 + shift;
    RegistryCode(base, start, global, callback);
    for (const auto thunk : {callback, getter, handler})
    {
        base[thunk] = 0xE9;
        Rel(base, thunk + 5, thunk + 0x100);
    }
    Hex(base + getter + 0x100, "488B41084989C94885C0743E440FB7808A000000B9FFFF0000664139C8742B488B4058"
        "488B90B00000004C8B92B8000000453B42087313498B024A833CC0007409410FB68130010000C330C0C3");
    const auto eventBody = handler + 0x100;
    const auto close = eventBody + (shift ? 0x12B : 0x127);
    Hex(base + eventBody, "48895C2408574883EC20410FB6014889CF3C0E0F8500000000");
    Rel(base, eventBody + 25, close);
    Hex(base + close, "3C0F0F8500000000488B410831DBC6813001000000B9FFFF00000FB7908A0000006639CA750489D8EB24"
        "488B4058488B88B00000004C8B81B8000000413B500872054889D8EB07498B00488B04D0");
    const auto flagGuard = close + 0x4E;
    Hex(base + flagGuard, "488B88A00000004885C97406488B4928EB034889D980A17D020000FEF6817D02000002741C80B97C020000007413"
        "488B89200100004889CA488B4958E8");
    Put(base + locator, std::array<U32, 6>{1, 0, 0, descriptor, 0x9900, locator});
    Put(base + descriptor + 16, ".?AVUIGamePlayControl_Root_MainMenu@uiCommonScript@pa@@");
    Put(base + vtable - 8, Address(base + locator));
    Put(base + vtable + 6 * 8, Address(base + getter));
    Put(base + vtable + 146 * 8, Address(base + handler));
    Ui ui(base + global, Address(base + callback), Address(base + vtable));
    Check(Initialize(Address(base), Size), "UI registry relocation failed");
    Check(Sample() == State::Closed, "closed menu lost");
    ui.Flags(0x0301);
    Check(Sample() == State::Open, "verified menu lost");
    ui.Flags(0x0201);
    Check(Sample() == State::Unknown, "unvalidated control bits authorized a menu");
    ui.Flags(0x0301);
    Put(ui.event.data() + 0x48, U32{2});
    Check(Sample() == State::Unknown, "two MainMenu roots authorized a menu");
    Put(ui.event.data() + 0x48, U32{1025});
    Check(Sample() == State::Unknown, "oversized registry authorized a menu");
    Put(ui.event.data() + 0x48, U32{1});
    Put(ui.control.data() + 0x120, U64{0});
    Check(Sample() == State::Unknown, "unpaired control authorized a menu");
    Put(ui.control.data() + 0x120, Address(ui.entry.data()));
    base[flagGuard + 22] ^= 1;
    Check(Sample() == State::Unknown, "changed control offset authorized a menu");
    base[flagGuard + 22] ^= 1;
    base[descriptor + 16] ^= 1;
    Check(Sample() == State::Unknown, "foreign RTTI authorized a menu");
    RegistryCode(base, 0x7000, global, callback);
    Check(!Initialize(Address(base), Size) && Sample() == State::Unknown, "ambiguous registry recovered");
}

// Optional replay reads preserved EXEs as data into a private, nonexecutable image
// at their preferred base. Only the synthetic UI global is written in the copy.
void Replay(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    const auto read = [&](U32 offset, void* destination, std::size_t bytes) {
        file.seekg(offset);
        file.read(static_cast<char*>(destination), static_cast<std::streamsize>(bytes));
        Check(static_cast<bool>(file), "could not read preserved EXE");
    };
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    read(0, &dos, sizeof(dos));
    Check(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 && dos.e_lfanew < 0x100000, "invalid replay DOS header");
    const auto pe = static_cast<U32>(dos.e_lfanew);
    read(pe, &nt, sizeof(nt));
    const auto size = nt.OptionalHeader.SizeOfImage;
    Check(nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.NumberOfSections <= 96 &&
        size >= 0x7200000 && size <= 0x40000000, "invalid replay PE header");
    auto* base = static_cast<std::uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(nt.OptionalHeader.ImageBase),
        size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(base != nullptr, "could not reserve preferred EXE base for replay");
    read(0, base, nt.OptionalHeader.SizeOfHeaders);
    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(base + pe + sizeof(nt));
    for (std::size_t i = 0; i < nt.FileHeader.NumberOfSections; ++i)
    {
        const auto& section = sections[i];
        Check(section.VirtualAddress < size && section.SizeOfRawData <= size - section.VirtualAddress, "invalid replay section");
        if (section.SizeOfRawData) read(section.PointerToRawData, base + section.VirtualAddress, section.SizeOfRawData);
    }
    Ui ui(base + 0x71BDF48, Address(base + 0x1022660), Address(base + 0x5A012F0));
    Check(Initialize(Address(base), size), "preserved EXE registry recovery failed");
    Check(Sample() == State::Closed, "preserved EXE closed-menu layout failed");
    ui.Flags(0x0301);
    Check(Sample() == State::Open, "preserved EXE open-menu layout failed");
    VirtualFree(base, 0, MEM_RELEASE);
}
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        Synthetic(0);
        Synthetic(0x300);
        for (int i = 1; i < argc; ++i) Replay(argv[i]);
        std::cout << "PASS guarded UI registry relocation, menu type and state, refusal cases"
            << (argc > 1 ? ", and preserved executable replay\n" : "\n");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

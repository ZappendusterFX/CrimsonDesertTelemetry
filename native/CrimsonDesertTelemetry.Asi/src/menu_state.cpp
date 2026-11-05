#include "menu_state.h"
#include "recovery_roles.h"
#include <functional>
#include <span>

namespace cdt::menu_state
{
namespace
{
using recovery::roles::detail::Image;
constexpr auto RegistrationTag = std::to_array<std::uint8_t>({
    0x4C,0x8B,0xB8,0xB0,0,0,0,0x4C,0x89,0x7D,0x40,0x49,0x8B,0xBF,0x38,0x0A,0,0,0x33,0xDB,0x84,0xD2,0x0F,0x84});
constexpr auto RegistrationHead = std::to_array<std::uint8_t>({0x4C,0x8B,0xE1,0x48,0x8B,0x05});
constexpr auto CallbackLea = std::to_array<std::uint8_t>({0x48,0x89,0x4D,0xC0,0x48,0x8D,0x15});
constexpr auto RegistrationTail = std::to_array<std::uint8_t>({
    0x48,0x89,0x55,0xC8,0x48,0x89,0x5D,0xD0,0x66,0x89,0x5D,0xD8,0x8B,0x4F,0x48,0x8B,0x47,0x4C});
constexpr auto Append = std::to_array<std::uint8_t>({
    0x8B,0x47,0x48,0x48,0xC1,0xE0,0x05,0x48,0x03,0x47,0x40,0xC5,0xFC,0x10,0x45,0xC0,0xC5,0xFC,0x11,0,0xFF,0x47,0x48});
constexpr auto RemovalHead = std::to_array<std::uint8_t>({
    0x8B,0x47,0x48,0x8B,0xF3,0x85,0xC0,0x74,0x6C,0x4C,0x8B,0x47,0x40,0x48,0x8D,0x15});
constexpr auto RemovalTail = std::to_array<std::uint8_t>({
    0x66,0x66,0x0F,0x1F,0x84,0,0,0,0,0,0x44,0x8B,0xF6,0x49,0xC1,0xE6,0x05,0x4B,0x8D,0x0C,0x30,
    0x4C,0x3B,0x21,0x75,0x06,0x48,0x3B,0x51,0x08,0x74,0x08});
constexpr auto Getter = std::to_array<std::uint8_t>({
    0x48,0x8B,0x41,0x08,0x49,0x89,0xC9,0x48,0x85,0xC0,0x74,0x3E,0x44,0x0F,0xB7,0x80,0x8A,0,0,0,
    0xB9,0xFF,0xFF,0,0,0x66,0x41,0x39,0xC8,0x74,0x2B,0x48,0x8B,0x40,0x58,0x48,0x8B,0x90,0xB0,0,0,0,
    0x4C,0x8B,0x92,0xB8,0,0,0,0x45,0x3B,0x42,0x08,0x73,0x13,0x49,0x8B,0x02,0x4A,0x83,0x3C,0xC0,0,0x74,
    0x09,0x41,0x0F,0xB6,0x81,0x30,0x01,0,0,0xC3,0x30,0xC0,0xC3});
constexpr auto EventHead = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x41,0x0F,0xB6,0x01,0x48,0x89,0xCF,0x3C,0x0E,0x0F,0x85});
constexpr auto CloseEventHead = std::to_array<std::uint8_t>({0x3C,0x0F,0x0F,0x85});
constexpr auto CloseChain = std::to_array<std::uint8_t>({
    0x48,0x8B,0x41,0x08,0x31,0xDB,0xC6,0x81,0x30,0x01,0,0,0,0xB9,0xFF,0xFF,0,0,
    0x0F,0xB7,0x90,0x8A,0,0,0,0x66,0x39,0xCA,0x75,0x04,0x89,0xD8,0xEB,0x24,
    0x48,0x8B,0x40,0x58,0x48,0x8B,0x88,0xB0,0,0,0,0x4C,0x8B,0x81,0xB8,0,0,0,
    0x41,0x3B,0x50,0x08,0x72,0x05,0x48,0x89,0xD8,0xEB,0x07,0x49,0x8B,0,0x48,0x8B,0x04,0xD0});
constexpr auto ControlFlags = std::to_array<std::uint8_t>({
    0x48,0x8B,0x88,0xA0,0,0,0,0x48,0x85,0xC9,0x74,0x06,0x48,0x8B,0x49,0x28,0xEB,0x03,0x48,0x89,0xD9,
    0x80,0xA1,0x7D,0x02,0,0,0xFE,0xF6,0x81,0x7D,0x02,0,0,0x02,0x74,0x1C,0x80,0xB9,0x7C,0x02,0,0,0,0x74,0x13,
    0x48,0x8B,0x89,0x20,0x01,0,0,0x48,0x89,0xCA,0x48,0x8B,0x49,0x58,0xE8});
constexpr std::string_view MainMenuName = ".?AVUIGamePlayControl_Root_MainMenu@uiCommonScript@pa@@";
constexpr auto Executable = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
constexpr auto ReadOnlyData = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_WRITE;
constexpr std::uint8_t Jump[] = {0xE9};
Image image;
std::uint64_t registry{}, callback{};

bool Exact(std::uint64_t rva, std::span<const std::uint8_t> expected)
{
    std::array<std::uint8_t, 128> bytes;
    return expected.size() <= bytes.size() && image.In(rva, expected.size(), Executable) &&
        image.Read(rva, bytes.data(), expected.size()) && std::equal(expected.begin(), expected.end(), bytes.begin());
}

std::uint64_t Relative(std::uint64_t next)
{
    std::int32_t displacement{};
    if (next < sizeof(displacement) || !image.Read(next - sizeof(displacement), &displacement, sizeof(displacement))) return 0;
    const auto target = static_cast<std::int64_t>(next) + displacement;
    return target > 0 && target < image.size ? static_cast<std::uint64_t>(target) : 0;
}

std::uint64_t JumpBody(std::uint64_t thunk)
{
    if (!image.In(thunk, 5, Executable) || !Exact(thunk, Jump)) return 0;
    const auto body = Relative(thunk + 5);
    return image.In(body, 1, Executable) ? body : 0;
}

bool ResolveRegistry(std::uint64_t start, std::uint64_t& global, std::uint64_t& callbackRva)
{
    if (!Exact(start, RegistrationHead) || !Exact(start + 38, CallbackLea) ||
        !Exact(start + 49, RegistrationTail)) return false;
    global = Relative(start + 10);
    callbackRva = Relative(start + 49);
    const auto removal = Relative(start + 38);
    if (!image.In(global, sizeof(std::uint64_t), IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE, IMAGE_SCN_MEM_EXECUTE) ||
        !JumpBody(callbackRva) || removal <= start + 67 || removal - start > 4096 ||
        !Exact(removal, RemovalHead) || !Exact(removal + 20, RemovalTail) || Relative(removal + 20) != callbackRva)
        return false;
    std::array<std::uint8_t, 4096> body;
    const auto size = static_cast<std::size_t>(removal - start);
    return image.In(start, size, Executable) && image.Read(start, body.data(), size) &&
        std::search(body.begin(), body.begin() + size, Append.begin(), Append.end()) != body.begin() + size;
}

bool FindRegistry()
{
    constexpr std::size_t Chunk = 65536;
    std::array<std::uint8_t, Chunk + RegistrationTag.size() - 1> bytes;
    const auto searcher = std::boyer_moore_horspool_searcher(RegistrationTag.begin(), RegistrationTag.end());
    std::uint64_t foundGlobal{}, foundCallback{}, scanned{};
    for (const auto& section : image.sections)
    {
        if ((section.Characteristics & Executable) != Executable || section.Characteristics & IMAGE_SCN_MEM_DISCARDABLE)
            continue;
        const auto size = section.Misc.VirtualSize;
        if (!size) continue;
        if (!image.In(section.VirtualAddress, size, Executable) || (scanned += size) > image.size) return false;
        for (std::size_t offset = 0; offset < size; offset += Chunk)
        {
            const auto length = std::min<std::size_t>(bytes.size(), size - offset);
            const auto rva = section.VirtualAddress + offset;
            if (!image.Read(rva, bytes.data(), length)) return false;
            const auto end = bytes.begin() + length;
            for (auto hit = std::search(bytes.begin(), end, searcher); hit != end; hit = std::search(hit + 1, end, searcher))
            {
                const auto tag = rva + static_cast<std::size_t>(hit - bytes.begin());
                std::uint64_t global{}, callbackRva{};
                if (tag < 10 || !ResolveRegistry(tag - 10, global, callbackRva)) continue;
                if (foundGlobal) return false;
                foundGlobal = global;
                foundCallback = callbackRva;
            }
        }
    }
    if (!foundGlobal) return false;
    registry = image.base + foundGlobal;
    callback = image.base + foundCallback;
    return true;
}

template<class T> bool Read(std::uint64_t address, T& value)
{
    return ch::mem::PlausiblePointer(address) && ch::mem::PlausiblePointer(address + sizeof(T) - 1) &&
        ch::mem::SafeRead(reinterpret_cast<const void*>(address), &value, sizeof(T));
}

bool ReadPointer(std::uint64_t address, std::uint64_t& value)
{
    return Read(address, value) && ch::mem::PlausiblePointer(value);
}

bool IsMainMenu(std::uint64_t vtable)
{
    if (vtable < image.base + 8 || !image.In(vtable - image.base - 8, 16, IMAGE_SCN_MEM_READ, ReadOnlyData)) return false;
    std::uint64_t locator{};
    std::array<std::uint32_t, 6> col{};
    std::array<char, MainMenuName.size() + 1> name{};
    return Read(vtable - 8, locator) && locator >= image.base &&
        image.In(locator - image.base, sizeof(col), IMAGE_SCN_MEM_READ, ReadOnlyData) &&
        Read(locator, col) && col[0] == 1 && col[1] == 0 && col[2] == 0 && col[5] == locator - image.base &&
        image.In(col[4], 16, IMAGE_SCN_MEM_READ, ReadOnlyData) &&
        image.In(col[3], 16 + name.size(), IMAGE_SCN_MEM_READ, IMAGE_SCN_MEM_EXECUTE) &&
        image.Read(std::uint64_t{col[3]} + 16, name.data(), name.size()) && name.back() == '\0' &&
        std::string_view(name.data(), name.size() - 1) == MainMenuName;
}

bool VerifyLayout(std::uint64_t vtable)
{
    std::uint64_t getter{}, handler{};
    if (!image.In(vtable - image.base, 147 * sizeof(vtable), IMAGE_SCN_MEM_READ, ReadOnlyData) ||
        !Read(vtable + 6 * sizeof(vtable), getter) || getter < image.base ||
        !Read(vtable + 146 * sizeof(vtable), handler) || handler < image.base ||
        !Exact(JumpBody(getter - image.base), Getter)) return false;
    const auto eventBody = JumpBody(handler - image.base);
    if (!Exact(eventBody, EventHead)) return false;
    const auto close = Relative(eventBody + EventHead.size() + sizeof(std::int32_t));
    const auto chain = close + CloseEventHead.size() + sizeof(std::int32_t);
    return close > eventBody && close - eventBody <= 512 && Exact(close, CloseEventHead) &&
        Exact(chain, CloseChain) && Exact(chain + CloseChain.size(), ControlFlags);
}

struct RegistryHeader { std::uint64_t entries{}; std::uint32_t count{}, capacity{}; };
struct Subscription { std::uint64_t root, callback, metadata[2]; };
struct VectorHeader { std::uint64_t entries{}; std::uint32_t count{}; };
static_assert(sizeof(RegistryHeader) == 16 && sizeof(Subscription) == 32);
constexpr std::uint32_t MaxSubscriptions = 1024;

bool ReadRegistry(std::uint64_t& app, std::uint64_t& manager, std::uint64_t& event, RegistryHeader& header)
{
    return ReadPointer(registry, app) && ReadPointer(app + 0xB0, manager) &&
        ReadPointer(manager + 0xA38, event) && Read(event + 0x40, header) &&
        header.count <= MaxSubscriptions && header.count <= header.capacity && header.capacity <= 4096;
}

struct ControlChain
{
    std::uint64_t owner{}, context{}, manager{}, vector{}, entries{}, entry{}, holder{}, control{};
    std::uint32_t count{};
    std::uint16_t index{}, flags{};
    bool operator==(const ControlChain&) const = default;
};

bool ReadControl(std::uint64_t root, ControlChain& chain)
{
    VectorHeader header;
    std::uint64_t backlink{};
    if (!ReadPointer(root + 8, chain.owner) || !Read(chain.owner + 0x8A, chain.index) || chain.index == 0xFFFF ||
        !ReadPointer(chain.owner + 0x58, chain.context) || !ReadPointer(chain.context + 0xB0, chain.manager) ||
        !ReadPointer(chain.manager + 0xB8, chain.vector) ||
        !ReadPointer(chain.vector, header.entries) || !Read(chain.vector + 8, header.count) ||
        header.count > 65536 || chain.index >= header.count ||
        !ReadPointer(header.entries + std::uint64_t{chain.index} * 8, chain.entry) ||
        !ReadPointer(chain.entry + 0xA0, chain.holder) || !ReadPointer(chain.holder + 0x28, chain.control) ||
        !Read(chain.control + 0x27C, chain.flags) || !Read(chain.control + 0x120, backlink) ||
        backlink != chain.entry) return false;
    chain.count = header.count;
    chain.entries = header.entries;
    return true;
}
}

bool Initialize(std::uint64_t moduleBase, std::uint32_t imageSize)
{
    registry = callback = 0;
    try { return image.Open(moduleBase, imageSize) && FindRegistry(); }
    catch (...) { return false; }
}

State Sample()
{
    if (!registry) return State::Unknown;
    std::uint64_t app{}, manager{}, event{}, root{}, vtable{};
    RegistryHeader header;
    std::array<Subscription, MaxSubscriptions> entries, after;
    if (!ReadRegistry(app, manager, event, header) || !header.count || !ch::mem::PlausiblePointer(header.entries))
        return State::Unknown;
    const auto bytes = header.count * sizeof(Subscription);
    if (!ch::mem::SafeRead(reinterpret_cast<const void*>(header.entries), entries.data(), bytes)) return State::Unknown;
    for (const auto& sub : std::span(entries).first(header.count))
    {
        if (sub.callback != callback || sub.root == root) continue;
        std::uint64_t candidate{};
        if (!Read(sub.root, candidate)) return State::Unknown;
        if (!IsMainMenu(candidate)) continue;
        if (root || !VerifyLayout(candidate)) return State::Unknown;
        root = sub.root;
        vtable = candidate;
    }
    ControlChain first, second;
    std::uint64_t appAfter{}, managerAfter{}, eventAfter{}, vtableAfter{};
    RegistryHeader headerAfter;
    if (!root || !ReadControl(root, first) || !ReadControl(root, second) || first != second ||
        !Read(root, vtableAfter) || vtableAfter != vtable ||
        !ReadRegistry(appAfter, managerAfter, eventAfter, headerAfter) ||
        appAfter != app || managerAfter != manager || eventAfter != event ||
        std::memcmp(&header, &headerAfter, sizeof(header)) != 0 ||
        !ch::mem::SafeRead(reinterpret_cast<const void*>(header.entries), after.data(), bytes) ||
        std::memcmp(entries.data(), after.data(), bytes) != 0) return State::Unknown;
    // The close handler clears bit 0 and checks bit 1 separately. The same
    // live MainMenu can carry unrelated high bits (00/03 and 60/63 observed).
    if ((first.flags & 0xFF) != 1) return State::Unknown;
    const auto active = (first.flags >> 8) & 0x03;
    if (active == 3) return State::Open;
    return active == 0 ? State::Closed : State::Unknown;
}
}

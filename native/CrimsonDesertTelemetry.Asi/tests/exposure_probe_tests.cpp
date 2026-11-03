#include "exposure_probe.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>

using namespace cdt::render;
namespace
{
int checks{};
void Check(bool value, const char* message)
{ ++checks; if (!value) { std::cerr << message << '\n'; std::exit(1); } }
struct Segment { uint64_t base; std::vector<uint8_t> data; };
struct Fixture
{
    std::vector<Segment> memory{{0x10000, std::vector<uint8_t>(0xA0)},
        {0x20000, std::vector<uint8_t>(RendererExposureOffset + 8)}, {0x30000, std::vector<uint8_t>(0x118)},
        {0x40000, std::vector<uint8_t>(0x38)}, {0x50000, std::vector<uint8_t>(0x170)}};
    uint64_t fail{}, mutate{}; bool mutateOnSecond{}; int hits{}; size_t cacheBytes{};
    template<class T> void Put(uint64_t address, T value)
    {
        for (auto& s : memory) if (address >= s.base && address + sizeof(T) <= s.base + s.data.size())
        { memcpy(s.data.data() + address - s.base, &value, sizeof(T)); return; }
        Check(false, "fixture write bounds");
    }
    Fixture()
    {
        Put(0x10010, uint64_t{0x20000}); Put((0x20000ULL + RendererSkyOffset), uint64_t{0x10000});
        Put((0x20000ULL + RendererExposureOffset), uint64_t{0x30000}); Put(0x30010, uint64_t{0x20000});
        Put(0x300C0, uint64_t{0x40000}); Put(0x40030, uint64_t{0x50000});
        Put(0x50168, uint64_t{0x60000}); Put(0x300D0, uint64_t{0x70000});
        Put(0x500C0, uint32_t{4}); Put(0x500C4, uint32_t{32}); Put(0x500AE, uint8_t{2});
        Put(0x300D8, 0.08f); Put(0x300F0, uint32_t{0xFFFefef5}); // packed NaN is legitimate
    }
    bool Read(uint64_t address, void* destination, size_t bytes)
    {
        if (address == fail) return false;
        if (address == mutate && ++hits == (mutateOnSecond ? 2 : 1)) return false;
        if (address == 0x300D8) cacheBytes = bytes;
        for (const auto& s : memory)
            if (address >= s.base && bytes <= s.data.size() && address - s.base <= s.data.size() - bytes)
            { memcpy(destination, s.data.data() + address - s.base, bytes); return true; }
        return false;
    }
    bool Sample(ExposureCacheSample& s, uint64_t sky = 0x10000)
    { return ReadExposureCache(sky, s, [&](uint64_t a, void* d, size_t n) { return Read(a,d,n); }); }
};
}
int main()
{
    ExposureCacheSample a{}, b{}; Fixture f;
    Check(f.Sample(a), "valid known chain"); Check(f.cacheBytes == 64, "never read pointers at +118");
    b=a; ExposureCacheContext c{};
    BeginExposureCache(c,a,true,100); EndExposureCache(c,b,true,101);
    Check(c.flags==31 && c.beginTick==100 && c.endTick==101, "stable positive cache flags");
    Check(c.before==a.data && c.after==a.data, "preserve packed bytes");
    for (const auto address : {0x10010ULL,(0x20000ULL + RendererSkyOffset),(0x20000ULL + RendererExposureOffset),0x30010ULL,0x300C0ULL,
         0x40030ULL,0x50168ULL,0x300D0ULL,0x500C0ULL,0x500C4ULL,0x500AEULL,0x300D8ULL})
    {
        Fixture missing; missing.fail=address; b=a;
        Check(!missing.Sample(b), "unreadable field rejected");
        Check(b.resource==0 && std::all_of(b.data.begin(),b.data.end(),[](uint8_t v){return v==0;}), "no stale/partial output");
    }
    for (const auto address : {0x10010ULL,(0x20000ULL + RendererSkyOffset),(0x20000ULL + RendererExposureOffset),0x30010ULL,0x300C0ULL,0x40030ULL,0x50168ULL,0x300D0ULL})
    {
        Fixture changing; changing.mutate=address; changing.mutateOnSecond=true;
        Check(!changing.Sample(b), "changed chain during read rejected");
    }
    for (const auto p : {0ULL,0xFFULL,0x800000000000ULL,UINT64_MAX})
        Check(!f.Sample(b,p), "invalid sky pointer rejected");
    Fixture back; back.Put((0x20000ULL + RendererSkyOffset),uint64_t{0x90000}); Check(!back.Sample(b),"sky backreference");
    Fixture ownerBack; ownerBack.Put(0x30010,uint64_t{0x90000}); Check(!ownerBack.Sample(b),"owner backreference");
    Fixture layout; layout.Put(0x500C0,uint32_t{16}); Check(!layout.Sample(b),"wrong stride");
    layout.Put(0x500C0,uint32_t{4}); layout.Put(0x500C4,uint32_t{19}); Check(!layout.Sample(b),"short view");
    layout.Put(0x500C4,uint32_t{16385}); Check(!layout.Sample(b),"oversized view");
    layout.Put(0x500C4,uint32_t{32}); layout.Put(0x500AE,uint8_t{1}); Check(!layout.Sample(b),"wrong mode");
    b=a; b.resource+=8; BeginExposureCache(c,a,true,100); EndExposureCache(c,b,true,101);
    Check(c.flags==3,"changed resource is not a stable pair");
    b=a; b.data[8]^=1; BeginExposureCache(c,a,true,100); EndExposureCache(c,b,true,101);
    Check(c.flags==7,"changed bytes not normalized");
    for (float value : {0.0f,-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
    {
        b=a; memcpy(b.data.data(),&value,4); BeginExposureCache(c,b,true,100); EndExposureCache(c,b,true,101);
        Check(c.flags==15,"invalid scalar preserves raw diagnostics without usable flag");
    }
    BeginExposureCache(c,a,true,100); EndExposureCache(c,b,false,101); Check(c.flags==1,"after failure");
    BeginExposureCache(c,a,false,100); EndExposureCache(c,a,true,101); Check(c.flags==2 && c.owner==0,"before failure cannot reuse last sample");
    BeginExposureCache(c,a,false,100); EndExposureCache(c,a,false,101); Check(c.flags==0,"fully unavailable appendix");
    std::cout << "PASS " << checks << " exposure cache checks (synthetic; not GPU frame pairing)\n";
}

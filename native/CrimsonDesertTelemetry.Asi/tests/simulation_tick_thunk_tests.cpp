#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

extern "C" {
void CdtSimulationTickThunk();
extern char CdtSimulationTickTail;
void CdtTestSite();
int CdtTestInvoke();
// Reuse the existing fixture's all-GPR/XMM/RFLAGS/displaced-instruction checks.
void CdtObserveCapture(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) {}
alignas(64) volatile LONG64 CdtSimulationTickCounter = 0;
void* CdtSimulationTickTrampoline = nullptr;
}

static bool UnwindChecks()
{
    const auto start = reinterpret_cast<DWORD64>(&CdtSimulationTickThunk);
    const auto tail = reinterpret_cast<DWORD64>(&CdtSimulationTickTail);
    DWORD64 image{};
    auto function = RtlLookupFunctionEntry(start + 1, &image, nullptr);
    if (!function || image + function->BeginAddress != start || image + function->EndAddress != tail ||
        RtlLookupFunctionEntry(tail, &image, nullptr)) return false;
    // ControlPc at POPFQ still has the pushed flags; virtual unwind must skip
    // those8bytes and recover the original caller, not interpret flags as RIP.
    alignas(16) DWORD64 stack[]{0x246, 0x12345678, 0, 0};
    CONTEXT context{};
    context.Rip = tail - 1; context.Rsp = reinterpret_cast<DWORD64>(stack);
    PVOID data{}; DWORD64 frame{};
    RtlLookupFunctionEntry(context.Rip, &image, nullptr);
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &data, &frame, nullptr);
    return context.Rip == 0x12345678 && context.Rsp == reinterpret_cast<DWORD64>(stack + 2);
}
int main()
{
    if (!UnwindChecks() || !CdtTestInvoke() || MH_Initialize() != MH_OK ||
        MH_CreateHook(CdtTestSite, CdtSimulationTickThunk, &CdtSimulationTickTrampoline) != MH_OK ||
        MH_EnableHook(CdtTestSite) != MH_OK) return 1;
    std::atomic<bool> valid{true};
    std::vector<std::thread> threads;
    for (unsigned n = 0; n < 8; ++n)
        threads.emplace_back([&] { for (unsigned i = 0; i < 10000; ++i) if (!CdtTestInvoke()) valid = false; });
    for (auto& thread : threads) thread.join();
    const auto count = InterlockedCompareExchange64(&CdtSimulationTickCounter, 0, 0);
    if (MH_DisableHook(CdtTestSite) != MH_OK || !valid || count != 80000 || !CdtTestInvoke() ||
        InterlockedCompareExchange64(&CdtSimulationTickCounter, 0, 0) != count) return 1;
    // As production does, retain the trampoline until process termination.
    std::cout << "PASS 80000 concurrent entries: atomic count, 15GPRs, XMM0..15, RFLAGS, stack/displaced instruction; unwind at POPFQ and leaf tail; disable stops counting.\n";
}

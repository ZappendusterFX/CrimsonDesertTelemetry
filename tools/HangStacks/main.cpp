#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

// Read-only developer diagnosis. Never installs hooks or captures raw memory.
// Each thread's temporary suspend count is restored before moving to the next.
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 1;
    const DWORD pid = wcstoul(argv[1], nullptr, 10);
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) { std::cout << "OpenProcess failed " << GetLastError() << '\n'; return 2; }
    BOOL debugger{}; CheckRemoteDebuggerPresent(process, &debugger);
    std::cout << "PID " << pid << " debugger=" << debugger << '\n';
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    if (!SymInitializeW(process, L"", TRUE)) return 3;
    std::vector<DWORD> threads;
    if (argc > 2) for (int i = 2; i < argc; ++i) threads.push_back(wcstoul(argv[i], nullptr, 10));
    else {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry)) do {
            if (entry.th32OwnerProcessID == pid) threads.push_back(entry.th32ThreadID);
            entry.dwSize = sizeof(entry);
        } while (Thread32Next(snapshot, &entry));
        CloseHandle(snapshot);
    }
    for (const DWORD tid : threads) {
        HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, tid);
        if (!thread) continue;
        const DWORD before = SuspendThread(thread);
        if (before == DWORD(-1)) { CloseHandle(thread); continue; }
        struct ResumeOnExit {
            HANDLE handle;
            ~ResumeOnExit() { ResumeThread(handle); CloseHandle(handle); }
        } resume{thread};
        std::cout << "THREAD " << tid << " previousSuspend=" << before << std::endl;
        CONTEXT context{}; context.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(thread, &context)) {
            STACKFRAME64 frame{}; frame.AddrPC.Offset = context.Rip; frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrFrame.Offset = context.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
            frame.AddrStack.Offset = context.Rsp; frame.AddrStack.Mode = AddrModeFlat;
            for (int depth = 0; depth < 48; ++depth) {
                const auto pc = frame.AddrPC.Offset;
                if (!pc) break;
                IMAGEHLP_MODULE64 module{}; module.SizeOfStruct = sizeof(module);
                SymGetModuleInfo64(process, pc, &module);
                alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
                auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
                symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = MAX_SYM_NAME;
                DWORD64 offset{};
                const bool named = SymFromAddr(process, pc, &offset, symbol) != FALSE;
                std::cout << "  " << depth << " 0x" << std::hex << pc << ' ' << module.ModuleName << "+0x"
                    << (pc - module.BaseOfImage);
                if (named) std::cout << ' ' << symbol->Name << "+0x" << offset;
                std::cout << std::dec << '\n';
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                    SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
            }
        } else std::cout << "GetThreadContext failed " << GetLastError() << '\n';
    }
    SymCleanup(process); CloseHandle(process);
    return 0;
}

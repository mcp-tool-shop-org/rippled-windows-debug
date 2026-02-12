/**
 * @file crash_handlers.h
 * @brief Verbose crash diagnostics for Windows
 *
 * Single-header crash handler that captures:
 * - Actual exception type and message (not just STATUS_STACK_BUFFER_OVERRUN)
 * - Full stack trace with symbol resolution
 * - System context (memory, CPU, process info)
 * - Signal information
 *
 * Usage:
 *   #include "crash_handlers.h"
 *   int main() {
 *       installVerboseCrashHandlers();
 *       // ... your code ...
 *   }
 *
 * Developed for XRPLF/rippled Windows debugging.
 * See: https://github.com/XRPLF/rippled/issues/6293
 */

#ifndef RIPPLED_WINDOWS_DEBUG_CRASH_HANDLERS_H
#define RIPPLED_WINDOWS_DEBUG_CRASH_HANDLERS_H

#ifdef _WIN32

#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <csignal>
#include <iostream>
#include <exception>
#include <typeinfo>
#include <ctime>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

namespace rippled_debug {

// ============================================================================
// System Information Gathering
// ============================================================================

/**
 * Get current timestamp as string.
 */
inline std::string getCrashTimestamp() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);

    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return std::string(buffer);
}

/**
 * Get process memory usage information.
 */
inline void printMemoryInfo() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        std::cerr << "\n--- Process Memory ---\n";
        std::cerr << "Working Set:        " << (pmc.WorkingSetSize / 1024 / 1024) << " MB\n";
        std::cerr << "Peak Working Set:   " << (pmc.PeakWorkingSetSize / 1024 / 1024) << " MB\n";
        std::cerr << "Private Bytes:      " << (pmc.PrivateUsage / 1024 / 1024) << " MB\n";
        std::cerr << "Page Faults:        " << pmc.PageFaultCount << "\n";
    }

    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        std::cerr << "\n--- System Memory ---\n";
        std::cerr << "Total Physical:     " << (memStatus.ullTotalPhys / 1024 / 1024) << " MB\n";
        std::cerr << "Available Physical: " << (memStatus.ullAvailPhys / 1024 / 1024) << " MB\n";
        std::cerr << "Memory Load:        " << memStatus.dwMemoryLoad << "%\n";
        std::cerr << "Total Virtual:      " << (memStatus.ullTotalVirtual / 1024 / 1024 / 1024) << " GB\n";
        std::cerr << "Available Virtual:  " << (memStatus.ullAvailVirtual / 1024 / 1024 / 1024) << " GB\n";
    }
}

/**
 * Get loaded module information.
 */
inline void printModuleInfo() {
    HMODULE modules[256];
    DWORD cbNeeded;

    if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &cbNeeded)) {
        int count = cbNeeded / sizeof(HMODULE);
        std::cerr << "\n--- Loaded Modules (" << count << " total, showing first 10) ---\n";

        for (int i = 0; i < count && i < 10; i++) {
            char moduleName[MAX_PATH];
            if (GetModuleFileNameExA(GetCurrentProcess(), modules[i], moduleName, sizeof(moduleName))) {
                // Extract just filename
                const char* filename = moduleName;
                for (const char* p = moduleName; *p; ++p) {
                    if (*p == '\\' || *p == '/') filename = p + 1;
                }

                MODULEINFO modInfo;
                if (GetModuleInformation(GetCurrentProcess(), modules[i], &modInfo, sizeof(modInfo))) {
                    std::cerr << "  " << std::left << std::setw(30) << filename
                              << " @ 0x" << std::hex << (uintptr_t)modInfo.lpBaseOfDll << std::dec
                              << " (" << (modInfo.SizeOfImage / 1024) << " KB)\n";
                }
            }
        }
        if (count > 10) {
            std::cerr << "  ... and " << (count - 10) << " more modules\n";
        }
    }
}

/**
 * Get thread information.
 */
inline void printThreadInfo() {
    std::cerr << "\n--- Thread Info ---\n";
    std::cerr << "Current Thread ID:  " << GetCurrentThreadId() << "\n";
    std::cerr << "Process ID:         " << GetCurrentProcessId() << "\n";

    // Get thread count
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te;
        te.dwSize = sizeof(te);

        int threadCount = 0;
        DWORD pid = GetCurrentProcessId();

        if (Thread32First(snapshot, &te)) {
            do {
                if (te.th32OwnerProcessID == pid) {
                    threadCount++;
                }
            } while (Thread32Next(snapshot, &te));
        }
        CloseHandle(snapshot);
        std::cerr << "Thread Count:       " << threadCount << "\n";
    }
}

/**
 * Print stack trace to stderr using DbgHelp.
 * Works best with PDB files available.
 */
inline void printStackTrace()
{
    std::cerr << "\n========== STACK TRACE ==========\n";

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (!SymInitialize(process, NULL, TRUE))
    {
        DWORD err = GetLastError();
        std::cerr << "Failed to initialize symbols. Error: " << err << "\n";
        std::cerr << "Hint: Ensure PDB files are in the same directory as the executable.\n";
        return;
    }

    CONTEXT context;
    RtlCaptureContext(&context);

    STACKFRAME64 stackFrame;
    memset(&stackFrame, 0, sizeof(stackFrame));

#ifdef _M_X64
    DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
    stackFrame.AddrPC.Offset = context.Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context.Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context.Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_IX86)
    DWORD machineType = IMAGE_FILE_MACHINE_I386;
    stackFrame.AddrPC.Offset = context.Eip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context.Ebp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context.Esp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_ARM64)
    DWORD machineType = IMAGE_FILE_MACHINE_ARM64;
    stackFrame.AddrPC.Offset = context.Pc;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context.Fp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context.Sp;
    stackFrame.AddrStack.Mode = AddrModeFlat;
#else
    #error "Unsupported architecture"
#endif

    char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO symbol = (PSYMBOL_INFO)symbolBuffer;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    IMAGEHLP_LINE64 line;
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    int frameNum = 0;
    bool hasSymbols = false;

    while (StackWalk64(
        machineType,
        process,
        thread,
        &stackFrame,
        &context,
        NULL,
        SymFunctionTableAccess64,
        SymGetModuleBase64,
        NULL) && frameNum < 50)
    {
        DWORD64 address = stackFrame.AddrPC.Offset;
        DWORD64 displacement64 = 0;
        DWORD displacement = 0;

        std::cerr << "[" << std::setw(2) << frameNum << "] 0x"
                  << std::hex << std::setw(16) << std::setfill('0') << address
                  << std::dec << std::setfill(' ') << " ";

        if (SymFromAddr(process, address, &displacement64, symbol))
        {
            hasSymbols = true;
            std::cerr << symbol->Name;

            if (SymGetLineFromAddr64(process, address, &displacement, &line))
            {
                // Extract just filename from path
                const char* filename = line.FileName;
                for (const char* p = line.FileName; *p; ++p) {
                    if (*p == '\\' || *p == '/') filename = p + 1;
                }
                std::cerr << " (" << filename << ":" << line.LineNumber << ")";
            }
        }
        else
        {
            // Try to get module name at least
            DWORD64 moduleBase = SymGetModuleBase64(process, address);
            if (moduleBase) {
                char moduleName[MAX_PATH];
                if (GetModuleFileNameA((HMODULE)moduleBase, moduleName, sizeof(moduleName))) {
                    const char* filename = moduleName;
                    for (const char* p = moduleName; *p; ++p) {
                        if (*p == '\\' || *p == '/') filename = p + 1;
                    }
                    std::cerr << "<" << filename << "+0x" << std::hex
                              << (address - moduleBase) << std::dec << ">";
                } else {
                    std::cerr << "<unknown>";
                }
            } else {
                std::cerr << "<unknown>";
            }
        }

        std::cerr << "\n";
        frameNum++;
    }

    if (!hasSymbols) {
        std::cerr << "\n[!] No symbols resolved. For better stack traces:\n";
        std::cerr << "    1. Build with /Zi (debug info)\n";
        std::cerr << "    2. Keep PDB files with the executable\n";
        std::cerr << "    3. Use RelWithDebInfo build type\n";
    }

    std::cerr << "========== END STACK TRACE (" << frameNum << " frames) ==========\n";
    SymCleanup(process);
}

/**
 * Print diagnostic summary for common exceptions.
 */
inline void printExceptionDiagnostics(const char* exceptionType) {
    std::cerr << "\n--- Diagnostic Hints ---\n";

    if (strcmp(exceptionType, "std::bad_alloc") == 0) {
        std::cerr << "MEMORY ALLOCATION FAILURE detected.\n";
        std::cerr << "Common causes:\n";
        std::cerr << "  1. Requesting impossibly large allocation (SIZE_MAX, negative size cast to size_t)\n";
        std::cerr << "  2. System out of memory (check Available Physical above)\n";
        std::cerr << "  3. Memory fragmentation (process can't find contiguous block)\n";
        std::cerr << "  4. Memory leak exhausting address space\n";
        std::cerr << "\n";
        std::cerr << "This often appears as STATUS_STACK_BUFFER_OVERRUN (0xC0000409) because:\n";
        std::cerr << "  bad_alloc -> terminate() -> abort() -> /GS security check\n";
    }
    else if (strstr(exceptionType, "runtime_error") || strstr(exceptionType, "logic_error")) {
        std::cerr << "Standard library exception thrown but not caught.\n";
        std::cerr << "Check the exception message above for details.\n";
    }
    else if (strstr(exceptionType, "out_of_range")) {
        std::cerr << "OUT OF RANGE access detected.\n";
        std::cerr << "Common causes:\n";
        std::cerr << "  1. Vector/string index >= size()\n";
        std::cerr << "  2. std::stoi/stol on invalid string\n";
        std::cerr << "  3. map::at() with non-existent key\n";
    }
    else if (strstr(exceptionType, "invalid_argument")) {
        std::cerr << "INVALID ARGUMENT passed to function.\n";
        std::cerr << "Check function parameters in the stack trace.\n";
    }
}

/**
 * Custom terminate handler that prints exception details before crashing.
 */
inline void verboseTerminateHandler()
{
    std::cerr << "\n";
    std::cerr << "################################################################################\n";
    std::cerr << "###                     VERBOSE CRASH HANDLER                                ###\n";
    std::cerr << "###                      terminate() called                                  ###\n";
    std::cerr << "################################################################################\n";
    std::cerr << "\n";
    std::cerr << "Timestamp: " << getCrashTimestamp() << "\n";

    const char* exceptionType = "unknown";

    if (auto eptr = std::current_exception())
    {
        try
        {
            std::rethrow_exception(eptr);
        }
        catch (const std::bad_alloc& e)
        {
            exceptionType = "std::bad_alloc";
            std::cerr << "\n--- Exception Details ---\n";
            std::cerr << "Type:    std::bad_alloc\n";
            std::cerr << "Message: " << e.what() << "\n";
        }
        catch (const std::exception& e)
        {
            exceptionType = typeid(e).name();
            std::cerr << "\n--- Exception Details ---\n";
            std::cerr << "Type:    " << typeid(e).name() << "\n";
            std::cerr << "Message: " << e.what() << "\n";
        }
        catch (...)
        {
            std::cerr << "\n--- Exception Details ---\n";
            std::cerr << "Type:    <unknown non-std::exception type>\n";
        }
    }
    else
    {
        std::cerr << "\n--- Exception Details ---\n";
        std::cerr << "No active exception - likely direct abort() or terminate() call.\n";
        std::cerr << "Common causes:\n";
        std::cerr << "  1. Assertion failure (assert() macro)\n";
        std::cerr << "  2. Pure virtual function call\n";
        std::cerr << "  3. Double free or heap corruption\n";
        std::cerr << "  4. Stack buffer overrun detected by /GS\n";
    }

    printExceptionDiagnostics(exceptionType);
    printMemoryInfo();
    printThreadInfo();
    printStackTrace();
    printModuleInfo();

    std::cerr << "\n################################################################################\n";
    std::cerr << "###                         END CRASH REPORT                                 ###\n";
    std::cerr << "################################################################################\n";
    std::cerr.flush();

    // Call default handler to generate crash dump
    std::abort();
}

/**
 * Signal handler for SIGABRT, SIGSEGV, etc.
 */
inline void signalHandler(int signal)
{
    std::cerr << "\n";
    std::cerr << "################################################################################\n";
    std::cerr << "###                     VERBOSE CRASH HANDLER                                ###\n";
    std::cerr << "###                      Signal " << signal << " received                                  ###\n";
    std::cerr << "################################################################################\n";
    std::cerr << "\n";
    std::cerr << "Timestamp: " << getCrashTimestamp() << "\n";
    std::cerr << "\n--- Signal Details ---\n";

    switch(signal)
    {
        case SIGABRT:
            std::cerr << "Signal:  SIGABRT (abnormal termination)\n";
            std::cerr << "Meaning: abort() was called\n";
            std::cerr << "Common causes:\n";
            std::cerr << "  1. Unhandled exception -> terminate() -> abort()\n";
            std::cerr << "  2. assert() failure\n";
            std::cerr << "  3. Heap corruption detected\n";
            std::cerr << "  4. /GS security check failure (buffer overrun)\n";
            break;
        case SIGSEGV:
            std::cerr << "Signal:  SIGSEGV (segmentation fault)\n";
            std::cerr << "Meaning: Invalid memory access\n";
            std::cerr << "Common causes:\n";
            std::cerr << "  1. Null pointer dereference\n";
            std::cerr << "  2. Use after free\n";
            std::cerr << "  3. Stack overflow\n";
            std::cerr << "  4. Writing to read-only memory\n";
            break;
        case SIGFPE:
            std::cerr << "Signal:  SIGFPE (floating point exception)\n";
            std::cerr << "Meaning: Arithmetic error\n";
            std::cerr << "Common causes:\n";
            std::cerr << "  1. Division by zero\n";
            std::cerr << "  2. Integer overflow (with trapping enabled)\n";
            break;
        case SIGILL:
            std::cerr << "Signal:  SIGILL (illegal instruction)\n";
            std::cerr << "Meaning: CPU encountered invalid opcode\n";
            std::cerr << "Common causes:\n";
            std::cerr << "  1. Corrupted code segment\n";
            std::cerr << "  2. Jump to invalid address\n";
            std::cerr << "  3. SSE/AVX instruction on unsupported CPU\n";
            break;
        default:
            std::cerr << "Signal:  Unknown (" << signal << ")\n";
            break;
    }

    printMemoryInfo();
    printThreadInfo();
    printStackTrace();

    std::cerr << "\n################################################################################\n";
    std::cerr << "###                         END CRASH REPORT                                 ###\n";
    std::cerr << "################################################################################\n";
    std::cerr.flush();

    // Reset and re-raise to get default behavior (crash dump)
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

/**
 * Install all verbose crash handlers.
 * Call this at the start of main().
 */
inline void installVerboseCrashHandlers()
{
    std::cerr << "[DEBUG] Installing verbose crash handlers for diagnostics\n";

    // Set terminate handler for unhandled exceptions
    std::set_terminate(verboseTerminateHandler);

    // Set signal handlers
    std::signal(SIGABRT, signalHandler);
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGFPE, signalHandler);
    std::signal(SIGILL, signalHandler);

    std::cerr << "[DEBUG] Verbose crash handlers installed\n";
}

} // namespace rippled_debug

// Convenience macro for global namespace
#define installVerboseCrashHandlers() rippled_debug::installVerboseCrashHandlers()

#endif // _WIN32

#endif // RIPPLED_WINDOWS_DEBUG_CRASH_HANDLERS_H

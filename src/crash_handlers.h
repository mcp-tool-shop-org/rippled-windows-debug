/**
 * @file crash_handlers.h
 * @brief Verbose crash diagnostics for Windows
 *
 * Single-header crash handler that captures:
 * - Actual exception type and message (not just STATUS_STACK_BUFFER_OVERRUN)
 * - Full stack trace with symbol resolution
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
#include <csignal>
#include <iostream>
#include <exception>
#include <typeinfo>

#pragma comment(lib, "dbghelp.lib")

namespace rippled_debug {

/**
 * Print stack trace to stderr using DbgHelp.
 * Works best with PDB files available.
 */
inline void printStackTrace()
{
    std::cerr << "\n========== STACK TRACE ==========\n";

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    if (!SymInitialize(process, NULL, TRUE))
    {
        std::cerr << "Failed to initialize symbols. Error: " << GetLastError() << "\n";
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

        std::cerr << "[" << frameNum << "] 0x" << std::hex << address << std::dec << " ";

        if (SymFromAddr(process, address, &displacement64, symbol))
        {
            std::cerr << symbol->Name;

            if (SymGetLineFromAddr64(process, address, &displacement, &line))
            {
                std::cerr << " (" << line.FileName << ":" << line.LineNumber << ")";
            }
        }
        else
        {
            std::cerr << "<unknown function>";
        }

        std::cerr << "\n";
        frameNum++;
    }

    std::cerr << "========== END STACK TRACE ==========\n\n";
    SymCleanup(process);
}

/**
 * Custom terminate handler that prints exception details before crashing.
 */
inline void verboseTerminateHandler()
{
    std::cerr << "\n";
    std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
    std::cerr << "!!! VERBOSE CRASH HANDLER - terminate() called\n";
    std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";

    if (auto eptr = std::current_exception())
    {
        try
        {
            std::rethrow_exception(eptr);
        }
        catch (const std::bad_alloc& e)
        {
            std::cerr << "Exception type: std::bad_alloc\n";
            std::cerr << "Exception message: " << e.what() << "\n";
            std::cerr << "\n*** MEMORY ALLOCATION FAILURE ***\n";
            std::cerr << "This often appears as STATUS_STACK_BUFFER_OVERRUN but is actually\n";
            std::cerr << "a memory allocation failure. Check system memory and allocation sizes.\n";
        }
        catch (const std::exception& e)
        {
            std::cerr << "Exception type: " << typeid(e).name() << "\n";
            std::cerr << "Exception message: " << e.what() << "\n";
        }
        catch (...)
        {
            std::cerr << "Unknown exception type\n";
        }
    }
    else
    {
        std::cerr << "No active exception - likely direct abort() call\n";
    }

    printStackTrace();

    std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
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
    std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
    std::cerr << "!!! VERBOSE CRASH HANDLER - Signal " << signal << " received\n";

    switch(signal)
    {
        case SIGABRT:
            std::cerr << "!!! Signal: SIGABRT (abort)\n";
            std::cerr << "!!! This usually means an unhandled exception triggered terminate().\n";
            break;
        case SIGSEGV:
            std::cerr << "!!! Signal: SIGSEGV (segmentation fault)\n";
            std::cerr << "!!! This means invalid memory access.\n";
            break;
        case SIGFPE:
            std::cerr << "!!! Signal: SIGFPE (floating point exception)\n";
            break;
        case SIGILL:
            std::cerr << "!!! Signal: SIGILL (illegal instruction)\n";
            break;
        default:
            std::cerr << "!!! Signal: Unknown (" << signal << ")\n";
            break;
    }
    std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";

    printStackTrace();

    std::cerr << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";
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

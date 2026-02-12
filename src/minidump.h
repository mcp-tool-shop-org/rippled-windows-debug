/**
 * @file minidump.h
 * @brief Automatic minidump generation for crash analysis
 *
 * Captures full crash dumps that can be analyzed with WinDbg or Visual Studio.
 *
 * Usage:
 *   #include "minidump.h"
 *   int main() {
 *       installMinidumpHandler();
 *       // or with custom path:
 *       installMinidumpHandler("C:\\CrashDumps");
 *   }
 */

#ifndef RIPPLED_WINDOWS_DEBUG_MINIDUMP_H
#define RIPPLED_WINDOWS_DEBUG_MINIDUMP_H

#ifdef _WIN32

#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>
#include <cstdio>
#include <ctime>
#include <string>

#pragma comment(lib, "dbghelp.lib")

namespace rippled_debug {

// Global dump directory
inline std::wstring& dumpDirectory() {
    static std::wstring dir;
    return dir;
}

// Generate dump filename with timestamp
inline std::wstring generateDumpFilename() {
    wchar_t filename[MAX_PATH];

    // Get current time
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);

    // Format: rippled_YYYYMMDD_HHMMSS.dmp
    swprintf_s(filename, MAX_PATH,
        L"%s\\rippled_%04d%02d%02d_%02d%02d%02d.dmp",
        dumpDirectory().c_str(),
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec);

    return std::wstring(filename);
}

// Exception filter that writes minidump
inline LONG WINAPI minidumpExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
    fprintf(stderr, "\n[MINIDUMP] Unhandled exception caught!\n");
    fprintf(stderr, "[MINIDUMP] Exception code: 0x%08X\n", exceptionInfo->ExceptionRecord->ExceptionCode);

    // Generate filename
    std::wstring dumpPath = generateDumpFilename();

    fprintf(stderr, "[MINIDUMP] Writing dump to: %ls\n", dumpPath.c_str());

    // Create dump file
    HANDLE hFile = CreateFileW(
        dumpPath.c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[MINIDUMP] Failed to create dump file. Error: %lu\n", GetLastError());
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Write minidump
    MINIDUMP_EXCEPTION_INFORMATION exInfo;
    exInfo.ThreadId = GetCurrentThreadId();
    exInfo.ExceptionPointers = exceptionInfo;
    exInfo.ClientPointers = FALSE;

    // Use MiniDumpWithFullMemory for most detailed dumps
    // Can use MiniDumpNormal for smaller dumps
    MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithFullMemory |
        MiniDumpWithFullMemoryInfo |
        MiniDumpWithHandleData |
        MiniDumpWithThreadInfo |
        MiniDumpWithUnloadedModules);

    BOOL success = MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        hFile,
        dumpType,
        &exInfo,
        NULL,
        NULL);

    CloseHandle(hFile);

    if (success) {
        fprintf(stderr, "[MINIDUMP] Dump written successfully!\n");
        fprintf(stderr, "[MINIDUMP] Analyze with: windbg -z \"%ls\"\n", dumpPath.c_str());
    } else {
        fprintf(stderr, "[MINIDUMP] Failed to write dump. Error: %lu\n", GetLastError());
    }

    fflush(stderr);

    // Continue to default handler (will terminate process)
    return EXCEPTION_CONTINUE_SEARCH;
}

/**
 * Install minidump handler.
 * @param dumpDir Directory to write dumps (default: %LOCALAPPDATA%\rippled\CrashDumps)
 */
inline void installMinidumpHandler(const char* dumpDir = nullptr) {
    // Set dump directory
    if (dumpDir) {
        // Convert to wide string
        int len = MultiByteToWideChar(CP_UTF8, 0, dumpDir, -1, NULL, 0);
        std::wstring wdir(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, dumpDir, -1, &wdir[0], len);
        wdir.resize(len - 1); // Remove null terminator
        dumpDirectory() = wdir;
    } else {
        // Default to %LOCALAPPDATA%\rippled\CrashDumps
        wchar_t localAppData[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
            dumpDirectory() = std::wstring(localAppData) + L"\\rippled\\CrashDumps";
        } else {
            dumpDirectory() = L".\\CrashDumps";
        }
    }

    // Create directory if it doesn't exist
    CreateDirectoryW(dumpDirectory().c_str(), NULL);

    // Install exception filter
    SetUnhandledExceptionFilter(minidumpExceptionFilter);

    fprintf(stderr, "[MINIDUMP] Handler installed. Dumps will be written to: %ls\n",
            dumpDirectory().c_str());
    fflush(stderr);
}

/**
 * Manually trigger a minidump (for debugging).
 */
inline void writeMinidump() {
    fprintf(stderr, "[MINIDUMP] Manual dump requested\n");

    std::wstring dumpPath = generateDumpFilename();

    HANDLE hFile = CreateFileW(
        dumpPath.c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[MINIDUMP] Failed to create dump file\n");
        return;
    }

    BOOL success = MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        hFile,
        MiniDumpWithFullMemory,
        NULL,
        NULL,
        NULL);

    CloseHandle(hFile);

    if (success) {
        fprintf(stderr, "[MINIDUMP] Manual dump written: %ls\n", dumpPath.c_str());
    }
    fflush(stderr);
}

} // namespace rippled_debug

// Convenience macros
#define installMinidumpHandler(...) rippled_debug::installMinidumpHandler(__VA_ARGS__)
#define writeMinidump() rippled_debug::writeMinidump()

#else // !_WIN32

#define installMinidumpHandler(...) ((void)0)
#define writeMinidump() ((void)0)

#endif // _WIN32

#endif // RIPPLED_WINDOWS_DEBUG_MINIDUMP_H

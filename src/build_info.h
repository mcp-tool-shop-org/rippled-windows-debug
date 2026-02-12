/**
 * @file build_info.h
 * @brief Captures build-time and runtime system information
 *
 * Provides detailed information about:
 * - Toolkit version
 * - Git commit/branch at build time
 * - Compiler version and flags
 * - Windows version
 * - CPU information
 * - System specs
 *
 * Usage:
 *   #include "build_info.h"
 *   rippled_debug::printBuildInfo();
 */

#ifndef RIPPLED_WINDOWS_DEBUG_BUILD_INFO_H
#define RIPPLED_WINDOWS_DEBUG_BUILD_INFO_H

#ifdef _WIN32

#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace rippled_debug {

// ============================================================================
// Toolkit Version
// ============================================================================

#define RIPPLED_DEBUG_VERSION_MAJOR 1
#define RIPPLED_DEBUG_VERSION_MINOR 1
#define RIPPLED_DEBUG_VERSION_PATCH 0
#define RIPPLED_DEBUG_VERSION_STRING "1.1.0"

// ============================================================================
// Build-time Information (set via compiler defines or defaults)
// ============================================================================

// Git information - set these via compiler flags:
//   -DGIT_COMMIT_HASH=\"abc123\"
//   -DGIT_BRANCH=\"main\"
//   -DGIT_DIRTY=1
#ifndef GIT_COMMIT_HASH
#define GIT_COMMIT_HASH "unknown"
#endif

#ifndef GIT_BRANCH
#define GIT_BRANCH "unknown"
#endif

#ifndef GIT_DIRTY
#define GIT_DIRTY 0
#endif

#ifndef GIT_COMMIT_DATE
#define GIT_COMMIT_DATE "unknown"
#endif

#ifndef GIT_DESCRIBE
#define GIT_DESCRIBE "unknown"
#endif

// Build timestamp
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

// Compiler detection
#if defined(_MSC_VER)
    #define COMPILER_NAME "MSVC"
    #define COMPILER_VERSION _MSC_VER
    #define COMPILER_VERSION_STRING _STRINGIZE(_MSC_VER)
    #define _STRINGIZE(x) _STRINGIZE2(x)
    #define _STRINGIZE2(x) #x
#elif defined(__clang__)
    #define COMPILER_NAME "Clang"
    #define COMPILER_VERSION (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
    #define COMPILER_VERSION_STRING __clang_version__
#elif defined(__GNUC__)
    #define COMPILER_NAME "GCC"
    #define COMPILER_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
    #define COMPILER_VERSION_STRING __VERSION__
#else
    #define COMPILER_NAME "Unknown"
    #define COMPILER_VERSION 0
    #define COMPILER_VERSION_STRING "unknown"
#endif

// Architecture
#if defined(_M_X64) || defined(__x86_64__)
    #define BUILD_ARCH "x64"
#elif defined(_M_IX86) || defined(__i386__)
    #define BUILD_ARCH "x86"
#elif defined(_M_ARM64) || defined(__aarch64__)
    #define BUILD_ARCH "ARM64"
#elif defined(_M_ARM) || defined(__arm__)
    #define BUILD_ARCH "ARM"
#else
    #define BUILD_ARCH "Unknown"
#endif

// Build configuration
#if defined(_DEBUG) || defined(DEBUG)
    #define BUILD_CONFIG "Debug"
#elif defined(NDEBUG)
    #define BUILD_CONFIG "Release"
#else
    #define BUILD_CONFIG "Unknown"
#endif

// ============================================================================
// Runtime System Information
// ============================================================================

/**
 * Get Windows version string.
 */
inline std::string getWindowsVersion() {
    // Use RtlGetVersion to get accurate version (GetVersionEx lies after Win8)
    typedef NTSTATUS (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

    RTL_OSVERSIONINFOW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);

    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        RtlGetVersionPtr pRtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNtdll, "RtlGetVersion");
        if (pRtlGetVersion) {
            pRtlGetVersion(&osvi);
        }
    }

    char buffer[128];
    const char* edition = "";

    // Determine Windows edition
    if (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 22000) {
        edition = "Windows 11";
    } else if (osvi.dwMajorVersion == 10) {
        edition = "Windows 10";
    } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 3) {
        edition = "Windows 8.1";
    } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 2) {
        edition = "Windows 8";
    } else if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 1) {
        edition = "Windows 7";
    } else {
        edition = "Windows";
    }

    snprintf(buffer, sizeof(buffer), "%s (Build %lu.%lu.%lu)",
        edition,
        osvi.dwMajorVersion,
        osvi.dwMinorVersion,
        osvi.dwBuildNumber);

    return std::string(buffer);
}

/**
 * Get detailed Windows build info.
 */
inline std::string getWindowsBuildDetails() {
    char buffer[256];

    // Get UBR (Update Build Revision) from registry
    DWORD ubr = 0;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD size = sizeof(ubr);
        RegQueryValueExW(hKey, L"UBR", NULL, NULL, (LPBYTE)&ubr, &size);

        // Get DisplayVersion (like "23H2")
        wchar_t displayVersion[64] = {};
        size = sizeof(displayVersion);
        RegQueryValueExW(hKey, L"DisplayVersion", NULL, NULL, (LPBYTE)displayVersion, &size);

        // Get EditionID
        wchar_t editionId[64] = {};
        size = sizeof(editionId);
        RegQueryValueExW(hKey, L"EditionID", NULL, NULL, (LPBYTE)editionId, &size);

        RegCloseKey(hKey);

        snprintf(buffer, sizeof(buffer), "%ls %ls (UBR: %lu)",
            editionId, displayVersion, ubr);
    } else {
        strcpy(buffer, "Unknown");
    }

    return std::string(buffer);
}

/**
 * Get CPU information using CPUID.
 */
inline std::string getCpuInfo() {
    int cpuInfo[4] = {0};
    char cpuBrand[49] = {0};

    // Get CPU brand string
    __cpuid(cpuInfo, 0x80000000);
    unsigned int maxExtended = cpuInfo[0];

    if (maxExtended >= 0x80000004) {
        __cpuid(cpuInfo, 0x80000002);
        memcpy(cpuBrand, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000003);
        memcpy(cpuBrand + 16, cpuInfo, sizeof(cpuInfo));
        __cpuid(cpuInfo, 0x80000004);
        memcpy(cpuBrand + 32, cpuInfo, sizeof(cpuInfo));
    }

    // Trim leading spaces
    char* brand = cpuBrand;
    while (*brand == ' ') brand++;

    return std::string(brand);
}

/**
 * Get CPU core counts.
 */
inline void getCpuCores(int& physical, int& logical) {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    logical = sysInfo.dwNumberOfProcessors;

    // Try to get physical core count
    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);

    physical = logical; // Default to logical if we can't get physical

    if (len > 0) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION* buffer =
            (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)malloc(len);
        if (buffer && GetLogicalProcessorInformation(buffer, &len)) {
            int count = len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
            physical = 0;
            for (int i = 0; i < count; i++) {
                if (buffer[i].Relationship == RelationProcessorCore) {
                    physical++;
                }
            }
        }
        free(buffer);
    }
}

/**
 * Get system memory in GB.
 */
inline void getSystemMemory(double& totalGB, double& availableGB) {
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        totalGB = memStatus.ullTotalPhys / 1024.0 / 1024.0 / 1024.0;
        availableGB = memStatus.ullAvailPhys / 1024.0 / 1024.0 / 1024.0;
    } else {
        totalGB = availableGB = 0;
    }
}

/**
 * Get computer name.
 */
inline std::string getComputerName() {
    char buffer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(buffer);
    if (GetComputerNameA(buffer, &size)) {
        return std::string(buffer);
    }
    return "Unknown";
}

/**
 * Get username.
 */
inline std::string getUserName() {
    char buffer[256];
    DWORD size = sizeof(buffer);
    if (GetUserNameA(buffer, &size)) {
        return std::string(buffer);
    }
    return "Unknown";
}

/**
 * Check if running as admin.
 */
inline bool isRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;

    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

/**
 * Get process bitness.
 */
inline const char* getProcessBitness() {
    return sizeof(void*) == 8 ? "64-bit" : "32-bit";
}

/**
 * Check if running under WoW64 (32-bit on 64-bit Windows).
 */
inline bool isWow64() {
    BOOL isWow = FALSE;
    IsWow64Process(GetCurrentProcess(), &isWow);
    return isWow != FALSE;
}

// ============================================================================
// Print Functions
// ============================================================================

/**
 * Print all build information.
 */
inline void printBuildInfo(FILE* output = stderr) {
    fprintf(output, "\n");
    fprintf(output, "================================================================================\n");
    fprintf(output, "                        rippled-windows-debug v%s\n", RIPPLED_DEBUG_VERSION_STRING);
    fprintf(output, "================================================================================\n");
    fprintf(output, "\n");

    // Toolkit info
    fprintf(output, "--- Toolkit ---\n");
    fprintf(output, "Version:          %d.%d.%d\n",
        RIPPLED_DEBUG_VERSION_MAJOR, RIPPLED_DEBUG_VERSION_MINOR, RIPPLED_DEBUG_VERSION_PATCH);
    fprintf(output, "Repository:       https://github.com/mcp-tool-shop-org/rippled-windows-debug\n");
    fprintf(output, "\n");

    // Git info
    fprintf(output, "--- Git (at build time) ---\n");
    fprintf(output, "Commit:           %s%s\n", GIT_COMMIT_HASH, GIT_DIRTY ? " (dirty)" : "");
    fprintf(output, "Branch:           %s\n", GIT_BRANCH);
    fprintf(output, "Describe:         %s\n", GIT_DESCRIBE);
    fprintf(output, "Commit Date:      %s\n", GIT_COMMIT_DATE);
    fprintf(output, "\n");

    // Build info
    fprintf(output, "--- Build ---\n");
    fprintf(output, "Date:             %s %s\n", BUILD_DATE, BUILD_TIME);
    fprintf(output, "Compiler:         %s %s\n", COMPILER_NAME, COMPILER_VERSION_STRING);
    fprintf(output, "Architecture:     %s\n", BUILD_ARCH);
    fprintf(output, "Configuration:    %s\n", BUILD_CONFIG);
    fprintf(output, "Process:          %s%s\n", getProcessBitness(), isWow64() ? " (WoW64)" : "");
    fprintf(output, "\n");

    // Windows info
    fprintf(output, "--- Windows ---\n");
    fprintf(output, "Version:          %s\n", getWindowsVersion().c_str());
    fprintf(output, "Edition:          %s\n", getWindowsBuildDetails().c_str());
    fprintf(output, "\n");

    // Hardware info
    int physCores, logCores;
    getCpuCores(physCores, logCores);
    double totalMem, availMem;
    getSystemMemory(totalMem, availMem);

    fprintf(output, "--- Hardware ---\n");
    fprintf(output, "Computer:         %s\n", getComputerName().c_str());
    fprintf(output, "CPU:              %s\n", getCpuInfo().c_str());
    fprintf(output, "Cores:            %d physical, %d logical\n", physCores, logCores);
    fprintf(output, "Memory:           %.1f GB total, %.1f GB available\n", totalMem, availMem);
    fprintf(output, "\n");

    // Runtime info
    fprintf(output, "--- Runtime ---\n");
    fprintf(output, "User:             %s%s\n", getUserName().c_str(), isRunningAsAdmin() ? " (Administrator)" : "");
    fprintf(output, "Process ID:       %lu\n", GetCurrentProcessId());
    fprintf(output, "Thread ID:        %lu\n", GetCurrentThreadId());
    fprintf(output, "\n");

    fprintf(output, "================================================================================\n");
    fprintf(output, "\n");

    fflush(output);
}

/**
 * Print compact single-line version info.
 */
inline void printVersionLine(FILE* output = stderr) {
    fprintf(output, "rippled-windows-debug v%s [%s %s] [%s %s] [%s]\n",
        RIPPLED_DEBUG_VERSION_STRING,
        COMPILER_NAME, COMPILER_VERSION_STRING,
        BUILD_DATE, BUILD_TIME,
        GIT_COMMIT_HASH);
    fflush(output);
}

/**
 * Get version string for embedding in crash reports.
 */
inline std::string getVersionString() {
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
        "rippled-windows-debug v%s (git:%s %s) built %s %s with %s",
        RIPPLED_DEBUG_VERSION_STRING,
        GIT_BRANCH, GIT_COMMIT_HASH,
        BUILD_DATE, BUILD_TIME,
        COMPILER_NAME);
    return std::string(buffer);
}

} // namespace rippled_debug

// Convenience macros
#define PRINT_BUILD_INFO() rippled_debug::printBuildInfo()
#define PRINT_VERSION() rippled_debug::printVersionLine()

#else // !_WIN32

#define RIPPLED_DEBUG_VERSION_STRING "1.1.0"
#define PRINT_BUILD_INFO() ((void)0)
#define PRINT_VERSION() ((void)0)

#endif // _WIN32

#endif // RIPPLED_WINDOWS_DEBUG_BUILD_INFO_H

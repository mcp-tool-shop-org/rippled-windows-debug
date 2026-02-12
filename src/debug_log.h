/**
 * @file debug_log.h
 * @brief Rich-style terminal logging for Windows debugging
 *
 * Provides beautiful, structured logging inspired by Python's Rich library.
 * Features colored output, box-drawing characters, and aligned columns.
 *
 * Features:
 * - Rich-style colored log levels (INFO=cyan, WARN=yellow, ERROR=red)
 * - Box-drawing characters for sections
 * - Delta timestamps showing time since last log
 * - Correlation IDs for tracking related log entries
 * - Multiple output formats (Rich, JSON)
 * - Thread-safe logging
 *
 * Usage:
 *   DEBUG_SECTION_BEGIN("rpc_startup");
 *   DEBUG_LOG("Processing command: %s", cmd.c_str());
 *   DEBUG_SECTION_END("rpc_startup");
 *
 * Output:
 *   [12:34:56.123] [+0.5ms] INFO     Loading config...          config.cpp:42
 */

#ifndef RIPPLED_WINDOWS_DEBUG_DEBUG_LOG_H
#define RIPPLED_WINDOWS_DEBUG_DEBUG_LOG_H

#ifdef _WIN32

#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <atomic>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>

#pragma comment(lib, "psapi.lib")

namespace rippled_debug {

// ============================================================================
// ANSI Color Codes
// ============================================================================

namespace colors {
    constexpr const char* RESET      = "\033[0m";
    constexpr const char* BOLD       = "\033[1m";
    constexpr const char* DIM        = "\033[2m";
    constexpr const char* ITALIC     = "\033[3m";
    constexpr const char* UNDERLINE  = "\033[4m";

    // Log level colors (Rich-style)
    constexpr const char* LVL_DEBUG    = "\033[38;5;244m";  // Gray
    constexpr const char* LVL_INFO     = "\033[38;5;39m";   // Cyan/Blue
    constexpr const char* LVL_WARN     = "\033[38;5;214m";  // Orange/Yellow
    constexpr const char* LVL_ERROR    = "\033[38;5;196m";  // Red
    constexpr const char* LVL_CRITICAL = "\033[38;5;196m\033[1m";  // Bold Red

    // Accent colors
    constexpr const char* TIMESTAMP  = "\033[38;5;242m";  // Dark gray
    constexpr const char* DELTA      = "\033[38;5;240m";  // Darker gray for delta time
    constexpr const char* LOCATION   = "\033[38;5;245m";  // Medium gray
    constexpr const char* CID        = "\033[38;5;141m";  // Purple
    constexpr const char* SECTION    = "\033[38;5;75m";   // Light blue
    constexpr const char* SUCCESS    = "\033[38;5;82m";   // Green
    constexpr const char* BOX_COLOR  = "\033[38;5;240m";  // Dark gray for box chars
    constexpr const char* MEMORY     = "\033[38;5;208m";  // Orange for memory
    constexpr const char* NUMBER     = "\033[38;5;141m";  // Purple for numbers
}

// Box-drawing characters (Unicode)
namespace box {
    constexpr const char* TL = "\u250C";  // ┌
    constexpr const char* TR = "\u2510";  // ┐
    constexpr const char* BL = "\u2514";  // └
    constexpr const char* BR = "\u2518";  // ┘
    constexpr const char* H  = "\u2500";  // ─
    constexpr const char* V  = "\u2502";  // │
    constexpr const char* ARROW_R = "\u25B6";  // ▶
    constexpr const char* ARROW_D = "\u25BC";  // ▼
    constexpr const char* CHECK   = "\u2714";  // ✔
    constexpr const char* CROSS   = "\u2718";  // ✘
    constexpr const char* BULLET  = "\u2022";  // •
    constexpr const char* WARN_ICON = "\u26A0";  // ⚠
    constexpr const char* INFO_ICON = "\u2139";  // ℹ
}

// ============================================================================
// Configuration
// ============================================================================

enum class LogFormat {
    RICH,   // Rich-style colored output (default)
    TEXT,   // Plain text (no colors)
    JSON    // Machine-parseable JSON
};

struct LogConfig {
    bool enabled = true;
    LogFormat format = LogFormat::RICH;
    FILE* output = stderr;
    bool includeThreadId = false;       // Off by default for cleaner output
    bool includeCorrelationId = true;
    bool includeDeltaTime = true;       // Show time since last log
    bool includeMemoryDelta = false;    // Show memory change since last log
    bool useColors = true;
    bool useMilliseconds = true;        // Include ms in timestamp
    int boxWidth = 76;
};

inline LogConfig& config() {
    static LogConfig cfg;
    return cfg;
}

// Enable Windows ANSI support
inline void enableAnsiSupport() {
    static bool initialized = false;
    if (initialized) return;

    HANDLE hOut = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode)) {
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    // Set console to UTF-8
    SetConsoleOutputCP(CP_UTF8);

    initialized = true;
}

// ============================================================================
// Correlation ID System
// ============================================================================

using CorrelationId = uint64_t;

inline CorrelationId generateCorrelationId() {
    static std::atomic<uint64_t> counter{0};
    return ++counter;
}

inline CorrelationId& currentCorrelationId() {
    thread_local CorrelationId cid = 0;
    return cid;
}

inline CorrelationId startCorrelation(const char* context) {
    CorrelationId cid = generateCorrelationId();
    currentCorrelationId() = cid;
    return cid;
}

inline void endCorrelation(CorrelationId cid) {
    if (currentCorrelationId() == cid) {
        currentCorrelationId() = 0;
    }
}

// ============================================================================
// Time Utilities
// ============================================================================

inline std::string getTimeString() {
    SYSTEMTIME st;
    GetLocalTime(&st);

    char buffer[32];
    if (config().useMilliseconds) {
        snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    } else {
        snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d",
            st.wHour, st.wMinute, st.wSecond);
    }
    return std::string(buffer);
}

inline double getTimestampMs() {
    static LARGE_INTEGER frequency = {};
    static LARGE_INTEGER startTime = {};
    static bool initialized = false;

    if (!initialized) {
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&startTime);
        initialized = true;
    }

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)(counter.QuadPart - startTime.QuadPart) / frequency.QuadPart * 1000.0;
}

// Track last log time for delta calculation
inline double& lastLogTime() {
    static double last = 0;
    return last;
}

inline std::string formatDelta(double deltaMs) {
    char buffer[16];
    if (deltaMs < 1.0) {
        snprintf(buffer, sizeof(buffer), "+%.0fus", deltaMs * 1000);
    } else if (deltaMs < 1000.0) {
        snprintf(buffer, sizeof(buffer), "+%.1fms", deltaMs);
    } else if (deltaMs < 60000.0) {
        snprintf(buffer, sizeof(buffer), "+%.2fs", deltaMs / 1000);
    } else {
        snprintf(buffer, sizeof(buffer), "+%.1fm", deltaMs / 60000);
    }
    return std::string(buffer);
}

inline DWORD getThreadId() {
    return GetCurrentThreadId();
}

// ============================================================================
// Memory Tracking
// ============================================================================

inline size_t getCurrentMemoryUsage() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
}

inline size_t& lastMemoryUsage() {
    static size_t last = 0;
    return last;
}

inline std::string formatMemoryDelta(size_t current, size_t last) {
    if (last == 0) return "";

    int64_t delta = (int64_t)current - (int64_t)last;
    char buffer[32];

    if (delta == 0) {
        return "";
    } else if (delta > 0) {
        if (delta < 1024) {
            snprintf(buffer, sizeof(buffer), " [+%lld B]", delta);
        } else if (delta < 1024 * 1024) {
            snprintf(buffer, sizeof(buffer), " [+%.1f KB]", delta / 1024.0);
        } else {
            snprintf(buffer, sizeof(buffer), " [+%.1f MB]", delta / 1024.0 / 1024.0);
        }
    } else {
        delta = -delta;
        if (delta < 1024) {
            snprintf(buffer, sizeof(buffer), " [-%lld B]", delta);
        } else if (delta < 1024 * 1024) {
            snprintf(buffer, sizeof(buffer), " [-%.1f KB]", delta / 1024.0);
        } else {
            snprintf(buffer, sizeof(buffer), " [-%.1f MB]", delta / 1024.0 / 1024.0);
        }
    }
    return std::string(buffer);
}

// ============================================================================
// JSON Escaping
// ============================================================================

inline std::string escapeJson(const char* str) {
    std::ostringstream ss;
    for (const char* p = str; *p; ++p) {
        switch (*p) {
            case '"':  ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:   ss << *p; break;
        }
    }
    return ss.str();
}

// ============================================================================
// Filename extraction (smart truncation)
// ============================================================================

inline std::string extractFilename(const char* path, int maxLen = 20) {
    const char* filename = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            filename = p + 1;
        }
    }

    std::string result(filename);
    if ((int)result.length() > maxLen && maxLen > 3) {
        // Truncate with ellipsis, keeping the extension
        size_t dotPos = result.rfind('.');
        if (dotPos != std::string::npos && dotPos > 0) {
            std::string ext = result.substr(dotPos);
            int nameLen = maxLen - (int)ext.length() - 2; // -2 for ".."
            if (nameLen > 0) {
                result = result.substr(0, nameLen) + ".." + ext;
            }
        } else {
            result = result.substr(0, maxLen - 2) + "..";
        }
    }
    return result;
}

// ============================================================================
// Core Logging Functions
// ============================================================================

inline const char* getLevelColor(const char* level) {
    if (strcmp(level, "DEBUG") == 0) return colors::LVL_DEBUG;
    if (strcmp(level, "INFO") == 0)  return colors::LVL_INFO;
    if (strcmp(level, "WARN") == 0)  return colors::LVL_WARN;
    if (strcmp(level, "ERROR") == 0) return colors::LVL_ERROR;
    if (strcmp(level, "CRIT") == 0)  return colors::LVL_CRITICAL;
    return colors::RESET;
}

inline void debugLogImpl(
    const char* level,
    const char* file,
    int line,
    CorrelationId cid,
    const char* message
) {
    if (!config().enabled) return;

    enableAnsiSupport();

    double currentTime = getTimestampMs();
    double deltaTime = currentTime - lastLogTime();
    lastLogTime() = currentTime;

    size_t currentMem = 0;
    std::string memDelta;
    if (config().includeMemoryDelta) {
        currentMem = getCurrentMemoryUsage();
        memDelta = formatMemoryDelta(currentMem, lastMemoryUsage());
        lastMemoryUsage() = currentMem;
    }

    std::string filename = extractFilename(file);
    CorrelationId effectiveCid = (cid != 0) ? cid : currentCorrelationId();

    if (config().format == LogFormat::JSON) {
        // JSON format
        fprintf(config().output,
            "{\"ts\":%.3f,\"delta\":%.3f,\"level\":\"%s\",\"tid\":%lu,\"cid\":%llu,"
            "\"file\":\"%s\",\"line\":%d,\"msg\":\"%s\"",
            currentTime, deltaTime, level, getThreadId(), effectiveCid,
            escapeJson(filename.c_str()).c_str(), line, escapeJson(message).c_str());

        if (config().includeMemoryDelta && currentMem > 0) {
            fprintf(config().output, ",\"mem\":%zu", currentMem);
        }
        fprintf(config().output, "}\n");
    }
    else if (config().format == LogFormat::RICH && config().useColors) {
        // Rich-style colored output
        // Format: [HH:MM:SS.mmm] [+delta] LEVEL    Message                  file.cpp:123

        std::string timeStr = getTimeString();
        const char* levelColor = getLevelColor(level);

        // Build location string
        char location[64];
        snprintf(location, sizeof(location), "%s:%d", filename.c_str(), line);

        // Build delta string
        std::string deltaStr = config().includeDeltaTime ? formatDelta(deltaTime) : "";

        // Calculate base content length for padding
        int baseLen = (int)timeStr.length() + 3;  // [time]
        if (config().includeDeltaTime) baseLen += (int)deltaStr.length() + 3;  // [delta]
        baseLen += 9;  // LEVEL + space
        baseLen += (int)strlen(message);
        baseLen += (int)strlen(location) + 2;

        int padding = config().boxWidth - baseLen;
        if (padding < 1) padding = 1;

        // Print timestamp
        fprintf(config().output, "%s[%s]%s ",
            colors::TIMESTAMP, timeStr.c_str(), colors::RESET);

        // Print delta time if enabled
        if (config().includeDeltaTime) {
            fprintf(config().output, "%s[%7s]%s ",
                colors::DELTA, deltaStr.c_str(), colors::RESET);
        }

        // Print level
        fprintf(config().output, "%s%-8s%s ", levelColor, level, colors::RESET);

        // Print message
        fprintf(config().output, "%s", message);

        // Print memory delta if enabled
        if (config().includeMemoryDelta && !memDelta.empty()) {
            fprintf(config().output, "%s%s%s", colors::MEMORY, memDelta.c_str(), colors::RESET);
        }

        // Right-align location
        fprintf(config().output, "%*s%s%s%s\n",
            padding, "", colors::LOCATION, location, colors::RESET);
    }
    else {
        // Plain text format
        std::string timeStr = getTimeString();
        std::string deltaStr = config().includeDeltaTime ? formatDelta(deltaTime) : "";

        fprintf(config().output, "[%s]", timeStr.c_str());
        if (config().includeDeltaTime) {
            fprintf(config().output, " [%7s]", deltaStr.c_str());
        }
        fprintf(config().output, " %-8s %s    %s:%d\n",
            level, message, filename.c_str(), line);
    }

    fflush(config().output);
}

inline void debugLog(const char* level, const char* file, int line, const char* fmt, ...) {
    if (!config().enabled) return;

    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    debugLogImpl(level, file, line, 0, buffer);
}

inline void debugLogCid(CorrelationId cid, const char* level, const char* file, int line, const char* fmt, ...) {
    if (!config().enabled) return;

    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    debugLogImpl(level, file, line, cid, buffer);
}

// ============================================================================
// Section Tracking with Rich-style boxes
// ============================================================================

inline void printBox(const char* title, bool isStart, const char* file = nullptr, int line = 0) {
    if (!config().enabled) return;

    enableAnsiSupport();

    int width = config().boxWidth;
    int titleLen = (int)strlen(title);

    if (config().format == LogFormat::RICH && config().useColors) {
        if (isStart) {
            // Top border: ┌── ▶ title ────────────────────── file.cpp:123 ──┐
            fprintf(config().output, "\n%s%s", colors::BOX_COLOR, box::TL);
            fprintf(config().output, "%s%s%s ", colors::RESET, box::H, box::H);
            fprintf(config().output, "%s%s %s%s%s",
                colors::SECTION, box::ARROW_R, colors::BOLD, title, colors::RESET);

            // Add location if provided
            if (file && line > 0) {
                std::string filename = extractFilename(file);
                char location[64];
                snprintf(location, sizeof(location), "%s:%d", filename.c_str(), line);
                int locLen = (int)strlen(location);

                int remaining = width - titleLen - locLen - 12;
                fprintf(config().output, " ");
                for (int i = 0; i < remaining; i++) fprintf(config().output, "%s", box::H);
                fprintf(config().output, " %s%s%s ", colors::LOCATION, location, colors::RESET);
            } else {
                int remaining = width - titleLen - 8;
                fprintf(config().output, " ");
                for (int i = 0; i < remaining; i++) fprintf(config().output, "%s", box::H);
            }
            fprintf(config().output, "%s%s%s\n", colors::BOX_COLOR, box::TR, colors::RESET);
        } else {
            // Bottom border (without timing - use printBoxWithTime for that)
            fprintf(config().output, "%s%s", colors::BOX_COLOR, box::BL);
            fprintf(config().output, "%s%s%s ", colors::RESET, box::H, box::H);
            fprintf(config().output, "%s%s%s %s ",
                colors::SUCCESS, box::CHECK, colors::RESET, title);

            int remaining = width - titleLen - 10;
            for (int i = 0; i < remaining; i++) fprintf(config().output, "%s", box::H);
            fprintf(config().output, "%s%s%s\n\n", colors::BOX_COLOR, box::BR, colors::RESET);
        }
    } else {
        // Plain text fallback
        if (isStart) {
            fprintf(config().output, "\n+-- %s ", title);
            if (file && line > 0) {
                std::string filename = extractFilename(file);
                fprintf(config().output, "(%s:%d) ", filename.c_str(), line);
            }
            for (int i = 0; i < width - titleLen - 6; i++) fprintf(config().output, "-");
            fprintf(config().output, "+\n");
        } else {
            fprintf(config().output, "+-- [done] %s ", title);
            for (int i = 0; i < width - titleLen - 14; i++) fprintf(config().output, "-");
            fprintf(config().output, "+\n\n");
        }
    }

    fflush(config().output);
}

inline void printBoxWithTime(const char* title, double elapsedMs, size_t memDelta = 0) {
    if (!config().enabled) return;

    enableAnsiSupport();

    int width = config().boxWidth;

    if (config().format == LogFormat::RICH && config().useColors) {
        // Format elapsed time nicely
        char timeStr[32];
        if (elapsedMs < 1.0) {
            snprintf(timeStr, sizeof(timeStr), "%.0fus", elapsedMs * 1000);
        } else if (elapsedMs < 1000.0) {
            snprintf(timeStr, sizeof(timeStr), "%.1fms", elapsedMs);
        } else {
            snprintf(timeStr, sizeof(timeStr), "%.2fs", elapsedMs / 1000);
        }

        int titleLen = (int)strlen(title);
        int timeLen = (int)strlen(timeStr);

        // Bottom border: └── ✔ title (123.4ms) [+1.2 MB] ─────────────────┘
        fprintf(config().output, "%s%s", colors::BOX_COLOR, box::BL);
        fprintf(config().output, "%s%s%s ", colors::RESET, box::H, box::H);
        fprintf(config().output, "%s%s%s %s %s(%s)%s",
            colors::SUCCESS, box::CHECK, colors::RESET,
            title,
            colors::DIM, timeStr, colors::RESET);

        int remaining = width - titleLen - timeLen - 14;

        // Add memory delta if significant
        if (memDelta > 1024) {
            char memStr[32];
            if (memDelta < 1024 * 1024) {
                snprintf(memStr, sizeof(memStr), " [+%.1f KB]", memDelta / 1024.0);
            } else {
                snprintf(memStr, sizeof(memStr), " [+%.1f MB]", memDelta / 1024.0 / 1024.0);
            }
            fprintf(config().output, "%s%s%s", colors::MEMORY, memStr, colors::RESET);
            remaining -= (int)strlen(memStr);
        }

        fprintf(config().output, " ");
        for (int i = 0; i < remaining - 1; i++) fprintf(config().output, "%s", box::H);
        fprintf(config().output, "%s%s%s\n\n", colors::BOX_COLOR, box::BR, colors::RESET);
    } else {
        fprintf(config().output, "+-- [done: %s] %s ",
            (elapsedMs < 1000) ? (std::to_string((int)elapsedMs) + "ms").c_str()
                               : (std::to_string(elapsedMs/1000) + "s").c_str(),
            title);
        int titleLen = (int)strlen(title);
        for (int i = 0; i < width - titleLen - 22; i++) fprintf(config().output, "-");
        fprintf(config().output, "+\n\n");
    }

    fflush(config().output);
}

struct SectionTimer {
    const char* name;
    const char* file;
    int line;
    double startTime;
    size_t startMem;
    CorrelationId cid;

    SectionTimer(const char* n, const char* f, int l)
        : name(n), file(f), line(l), startTime(getTimestampMs()),
          startMem(getCurrentMemoryUsage()), cid(startCorrelation(n)) {
        if (!config().enabled) return;

        // Update timing tracker
        lastLogTime() = startTime;

        if (config().format == LogFormat::JSON) {
            debugLogImpl("ENTER", file, line, cid,
                (std::string("section_start:") + name).c_str());
        } else {
            printBox(name, true, file, line);
        }
    }

    ~SectionTimer() {
        if (!config().enabled) return;

        double elapsed = getTimestampMs() - startTime;
        size_t endMem = getCurrentMemoryUsage();
        size_t memDelta = (endMem > startMem) ? (endMem - startMem) : 0;

        if (config().format == LogFormat::JSON) {
            char msg[256];
            snprintf(msg, sizeof(msg), "section_end:%s,elapsed_ms:%.3f,mem_delta:%zu",
                name, elapsed, memDelta);
            debugLogImpl("EXIT", file, line, cid, msg);
        } else {
            printBoxWithTime(name, elapsed, memDelta);
        }

        endCorrelation(cid);
    }
};

// ============================================================================
// Configuration Functions
// ============================================================================

inline void setDebugEnabled(bool enabled) {
    config().enabled = enabled;
}

inline void setLogFormat(LogFormat format) {
    config().format = format;
}

inline void setLogOutput(FILE* output) {
    config().output = output;
}

inline void setUseColors(bool useColors) {
    config().useColors = useColors;
}

inline void setBoxWidth(int width) {
    config().boxWidth = width;
}

inline void setIncludeDeltaTime(bool include) {
    config().includeDeltaTime = include;
}

inline void setIncludeMemoryDelta(bool include) {
    config().includeMemoryDelta = include;
}

// Print a banner (useful for startup)
inline void printBanner(const char* title, const char* subtitle = nullptr) {
    if (!config().enabled) return;

    enableAnsiSupport();

    int width = config().boxWidth;
    int titleLen = (int)strlen(title);
    int subtitleLen = subtitle ? (int)strlen(subtitle) : 0;

    if (config().format == LogFormat::RICH && config().useColors) {
        // Top border
        fprintf(config().output, "\n%s%s", colors::SECTION, box::TL);
        for (int i = 0; i < width - 2; i++) fprintf(config().output, "%s", box::H);
        fprintf(config().output, "%s%s\n", box::TR, colors::RESET);

        // Title line
        int padding = (width - 4 - titleLen) / 2;
        fprintf(config().output, "%s%s%s", colors::SECTION, box::V, colors::RESET);
        fprintf(config().output, "%*s%s%s%s%*s",
            padding, "", colors::BOLD, title, colors::RESET,
            width - 4 - padding - titleLen, "");
        fprintf(config().output, "%s%s%s\n", colors::SECTION, box::V, colors::RESET);

        // Subtitle line (if provided)
        if (subtitle) {
            int subPadding = (width - 4 - subtitleLen) / 2;
            fprintf(config().output, "%s%s%s", colors::SECTION, box::V, colors::RESET);
            fprintf(config().output, "%*s%s%s%s%*s",
                subPadding, "", colors::DIM, subtitle, colors::RESET,
                width - 4 - subPadding - subtitleLen, "");
            fprintf(config().output, "%s%s%s\n", colors::SECTION, box::V, colors::RESET);
        }

        // Bottom border
        fprintf(config().output, "%s%s", colors::SECTION, box::BL);
        for (int i = 0; i < width - 2; i++) fprintf(config().output, "%s", box::H);
        fprintf(config().output, "%s%s\n\n", box::BR, colors::RESET);
    } else {
        // Plain text
        fprintf(config().output, "\n");
        for (int i = 0; i < width; i++) fprintf(config().output, "=");
        fprintf(config().output, "\n  %s\n", title);
        if (subtitle) fprintf(config().output, "  %s\n", subtitle);
        for (int i = 0; i < width; i++) fprintf(config().output, "=");
        fprintf(config().output, "\n\n");
    }

    fflush(config().output);
}

// Print current memory status
inline void printMemoryStatus(const char* label = "Memory") {
    if (!config().enabled) return;

    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: Working=%lluMB Peak=%lluMB Private=%lluMB",
            label,
            (unsigned long long)(pmc.WorkingSetSize / 1024 / 1024),
            (unsigned long long)(pmc.PeakWorkingSetSize / 1024 / 1024),
            (unsigned long long)(pmc.PrivateUsage / 1024 / 1024));
        debugLogImpl("INFO", __FILE__, __LINE__, 0, msg);
    }
}

} // namespace rippled_debug

// ============================================================================
// Convenience Macros
// ============================================================================

// Basic logging (Rich-style)
#define DEBUG_LOG(fmt, ...) \
    rippled_debug::debugLog("DEBUG", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DEBUG_INFO(fmt, ...) \
    rippled_debug::debugLog("INFO", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DEBUG_WARN(fmt, ...) \
    rippled_debug::debugLog("WARN", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DEBUG_ERROR(fmt, ...) \
    rippled_debug::debugLog("ERROR", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DEBUG_CRITICAL(fmt, ...) \
    rippled_debug::debugLog("CRIT", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

// Logging with explicit correlation ID
#define DEBUG_LOG_CID(cid, fmt, ...) \
    rippled_debug::debugLogCid(cid, "DEBUG", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

// Section tracking with RAII (auto-timing, Rich-style boxes)
#define DEBUG_SECTION(name) \
    rippled_debug::SectionTimer _section_##__LINE__(name, __FILE__, __LINE__)

// Legacy section macros (for compatibility)
#define DEBUG_SECTION_BEGIN(name) \
    { rippled_debug::SectionTimer _section_(name, __FILE__, __LINE__)

#define DEBUG_SECTION_END(name) \
    }

// Correlation ID management
#define DEBUG_CORRELATION_START(context) \
    rippled_debug::startCorrelation(context)

#define DEBUG_CORRELATION_END(cid) \
    rippled_debug::endCorrelation(cid)

// Variable inspection
#define DEBUG_VAR(var) \
    DEBUG_LOG(#var " = %s", std::to_string(var).c_str())

#define DEBUG_PTR(ptr) \
    DEBUG_LOG(#ptr " = %p", (void*)(ptr))

#define DEBUG_STR(str) \
    DEBUG_LOG(#str " = \"%s\"", (str).c_str())

// Memory status
#define DEBUG_MEMORY() \
    rippled_debug::printMemoryStatus()

#define DEBUG_MEMORY_LABEL(label) \
    rippled_debug::printMemoryStatus(label)

// Banner for startup
#define DEBUG_BANNER(title, subtitle) \
    rippled_debug::printBanner(title, subtitle)

// Configuration
#define DEBUG_ENABLED(enabled) \
    rippled_debug::setDebugEnabled(enabled)

#define DEBUG_FORMAT_RICH() \
    rippled_debug::setLogFormat(rippled_debug::LogFormat::RICH)

#define DEBUG_FORMAT_JSON() \
    rippled_debug::setLogFormat(rippled_debug::LogFormat::JSON)

#define DEBUG_FORMAT_TEXT() \
    rippled_debug::setLogFormat(rippled_debug::LogFormat::TEXT)

#define DEBUG_COLORS(enabled) \
    rippled_debug::setUseColors(enabled)

#define DEBUG_DELTA_TIME(enabled) \
    rippled_debug::setIncludeDeltaTime(enabled)

#define DEBUG_MEMORY_TRACKING(enabled) \
    rippled_debug::setIncludeMemoryDelta(enabled)

#else // !_WIN32

// No-op on non-Windows platforms
#define DEBUG_LOG(fmt, ...) ((void)0)
#define DEBUG_INFO(fmt, ...) ((void)0)
#define DEBUG_WARN(fmt, ...) ((void)0)
#define DEBUG_ERROR(fmt, ...) ((void)0)
#define DEBUG_CRITICAL(fmt, ...) ((void)0)
#define DEBUG_LOG_CID(cid, fmt, ...) ((void)0)
#define DEBUG_SECTION(name) ((void)0)
#define DEBUG_SECTION_BEGIN(name) {
#define DEBUG_SECTION_END(name) }
#define DEBUG_CORRELATION_START(context) 0
#define DEBUG_CORRELATION_END(cid) ((void)0)
#define DEBUG_VAR(var) ((void)0)
#define DEBUG_PTR(ptr) ((void)0)
#define DEBUG_STR(str) ((void)0)
#define DEBUG_MEMORY() ((void)0)
#define DEBUG_MEMORY_LABEL(label) ((void)0)
#define DEBUG_BANNER(title, subtitle) ((void)0)
#define DEBUG_ENABLED(enabled) ((void)0)
#define DEBUG_FORMAT_RICH() ((void)0)
#define DEBUG_FORMAT_JSON() ((void)0)
#define DEBUG_FORMAT_TEXT() ((void)0)
#define DEBUG_COLORS(enabled) ((void)0)
#define DEBUG_DELTA_TIME(enabled) ((void)0)
#define DEBUG_MEMORY_TRACKING(enabled) ((void)0)

#endif // _WIN32

#endif // RIPPLED_WINDOWS_DEBUG_DEBUG_LOG_H

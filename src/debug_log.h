/**
 * @file debug_log.h
 * @brief Debug logging macros for tracking execution flow
 *
 * Provides structured logging for debugging rippled on Windows.
 * Inspired by FlexiFlow's correlation ID and structured logging patterns.
 *
 * Features:
 * - Correlation IDs for tracking related log entries across threads
 * - Thread-safe logging
 * - Multiple output formats (text, JSON)
 * - Section tracking for structured flow analysis
 *
 * Usage:
 *   DEBUG_SECTION_BEGIN("rpc_startup");
 *   DEBUG_LOG("Processing command: %s", cmd.c_str());
 *   DEBUG_SECTION_END("rpc_startup");
 *
 *   // With correlation ID:
 *   auto cid = DEBUG_CORRELATION_START("rpc_request");
 *   DEBUG_LOG_CID(cid, "Handling request");
 *   DEBUG_CORRELATION_END(cid);
 */

#ifndef RIPPLED_WINDOWS_DEBUG_DEBUG_LOG_H
#define RIPPLED_WINDOWS_DEBUG_DEBUG_LOG_H

#ifdef _WIN32

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <atomic>
#include <string>
#include <sstream>
#include <iomanip>

namespace rippled_debug {

// ============================================================================
// Configuration
// ============================================================================

enum class LogFormat {
    TEXT,   // Human-readable text format
    JSON    // Machine-parseable JSON format
};

struct LogConfig {
    bool enabled = true;
    LogFormat format = LogFormat::TEXT;
    FILE* output = stderr;
    bool includeThreadId = true;
    bool includeCorrelationId = true;
};

inline LogConfig& config() {
    static LogConfig cfg;
    return cfg;
}

// ============================================================================
// Correlation ID System (FlexiFlow-inspired)
// ============================================================================

using CorrelationId = uint64_t;

inline CorrelationId generateCorrelationId() {
    static std::atomic<uint64_t> counter{0};
    return ++counter;
}

// Thread-local correlation ID for automatic propagation
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
// Timestamp and Thread ID
// ============================================================================

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

inline DWORD getThreadId() {
    return GetCurrentThreadId();
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
// Core Logging Functions
// ============================================================================

inline void debugLogImpl(
    const char* level,
    const char* file,
    int line,
    CorrelationId cid,
    const char* message
) {
    if (!config().enabled) return;

    double timestamp = getTimestampMs();
    DWORD threadId = getThreadId();
    CorrelationId effectiveCid = (cid != 0) ? cid : currentCorrelationId();

    // Extract just the filename from path
    const char* filename = file;
    for (const char* p = file; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            filename = p + 1;
        }
    }

    if (config().format == LogFormat::JSON) {
        // JSON format for machine parsing
        fprintf(config().output,
            "{\"ts\":%.3f,\"level\":\"%s\",\"tid\":%lu,\"cid\":%llu,\"file\":\"%s\",\"line\":%d,\"msg\":\"%s\"}\n",
            timestamp, level, threadId, effectiveCid, escapeJson(filename).c_str(), line, escapeJson(message).c_str());
    } else {
        // Text format for human reading
        if (config().includeCorrelationId && effectiveCid != 0) {
            fprintf(config().output, "[%8.3f] [%5lu] [cid:%04llu] [%-5s] %s:%d: %s\n",
                timestamp, threadId, effectiveCid, level, filename, line, message);
        } else if (config().includeThreadId) {
            fprintf(config().output, "[%8.3f] [%5lu] [%-5s] %s:%d: %s\n",
                timestamp, threadId, level, filename, line, message);
        } else {
            fprintf(config().output, "[%8.3f] [%-5s] %s:%d: %s\n",
                timestamp, level, filename, line, message);
        }
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
// Section Tracking (with timing)
// ============================================================================

struct SectionTimer {
    const char* name;
    double startTime;
    CorrelationId cid;

    SectionTimer(const char* n, const char* file, int line)
        : name(n), startTime(getTimestampMs()), cid(startCorrelation(n)) {
        if (!config().enabled) return;

        if (config().format == LogFormat::JSON) {
            debugLogImpl("ENTER", file, line, cid,
                (std::string("section_start:") + name).c_str());
        } else {
            fprintf(config().output, "\n");
            fprintf(config().output, "[%8.3f] [%5lu] [cid:%04llu] =====> ENTERING [%s] <=====\n",
                startTime, getThreadId(), cid, name);
            fprintf(config().output, "[%8.3f] [%5lu] [cid:%04llu]        Location: %s:%d\n",
                startTime, getThreadId(), cid, file, line);
            fflush(config().output);
        }
    }

    ~SectionTimer() {
        if (!config().enabled) return;

        double endTime = getTimestampMs();
        double elapsed = endTime - startTime;

        if (config().format == LogFormat::JSON) {
            char msg[256];
            snprintf(msg, sizeof(msg), "section_end:%s,elapsed_ms:%.3f", name, elapsed);
            debugLogImpl("EXIT", "", 0, cid, msg);
        } else {
            fprintf(config().output, "[%8.3f] [%5lu] [cid:%04llu] <===== EXITING [%s] (%.3f ms) =====>\n",
                endTime, getThreadId(), cid, name, elapsed);
            fprintf(config().output, "\n");
            fflush(config().output);
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

} // namespace rippled_debug

// ============================================================================
// Convenience Macros
// ============================================================================

// Basic logging (auto-inherits correlation ID from thread context)
#define DEBUG_LOG(fmt, ...) \
    rippled_debug::debugLog("DEBUG", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DEBUG_INFO(fmt, ...) \
    rippled_debug::debugLog("INFO", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DEBUG_WARN(fmt, ...) \
    rippled_debug::debugLog("WARN", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DEBUG_ERROR(fmt, ...) \
    rippled_debug::debugLog("ERROR", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

// Logging with explicit correlation ID
#define DEBUG_LOG_CID(cid, fmt, ...) \
    rippled_debug::debugLogCid(cid, "DEBUG", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

// Section tracking with RAII (auto-timing)
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

// Configuration
#define DEBUG_ENABLED(enabled) \
    rippled_debug::setDebugEnabled(enabled)

#define DEBUG_FORMAT_JSON() \
    rippled_debug::setLogFormat(rippled_debug::LogFormat::JSON)

#define DEBUG_FORMAT_TEXT() \
    rippled_debug::setLogFormat(rippled_debug::LogFormat::TEXT)

#else // !_WIN32

// No-op on non-Windows platforms
#define DEBUG_LOG(fmt, ...) ((void)0)
#define DEBUG_INFO(fmt, ...) ((void)0)
#define DEBUG_WARN(fmt, ...) ((void)0)
#define DEBUG_ERROR(fmt, ...) ((void)0)
#define DEBUG_LOG_CID(cid, fmt, ...) ((void)0)
#define DEBUG_SECTION(name) ((void)0)
#define DEBUG_SECTION_BEGIN(name) {
#define DEBUG_SECTION_END(name) }
#define DEBUG_CORRELATION_START(context) 0
#define DEBUG_CORRELATION_END(cid) ((void)0)
#define DEBUG_VAR(var) ((void)0)
#define DEBUG_PTR(ptr) ((void)0)
#define DEBUG_STR(str) ((void)0)
#define DEBUG_ENABLED(enabled) ((void)0)
#define DEBUG_FORMAT_JSON() ((void)0)
#define DEBUG_FORMAT_TEXT() ((void)0)

#endif // _WIN32

#endif // RIPPLED_WINDOWS_DEBUG_DEBUG_LOG_H

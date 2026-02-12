/**
 * @file debug_log.h
 * @brief Debug logging macros for tracking execution flow
 *
 * Provides structured logging for debugging rippled on Windows.
 * All output goes to stderr to avoid interfering with stdout.
 *
 * Usage:
 *   DEBUG_SECTION_BEGIN("rpc_startup");
 *   DEBUG_LOG("Processing command: %s", cmd.c_str());
 *   DEBUG_SECTION_END("rpc_startup");
 */

#ifndef RIPPLED_WINDOWS_DEBUG_DEBUG_LOG_H
#define RIPPLED_WINDOWS_DEBUG_DEBUG_LOG_H

#ifdef _WIN32

#include <windows.h>
#include <cstdio>
#include <cstdarg>

namespace rippled_debug {

// Enable/disable debug logging at runtime
inline bool& debugEnabled() {
    static bool enabled = true;
    return enabled;
}

inline void setDebugEnabled(bool enabled) {
    debugEnabled() = enabled;
}

// Get high-resolution timestamp
inline double getTimestampMs() {
    static LARGE_INTEGER frequency = {};
    static bool initialized = false;

    if (!initialized) {
        QueryPerformanceFrequency(&frequency);
        initialized = true;
    }

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / frequency.QuadPart * 1000.0;
}

// Core logging function
inline void debugLog(const char* level, const char* file, int line, const char* fmt, ...) {
    if (!debugEnabled()) return;

    double timestamp = getTimestampMs();

    // Print prefix
    fprintf(stderr, "[%.3f] [%s] %s:%d: ", timestamp, level, file, line);

    // Print message
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
    fflush(stderr);
}

// Section tracking for structured logging
inline void sectionBegin(const char* name, const char* file, int line) {
    if (!debugEnabled()) return;
    fprintf(stderr, "\n");
    fprintf(stderr, "[%.3f] [SECTION] ========== ENTERING %s ==========\n",
            getTimestampMs(), name);
    fprintf(stderr, "[%.3f] [SECTION] Location: %s:%d\n", getTimestampMs(), file, line);
    fflush(stderr);
}

inline void sectionEnd(const char* name, const char* file, int line) {
    if (!debugEnabled()) return;
    fprintf(stderr, "[%.3f] [SECTION] ========== EXITING %s ==========\n",
            getTimestampMs(), name);
    fprintf(stderr, "\n");
    fflush(stderr);
}

} // namespace rippled_debug

// Convenience macros
#define DEBUG_LOG(fmt, ...) \
    rippled_debug::debugLog("DEBUG", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DEBUG_INFO(fmt, ...) \
    rippled_debug::debugLog("INFO", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DEBUG_WARN(fmt, ...) \
    rippled_debug::debugLog("WARN", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DEBUG_ERROR(fmt, ...) \
    rippled_debug::debugLog("ERROR", __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DEBUG_SECTION_BEGIN(name) \
    rippled_debug::sectionBegin(name, __FILE__, __LINE__)

#define DEBUG_SECTION_END(name) \
    rippled_debug::sectionEnd(name, __FILE__, __LINE__)

#define DEBUG_VAR(var) \
    DEBUG_LOG(#var " = %s", std::to_string(var).c_str())

#define DEBUG_PTR(ptr) \
    DEBUG_LOG(#ptr " = %p", (void*)(ptr))

#define DEBUG_ENABLED(enabled) \
    rippled_debug::setDebugEnabled(enabled)

#else // !_WIN32

// No-op on non-Windows platforms
#define DEBUG_LOG(fmt, ...) ((void)0)
#define DEBUG_INFO(fmt, ...) ((void)0)
#define DEBUG_WARN(fmt, ...) ((void)0)
#define DEBUG_ERROR(fmt, ...) ((void)0)
#define DEBUG_SECTION_BEGIN(name) ((void)0)
#define DEBUG_SECTION_END(name) ((void)0)
#define DEBUG_VAR(var) ((void)0)
#define DEBUG_PTR(ptr) ((void)0)
#define DEBUG_ENABLED(enabled) ((void)0)

#endif // _WIN32

#endif // RIPPLED_WINDOWS_DEBUG_DEBUG_LOG_H

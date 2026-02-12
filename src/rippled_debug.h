/**
 * @file rippled_debug.h
 * @brief Single-include header for all rippled Windows debugging tools
 *
 * This is the recommended way to include the toolkit.
 *
 * Usage:
 *   #include "rippled_debug.h"
 *
 *   int main() {
 *       RIPPLED_DEBUG_INIT();  // Install all handlers
 *       // ... your code ...
 *   }
 *
 * Or selectively:
 *   RIPPLED_DEBUG_INIT_CRASH();     // Only crash handlers
 *   RIPPLED_DEBUG_INIT_MINIDUMP();  // Only minidump handler
 */

#ifndef RIPPLED_WINDOWS_DEBUG_H
#define RIPPLED_WINDOWS_DEBUG_H

#include "crash_handlers.h"
#include "debug_log.h"
#include "minidump.h"

#ifdef _WIN32

namespace rippled_debug {

/**
 * Initialize all debug handlers.
 * Call this at the start of main().
 */
inline void initAll() {
    fprintf(stderr, "\n");
    fprintf(stderr, "╔══════════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║         rippled-windows-debug toolkit initialized        ║\n");
    fprintf(stderr, "╚══════════════════════════════════════════════════════════╝\n");
    fprintf(stderr, "\n");

    installVerboseCrashHandlers();
    installMinidumpHandler();

    fprintf(stderr, "\n");
    fflush(stderr);
}

/**
 * Initialize only crash handlers (no minidump).
 */
inline void initCrashHandlersOnly() {
    installVerboseCrashHandlers();
}

/**
 * Initialize only minidump handler (no verbose crash output).
 */
inline void initMinidumpOnly() {
    installMinidumpHandler();
}

} // namespace rippled_debug

// Convenience macros
#define RIPPLED_DEBUG_INIT() rippled_debug::initAll()
#define RIPPLED_DEBUG_INIT_CRASH() rippled_debug::initCrashHandlersOnly()
#define RIPPLED_DEBUG_INIT_MINIDUMP() rippled_debug::initMinidumpOnly()

#else // !_WIN32

#define RIPPLED_DEBUG_INIT() ((void)0)
#define RIPPLED_DEBUG_INIT_CRASH() ((void)0)
#define RIPPLED_DEBUG_INIT_MINIDUMP() ((void)0)

#endif // _WIN32

#endif // RIPPLED_WINDOWS_DEBUG_H

/**
 * @file test_crash.cpp
 * @brief Test the crash handlers with various failure modes
 *
 * Build:
 *   cl /EHsc /Zi /utf-8 test_crash.cpp /link dbghelp.lib
 *
 * Run:
 *   test_crash.exe [mode]
 *
 * Modes:
 *   1 - std::bad_alloc (memory allocation failure)
 *   2 - std::runtime_error
 *   3 - null pointer dereference
 *   4 - abort()
 *   5 - stack overflow (recursive)
 *   6 - demo mode (show Rich-style logging)
 */

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

// Include the debug toolkit
#include "../src/rippled_debug.h"

void simulateWork(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void testBadAlloc() {
    DEBUG_SECTION("testBadAlloc");
    DEBUG_INFO("Attempting to allocate impossibly large vector...");

    // Try to allocate way more memory than available
    std::vector<char> huge(SIZE_MAX / 2);
}

void testRuntimeError() {
    DEBUG_SECTION("testRuntimeError");
    DEBUG_INFO("Throwing std::runtime_error...");

    throw std::runtime_error("Test runtime error from rippled-windows-debug");
}

void testNullPointer() {
    DEBUG_SECTION("testNullPointer");
    DEBUG_WARN("About to dereference null pointer...");

    int* ptr = nullptr;
    *ptr = 42;  // SIGSEGV
}

void testAbort() {
    DEBUG_SECTION("testAbort");
    DEBUG_ERROR("Calling abort()...");

    std::abort();
}

void testStackOverflow(int depth = 0) {
    char buffer[4096];  // Use stack space
    buffer[0] = static_cast<char>(depth);

    if (depth % 1000 == 0) {
        DEBUG_LOG("Stack overflow depth: %d", depth);
    }

    testStackOverflow(depth + 1);  // Infinite recursion
}

void demoRichLogging() {
    DEBUG_BANNER("rippled-windows-debug", "Rich-style Terminal Logging Demo");

    DEBUG_INFO("Starting demonstration of Rich-style logging...");
    simulateWork(50);

    DEBUG_LOG("This is a DEBUG level message");
    DEBUG_INFO("This is an INFO level message");
    DEBUG_WARN("This is a WARNING level message");
    DEBUG_ERROR("This is an ERROR level message");
    DEBUG_CRITICAL("This is a CRITICAL level message");

    simulateWork(50);

    {
        DEBUG_SECTION("database_init");
        DEBUG_INFO("Connecting to database...");
        simulateWork(100);
        DEBUG_INFO("Loading schema...");
        simulateWork(50);
        DEBUG_INFO("Connection established");
    }

    {
        DEBUG_SECTION("rpc_startup");
        DEBUG_INFO("Initializing RPC handlers...");
        simulateWork(75);

        {
            DEBUG_SECTION("json_context");
            DEBUG_LOG("Creating JSON context...");
            simulateWork(25);
            DEBUG_LOG("Registering methods...");
            simulateWork(25);
        }

        DEBUG_INFO("RPC system ready");
    }

    {
        DEBUG_SECTION("network_init");
        DEBUG_INFO("Starting peer connections...");
        simulateWork(150);
        DEBUG_WARN("Peer 192.168.1.50 slow to respond");
        simulateWork(50);
        DEBUG_INFO("Connected to 5 peers");
    }

    DEBUG_INFO("All systems initialized successfully!");
}

void printUsage() {
    std::cout << "\nrippled-windows-debug crash test\n";
    std::cout << "================================\n\n";
    std::cout << "Usage: test_crash.exe [mode]\n\n";
    std::cout << "Modes:\n";
    std::cout << "  1 - std::bad_alloc (memory allocation failure)\n";
    std::cout << "  2 - std::runtime_error\n";
    std::cout << "  3 - null pointer dereference (SIGSEGV)\n";
    std::cout << "  4 - abort() call (SIGABRT)\n";
    std::cout << "  5 - stack overflow\n";
    std::cout << "\n";
}

int main(int argc, char* argv[]) {
    if (argc >= 2 && std::string(argv[1]) == "7") {
        // Mode 7: Just show build info without initializing handlers
        PRINT_BUILD_INFO();
        return 0;
    }

    // Initialize all debug handlers (prints full build info)
    RIPPLED_DEBUG_INIT();

    if (argc < 2) {
        printUsage();
        return 1;
    }

    int mode = std::stoi(argv[1]);

    if (mode == 6) {
        // Demo mode - just show the logging
        demoRichLogging();
        return 0;
    }

    std::cout << "\nRunning crash test mode " << mode << "...\n";
    std::cout << "You should see verbose crash diagnostics below.\n\n";

    switch (mode) {
        case 1:
            testBadAlloc();
            break;
        case 2:
            testRuntimeError();
            break;
        case 3:
            testNullPointer();
            break;
        case 4:
            testAbort();
            break;
        case 5:
            testStackOverflow();
            break;
        default:
            std::cerr << "Unknown mode: " << mode << "\n";
            printUsage();
            return 1;
    }

    std::cout << "Test completed without crash (unexpected!)\n";
    return 0;
}

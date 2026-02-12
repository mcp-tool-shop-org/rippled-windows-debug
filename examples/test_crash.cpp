/**
 * @file test_crash.cpp
 * @brief Test the crash handlers with various failure modes
 *
 * Build:
 *   cl /EHsc /Zi test_crash.cpp /link dbghelp.lib
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
 */

#include <iostream>
#include <string>
#include <vector>

// Include the debug toolkit
#include "../src/rippled_debug.h"

void testBadAlloc() {
    DEBUG_SECTION_BEGIN("testBadAlloc");
    DEBUG_LOG("Attempting to allocate impossibly large vector...");

    // Try to allocate way more memory than available
    std::vector<char> huge(SIZE_MAX / 2);

    DEBUG_SECTION_END("testBadAlloc");
}

void testRuntimeError() {
    DEBUG_SECTION_BEGIN("testRuntimeError");
    DEBUG_LOG("Throwing std::runtime_error...");

    throw std::runtime_error("Test runtime error from rippled-windows-debug");

    DEBUG_SECTION_END("testRuntimeError");
}

void testNullPointer() {
    DEBUG_SECTION_BEGIN("testNullPointer");
    DEBUG_LOG("Dereferencing null pointer...");

    int* ptr = nullptr;
    *ptr = 42;  // SIGSEGV

    DEBUG_SECTION_END("testNullPointer");
}

void testAbort() {
    DEBUG_SECTION_BEGIN("testAbort");
    DEBUG_LOG("Calling abort()...");

    std::abort();

    DEBUG_SECTION_END("testAbort");
}

void testStackOverflow(int depth = 0) {
    char buffer[4096];  // Use stack space
    buffer[0] = static_cast<char>(depth);

    if (depth % 1000 == 0) {
        DEBUG_LOG("Stack overflow depth: %d", depth);
    }

    testStackOverflow(depth + 1);  // Infinite recursion
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
    // Initialize all debug handlers
    RIPPLED_DEBUG_INIT();

    if (argc < 2) {
        printUsage();
        return 1;
    }

    int mode = std::stoi(argv[1]);

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

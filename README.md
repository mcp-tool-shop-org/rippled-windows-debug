# rippled-windows-debug

**Windows debugging toolkit for rippled (XRPL validator node)**

This toolkit provides verbose crash diagnostics for rippled on Windows, making it easier to identify and debug issues that are difficult to diagnose on the platform.

![Rich-style logging demo](docs/rich-demo.png)

## The Problem

Windows crashes often show misleading error codes:
- `STATUS_STACK_BUFFER_OVERRUN (0xC0000409)` - Often not actually stack corruption
- `abort() has been called` - Hides the real exception
- No stack traces in release builds

**Example**: A `std::bad_alloc` (memory allocation failure) can appear as `STATUS_STACK_BUFFER_OVERRUN` because:
1. Exception not caught → `std::terminate()` called
2. `terminate()` calls `abort()`
3. MSVC's `/GS` security checks interpret this as buffer overrun

## What This Toolkit Provides

### 1. Verbose Crash Handlers (`crash_handlers.h`)

Single-header crash diagnostics that capture:
- Actual exception type and message
- Full stack trace with symbol resolution
- Signal information (SIGABRT, SIGSEGV, etc.)
- **Complete build info** (toolkit version, git commit, compiler)
- **System info** (Windows version, CPU, memory, computer name)

### 2. Rich-style Debug Logging (`debug_log.h`)

Beautiful terminal logging inspired by Python's [Rich](https://github.com/Textualize/rich) library:
- **Colored log levels** - INFO (cyan), WARN (yellow), ERROR (red)
- **Box-drawing characters** - Visual section boundaries with Unicode
- **Automatic timing** - Sections show elapsed time on completion
- **Correlation IDs** - Track related log entries across threads
- **Multiple formats** - Rich (colored), Text (plain), JSON (machine-parseable)

### 3. Minidump Generation (`minidump.h`)

Automatic crash dump capture:
- Full memory dumps for debugging
- Configurable dump location
- Automatic cleanup of old dumps

### 4. Build Information (`build_info.h`)

Comprehensive build and system info:
- Toolkit version
- Git commit hash, branch, dirty status
- Compiler name and version
- Build date/time and architecture
- Windows version and build number
- CPU model and core count
- System memory

## Quick Start

### Option 1: Patch rippled (Recommended for debugging)

Apply the patch to `src/xrpld/app/main/Main.cpp`:

```cpp
// Add at top of file (after existing includes)
#if BOOST_OS_WINDOWS
#include "crash_handlers.h"
#endif

// Add at start of main()
#if BOOST_OS_WINDOWS
    installVerboseCrashHandlers();
#endif
```

### Option 2: Standalone test wrapper

```cpp
#include "crash_handlers.h"

int main() {
    installVerboseCrashHandlers();

    // Your test code here
    // Any crash will now show full diagnostics
}
```

## Example Output

### Rich-style Logging (default)

```
┌────────────────────────────────────────────────────────────────────┐
│                    rippled-windows-debug                           │
│               Rich-style Terminal Logging Demo                     │
└────────────────────────────────────────────────────────────────────┘

[14:32:15] INFO     Starting demonstration of Rich-style logging...   demo.cpp:42
[14:32:15] DEBUG    This is a DEBUG level message                     demo.cpp:45
[14:32:15] INFO     This is an INFO level message                     demo.cpp:46
[14:32:15] WARN     This is a WARNING level message                   demo.cpp:47
[14:32:15] ERROR    This is an ERROR level message                    demo.cpp:48
[14:32:15] CRIT     This is a CRITICAL level message                  demo.cpp:49

┌── ▶ database_init ──────────────────────────────────────────────────┐
[14:32:15] INFO     Connecting to database...                         db.cpp:12
[14:32:15] INFO     Loading schema...                                 db.cpp:15
[14:32:15] INFO     Connection established                            db.cpp:18
└── ✔ database_init (156.2ms) ────────────────────────────────────────┘

┌── ▶ rpc_startup ────────────────────────────────────────────────────┐
[14:32:15] INFO     Initializing RPC handlers...                      rpc.cpp:42
  ┌── ▶ json_context ─────────────────────────────────────────────────┐
  [14:32:15] DEBUG    Creating JSON context...                        json.cpp:8
  [14:32:15] DEBUG    Registering methods...                          json.cpp:12
  └── ✔ json_context (52.3ms) ────────────────────────────────────────┘
[14:32:15] INFO     RPC system ready                                  rpc.cpp:58
└── ✔ rpc_startup (128.7ms) ──────────────────────────────────────────┘
```

### Crash Handler Output

When a crash occurs, you'll see a comprehensive report:

```
################################################################################
###                     VERBOSE CRASH HANDLER                                ###
###                      terminate() called                                  ###
################################################################################

Timestamp: 2024-02-12 14:32:15

--- Build & System Info ---
Toolkit:          rippled-windows-debug v1.1.0
Git:              main @ a1b2c3d4e5f6 (dirty)
Built:            Feb 12 2024 14:30:00
Compiler:         MSVC 1944
Architecture:     x64 64-bit
Windows:          Windows 11 (Build 10.0.22631)
Edition:          Professional 23H2 (UBR: 2861)
CPU:              AMD Ryzen 9 5900X 12-Core Processor
Computer:         DESKTOP-ABC123
User:             developer (Administrator)

--- Exception Details ---
Type:    std::bad_alloc
Message: bad allocation

--- Diagnostic Hints ---
MEMORY ALLOCATION FAILURE detected.
Common causes:
  1. Requesting impossibly large allocation (SIZE_MAX, negative size cast to size_t)
  2. System out of memory (check Available Physical above)
  3. Memory fragmentation

This often appears as STATUS_STACK_BUFFER_OVERRUN (0xC0000409) because:
  bad_alloc -> terminate() -> abort() -> /GS security check

--- Process Memory ---
Working Set:        512 MB
Peak Working Set:   1024 MB
Private Bytes:      480 MB

--- System Memory ---
Total Physical:     32768 MB
Available Physical: 8192 MB
Memory Load:        75%

========== STACK TRACE ==========
[ 0] 0x00007ff716653901 printStackTrace (crash_handlers.h:142)
[ 1] 0x00007ff716653d62 verboseTerminateHandler (crash_handlers.h:245)
[ 2] 0x00007ff7179bfd57 terminate
[ 3] 0x00007ff7179aef66 __scrt_unhandled_exception_filter
...
========== END STACK TRACE (12 frames) ==========

--- Loaded Modules (45 total, showing first 10) ---
  rippled.exe                    @ 0x7ff716650000 (45678 KB)
  ntdll.dll                      @ 0x7fff12340000 (1234 KB)
  ...

################################################################################
###                         END CRASH REPORT                                 ###
################################################################################
```

### JSON format (for machine parsing)

```json
{"ts":0.123,"level":"ENTER","tid":12345,"cid":1,"file":"Application.cpp","line":156,"msg":"section_start:rpc_startup"}
{"ts":0.124,"level":"DEBUG","tid":12345,"cid":1,"file":"Application.cpp","line":160,"msg":"Creating RPC context"}
{"ts":0.130,"level":"EXIT","tid":12345,"cid":1,"file":"Application.cpp","line":0,"msg":"section_end:rpc_startup,elapsed_ms:7.234"}
```

Enable JSON format with:
```cpp
DEBUG_FORMAT_JSON();
```

## Usage

### Basic Logging

```cpp
#include "rippled_debug.h"

int main() {
    RIPPLED_DEBUG_INIT();

    DEBUG_INFO("Application starting...");
    DEBUG_WARN("Configuration file not found, using defaults");
    DEBUG_ERROR("Failed to connect to peer");

    {
        DEBUG_SECTION("initialization");
        // ... code here ...
        // Automatically logs timing when scope exits
    }

    return 0;
}
```

### Configuration

```cpp
// Switch to plain text (no colors)
DEBUG_FORMAT_TEXT();

// Switch to JSON output
DEBUG_FORMAT_JSON();

// Back to Rich-style (default)
DEBUG_FORMAT_RICH();

// Disable colors (but keep box drawing)
DEBUG_COLORS(false);

// Disable all logging
DEBUG_ENABLED(false);
```

## Building rippled with Debug Toolkit

### Prerequisites

- Visual Studio 2022 Build Tools (or full VS2022)
- Conan 2.x
- CMake 3.25+
- Ninja

### Build Steps

```batch
REM Set up VS2022 environment
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

REM Install dependencies
conan install . --output-folder=build --build=missing

REM Configure with debug info in release
cmake -G Ninja -B build ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake ^
    -Dxrpld=ON

REM Build
cmake --build build
```

### Generating PDB files for Release builds

For symbol resolution in release builds, add to CMakeLists.txt:

```cmake
if(MSVC)
    # Generate PDB for release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()
```

## Demo

Run the demo to see Rich-style logging in action:

```batch
cd examples

REM Basic build
cl /EHsc /Zi /utf-8 test_crash.cpp /link dbghelp.lib shell32.lib

REM Or with git info captured at build time
for /f "tokens=*" %%i in ('..\scripts\get_git_info.bat') do set GIT_FLAGS=%%i
cl %GIT_FLAGS% /EHsc /Zi /utf-8 test_crash.cpp /link dbghelp.lib shell32.lib

REM Run demo
test_crash.exe 6    REM Rich-style logging demo
test_crash.exe 7    REM Show build & system info only
test_crash.exe 1    REM Trigger bad_alloc crash with full report
```

**Note:** Use Windows Terminal or a terminal with VT/ANSI support for full color output.

## Common Windows Issues

### 1. `std::bad_alloc` appearing as `STATUS_STACK_BUFFER_OVERRUN`

**Cause**: Unhandled exception → terminate → abort → /GS check

**Solution**: Use this toolkit to see the real exception

### 2. Missing symbols in stack traces

**Cause**: No PDB files for release builds

**Solution**: Build with `/Zi` and `/DEBUG` linker flag

### 3. Crash during RPC handler construction

**Cause**: Memory allocation failure in JsonContext

**Solution**: Check system memory, investigate allocator behavior

## Integration with CI/CD

```yaml
# GitHub Actions example
- name: Build with debug toolkit
  run: |
    # Apply crash handler patch
    # Build with RelWithDebInfo
    # Run tests
    # Upload crash dumps as artifacts if any
```

## Files

```
rippled-windows-debug/
├── src/
│   ├── build_info.h        # Build & system info capture
│   ├── crash_handlers.h    # Verbose crash diagnostics
│   ├── debug_log.h         # Rich-style debug logging
│   ├── minidump.h          # Minidump generation
│   └── rippled_debug.h     # Single-include header
├── cmake/
│   └── GitInfo.cmake       # CMake helper for git info
├── scripts/
│   └── get_git_info.bat    # Batch script for git info
├── patches/
│   └── rippled_main.patch  # Patch for Main.cpp
├── examples/
│   └── test_crash.cpp      # Example usage + demo
├── docs/
│   └── WINDOWS_DEBUGGING.md
└── README.md
```

## Related Tools

- **[FlexiFlow](https://github.com/mcp-tool-shop-org/flexiflow)** - Python async engine with structured logging (inspired debug_log.h patterns)
- **[build-governor](https://github.com/mcp-tool-shop-org/build-governor)** - Memory-aware build orchestrator for parallel compilation (prevents OOM during rippled builds)

## Contributing

This toolkit was developed while debugging issue [XRPLF/rippled#6293](https://github.com/XRPLF/rippled/issues/6293).

Contributions welcome!

## License

MIT License - Same as rippled

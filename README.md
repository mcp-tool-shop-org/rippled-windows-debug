# rippled-windows-debug

**Complete Windows debugging toolkit for rippled (XRPL validator node)**

This toolkit provides **automatic build protection** and **verbose crash diagnostics** for rippled on Windows - preventing and debugging the memory issues that plague parallel C++ builds.

![Rich-style logging demo](docs/rich-demo.png)

## Quick Start

```powershell
# Clone the toolkit
git clone https://github.com/mcp-tool-shop-org/rippled-windows-debug.git
cd rippled-windows-debug

# Set up automatic build protection (one-time, no admin required)
.\scripts\setup-governor.ps1

# Restart terminal, then build rippled safely
cmake --build build --parallel 16  # Governor prevents OOM automatically
```

## The Problem

Parallel C++ builds on Windows frequently fail due to memory exhaustion:

1. **Build failures**: Each `cl.exe` can use 1-4 GB RAM. High `-j` values exhaust memory.
2. **Misleading errors**: `STATUS_STACK_BUFFER_OVERRUN (0xC0000409)` is often actually `std::bad_alloc`
3. **No diagnostics**: Silent `cl.exe` exits with code 1, no explanation
4. **System freezes**: When commit charge hits 100%, Windows becomes unresponsive

**Root cause**: A `std::bad_alloc` appears as `STATUS_STACK_BUFFER_OVERRUN` because:
1. Exception not caught → `std::terminate()` called
2. `terminate()` calls `abort()`
3. MSVC's `/GS` security checks interpret this as buffer overrun

## What This Toolkit Provides

### 1. Build Governor (Automatic OOM Protection)

**Prevents crashes before they happen.** Located in `tools/build-governor/`:

- **Zero-config protection**: Wrappers auto-start governor on first build
- **Adaptive throttling**: Monitors commit charge, slows builds when memory pressure rises
- **Actionable diagnostics**: "Memory pressure detected, recommend -j4"
- **Auto-shutdown**: Governor exits after 30 min idle

```powershell
# One-time setup
.\scripts\setup-governor.ps1

# All builds are now protected automatically
cmake --build . --parallel 16
msbuild /m:16
ninja -j 8
```

### 2. Verbose Crash Handlers (`crash_handlers.h`)

**Diagnoses crashes that do happen.** Single-header crash diagnostics that capture:
- Actual exception type and message (reveals `std::bad_alloc` hidden as `STATUS_STACK_BUFFER_OVERRUN`)
- Full stack trace with symbol resolution
- Signal information (SIGABRT, SIGSEGV, etc.)
- **Complete build info** (toolkit version, git commit, compiler)
- **System info** (Windows version, CPU, memory, computer name)

### 3. Rich-style Debug Logging (`debug_log.h`)

Beautiful terminal logging inspired by Python's [Rich](https://github.com/Textualize/rich) library:
- **Colored log levels** - INFO (cyan), WARN (yellow), ERROR (red)
- **Box-drawing characters** - Visual section boundaries with Unicode
- **Automatic timing** - Sections show elapsed time on completion
- **Correlation IDs** - Track related log entries across threads
- **Multiple formats** - Rich (colored), Text (plain), JSON (machine-parseable)

### 4. Minidump Generation (`minidump.h`)

Automatic crash dump capture:
- Full memory dumps for debugging
- Configurable dump location
- Automatic cleanup of old dumps

### 5. Build Information (`build_info.h`)

Comprehensive build and system info:
- Toolkit version
- Git commit hash, branch, dirty status
- Compiler name and version
- Build date/time and architecture
- Windows version and build number
- CPU model and core count
- System memory

## How the Governor Works

```
  cmake --build . --parallel 16
        │
        ▼
    ┌───────────┐
    │  cl.exe   │ ← Actually the wrapper (in PATH)
    │  wrapper  │
    └─────┬─────┘
          │
          ▼
  ┌───────────────────┐
  │ Governor running? │
  └─────────┬─────────┘
       No   │   Yes
            │
     ┌──────┴──────┐
     ▼             ▼
  Auto-start    Connect
  Governor      directly
     │             │
     └──────┬──────┘
            ▼
    Request tokens (based on commit charge)
            │
            ▼
    Run real cl.exe
            │
            ▼
    Release tokens
```

The governor monitors **commit charge** (not free RAM) because:
- Commit charge = promised memory (even if not yet paged in)
- When commit limit is reached, allocations fail immediately
- Free RAM can be misleading (file cache, standby pages)

## Patching rippled for Crash Diagnostics

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

## Example Crash Output

When a crash occurs, you'll see a comprehensive report instead of cryptic error codes:

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
CPU:              AMD Ryzen 9 5900X 12-Core Processor

--- Exception Details ---
Type:    std::bad_alloc
Message: bad allocation

--- Diagnostic Hints ---
MEMORY ALLOCATION FAILURE detected.
Common causes:
  1. Requesting impossibly large allocation
  2. System out of memory (check Available Physical above)
  3. Memory fragmentation

This often appears as STATUS_STACK_BUFFER_OVERRUN (0xC0000409) because:
  bad_alloc -> terminate() -> abort() -> /GS security check

--- System Memory ---
Total Physical:     32768 MB
Available Physical: 8192 MB
Memory Load:        75%

========== STACK TRACE ==========
[ 0] 0x00007ff716653901 printStackTrace (crash_handlers.h:142)
[ 1] 0x00007ff716653d62 verboseTerminateHandler (crash_handlers.h:245)
...
========== END STACK TRACE (12 frames) ==========

################################################################################
###                         END CRASH REPORT                                 ###
################################################################################
```

## Rich-style Logging Example

```
┌────────────────────────────────────────────────────────────────────┐
│                    rippled-windows-debug                           │
│               Rich-style Terminal Logging Demo                     │
└────────────────────────────────────────────────────────────────────┘

[14:32:15] INFO     Starting demonstration of Rich-style logging...   demo.cpp:42
[14:32:15] DEBUG    This is a DEBUG level message                     demo.cpp:45
[14:32:15] WARN     This is a WARNING level message                   demo.cpp:47
[14:32:15] ERROR    This is an ERROR level message                    demo.cpp:48

┌── ▶ database_init ──────────────────────────────────────────────────┐
[14:32:15] INFO     Connecting to database...                         db.cpp:12
[14:32:15] INFO     Connection established                            db.cpp:18
└── ✔ database_init (156.2ms) ────────────────────────────────────────┘
```

## Building rippled with Debug Toolkit

### Prerequisites

- Visual Studio 2022 Build Tools (or full VS2022)
- .NET 9.0 SDK (for Build Governor)
- Python 3.x with Conan 2.x (`pip install conan`)
- CMake 3.25+ (comes with Conan or install separately)
- Ninja (comes with Conan or install separately)

### Option 1: One-Command Build (Recommended)

The toolkit includes a PowerShell script that handles everything:

```powershell
# In your rippled directory
cd F:\rippled

# Copy the build script from the toolkit
copy F:\AI\rippled-windows-debug\scripts\build-rippled.ps1 .

# Run the full build with governor protection
powershell -ExecutionPolicy Bypass -File build-rippled.ps1 -Parallel 8
```

This script automatically:
- Sets up VS2022 environment
- Adds Python Scripts to PATH (for Conan)
- Configures Build Governor wrappers
- Runs Conan install
- Configures CMake with Ninja
- Builds with governor protection

### Option 2: Manual Build Steps

```batch
REM 1. Set up automatic build protection first!
powershell -ExecutionPolicy Bypass -File scripts\setup-governor.ps1

REM 2. Set up VS2022 environment
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

REM 3. Install dependencies
conan install . --output-folder=build --build=missing

REM 4. Configure with debug info in release
cmake -G Ninja -B build ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake ^
    -Dxrpld=ON

REM 5. Build (governor automatically protects this!)
cmake --build build --parallel 16
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

REM Run demo
test_crash.exe 6    REM Rich-style logging demo
test_crash.exe 7    REM Show build & system info only
test_crash.exe 1    REM Trigger bad_alloc crash with full report
```

**Note:** Use Windows Terminal or a terminal with VT/ANSI support for full color output.

## Files

```
rippled-windows-debug/
├── src/
│   ├── build_info.h        # Build & system info capture
│   ├── crash_handlers.h    # Verbose crash diagnostics
│   ├── debug_log.h         # Rich-style debug logging
│   ├── minidump.h          # Minidump generation
│   └── rippled_debug.h     # Single-include header
├── tools/
│   └── build-governor/     # Automatic OOM protection
│       ├── src/            # Governor source code
│       ├── scripts/        # Setup scripts
│       └── README.md       # Governor documentation
├── scripts/
│   ├── setup-governor.ps1  # One-command governor setup
│   └── get_git_info.bat    # Batch script for git info
├── cmake/
│   └── GitInfo.cmake       # CMake helper for git info
├── patches/
│   └── rippled_main.patch  # Patch for Main.cpp
├── examples/
│   └── test_crash.cpp      # Example usage + demo
├── docs/
│   └── WINDOWS_DEBUGGING.md
└── README.md
```

## Common Windows Issues

### 1. `std::bad_alloc` appearing as `STATUS_STACK_BUFFER_OVERRUN`

**Cause**: Unhandled exception → terminate → abort → /GS check

**Solution**:
1. **Prevent it**: Use Build Governor (`.\scripts\setup-governor.ps1`)
2. **Diagnose it**: Use crash handlers to see the real exception

### 2. Missing symbols in stack traces

**Cause**: No PDB files for release builds

**Solution**: Build with `/Zi` and `/DEBUG` linker flag

### 3. Build hangs or system freezes

**Cause**: Too many parallel compilations exhausting commit charge

**Solution**: Use Build Governor - it automatically throttles based on memory pressure

## Related Tools

- **[FlexiFlow](https://github.com/mcp-tool-shop-org/flexiflow)** - Python async engine with structured logging (inspired debug_log.h patterns)

## Contributing

This toolkit was developed while debugging issue [XRPLF/rippled#6293](https://github.com/XRPLF/rippled/issues/6293).

Contributions welcome!

## License

MIT License - Same as rippled

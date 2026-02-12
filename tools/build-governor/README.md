# Build Reliability Governor

**Automatic protection** against C++ build memory exhaustion. No manual steps required.

## The Problem

Parallel C++ builds (`cmake --parallel`, `msbuild /m`, `ninja -j`) can easily exhaust system memory:

- Each `cl.exe` instance can use 1-4 GB RAM (templates, LTCG, heavy headers)
- Build systems launch N parallel jobs and hope for the best
- When RAM exhausts: system freeze, or `CL.exe exited with code 1` (no diagnostic)
- The killer metric is **Commit Charge**, not "free RAM"

## The Solution

A lightweight governor that **automatically** sits between your build system and the compiler:

1. **Zero-config protection**: Wrappers auto-start governor on first build
2. **Adaptive concurrency** based on commit charge, not job count
3. **Silent failure → actionable diagnosis**: "Memory pressure detected, recommend -j4"
4. **Auto-throttling**: builds slow down instead of crashing
5. **Fail-safe**: if governor is down, tools run ungoverned

## Quick Start (Automatic Protection)

```powershell
# One-time setup (no admin required)
cd build-governor
.\scripts\enable-autostart.ps1

# That's it! All builds are now protected
cmake --build . --parallel 16
msbuild /m:16
ninja -j 8
```

The wrappers automatically:
- Start the governor if it's not running
- Monitor memory and throttle when needed
- Shut down after 30 min of inactivity

## Alternative: Windows Service (Enterprise)

For always-on protection across all users:

```powershell
# Requires Administrator
.\scripts\install-service.ps1
```

## Manual Mode

If you prefer explicit control:

```powershell
# 1. Build
dotnet build -c Release
dotnet publish src/Gov.Wrapper.CL -c Release -o bin/wrappers
dotnet publish src/Gov.Wrapper.Link -c Release -o bin/wrappers
dotnet publish src/Gov.Cli -c Release -o bin/cli

# 2. Start governor (in one terminal)
dotnet run --project src/Gov.Service -c Release

# 3. Run your build (in another terminal)
bin/cli/gov.exe run -- cmake --build . --parallel 16
```

## NuGet Packages

| Package | Description |
|---------|-------------|
| `Gov.Protocol` | Shared message DTOs for client-service communication over named pipes. |
| `Gov.Common` | Windows memory metrics, OOM classification, auto-start client. |

## How It Works

### Automatic Protection Flow

```
  cmake --build .
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
    Request tokens
            │
            ▼
    Run real cl.exe
            │
            ▼
    Release tokens
```

### Architecture

```
                    ┌─────────────────┐
                    │  Gov.Service    │
                    │  (Token Pool)   │
                    │  - Monitor RAM  │
                    │  - Grant tokens │
                    │  - Classify OOM │
                    └────────┬────────┘
                             │ Named Pipe
         ┌───────────────────┼───────────────────┐
         │                   │                   │
    ┌────┴────┐        ┌────┴────┐        ┌────┴────┐
    │ cl.exe  │        │ cl.exe  │        │link.exe │
    │ wrapper │        │ wrapper │        │ wrapper │
    └────┬────┘        └────┬────┘        └────┬────┘
         │                   │                   │
    ┌────┴────┐        ┌────┴────┐        ┌────┴────┐
    │ real    │        │ real    │        │ real    │
    │ cl.exe  │        │ cl.exe  │        │ link.exe│
    └─────────┘        └─────────┘        └─────────┘
```

## Token Cost Model

| Action | Tokens | Notes |
|--------|--------|-------|
| Normal compile | 1 | Baseline |
| Heavy compile (Boost/gRPC) | 2-4 | Template-heavy |
| Compile with /GL | +3 | LTCG codegen |
| Link | 4 | Base link cost |
| Link with /LTCG | 8-12 | Full LTCG |

## Throttle Levels

| Commit Ratio | Level | Behavior |
|--------------|-------|----------|
| < 80% | Normal | Grant tokens immediately |
| 80-88% | Caution | Slower grants, delay 200ms |
| 88-92% | SoftStop | Significant delays, 500ms |
| > 92% | HardStop | Refuse new tokens |

## Failure Classification

When a build tool exits with an error, the governor classifies it:

- **LikelyOOM**: High commit ratio + process peaked high + no compiler diagnostics
- **LikelyPagingDeath**: Moderate pressure signals
- **NormalCompileError**: Compiler diagnostics present in stderr
- **Unknown**: Can't determine

On OOM, you see:
```
╔══════════════════════════════════════════════════════════════════╗
║  BUILD FAILED: Memory Pressure Detected                          ║
╠══════════════════════════════════════════════════════════════════╣
║  Exit code: 1                                                    ║
║  System commit: 94% (45.2 GB / 48.0 GB)                          ║
║  Process peak:  3.1 GB                                           ║
╠══════════════════════════════════════════════════════════════════╣
║  Recommendation: Reduce parallelism                              ║
║    CMAKE_BUILD_PARALLEL_LEVEL=4                                  ║
║    MSBuild: /m:4                                                 ║
║    Ninja: -j4                                                    ║
╚══════════════════════════════════════════════════════════════════╝
```

## Safety Features

- **Fail-safe**: If governor unavailable, wrappers run tools ungoverned
- **Lease TTL**: If wrapper crashes, tokens auto-reclaim after 30 min
- **No deadlock**: Timeouts on all pipe operations
- **Tool auto-detection**: Uses vswhere to find real cl.exe/link.exe

## CLI Commands

```powershell
# Run a governed build
gov run -- cmake --build . --parallel 16

# Check governor status
gov status

# Run without auto-starting governor
gov run --no-start -- ninja -j 8
```

## Environment Variables

| Variable | Description |
|----------|-------------|
| `GOV_REAL_CL` | Path to real cl.exe (auto-detected via vswhere) |
| `GOV_REAL_LINK` | Path to real link.exe (auto-detected) |
| `GOV_ENABLED` | Set by `gov run` to indicate governed mode |
| `GOV_SERVICE_PATH` | Path to Gov.Service.exe for auto-start |
| `GOV_DEBUG` | Set to "1" for verbose auto-start logging |

## Project Structure

```
build-governor/
├── src/
│   ├── Gov.Protocol/    # Shared DTOs
│   ├── Gov.Common/      # Windows metrics, classifier, auto-start
│   ├── Gov.Service/     # Background governor (supports --background)
│   ├── Gov.Wrapper.CL/  # cl.exe shim (auto-starts governor)
│   ├── Gov.Wrapper.Link/# link.exe shim
│   └── Gov.Cli/         # `gov` command
├── scripts/
│   ├── enable-autostart.ps1  # User setup (no admin)
│   ├── install-service.ps1   # Windows Service (admin)
│   └── uninstall-service.ps1 # Remove service
├── bin/
│   ├── wrappers/        # Published shims
│   ├── service/         # Published service
│   └── cli/             # Published CLI
└── gov-env.cmd          # Manual PATH setup
```

## Auto-Start Behavior

The wrappers use a global mutex to ensure only one governor instance runs.
When multiple compilers start simultaneously:

1. First wrapper acquires mutex, checks if governor running
2. If not, starts `Gov.Service.exe --background`
3. Other wrappers wait on mutex, then connect to now-running governor
4. Background mode: governor shuts down after 30 min idle

## License

MIT

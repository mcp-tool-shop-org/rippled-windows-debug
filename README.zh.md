<p align="center">
  <a href="README.ja.md">日本語</a> | <a href="README.md">English</a> | <a href="README.es.md">Español</a> | <a href="README.fr.md">Français</a> | <a href="README.hi.md">हिन्दी</a> | <a href="README.it.md">Italiano</a> | <a href="README.pt-BR.md">Português (BR)</a>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/mcp-tool-shop-org/brand/main/logos/rippled-windows-debug/readme.png" width="400" alt="rippled-windows-debug">
</p>

<p align="center">
  <a href="https://github.com/mcp-tool-shop-org/rippled-windows-debug/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT License"></a>
  <a href="https://mcp-tool-shop-org.github.io/rippled-windows-debug/"><img src="https://img.shields.io/badge/Landing_Page-live-blue" alt="Landing Page"></a>
</p>

Windows 调试工具包，用于 rippled (XRPL 验证节点)。自动构建保护和详细的崩溃诊断，可防止和调试并行 C++ 构建中常见的内存问题。

## 快速开始

```powershell
# Clone the toolkit
git clone https://github.com/mcp-tool-shop-org/rippled-windows-debug.git
cd rippled-windows-debug

# Set up automatic build protection (one-time, no admin required)
.\scripts\setup-governor.ps1

# Restart terminal, then build rippled safely
cmake --build build --parallel 16  # Governor prevents OOM automatically
```

## 问题

Windows 上的并行 C++ 构建经常因内存耗尽而失败：

1. **构建失败**: 每个 `cl.exe` 可能使用 1-4 GB 的 RAM。过高的 `-j` 值会导致内存耗尽。
2. **误导性错误**: `STATUS_STACK_BUFFER_OVERRUN (0xC0000409)` 实际上通常是 `std::bad_alloc`。
3. **无诊断信息**: `cl.exe` 静默退出，返回代码 1，没有解释。
4. **系统冻结**: 当提交费用达到 100% 时，Windows 变得无响应。

**根本原因**: `std::bad_alloc` 会显示为 `STATUS_STACK_BUFFER_OVERRUN`，因为：
1. 异常未捕获 → 调用 `std::terminate()`
2. `terminate()` 调用 `abort()`
3. MSVC 的 `/GS` 安全检查将其解释为缓冲区溢出。

## 此工具包提供的功能

### 1. 构建管理器（自动 OOM 保护）

**在崩溃发生之前进行预防。** 位于 `tools/build-governor/` 目录中：

- **零配置保护**: 包装器会在首次构建时自动启动管理器。
- **自适应节流**: 监控提交费用，并在内存压力增加时减慢构建速度。
- **可操作的诊断信息**: "检测到内存压力，建议使用 -j4"。
- **自动关闭**: 30 分钟无操作后，管理器自动退出。

```powershell
# One-time setup
.\scripts\setup-governor.ps1

# All builds are now protected automatically
cmake --build . --parallel 16
msbuild /m:16
ninja -j 8
```

### 2. 详细的崩溃处理程序 (`crash_handlers.h`)

**诊断已发生的崩溃。** 单个头文件，提供崩溃诊断信息，可以捕获：
- 实际的异常类型和消息（揭示隐藏在 `STATUS_STACK_BUFFER_OVERRUN` 中的 `std::bad_alloc`）。
- 完整的堆栈跟踪，包含符号解析。
- 信号信息（SIGABRT, SIGSEGV 等）。
- **完整的构建信息**（工具包版本、git 提交、编译器）。
- **系统信息**（Windows 版本、CPU、内存、计算机名称）。

### 3. 丰富的日志记录 (`debug_log.h`)

灵感来自 Python 的 [Rich](https://github.com/Textualize/rich) 库的精美终端日志：
- **彩色日志级别** - 信息 (INFO，青色)、警告 (WARN，黄色)、错误 (ERROR，红色)。
- **边框字符** - 使用 Unicode 字符，在日志中创建视觉区域分隔。
- **自动计时** - 每个区域显示完成后的耗时。
- **关联 ID** - 跟踪跨线程的关联日志条目。
- **多种格式** - Rich (彩色)、Text (纯文本)、JSON (可供机器解析)。

### 4. 崩溃转储生成 (`minidump.h`)

自动崩溃转储捕获：
- 用于调试的完整内存转储。
- 可配置的转储位置。
- 自动清理旧的转储文件。

### 5. 构建信息 (`build_info.h`)

全面的构建和系统信息：
- 工具包版本。
- Git 提交哈希、分支、脏状态。
- 编译器名称和版本。
- 构建日期/时间以及架构。
- Windows 版本和构建号。
- CPU 型号和核心数。
- 系统内存。

## 构建管理器的工作原理

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

构建管理器监控 **提交费用**（而不是空闲 RAM），因为：
- 提交费用 = 承诺的内存（即使尚未分页到内存中）。
- 当达到提交限制时，分配会立即失败。
- 空闲 RAM 可能具有误导性（文件缓存、预留页面）。

## 打补丁 rippled 以进行崩溃诊断

将补丁应用到 `src/xrpld/app/main/Main.cpp`：

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

## 崩溃输出示例

当发生崩溃时，您将看到一份全面的报告，而不是神秘的错误代码：

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

## 丰富的日志记录示例

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

## 使用调试工具包构建 rippled

### 先决条件

- Visual Studio 2022 构建工具（或完整版 VS2022）
- .NET 9.0 SDK（用于构建管理器）
- Python 3.x，以及 Conan 2.x (`pip install conan`)
- CMake 3.25+（包含在 Conan 中，或单独安装）
- Ninja（包含在 Conan 中，或单独安装）

### 选项 1：一键构建（推荐）

该工具包包含一个 PowerShell 脚本，可以处理所有操作：

```powershell
# In your rippled directory
cd F:\rippled

# Copy the build script from the toolkit
copy F:\AI\rippled-windows-debug\scripts\build-rippled.ps1 .

# Run the full build with governor protection
powershell -ExecutionPolicy Bypass -File build-rippled.ps1 -Parallel 8
```

该脚本会自动执行以下操作：
- 设置 VS2022 环境
- 将 Python 脚本添加到 PATH 环境变量（用于 Conan）
- 配置构建管理器包装器
- 运行 Conan 安装
- 使用 Ninja 配置 CMake
- 使用构建管理器进行构建

### 选项 2：手动构建步骤

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

### 为发布版本生成 PDB 文件

为了在发布版本的符号解析中，请在 CMakeLists.txt 文件中添加以下内容：

```cmake
if(MSVC)
    # Generate PDB for release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()
```

## 演示

运行演示以查看 Rich 风格的日志记录：

```batch
cd examples

REM Basic build
cl /EHsc /Zi /utf-8 test_crash.cpp /link dbghelp.lib shell32.lib

REM Run demo
test_crash.exe 6    REM Rich-style logging demo
test_crash.exe 7    REM Show build & system info only
test_crash.exe 1    REM Trigger bad_alloc crash with full report
```

**注意：** 为了获得完整的彩色输出，请使用 Windows Terminal 或支持 VT/ANSI 的终端。

## 文件

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

## Windows 常见问题

### 1. `std::bad_alloc` 错误显示为 `STATUS_STACK_BUFFER_OVERRUN`

**原因：** 未处理的异常 → terminate → abort → /GS 检查

**解决方案：**
1. **预防：** 使用构建管理器 (`.\scripts\setup-governor.ps1`)
2. **诊断：** 使用崩溃处理程序以查看实际的异常

### 2. 堆栈跟踪中缺少符号

**原因：** 发布版本缺少 PDB 文件

**解决方案：** 使用 `/Zi` 和 `/DEBUG` 链接器标志进行构建

### 3. 构建卡住或系统冻结

**原因：** 过多的并行编译导致资源耗尽

**解决方案：** 使用构建管理器 - 它会自动根据内存压力进行限制

## 相关工具

- **[FlexiFlow](https://github.com/mcp-tool-shop-org/flexiflow)** - Python 异步引擎，具有结构化日志记录（灵感来自 debug_log.h 模式）

## 贡献

该工具包是在调试问题 [XRPLF/rippled#6293](https://github.com/XRPLF/rippled/issues/6293) 时开发的。

欢迎贡献！

## 许可证

MIT 许可证 — 与 rippled 相同。

---

构建者：<a href="https://mcp-tool-shop.github.io/">MCP Tool Shop</a>

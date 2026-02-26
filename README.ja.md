<p align="center">
  <a href="README.md">English</a> | <a href="README.zh.md">中文</a> | <a href="README.es.md">Español</a> | <a href="README.fr.md">Français</a> | <a href="README.hi.md">हिन्दी</a> | <a href="README.it.md">Italiano</a> | <a href="README.pt-BR.md">Português (BR)</a>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/mcp-tool-shop-org/brand/main/logos/rippled-windows-debug/readme.png" width="400" alt="rippled-windows-debug">
</p>

<p align="center">
  <a href="https://github.com/mcp-tool-shop-org/rippled-windows-debug/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT License"></a>
  <a href="https://mcp-tool-shop-org.github.io/rippled-windows-debug/"><img src="https://img.shields.io/badge/Landing_Page-live-blue" alt="Landing Page"></a>
</p>

Windows デバッグツールキット for rippled (XRPL 検証ノード)。自動ビルド保護機能と詳細なクラッシュ診断機能により、並列 C++ ビルドで発生するメモリ問題を未然に防ぎ、デバッグを支援します。

## クイックスタート

```powershell
# Clone the toolkit
git clone https://github.com/mcp-tool-shop-org/rippled-windows-debug.git
cd rippled-windows-debug

# Set up automatic build protection (one-time, no admin required)
.\scripts\setup-governor.ps1

# Restart terminal, then build rippled safely
cmake --build build --parallel 16  # Governor prevents OOM automatically
```

## 問題点

Windows 上での並列 C++ ビルドは、メモリ不足により頻繁に失敗します。

1. **ビルドの失敗**: `cl.exe` の1つが 1～4GB の RAM を使用する可能性があります。高い `-j` の値はメモリを枯渇させます。
2. **誤解を招くエラー**: `STATUS_STACK_BUFFER_OVERRUN (0xC0000409)` は、実際には `std::bad_alloc` であることが多い。
3. **診断情報の欠如**: `cl.exe` がエラーコード 1 で静かに終了し、説明がない。
4. **システムフリーズ**: コミットチャージが 100% に達すると、Windows が応答しなくなる。

**根本原因**: `std::bad_alloc` が `STATUS_STACK_BUFFER_OVERRUN` として表示されるのは、以下の理由によります。
1. 例外がキャッチされない → `std::terminate()` が呼び出される
2. `terminate()` が `abort()` を呼び出す
3. MSVC の `/GS` セキュリティチェックが、これをバッファオーバーフローと解釈する

## このツールキットで提供するもの

### 1. ビルドガバナー (自動 OOM 保護)

**クラッシュが発生する前に、それを防ぎます。** `tools/build-governor/` にあります。

- **設定不要の保護**: ラッパーが、最初のビルド時にガバナーを自動的に起動します。
- **適応的なスロットリング**: コミットチャージを監視し、メモリ使用率が上昇するとビルドを遅くします。
- **実行可能な診断**: 「メモリ不足が検出されました。-j4 を推奨します」
- **自動シャットダウン**: ガバナーは、30 分間アイドル状態になると終了します。

```powershell
# One-time setup
.\scripts\setup-governor.ps1

# All builds are now protected automatically
cmake --build . --parallel 16
msbuild /m:16
ninja -j 8
```

### 2. 詳細なクラッシュハンドラ (`crash_handlers.h`)

**発生したクラッシュを診断します。** 以下の情報を収集する、単一ヘッダーのクラッシュ診断機能です。
- 実際の例外タイプとメッセージ ( `STATUS_STACK_BUFFER_OVERRUN` として隠されている `std::bad_alloc` を明らかにします)
- シンボル解決を含む完全なスタックトレース
- シグナル情報 (SIGABRT, SIGSEGV など)
- **完全なビルド情報** (ツールキットのバージョン、Git コミット、コンパイラ)
- **システム情報** (Windows のバージョン、CPU、メモリ、コンピュータ名)

### 3. リッチ形式のデバッグログ (`debug_log.h`)

Python の [Rich](https://github.com/Textualize/rich) ライブラリに触発された、美しいターミナルロギング機能です。
- **色付きのログレベル**: INFO (シアン)、WARN (黄色)、ERROR (赤)
- **ボックス描画文字**: Unicode を使用したセクションの視覚的な区切り
- **自動タイミング**: セクションの完了時に経過時間を表示
- **相関 ID**: スレッド間で関連するログエントリを追跡
- **複数の形式**: Rich (色付き)、Text (プレーン)、JSON (機械可読)

### 4. ミニダンプ生成 (`minidump.h`)

自動クラッシュダンプのキャプチャ:
- デバッグ用の完全なメモリダンプ
- 設定可能なダンプの保存場所
- 古いダンプの自動削除

### 5. ビルド情報 (`build_info.h`)

包括的なビルドおよびシステム情報:
- ツールキットのバージョン
- Git コミットハッシュ、ブランチ、ダーティ状態
- コンパイラの名前とバージョン
- ビルドの日付/時刻とアーキテクチャ
- Windows のバージョンとビルド番号
- CPU モデルとコア数
- システムメモリ

## ガバナーの動作原理

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

ガバナーは、**コミットチャージ** (空き RAM ではなく) を監視します。なぜなら:
- コミットチャージ = 割り当てられたメモリ (まだページングされていない場合でも)
- コミット制限に達すると、割り当てはすぐに失敗する
- 空き RAM は誤解を招く可能性がある (ファイルキャッシュ、スタンバイページ)

## rippled のクラッシュ診断のためのパッチ適用

`src/xrpld/app/main/Main.cpp` にパッチを適用します。

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

## クラッシュ時の出力例

クラッシュが発生すると、不明瞭なエラーコードではなく、包括的なレポートが表示されます。

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

## リッチ形式のロギングの例

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

## デバッグツールキットを使用した rippled のビルド

### 前提条件

- Visual Studio 2022 ビルド ツール (またはフル版の VS2022)
- .NET 9.0 SDK (ビルド ガバナ用)
- Python 3.x と Conan 2.x (`pip install conan`)
- CMake 3.25 以降 (Conan に付属するか、別途インストール)
- Ninja (Conan に付属するか、別途インストール)

### オプション 1: ワンコマンドビルド (推奨)

このツールキットには、すべてを処理する PowerShell スクリプトが含まれています。

```powershell
# In your rippled directory
cd F:\rippled

# Copy the build script from the toolkit
copy F:\AI\rippled-windows-debug\scripts\build-rippled.ps1 .

# Run the full build with governor protection
powershell -ExecutionPolicy Bypass -File build-rippled.ps1 -Parallel 8
```

このスクリプトは、自動的に以下の処理を行います。
- VS2022 環境の設定
- Python スクリプトを PATH に追加 (Conan 用)
- ビルド ガバナのラッパーの設定
- Conan のインストール
- Ninja を使用した CMake の設定
- ガバナによる保護下でのビルド

### オプション 2: 手動でのビルド手順

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

### リリースビルド用の PDB ファイルの生成

リリースビルドでのシンボル解決のため、CMakeLists.txt に以下の記述を追加してください。

```cmake
if(MSVC)
    # Generate PDB for release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()
```

## デモ

デモを実行して、Rich スタイルのログ出力を確認してください。

```batch
cd examples

REM Basic build
cl /EHsc /Zi /utf-8 test_crash.cpp /link dbghelp.lib shell32.lib

REM Run demo
test_crash.exe 6    REM Rich-style logging demo
test_crash.exe 7    REM Show build & system info only
test_crash.exe 1    REM Trigger bad_alloc crash with full report
```

**注意:** フルカラーの出力には、Windows Terminal または VT/ANSI をサポートするターミナルを使用してください。

## ファイル

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

## Windows でよく発生する問題

### 1. `std::bad_alloc` が `STATUS_STACK_BUFFER_OVERRUN` として表示される

**原因**: 未処理の例外 → terminate → abort → /GS チェック

**解決策**:
1. **発生を防止**: ビルド ガバナを使用する (`.\scripts\setup-governor.ps1`)
2. **原因を特定**: クラッシュハンドラを使用して、実際の例外を確認する

### 2. スタックトレースにシンボルが欠落している

**原因**: リリースビルド用の PDB ファイルがない

**解決策**: `/Zi` と `/DEBUG` リンカフラグを使用してビルドする

### 3. ビルドが停止したり、システムがフリーズしたりする

**原因**: 並列コンパイルが過剰で、メモリ使用量が上限に達している

**解決策**: ビルド ガバナを使用する。これは、メモリ使用量に応じて自動的に処理速度を調整します。

## 関連ツール

- **[FlexiFlow](https://github.com/mcp-tool-shop-org/flexiflow)** - 構造化されたログ出力機能を持つ Python 非同期エンジン (debug_log.h のパターンに影響を受けています)

## 貢献

このツールキットは、問題 [XRPLF/rippled#6293](https://github.com/XRPLF/rippled/issues/6293) のデバッグ中に開発されました。

貢献を歓迎します！

## ライセンス

MIT ライセンス — rippled と同じです。

---

<a href="https://mcp-tool-shop.github.io/">MCP Tool Shop</a> が作成しました。

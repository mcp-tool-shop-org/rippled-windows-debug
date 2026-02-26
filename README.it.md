<p align="center">
  <a href="README.ja.md">日本語</a> | <a href="README.zh.md">中文</a> | <a href="README.es.md">Español</a> | <a href="README.fr.md">Français</a> | <a href="README.hi.md">हिन्दी</a> | <a href="README.md">English</a> | <a href="README.pt-BR.md">Português (BR)</a>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/mcp-tool-shop-org/brand/main/logos/rippled-windows-debug/readme.png" width="400" alt="rippled-windows-debug">
</p>

<p align="center">
  <a href="https://github.com/mcp-tool-shop-org/rippled-windows-debug/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT License"></a>
  <a href="https://mcp-tool-shop-org.github.io/rippled-windows-debug/"><img src="https://img.shields.io/badge/Landing_Page-live-blue" alt="Landing Page"></a>
</p>

Kit di debug per Windows per rippled (nodo di convalida XRPL). Protezione automatica della compilazione e diagnostica dettagliata degli arresti anomali, per prevenire e risolvere i problemi di memoria che affliggono le compilazioni parallele in C++.

## Guida Rapida

```powershell
# Clone the toolkit
git clone https://github.com/mcp-tool-shop-org/rippled-windows-debug.git
cd rippled-windows-debug

# Set up automatic build protection (one-time, no admin required)
.\scripts\setup-governor.ps1

# Restart terminal, then build rippled safely
cmake --build build --parallel 16  # Governor prevents OOM automatically
```

## Il Problema

Le compilazioni parallele in C++ su Windows spesso falliscono a causa dell'esaurimento della memoria:

1. **Errori di compilazione**: Ogni istanza di `cl.exe` può utilizzare da 1 a 4 GB di RAM. Valori elevati di `-j` esauriscono la memoria.
2. **Errori fuorvianti**: `STATUS_STACK_BUFFER_OVERRUN (0xC0000409)` è spesso in realtà `std::bad_alloc`.
3. **Nessuna diagnostica**: `cl.exe` si interrompe silenziosamente con codice 1, senza alcuna spiegazione.
4. **Blocchi del sistema**: Quando la "commit charge" raggiunge il 100%, Windows diventa non reattivo.

**Causa principale**: Un errore `std::bad_alloc` viene visualizzato come `STATUS_STACK_BUFFER_OVERRUN` perché:
1. L'eccezione non viene catturata → viene chiamata `std::terminate()`.
2. `terminate()` chiama `abort()`.
3. I controlli di sicurezza `/GS` di MSVC interpretano questo come un overflow del buffer.

## Cosa Fornisce Questo Kit

### 1. Build Governor (Protezione Automatica da Esaurimento Memoria)

**Previene gli arresti anomali prima che si verifichino.** Si trova in `tools/build-governor/`:

- **Protezione senza configurazione**: I wrapper avviano automaticamente il governor alla prima compilazione.
- **Regolazione adattiva**: Monitora la "commit charge" e rallenta le compilazioni quando la pressione sulla memoria aumenta.
- **Diagnostica utile**: "Pressione sulla memoria rilevata, si consiglia -j4".
- **Arresto automatico**: Il governor si interrompe dopo 30 minuti di inattività.

```powershell
# One-time setup
.\scripts\setup-governor.ps1

# All builds are now protected automatically
cmake --build . --parallel 16
msbuild /m:16
ninja -j 8
```

### 2. Gestori di Arresti Anomali Dettagliati (`crash_handlers.h`)

**Diagnostica gli arresti anomali che si verificano.** Diagnostica degli arresti anomali in un unico file di intestazione che cattura:
- Il tipo e il messaggio effettivo dell'eccezione (rivela `std::bad_alloc` nascosto come `STATUS_STACK_BUFFER_OVERRUN`).
- La traccia dello stack completa con la risoluzione dei simboli.
- Informazioni sui segnali (SIGABRT, SIGSEGV, ecc.).
- **Informazioni complete sulla compilazione** (versione del kit, commit di Git, compilatore).
- **Informazioni sul sistema** (versione di Windows, CPU, memoria, nome del computer).

### 3. Logging Dettagliato in Stile "Rich" (`debug_log.h`)

Logging dettagliato ispirato alla libreria [Rich](https://github.com/Textualize/rich) di Python:
- **Livelli di log colorati** - INFO (ciano), AVVISO (giallo), ERRORE (rosso).
- **Caratteri di disegno di riquadri** - Confini visivi delle sezioni con caratteri Unicode.
- **Temporizzazione automatica** - Le sezioni mostrano il tempo trascorso al termine.
- **ID di correlazione** - Traccia le voci di log correlate tra i thread.
- **Formati multipli** - Rich (colorato), Testo (semplice), JSON (analizzabile dalle macchine).

### 4. Generazione di Minidump (`minidump.h`)

Cattura automatica degli arresti anomali:
- Dump completi della memoria per il debug.
- Posizione del dump configurabile.
- Pulizia automatica dei dump obsoleti.

### 5. Informazioni sulla Compilazione (`build_info.h`)

Informazioni complete sulla compilazione e sul sistema:
- Versione del kit.
- Hash del commit di Git, ramo, stato "dirty".
- Nome e versione del compilatore.
- Data/ora di compilazione e architettura.
- Versione e numero di build di Windows.
- Modello e numero di core della CPU.
- Memoria del sistema.

## Come Funziona il Governor

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

Il governor monitora la **"commit charge"** (non la RAM libera) perché:
- La "commit charge" rappresenta la memoria promessa (anche se non ancora caricata).
- Quando si raggiunge il limite di "commit", le allocazioni falliscono immediatamente.
- La RAM libera può essere fuorviante (cache dei file, pagine di standby).

## Applicazione della Patch a rippled per la Diagnostica degli Arresti Anomali

Applica la patch a `src/xrpld/app/main/Main.cpp`:

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

## Esempio di Output in Caso di Arresto Anomalo

Quando si verifica un arresto anomalo, vedrai un rapporto completo invece di codici di errore criptici:

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

## Esempio di Logging in Stile "Rich"

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

## Compilazione di rippled con il Kit di Debug

### Prerequisiti

- Visual Studio 2022 Build Tools (o la versione completa di VS2022)
- .NET 9.0 SDK (per Build Governor)
- Python 3.x con Conan 2.x (`pip install conan`)
- CMake 3.25+ (incluso in Conan o installabile separatamente)
- Ninja (incluso in Conan o installabile separatamente)

### Opzione 1: Build con un solo comando (consigliata)

Il toolkit include uno script PowerShell che gestisce tutto:

```powershell
# In your rippled directory
cd F:\rippled

# Copy the build script from the toolkit
copy F:\AI\rippled-windows-debug\scripts\build-rippled.ps1 .

# Run the full build with governor protection
powershell -ExecutionPolicy Bypass -File build-rippled.ps1 -Parallel 8
```

Questo script esegue automaticamente le seguenti operazioni:
- Configura l'ambiente VS2022
- Aggiunge gli script Python a PATH (per Conan)
- Configura i wrapper di Build Governor
- Esegue l'installazione di Conan
- Configura CMake con Ninja
- Esegue la compilazione con protezione tramite governor

### Opzione 2: Passaggi di compilazione manuali

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

### Generazione dei file PDB per le build di rilascio

Per la risoluzione dei simboli nelle build di rilascio, aggiungere quanto segue a CMakeLists.txt:

```cmake
if(MSVC)
    # Generate PDB for release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()
```

## Demo

Eseguire la demo per vedere il logging in stile Rich in azione:

```batch
cd examples

REM Basic build
cl /EHsc /Zi /utf-8 test_crash.cpp /link dbghelp.lib shell32.lib

REM Run demo
test_crash.exe 6    REM Rich-style logging demo
test_crash.exe 7    REM Show build & system info only
test_crash.exe 1    REM Trigger bad_alloc crash with full report
```

**Nota:** Utilizzare Windows Terminal o un terminale con supporto VT/ANSI per ottenere l'output a colori.

## File

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

## Problemi comuni su Windows

### 1. `std::bad_alloc` che appare come `STATUS_STACK_BUFFER_OVERRUN`

**Causa**: Eccezione non gestita → terminate → abort → controllo /GS

**Soluzione**:
1. **Prevenire il problema**: Utilizzare Build Governor (`.\scripts\setup-governor.ps1`)
2. **Diagnosticare il problema**: Utilizzare i gestori di crash per visualizzare l'eccezione reale

### 2. Simboli mancanti nelle tracce dello stack

**Causa**: Assenza di file PDB per le build di rilascio

**Soluzione**: Compilare con i flag del linker `/Zi` e `/DEBUG`

### 3. La compilazione si blocca o il sistema si blocca

**Causa**: Troppe compilazioni parallele che esauriscono la memoria disponibile

**Soluzione**: Utilizzare Build Governor, che regola automaticamente la velocità in base alla pressione sulla memoria

## Strumenti correlati

- **[FlexiFlow](https://github.com/mcp-tool-shop-org/flexiflow)** - Motore asincrono Python con logging strutturato (ispirato ai pattern di debug_log.h)

## Contributi

Questo toolkit è stato sviluppato durante il debug del problema [XRPLF/rippled#6293](https://github.com/XRPLF/rippled/issues/6293).

Sono benvenuti i contributi!

## Licenza

Licenza MIT — la stessa di rippled.

---

Creato da <a href="https://mcp-tool-shop.github.io/">MCP Tool Shop</a

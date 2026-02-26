<p align="center">
  <a href="README.ja.md">日本語</a> | <a href="README.zh.md">中文</a> | <a href="README.es.md">Español</a> | <a href="README.md">English</a> | <a href="README.hi.md">हिन्दी</a> | <a href="README.it.md">Italiano</a> | <a href="README.pt-BR.md">Português (BR)</a>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/mcp-tool-shop-org/brand/main/logos/rippled-windows-debug/readme.png" width="400" alt="rippled-windows-debug">
</p>

<p align="center">
  <a href="https://github.com/mcp-tool-shop-org/rippled-windows-debug/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT License"></a>
  <a href="https://mcp-tool-shop-org.github.io/rippled-windows-debug/"><img src="https://img.shields.io/badge/Landing_Page-live-blue" alt="Landing Page"></a>
</p>

Kit de débogage Windows pour rippled (nœud de validation XRPL). Protection automatique des builds et diagnostics détaillés des plantages, permettant de prévenir et de déboguer les problèmes de mémoire qui affectent les builds C++ parallèles.

## Démarrage rapide

```powershell
# Clone the toolkit
git clone https://github.com/mcp-tool-shop-org/rippled-windows-debug.git
cd rippled-windows-debug

# Set up automatic build protection (one-time, no admin required)
.\scripts\setup-governor.ps1

# Restart terminal, then build rippled safely
cmake --build build --parallel 16  # Governor prevents OOM automatically
```

## Le problème

Les builds C++ parallèles sur Windows échouent fréquemment en raison d'une exhaustion de la mémoire :

1. **Échecs de build** : Chaque instance de `cl.exe` peut utiliser de 1 à 4 Go de RAM. Des valeurs élevées de `-j` épuisent la mémoire.
2. **Erreurs trompeuses** : `STATUS_STACK_BUFFER_OVERRUN (0xC0000409)` est souvent en réalité `std::bad_alloc`.
3. **Absence de diagnostics** : `cl.exe` se termine silencieusement avec le code 1, sans explication.
4. **Blocages du système** : Lorsque la charge de validation atteint 100 %, Windows devient inutilisable.

**Cause profonde** : Une erreur `std::bad_alloc` apparaît comme `STATUS_STACK_BUFFER_OVERRUN` parce que :
1. L'exception n'est pas interceptée → `std::terminate()` est appelé.
2. `terminate()` appelle `abort()`.
3. Les vérifications de sécurité `/GS` de MSVC interprètent cela comme un dépassement de tampon.

## Ce que ce kit fournit

### 1. Build Governor (Protection automatique contre les erreurs de mémoire)

**Empêche les plantages avant qu'ils ne se produisent.** Situé dans `tools/build-governor/` :

- **Protection sans configuration** : Les wrappers démarrent automatiquement le gouverneur lors du premier build.
- **Régulation adaptative** : Surveille la charge de validation et ralentit les builds lorsque la pression mémoire augmente.
- **Diagnostics exploitables** : "Pression mémoire détectée, recommandation : -j4".
- **Arrêt automatique** : Le gouverneur se termine après 30 minutes d'inactivité.

```powershell
# One-time setup
.\scripts\setup-governor.ps1

# All builds are now protected automatically
cmake --build . --parallel 16
msbuild /m:16
ninja -j 8
```

### 2. Gestionnaires de plantages détaillés (`crash_handlers.h`)

**Diagnostique les plantages qui se produisent.** Diagnostics de plantages en un seul fichier qui capture :
- Le type et le message réels de l'exception (révèle `std::bad_alloc` masqué par `STATUS_STACK_BUFFER_OVERRUN`).
- La trace de pile complète avec résolution des symboles.
- Les informations sur les signaux (SIGABRT, SIGSEGV, etc.).
- **Informations complètes sur le build** (version du kit, commit Git, compilateur).
- **Informations système** (version de Windows, CPU, mémoire, nom de l'ordinateur).

### 3. Journalisation de style enrichi (`debug_log.h`)

Journalisation élégante pour le terminal inspirée de la bibliothèque [Rich](https://github.com/Textualize/rich) de Python :
- **Niveaux de journalisation colorés** - INFO (cyan), WARN (jaune), ERROR (rouge).
- **Caractères de dessin de boîtes** - Délimitation visuelle des sections avec des caractères Unicode.
- **Minutage automatique** - Les sections affichent le temps écoulé à la fin.
- **Identifiants de corrélation** - Suivi des entrées de journal liées sur différents threads.
- **Formats multiples** - Rich (coloré), Text (texte brut), JSON (lisible par machine).

### 4. Génération de minidumps (`minidump.h`)

Capture automatique des plantages :
- Dumps de mémoire complets pour le débogage.
- Emplacement du dump configurable.
- Nettoyage automatique des dumps anciens.

### 5. Informations de build (`build_info.h`)

Informations complètes sur le build et le système :
- Version du kit.
- Hash du commit Git, branche, statut "dirty".
- Nom et version du compilateur.
- Date et heure du build et architecture.
- Version et numéro de build de Windows.
- Modèle de CPU et nombre de cœurs.
- Mémoire système.

## Fonctionnement du gouverneur

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

Le gouverneur surveille la **charge de validation** (et non la RAM libre) parce que :
- La charge de validation représente la mémoire promise (même si elle n'est pas encore chargée en mémoire).
- Lorsque la limite de charge est atteinte, les allocations échouent immédiatement.
- La RAM libre peut être trompeuse (cache de fichiers, pages de secours).

## Application du correctif à rippled pour les diagnostics de plantages

Appliquez le correctif à `src/xrpld/app/main/Main.cpp` :

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

## Exemple de sortie en cas de plantage

Lorsqu'un plantage se produit, vous verrez un rapport complet au lieu de codes d'erreur obscurs :

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

## Exemple de journalisation de style enrichi

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

## Build de rippled avec le kit de débogage

### Prérequis

- Visual Studio 2022 Build Tools (ou la version complète de VS2022)
- SDK .NET 9.0 (pour Build Governor)
- Python 3.x avec Conan 2.x (`pip install conan`)
- CMake 3.25+ (fourni avec Conan ou à installer séparément)
- Ninja (fourni avec Conan ou à installer séparément)

### Option 1 : Compilation en une seule commande (recommandée)

La boîte à outils comprend un script PowerShell qui gère tout :

```powershell
# In your rippled directory
cd F:\rippled

# Copy the build script from the toolkit
copy F:\AI\rippled-windows-debug\scripts\build-rippled.ps1 .

# Run the full build with governor protection
powershell -ExecutionPolicy Bypass -File build-rippled.ps1 -Parallel 8
```

Ce script effectue automatiquement les actions suivantes :
- Configure l'environnement VS2022
- Ajoute les scripts Python à la variable PATH (pour Conan)
- Configure les wrappers de Build Governor
- Exécute l'installation de Conan
- Configure CMake avec Ninja
- Effectue la compilation avec protection Build Governor

### Option 2 : Étapes de compilation manuelles

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

### Génération des fichiers PDB pour les compilations en mode Release

Pour la résolution des symboles dans les compilations en mode Release, ajoutez ce qui suit à CMakeLists.txt :

```cmake
if(MSVC)
    # Generate PDB for release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()
```

## Démonstration

Exécutez la démonstration pour voir le système de journalisation de type Rich en action :

```batch
cd examples

REM Basic build
cl /EHsc /Zi /utf-8 test_crash.cpp /link dbghelp.lib shell32.lib

REM Run demo
test_crash.exe 6    REM Rich-style logging demo
test_crash.exe 7    REM Show build & system info only
test_crash.exe 1    REM Trigger bad_alloc crash with full report
```

**Remarque :** Utilisez Windows Terminal ou un terminal prenant en charge VT/ANSI pour obtenir une sortie avec des couleurs.

## Fichiers

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

## Problèmes courants sous Windows

### 1. `std::bad_alloc` apparaissant sous la forme `STATUS_STACK_BUFFER_OVERRUN`

**Cause :** Exception non gérée → terminate → abort → vérification /GS

**Solution :**
1. **Prévention :** Utilisez Build Governor (`.\scripts\setup-governor.ps1`)
2. **Diagnostic :** Utilisez des gestionnaires de plantage pour voir la véritable exception

### 2. Symboles manquants dans les traces de pile

**Cause :** Absence de fichiers PDB pour les compilations en mode Release

**Solution :** Compilez avec les options `/Zi` et `/DEBUG` du linker.

### 3. La compilation se bloque ou le système se fige

**Cause :** Trop de compilations parallèles épuisant la mémoire disponible.

**Solution :** Utilisez Build Governor, qui ajuste automatiquement la charge en fonction de la pression mémoire.

## Outils connexes

- **[FlexiFlow](https://github.com/mcp-tool-shop-org/flexiflow)** - Moteur asynchrone Python avec journalisation structurée (inspiré des modèles debug_log.h)

## Contributions

Cette boîte à outils a été développée lors du débogage du problème [XRPLF/rippled#6293](https://github.com/XRPLF/rippled/issues/6293).

Les contributions sont les bienvenues !

## Licence

Licence MIT — identique à celle de rippled.

---

Créé par <a href="https://mcp-tool-shop.github.io/">MCP Tool Shop</a>

<p align="center">
  <a href="README.ja.md">日本語</a> | <a href="README.zh.md">中文</a> | <a href="README.md">English</a> | <a href="README.fr.md">Français</a> | <a href="README.hi.md">हिन्दी</a> | <a href="README.it.md">Italiano</a> | <a href="README.pt-BR.md">Português (BR)</a>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/mcp-tool-shop-org/brand/main/logos/rippled-windows-debug/readme.png" width="400" alt="rippled-windows-debug">
</p>

<p align="center">
  <a href="https://github.com/mcp-tool-shop-org/rippled-windows-debug/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT License"></a>
  <a href="https://mcp-tool-shop-org.github.io/rippled-windows-debug/"><img src="https://img.shields.io/badge/Landing_Page-live-blue" alt="Landing Page"></a>
</p>

Kit de depuración para Windows para rippled (nodo de validación de XRPL). Protección automática de compilaciones y diagnósticos detallados de fallos, lo que permite prevenir y depurar los problemas de memoria que afectan a las compilaciones paralelas de C++.

## Inicio rápido

```powershell
# Clone the toolkit
git clone https://github.com/mcp-tool-shop-org/rippled-windows-debug.git
cd rippled-windows-debug

# Set up automatic build protection (one-time, no admin required)
.\scripts\setup-governor.ps1

# Restart terminal, then build rippled safely
cmake --build build --parallel 16  # Governor prevents OOM automatically
```

## El problema

Las compilaciones paralelas de C++ en Windows a menudo fallan debido al agotamiento de la memoria:

1. **Fallos en la compilación**: Cada instancia de `cl.exe` puede utilizar de 1 a 4 GB de RAM. Los valores altos de `-j` agotan la memoria.
2. **Errores engañosos**: `STATUS_STACK_BUFFER_OVERRUN (0xC0000409)` a menudo es en realidad `std::bad_alloc`.
3. **Sin diagnósticos**: `cl.exe` finaliza silenciosamente con el código 1, sin explicación.
4. **Congelamientos del sistema**: Cuando la carga de confirmación alcanza el 100%, Windows deja de responder.

**Causa raíz**: Un `std::bad_alloc` aparece como `STATUS_STACK_BUFFER_OVERRUN` porque:
1. La excepción no se captura → se llama a `std::terminate()`.
2. `terminate()` llama a `abort()`.
3. Las comprobaciones de seguridad `/GS` de MSVC interpretan esto como un desbordamiento de búfer.

## Qué ofrece este kit de herramientas

### 1. Controlador de compilaciones (protección automática contra errores de memoria)

**Previene los fallos antes de que ocurran.** Se encuentra en `tools/build-governor/`:

- **Protección sin configuración**: Los envoltorios inician automáticamente el controlador en la primera compilación.
- **Limitación adaptativa**: Supervisa la carga de confirmación y reduce la velocidad de las compilaciones cuando la presión de memoria aumenta.
- **Diagnósticos útiles**: "Se ha detectado presión de memoria, se recomienda -j4".
- **Apagado automático**: El controlador se cierra después de 30 minutos de inactividad.

```powershell
# One-time setup
.\scripts\setup-governor.ps1

# All builds are now protected automatically
cmake --build . --parallel 16
msbuild /m:16
ninja -j 8
```

### 2. Controladores de fallos detallados (`crash_handlers.h`)

**Diagnostica los fallos que sí ocurren.** Diagnósticos de fallos en un único archivo de encabezado que capturan:
- El tipo y el mensaje de la excepción real (revela `std::bad_alloc` oculto como `STATUS_STACK_BUFFER_OVERRUN`).
- La pila completa con resolución de símbolos.
- Información de la señal (SIGABRT, SIGSEGV, etc.).
- **Información completa de la compilación** (versión del kit de herramientas, confirmación de Git, compilador).
- **Información del sistema** (versión de Windows, CPU, memoria, nombre del equipo).

### 3. Registro detallado de estilo enriquecido (`debug_log.h`)

Registro en la terminal con un estilo visualmente atractivo inspirado en la biblioteca [Rich](https://github.com/Textualize/rich) de Python:
- **Niveles de registro con colores** - INFO (cian), ADVERTENCIA (amarillo), ERROR (rojo).
- **Caracteres de dibujo de cuadros** - Límites de sección visuales con Unicode.
- **Temporización automática** - Las secciones muestran el tiempo transcurrido al finalizar.
- **ID de correlación** - Realiza un seguimiento de las entradas de registro relacionadas en diferentes hilos.
- **Múltiples formatos** - Rich (con colores), Texto (sin formato), JSON (legible por máquina).

### 4. Generación de minidumps (`minidump.h`)

Captura automática de volcados de fallos:
- Volcados de memoria completos para la depuración.
- Ubicación de volcado configurable.
- Limpieza automática de volcados antiguos.

### 5. Información de compilación (`build_info.h`)

Información completa de la compilación y del sistema:
- Versión del kit de herramientas.
- Hash de la confirmación de Git, rama, estado "dirty".
- Nombre y versión del compilador.
- Fecha y hora de la compilación y arquitectura.
- Versión y número de compilación de Windows.
- Modelo y número de núcleos de la CPU.
- Memoria del sistema.

## Cómo funciona el controlador

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

El controlador supervisa la **carga de confirmación** (no la RAM libre) porque:
- La carga de confirmación es la memoria prometida (incluso si aún no se ha paginado).
- Cuando se alcanza el límite de confirmación, las asignaciones fallan inmediatamente.
- La RAM libre puede ser engañosa (caché de archivos, páginas de espera).

## Aplicación del parche a rippled para el diagnóstico de fallos

Aplique el parche a `src/xrpld/app/main/Main.cpp`:

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

## Ejemplo de salida de un fallo

Cuando se produce un fallo, verá un informe completo en lugar de códigos de error crípticos:

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

## Ejemplo de registro con estilo enriquecido

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

## Compilación de rippled con el kit de herramientas de depuración

### Requisitos previos

- Visual Studio 2022 Build Tools (o la versión completa de VS2022)
- SDK de .NET 9.0 (para Build Governor)
- Python 3.x con Conan 2.x (`pip install conan`)
- CMake 3.25+ (se incluye con Conan o se instala por separado)
- Ninja (se incluye con Conan o se instala por separado)

### Opción 1: Compilación con un solo comando (recomendada)

El conjunto de herramientas incluye un script de PowerShell que gestiona todo:

```powershell
# In your rippled directory
cd F:\rippled

# Copy the build script from the toolkit
copy F:\AI\rippled-windows-debug\scripts\build-rippled.ps1 .

# Run the full build with governor protection
powershell -ExecutionPolicy Bypass -File build-rippled.ps1 -Parallel 8
```

Este script realiza automáticamente las siguientes tareas:
- Configura el entorno de VS2022
- Agrega los scripts de Python a la variable PATH (para Conan)
- Configura los wrappers de Build Governor
- Ejecuta la instalación de Conan
- Configura CMake con Ninja
- Realiza la compilación con protección de governor

### Opción 2: Pasos de compilación manuales

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

### Generación de archivos PDB para compilaciones de Release

Para la resolución de símbolos en las compilaciones de Release, agregue lo siguiente a CMakeLists.txt:

```cmake
if(MSVC)
    # Generate PDB for release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()
```

## Demostración

Ejecute la demostración para ver el registro con estilo Rich en acción:

```batch
cd examples

REM Basic build
cl /EHsc /Zi /utf-8 test_crash.cpp /link dbghelp.lib shell32.lib

REM Run demo
test_crash.exe 6    REM Rich-style logging demo
test_crash.exe 7    REM Show build & system info only
test_crash.exe 1    REM Trigger bad_alloc crash with full report
```

**Nota:** Utilice Windows Terminal o una terminal con soporte VT/ANSI para obtener la salida con colores completos.

## Archivos

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

## Problemas comunes en Windows

### 1. `std::bad_alloc` que aparece como `STATUS_STACK_BUFFER_OVERRUN`

**Causa**: Excepción no controlada → terminate → abort → verificación /GS

**Solución**:
1. **Prevenirlo**: Utilice Build Governor (`.\scripts\setup-governor.ps1`)
2. **Diagnosticarlo**: Utilice manejadores de fallos para ver la excepción real

### 2. Símbolos faltantes en los rastreos de pila

**Causa**: No hay archivos PDB para las compilaciones de Release

**Solución**: Compile con las opciones de enlazador `/Zi` y `/DEBUG`

### 3. La compilación se bloquea o el sistema se congela

**Causa**: Demasiadas compilaciones paralelas que agotan la capacidad de procesamiento

**Solución**: Utilice Build Governor, que ajusta automáticamente la velocidad en función de la presión de memoria

## Herramientas relacionadas

- **[FlexiFlow](https://github.com/mcp-tool-shop-org/flexiflow)**: Motor asíncrono de Python con registro estructurado (inspirado en los patrones de debug_log.h)

## Contribuciones

Este conjunto de herramientas se desarrolló mientras se depuraba el problema [XRPLF/rippled#6293](https://github.com/XRPLF/rippled/issues/6293).

¡Las contribuciones son bienvenidas!

## Licencia

Licencia MIT — igual que rippled.

---

Creado por <a href="https://mcp-tool-shop.github.io/">MCP Tool Shop</a>

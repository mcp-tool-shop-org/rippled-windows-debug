<p align="center">
  <a href="README.ja.md">日本語</a> | <a href="README.zh.md">中文</a> | <a href="README.es.md">Español</a> | <a href="README.fr.md">Français</a> | <a href="README.hi.md">हिन्दी</a> | <a href="README.it.md">Italiano</a> | <a href="README.md">English</a>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/mcp-tool-shop-org/brand/main/logos/rippled-windows-debug/readme.png" width="400" alt="rippled-windows-debug">
</p>

<p align="center">
  <a href="https://github.com/mcp-tool-shop-org/rippled-windows-debug/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT License"></a>
  <a href="https://mcp-tool-shop-org.github.io/rippled-windows-debug/"><img src="https://img.shields.io/badge/Landing_Page-live-blue" alt="Landing Page"></a>
</p>

Kit de depuração para Windows para rippled (nó de validação XRPL). Proteção automática de compilações e diagnósticos detalhados de falhas — prevenindo e depurando problemas de memória que afetam as compilações paralelas em C++.

## Início Rápido

```powershell
# Clone the toolkit
git clone https://github.com/mcp-tool-shop-org/rippled-windows-debug.git
cd rippled-windows-debug

# Set up automatic build protection (one-time, no admin required)
.\scripts\setup-governor.ps1

# Restart terminal, then build rippled safely
cmake --build build --parallel 16  # Governor prevents OOM automatically
```

## O Problema

Compilações paralelas em C++ no Windows frequentemente falham devido ao esgotamento de memória:

1. **Falhas de compilação**: Cada instância de `cl.exe` pode usar de 1 a 4 GB de RAM. Valores altos de `-j` esgotam a memória.
2. **Erros enganosos**: `STATUS_STACK_BUFFER_OVERRUN (0xC0000409)` é frequentemente, na verdade, `std::bad_alloc`.
3. **Sem diagnósticos**: O `cl.exe` termina silenciosamente com o código 1, sem explicação.
4. **Travamentos do sistema**: Quando a carga de compromisso atinge 100%, o Windows fica não responsivo.

**Causa raiz**: Um `std::bad_alloc` aparece como `STATUS_STACK_BUFFER_OVERRUN` porque:
1. A exceção não é capturada → `std::terminate()` é chamado.
2. `terminate()` chama `abort()`.
3. As verificações de segurança `/GS` da MSVC interpretam isso como estouro de buffer.

## O que este Kit de Ferramentas Fornece

### 1. Controlador de Compilação (Proteção Automática contra Esgotamento de Memória)

**Previne falhas antes que elas aconteçam.** Localizado em `tools/build-governor/`:

- **Proteção sem configuração**: Wrappers iniciam automaticamente o controlador na primeira compilação.
- **Controle adaptativo**: Monitora a carga de compromisso, reduz a velocidade das compilações quando a pressão da memória aumenta.
- **Diagnósticos acionáveis**: "Pressão de memória detectada, recomenda-se -j4".
- **Desligamento automático**: O controlador termina após 30 minutos de inatividade.

```powershell
# One-time setup
.\scripts\setup-governor.ps1

# All builds are now protected automatically
cmake --build . --parallel 16
msbuild /m:16
ninja -j 8
```

### 2. Manipuladores de Falhas Detalhados (`crash_handlers.h`)

**Diagnostica falhas que ocorrem.** Diagnósticos de falhas em um único arquivo de cabeçalho que capturam:
- Tipo e mensagem reais da exceção (revela `std::bad_alloc` disfarçado de `STATUS_STACK_BUFFER_OVERRUN`).
- Rastreamento de pilha completo com resolução de símbolos.
- Informações do sinal (SIGABRT, SIGSEGV, etc.).
- **Informações completas da compilação** (versão do kit de ferramentas, commit do Git, compilador).
- **Informações do sistema** (versão do Windows, CPU, memória, nome do computador).

### 3. Registro Detalhado em Estilo Rich (`debug_log.h`)

Registro de terminal elegante inspirado na biblioteca [Rich](https://github.com/Textualize/rich) do Python:
- **Níveis de log coloridos** - INFO (ciano), AVISO (amarelo), ERRO (vermelho).
- **Caracteres de desenho de caixa** - Limites de seção visuais com Unicode.
- **Temporização automática** - As seções mostram o tempo decorrido ao serem concluídas.
- **IDs de correlação** - Rastreia entradas de log relacionadas em threads.
- **Múltiplos formatos** - Rich (colorido), Texto (simples), JSON (interpretável por máquinas).

### 4. Geração de Minidump (`minidump.h`)

Captura automática de despejos de falhas:
- Despejos de memória completos para depuração.
- Localização de despejo configurável.
- Limpeza automática de despejos antigos.

### 5. Informações de Compilação (`build_info.h`)

Informações abrangentes de compilação e sistema:
- Versão do kit de ferramentas.
- Hash do commit do Git, branch, status "dirty".
- Nome e versão do compilador.
- Data/hora da compilação e arquitetura.
- Versão e número de compilação do Windows.
- Modelo e número de núcleos da CPU.
- Memória do sistema.

## Como o Controlador Funciona

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

O controlador monitora a **carga de compromisso** (e não a RAM livre) porque:
- A carga de compromisso é a memória prometida (mesmo que ainda não tenha sido carregada na memória).
- Quando o limite de compromisso é atingido, as alocações falham imediatamente.
- A RAM livre pode ser enganosa (cache de arquivos, páginas de espera).

## Aplicando o Patch para rippled para Diagnósticos de Falhas

Aplique o patch em `src/xrpld/app/main/Main.cpp`:

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

## Exemplo de Saída de Falha

Quando uma falha ocorre, você verá um relatório abrangente em vez de códigos de erro obscuros:

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

## Exemplo de Registro em Estilo Rich

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

## Compilando rippled com o Kit de Ferramentas de Depuração

### Pré-requisitos

- Visual Studio 2022 Build Tools (ou a versão completa do VS2022)
- SDK .NET 9.0 (para o Build Governor)
- Python 3.x com Conan 2.x (`pip install conan`)
- CMake 3.25+ (vem com o Conan ou pode ser instalado separadamente)
- Ninja (vem com o Conan ou pode ser instalado separadamente)

### Opção 1: Compilação com um único comando (recomendado)

O conjunto de ferramentas inclui um script do PowerShell que gerencia tudo:

```powershell
# In your rippled directory
cd F:\rippled

# Copy the build script from the toolkit
copy F:\AI\rippled-windows-debug\scripts\build-rippled.ps1 .

# Run the full build with governor protection
powershell -ExecutionPolicy Bypass -File build-rippled.ps1 -Parallel 8
```

Este script faz automaticamente:
- Configura o ambiente do VS2022
- Adiciona os scripts Python ao PATH (para o Conan)
- Configura os wrappers do Build Governor
- Executa a instalação do Conan
- Configura o CMake com o Ninja
- Compila com proteção do governor

### Opção 2: Passos de compilação manuais

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

### Geração de arquivos PDB para compilações de versão (Release)

Para a resolução de símbolos em compilações de versão, adicione o seguinte ao arquivo CMakeLists.txt:

```cmake
if(MSVC)
    # Generate PDB for release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()
```

## Demonstração

Execute a demonstração para ver o registro no estilo Rich em ação:

```batch
cd examples

REM Basic build
cl /EHsc /Zi /utf-8 test_crash.cpp /link dbghelp.lib shell32.lib

REM Run demo
test_crash.exe 6    REM Rich-style logging demo
test_crash.exe 7    REM Show build & system info only
test_crash.exe 1    REM Trigger bad_alloc crash with full report
```

**Observação:** Use o Windows Terminal ou um terminal com suporte a VT/ANSI para obter a saída com cores completas.

## Arquivos

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

## Problemas comuns no Windows

### 1. `std::bad_alloc` aparecendo como `STATUS_STACK_BUFFER_OVERRUN`

**Causa**: Exceção não tratada → terminate → abort → verificação /GS

**Solução**:
1. **Prevenir**: Use o Build Governor (`.\scripts\setup-governor.ps1`)
2. **Diagnosticar**: Use manipuladores de falhas para ver a exceção real

### 2. Símbolos ausentes nos rastreamentos de pilha

**Causa**: Ausência de arquivos PDB para compilações de versão

**Solução**: Compile com as flags do linker `/Zi` e `/DEBUG`

### 3. A compilação trava ou o sistema congela

**Causa**: Muitas compilações paralelas esgotando a capacidade de processamento

**Solução**: Use o Build Governor - ele ajusta automaticamente a velocidade com base na pressão de memória

## Ferramentas relacionadas

- **[FlexiFlow](https://github.com/mcp-tool-shop-org/flexiflow)** - Motor assíncrono Python com registro estruturado (inspirado nos padrões de debug_log.h)

## Contribuições

Este conjunto de ferramentas foi desenvolvido durante a depuração do problema [XRPLF/rippled#6293](https://github.com/XRPLF/rippled/issues/6293).

Contribuições são bem-vindas!

## Licença

Licença MIT — a mesma do rippled.

---

Criado por <a href="https://mcp-tool-shop.github.io/">MCP Tool Shop</a

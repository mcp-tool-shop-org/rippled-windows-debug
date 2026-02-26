<p align="center">
  <a href="README.ja.md">日本語</a> | <a href="README.zh.md">中文</a> | <a href="README.es.md">Español</a> | <a href="README.fr.md">Français</a> | <a href="README.md">English</a> | <a href="README.it.md">Italiano</a> | <a href="README.pt-BR.md">Português (BR)</a>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/mcp-tool-shop-org/brand/main/logos/rippled-windows-debug/readme.png" width="400" alt="rippled-windows-debug">
</p>

<p align="center">
  <a href="https://github.com/mcp-tool-shop-org/rippled-windows-debug/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT License"></a>
  <a href="https://mcp-tool-shop-org.github.io/rippled-windows-debug/"><img src="https://img.shields.io/badge/Landing_Page-live-blue" alt="Landing Page"></a>
</p>

विंडोज डिबगिंग टूलकिट, रिपलड (XRPL वैलिडेटर नोड) के लिए। स्वचालित बिल्ड सुरक्षा और विस्तृत क्रैश डायग्नोस्टिक्स - उन मेमोरी समस्याओं को रोकने और डिबग करने के लिए जो समानांतर C++ बिल्ड में होती हैं।

## शुरुआत कैसे करें

```powershell
# Clone the toolkit
git clone https://github.com/mcp-tool-shop-org/rippled-windows-debug.git
cd rippled-windows-debug

# Set up automatic build protection (one-time, no admin required)
.\scripts\setup-governor.ps1

# Restart terminal, then build rippled safely
cmake --build build --parallel 16  # Governor prevents OOM automatically
```

## समस्या

विंडोज पर समानांतर C++ बिल्ड अक्सर मेमोरी की कमी के कारण विफल हो जाते हैं:

1. **बिल्ड विफलताएं**: प्रत्येक `cl.exe` 1-4 जीबी रैम का उपयोग कर सकता है। उच्च `-j` मान मेमोरी को समाप्त कर देते हैं।
2. **भ्रामक त्रुटियां**: `STATUS_STACK_BUFFER_OVERRUN (0xC0000409)` अक्सर वास्तव में `std::bad_alloc` होता है।
3. **कोई डायग्नोस्टिक्स नहीं**: `cl.exe` चुपचाप कोड 1 के साथ समाप्त हो जाता है, कोई स्पष्टीकरण नहीं।
4. **सिस्टम फ्रीज**: जब कमिट चार्ज 100% तक पहुँच जाता है, तो विंडोज अनुत्तरदायी हो जाता है।

**मूल कारण**: `std::bad_alloc` `STATUS_STACK_BUFFER_OVERRUN` के रूप में दिखाई देता है क्योंकि:
1. अपवाद पकड़ा नहीं जाता → `std::terminate()` कॉल किया जाता है।
2. `terminate()` `abort()` को कॉल करता है।
3. MSVC की `/GS` सुरक्षा जांच इसे बफर ओवररन के रूप में व्याख्या करती है।

## यह टूलकिट क्या प्रदान करता है

### 1. बिल्ड गवर्नर (स्वचालित OOM सुरक्षा)

**यह क्रैश को होने से पहले ही रोकता है।** `tools/build-governor/` में स्थित:

- **शून्य-कॉन्फ़िगरेशन सुरक्षा**: रैपर पहले बिल्ड पर स्वचालित रूप से गवर्नर को शुरू करते हैं।
- **अनुकूल थ्रॉटलिंग**: कमिट चार्ज की निगरानी करता है, जब मेमोरी का दबाव बढ़ता है तो बिल्ड को धीमा कर देता है।
- **कार्रवाई योग्य डायग्नोस्टिक्स**: "मेमोरी का दबाव detected, -j4 की सिफारिश की जाती है।"
- **स्वचालित शटडाउन**: गवर्नर 30 मिनट की निष्क्रियता के बाद बंद हो जाता है।

```powershell
# One-time setup
.\scripts\setup-governor.ps1

# All builds are now protected automatically
cmake --build . --parallel 16
msbuild /m:16
ninja -j 8
```

### 2. विस्तृत क्रैश हैंडलर (`crash_handlers.h`)

**उन क्रैश का निदान करता है जो होते हैं।** सिंगल-हेडर क्रैश डायग्नोस्टिक्स जो कैप्चर करते हैं:
- वास्तविक अपवाद प्रकार और संदेश (जो `STATUS_STACK_BUFFER_OVERRUN` के रूप में छिपा हुआ `std::bad_alloc` को प्रकट करता है)।
- पूर्ण स्टैक ट्रेस जिसमें सिंबल रिज़ॉल्यूशन शामिल है।
- सिग्नल जानकारी (SIGABRT, SIGSEGV, आदि)।
- **पूर्ण बिल्ड जानकारी** (टूलकिट संस्करण, गिट कमिट, कंपाइलर)।
- **सिस्टम जानकारी** (विंडोज संस्करण, सीपीयू, मेमोरी, कंप्यूटर का नाम)।

### 3. रिच-स्टाइल डिबग लॉगिंग (`debug_log.h`)

पायथन की [Rich](https://github.com/Textualize/rich) लाइब्रेरी से प्रेरित सुंदर टर्मिनल लॉगिंग:
- **रंगीन लॉग स्तर** - जानकारी (सियान), चेतावनी (पीला), त्रुटि (लाल)।
- **बॉक्स-ड्राइंग कैरेक्टर** - यूनिकोड के साथ दृश्य अनुभाग सीमाएं।
- **स्वचालित टाइमिंग** - अनुभाग पूर्ण होने पर व्यतीत समय दिखाते हैं।
- **सहसंबंध आईडी** - थ्रेड्स में संबंधित लॉग प्रविष्टियों को ट्रैक करें।
- **एकाधिक प्रारूप** - रिच (रंगीन), टेक्स्ट (सादा), JSON (मशीन-पठनीय)।

### 4. मिनीडंप पीढ़ी (`minidump.h`)

स्वचालित क्रैश डंप कैप्चर:
- डिबगिंग के लिए पूर्ण मेमोरी डंप।
- कॉन्फ़िगर करने योग्य डंप स्थान।
- पुराने डंप की स्वचालित सफाई।

### 5. बिल्ड जानकारी (`build_info.h`)

व्यापक बिल्ड और सिस्टम जानकारी:
- टूलकिट संस्करण।
- गिट कमिट हैश, शाखा, गंदा स्थिति।
- कंपाइलर का नाम और संस्करण।
- बिल्ड तिथि/समय और आर्किटेक्चर।
- विंडोज संस्करण और बिल्ड नंबर।
- सीपीयू मॉडल और कोर की संख्या।
- सिस्टम मेमोरी।

## गवर्नर कैसे काम करता है

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

गवर्नर **कमिट चार्ज** (मुफ़्त रैम नहीं) की निगरानी करता है क्योंकि:
- कमिट चार्ज = वादा की गई मेमोरी (भले ही अभी तक पेज इन न की गई हो)।
- जब कमिट सीमा तक पहुँच जाती है, तो आवंटन तुरंत विफल हो जाते हैं।
- मुफ़्त रैम भ्रामक हो सकती है (फ़ाइल कैश, स्टैंडबाय पेज)।

## रिपलड को क्रैश डायग्नोस्टिक्स के लिए पैच करना

`src/xrpld/app/main/Main.cpp` पर पैच लागू करें:

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

## उदाहरण क्रैश आउटपुट

जब कोई क्रैश होता है, तो आपको अस्पष्ट त्रुटि कोड के बजाय एक व्यापक रिपोर्ट दिखाई देगी:

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

## रिच-स्टाइल लॉगिंग उदाहरण

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

## डिबग टूलकिट के साथ रिपलड का निर्माण

### आवश्यकताएं

- विजुअल स्टूडियो 2022 बिल्ड टूल्स (या पूरा VS2022)
- .NET 9.0 SDK (बिल्ड गवर्नर के लिए)
- पायथन 3.x, जिसमें कॉनन 2.x शामिल है (`pip install conan`)
- CMake 3.25+ (यह कॉनन के साथ आता है या इसे अलग से स्थापित करें)
- निंजा (यह कॉनन के साथ आता है या इसे अलग से स्थापित करें)

### विकल्प 1: एक कमांड के साथ बिल्ड (अनुशंसित)

इस टूलकिट में एक पॉवरशेल स्क्रिप्ट शामिल है जो सब कुछ संभालती है:

```powershell
# In your rippled directory
cd F:\rippled

# Copy the build script from the toolkit
copy F:\AI\rippled-windows-debug\scripts\build-rippled.ps1 .

# Run the full build with governor protection
powershell -ExecutionPolicy Bypass -File build-rippled.ps1 -Parallel 8
```

यह स्क्रिप्ट स्वचालित रूप से:
- VS2022 वातावरण स्थापित करती है
- कॉनन के लिए PATH में पायथन स्क्रिप्ट्स जोड़ती है
- बिल्ड गवर्नर रैपर को कॉन्फ़िगर करती है
- कॉनन इंस्टॉलेशन चलाती है
- निंजा के साथ CMake को कॉन्फ़िगर करती है
- गवर्नर सुरक्षा के साथ बिल्ड करती है

### विकल्प 2: मैन्युअल बिल्ड चरण

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

### रिलीज़ बिल्ड के लिए PDB फ़ाइलें उत्पन्न करना

रिलीज़ बिल्ड में सिंबल रिज़ॉल्यूशन के लिए, CMakeLists.txt में निम्नलिखित जोड़ें:

```cmake
if(MSVC)
    # Generate PDB for release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()
```

## डेमो

रिच-शैली लॉगिंग को देखने के लिए डेमो चलाएं:

```batch
cd examples

REM Basic build
cl /EHsc /Zi /utf-8 test_crash.cpp /link dbghelp.lib shell32.lib

REM Run demo
test_crash.exe 6    REM Rich-style logging demo
test_crash.exe 7    REM Show build & system info only
test_crash.exe 1    REM Trigger bad_alloc crash with full report
```

**ध्यान दें:** पूर्ण रंग आउटपुट के लिए विंडोज टर्मिनल या VT/ANSI सपोर्ट वाले टर्मिनल का उपयोग करें।

## फ़ाइलें

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

## विंडोज से संबंधित सामान्य समस्याएं

### 1. `std::bad_alloc` जो `STATUS_STACK_BUFFER_OVERRUN` के रूप में दिखाई देता है

**कारण**: अप्रत्याशित त्रुटि → समाप्त → रद्द करें → /GS जांच

**समाधान**:
1. **इसे रोकें**: बिल्ड गवर्नर का उपयोग करें (`.\scripts\setup-governor.ps1`)
2. **इसका निदान करें**: वास्तविक त्रुटि देखने के लिए क्रैश हैंडलर का उपयोग करें

### 2. स्टैक ट्रेस में गायब सिंबल

**कारण**: रिलीज़ बिल्ड के लिए कोई PDB फ़ाइल नहीं

**समाधान**: `/Zi` और `/DEBUG` लिंकर फ़्लैग के साथ बिल्ड करें

### 3. बिल्ड अटक जाता है या सिस्टम जम जाता है

**कारण**: बहुत अधिक समानांतर कंपाइलेशन, जिससे मेमोरी पर अत्यधिक दबाव पड़ता है

**समाधान**: बिल्ड गवर्नर का उपयोग करें - यह स्वचालित रूप से मेमोरी दबाव के आधार पर गति को नियंत्रित करता है

## संबंधित उपकरण

- **[FlexiFlow](https://github.com/mcp-tool-shop-org/flexiflow)** - संरचित लॉगिंग (debug_log.h पैटर्न से प्रेरित) के साथ पायथन एसिंक्रोनस इंजन

## योगदान

यह टूलकिट समस्या [XRPLF/rippled#6293](https://github.com/XRPLF/rippled/issues/6293) को डीबग करते समय विकसित किया गया था।

योगदान का स्वागत है!

## लाइसेंस

MIT लाइसेंस — rippled के समान।

---

<a href="https://mcp-tool-shop.github.io/">MCP Tool Shop</a> द्वारा निर्मित

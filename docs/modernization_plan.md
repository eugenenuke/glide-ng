# Refined Implementation Plan: Glide Extensions & Reusable Utility Library

Based on user feedback, this refined plan outlines the architectural design and implementation details to:
1. Relocate the plan to the repository's `./docs/` directory.
2. Confirm the dual-binary pipeline architecture (Glide 2.x vs Glide 3.x) and explain how the build system integrates them.
3. Implement Glide 2.x extension stubs and dynamically query the driver version using `grGetString(GR_VERSION)`.
4. Expose a live, visual **Extension Support Checklist** in the diagnostic console header, distinguishing between fully supported (`[X]`) and stubbed/absent (`[ ]`) extensions.
5. Redesign the `DiagnosticInfo` library to serve as a **comprehensive command-line parser and hardware initializer** that:
   * Consolidates the resolution definitions (`ResolutionInfo` list) for each Glide version.
   * Parses the command-line arguments (handling `-l` / `--list` and `-r` / `--resolution` dynamically).
   * Automatically prints the VRAM/Resolution support matrix and exits if `-l` is passed.
   * Performs hardware polling and prints the beautiful startup header (including the extension checklist).
   * Validates the requested resolution and returns a clean, fully-populated configuration struct to the tool.

---

## 1. Architectural Confirmation: Dual-Binary Pipeline (Glide 2.x vs 3.x)

### A. Why Separate Binaries are Required
Your observation is 100% correct! Because of the fundamental evolution of the Glide API, **we absolutely require separate binaries for Glide 2.x and Glide 3.x applications**. They cannot share a single executable because:
1. **API Signature Overhaul**: Glide 3.x completely overhauled the API. For example:
   * **Window opening**: In Glide 2.x, `grSstWinOpen` returns `void`. In Glide 3.x, it returns a `GrContext_t` context handle.
   * **State queries**: Glide 2.x uses explicit structures and functions like `grSstQueryHardware(&hwconfig)`. Glide 3.x replaces this entirely with a generic key-value query function `grGet(pname, plength, pparams)`.
2. **Header Incompatibility**: The structure definitions, function prototypes, and macros in `include/glide2/glide.h` and `include/glide3/glide.h` are mutually exclusive and cannot be mixed in the same compilation unit.
3. **Linkage Isolation**: Glide 2.x apps must link against the Glide 2.x wrapper (`libglide2x.so`), whereas Glide 3.x apps must link against the Glide 3.x wrapper (`libglide3x.so`).

### B. How Our Pipeline Handles This
Our CMake build system is already designed to handle this dual-binary architecture elegantly. It compiles the same source code files twice under different target configurations:
* **Gears**: Builds `glide2_gears` (linked against `glide2x` and using `include/glide2`) and `glide3_gears` (linked against `glide3x` and using `include/glide3`).
* **Cube**: Builds `glide2_cube` and `glide3_cube` in the same manner.

### C. Alignment of the `DiagnosticInfo` Library
To fit into this dual-binary architecture, our shared utility `DiagnosticInfo.cpp` will also be compiled twice, producing two isolated static libraries:
1. **`tool_info2`**: Compiled with `-DGLIDE_VERSION=2`, including Glide 2.x headers. Linked by `glide2_gears` and `glide2_cube`.
2. **`tool_info3`**: Compiled with `-DGLIDE_VERSION=3`, including Glide 3.x headers. Linked by `glide3_gears` and `glide3_cube`.

This allows us to maintain a **single source file** (`DiagnosticInfo.cpp`), while generating two perfectly tailored, type-safe binaries for each API version!

---

## 2. Glide 2.x Extensions & Version Roadmaps

### A. Dynamic Version Detection
Instead of hardcoding the API version strings (`"3.10"` or `"2.43"`), the shared utility library will dynamically query the version from the active driver by calling **`grGetString(GR_VERSION)`**. This ensures the version reported in the header is always live and accurate.

### B. Extension Support Checklist
In the diagnostic console header, we will output a live checklist showing the support status of all key extensions. 
* Extensions returned by `grGetString(GR_EXTENSION)` will be marked with **`[X]`** (Fully Supported).
* Extensions that are stubbed or unsupported will be marked with **`[ ]`** (Absent).

#### Glide 2.x Extensions to Support & Checklist Mapping:
* **`[X] CHROMARANGE`** (`grChromakeyRangeExt`): Range-based chroma keying transparency. (**Fully Implemented**).
* **`[ ] COLORMASK`** (`grColorMaskExt`): Sub-pixel write masking. (**Stub**).
* **`[ ] STENCIL`** (`grStencilFuncExt`, `grStencilMaskExt`, `grStencilOpExt`): Stencil buffer operations. (**Stub**).
* **`[ ] BUFFERCLEAR`** (`grBufferClearExt`): Stencil-aware clearing. (**Stub**).
* **`[ ] LFBSTENCIL`** (`grLfbConstantStencilExt`): Constant stencil writes. (**Stub**).
* **`[ ] TBUFFER`** (`grTBufferWriteMaskExt`): Accumulation buffer masking. (**Stub**).
* **`[ ] BLEND`** (`grAlphaBlendFunctionExt`): Advanced alpha blend modes. (**Stub**).

#### Glide 3.x Extensions to Support & Checklist Mapping:
* **`[X] CHROMARANGE`** (`grChromaRangeExt`, `grChromaRangeModeExt`): Range-based chroma keying. (**Fully Implemented**).
* **`[ ] STENCIL`** (`grStencilFuncExt`, `grStencilMaskExt`, `grStencilOpExt`): Stencil buffering. (**Stub**).
* **`[ ] COMBINER`** (`grColorCombineExt`, `grAlphaCombineExt`): Advanced stage-based combiners. (**Stub**).
* **`[ ] TEXCOMBINER`** (`grTexColorCombineExt`, `grTexAlphaCombineExt`): Advanced TMU combining. (**Stub**).
* **`[ ] TBUFFER`** (`grTBufferWriteMaskExt`): Multi-sample T-Buffer control. (**Stub**).

---

## 3. Reusable CLI & Utility Library Design (`DiagnosticInfo`)

We will restructure `DiagnosticInfo` to be a comprehensive command-line parser and hardware initializer.

### A. Library Header (`tools/DiagnosticInfo.h`)
```cpp
#pragma once
#include <string>
#include <vector>
#include <glide.h>

namespace Tools {
    // Structured device parameters returned to the calling tools
    struct SystemConfig {
        std::string boardName;
        int fbiMemMb;
        int tmuCount;
        bool sli;
    };

    // The final validated runtime configuration returned to the tool
    struct ToolRunConfig {
        SystemConfig sysConfig;
        std::string resName;
#if GLIDE_VERSION == 3
        GrResolution_t resEnum;
#else
        GrResolution_t resEnum; // Note: GrResolution_t is FxI32 in both APIs
#endif
        int width;
        int height;
    };

    // Parses command line arguments, handles '-l' (exits immediately if passed),
    // initializes Glide, polls hardware, prints the diagnostic header,
    // validates the requested resolution, and returns the runtime configuration.
    ToolRunConfig InitializeAndParse(
        int argc,
        char* argv[],
        const std::string& toolName,
        const std::vector<std::string>& keybindings
    );
}
```

### B. Library Source (`tools/DiagnosticInfo.cpp`)
```cpp
#include "DiagnosticInfo.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <glide.h>

#if GLIDE_VERSION == 2
#include <sst1vid.h>
#endif

// Define unified Glide resolution tables
struct ResolutionInfo {
    const char* name;
    GrResolution_t resolution;
    int width;
    int height;
};

static ResolutionInfo resolutions[] = {
    {"320x200", GR_RESOLUTION_320x200, 320, 200},
    {"320x240", GR_RESOLUTION_320x240, 320, 240},
    {"400x256", GR_RESOLUTION_400x256, 400, 256},
    {"400x300", GR_RESOLUTION_400x300, 400, 300},
    {"512x384", GR_RESOLUTION_512x384, 512, 384},
    {"640x200", GR_RESOLUTION_640x200, 640, 200},
    {"640x350", GR_RESOLUTION_640x350, 640, 350},
    {"640x400", GR_RESOLUTION_640x400, 640, 400},
    {"640x480", GR_RESOLUTION_640x480, 640, 480},
    {"800x600", GR_RESOLUTION_800x600, 800, 600},
    {"856x480", GR_RESOLUTION_856x480, 856, 480},
    {"960x720", GR_RESOLUTION_960x720, 960, 720},
    {"1024x768", GR_RESOLUTION_1024x768, 1024, 768},
#if GLIDE_VERSION == 3
    {"1152x864", GR_RESOLUTION_1152x864, 1152, 864},
    {"1280x960", GR_RESOLUTION_1280x960, 1280, 960},
#endif
    {"1280x1024", GR_RESOLUTION_1280x1024, 1280, 1024},
    {"1600x1200", GR_RESOLUTION_1600x1200, 1600, 1200}
};

static const int num_resolutions = sizeof(resolutions) / sizeof(resolutions[0]);

Tools::ToolRunConfig Tools::InitializeAndParse(
    int argc,
    char* argv[],
    const std::string& toolName,
    const std::vector<std::string>& keybindings
) {
    bool list_mode = false;
    std::string res_name = "640x480";

    // 1. Parse Command-Line Arguments
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-l") == 0 || std::strcmp(argv[i], "--list") == 0) {
            list_mode = true;
        } else if (std::strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            res_name = argv[i + 1];
            i++;
        }
    }

    // 2. Initialize Glide
    grGlideInit();

    // 3. Poll Device Parameters Dynamically
    SystemConfig sysConfig;
    sysConfig.boardName = "Unknown 3dfx Card";
    sysConfig.fbiMemMb = 4;
    sysConfig.tmuCount = 1;
    sysConfig.sli = false;

#if GLIDE_VERSION == 3
    sysConfig.boardName = "Voodoo3 (H3) / Voodoo5";
    FxI32 fbi_mem_val = 4;
    grGet(GR_MEMORY_FB, 4, &fbi_mem_val);
    sysConfig.fbiMemMb = fbi_mem_val / (1024 * 1024);

    FxI32 tmu_count_val = 1;
    grGet(GR_NUM_TMU, 4, &tmu_count_val);
    sysConfig.tmuCount = tmu_count_val;
    sysConfig.sli = false;
#else
    GrHwConfiguration hwconfig;
    if (grSstQueryHardware(&hwconfig)) {
        if (hwconfig.SSTs[0].type == GR_SSTTYPE_VOODOO) {
            sysConfig.boardName = "Voodoo Graphics (SST-1)";
            sysConfig.fbiMemMb = hwconfig.SSTs[0].sstBoard.VoodooConfig.fbRam;
            sysConfig.tmuCount = 1;
            sysConfig.sli = false;
        } else if (hwconfig.SSTs[0].type == GR_SSTTYPE_Voodoo2) {
            sysConfig.boardName = "Voodoo2 (CVG)";
            sysConfig.fbiMemMb = hwconfig.SSTs[0].sstBoard.Voodoo2Config.fbRam;
            sysConfig.tmuCount = hwconfig.SSTs[0].sstBoard.Voodoo2Config.nTexelfx;
            sysConfig.sli = hwconfig.SSTs[0].sstBoard.Voodoo2Config.sliDetect ? true : false;
        }
    }
#endif

    // 4. Handle List Mode (-l / --list)
    if (list_mode) {
        std::cout << "======================================================================" << std::endl;
        std::cout << "3DFX GLIDE DIAGNOSTIC TOOL - RESOLUTION MATRIX (GLIDE " << GLIDE_VERSION << " API)" << std::endl;
        std::cout << "Active Device: " << sysConfig.boardName << " | Emulated VRAM: " << sysConfig.fbiMemMb << ".00 MB" << std::endl;
        std::cout << "======================================================================" << std::endl;

        for (int i = 0; i < num_resolutions; i++) {
            // Standard Voodoo formula: 3 buffers * 16-bit depth (2 bytes/pixel) = 6 bytes/pixel
            float req_mem = (resolutions[i].width * resolutions[i].height * 6.0f) / (1024.0f * 1024.0f);
            bool supported = (req_mem <= sysConfig.fbiMemMb);
            
            std::cout << "  [" << (supported ? "*" : " ") << "]  " 
                      << std::left << std::setw(10) << resolutions[i].name 
                      << " (" << std::fixed << std::setprecision(2) << req_mem << " MB required";
            if (!supported) {
                std::cout << " - EXCEEDS VRAM!)" << std::endl;
            } else {
                std::cout << ")" << std::endl;
            }
        }
        std::cout << "======================================================================" << std::endl;
        grGlideShutdown();
        std::exit(EXIT_SUCCESS);
    }

    // 5. Resolve and Validate Requested Resolution
    int res_index = -1;
    for (int i = 0; i < num_resolutions; i++) {
        if (res_name == resolutions[i].name) {
            res_index = i;
            break;
        }
    }

    if (res_index == -1) {
        std::cerr << "ERROR: Invalid resolution requested: '" << res_name << "'" << std::endl;
        std::cerr << "Supported resolutions for this binary:" << std::endl;
        for (int i = 0; i < num_resolutions; i++) {
            std::cerr << "  " << resolutions[i].name << std::endl;
        }
        grGlideShutdown();
        std::exit(EXIT_FAILURE);
    }

    // Check VRAM compatibility for requested resolution
    float req_mem = (resolutions[res_index].width * resolutions[res_index].height * 6.0f) / (1024.0f * 1024.0f);
    if (req_mem > sysConfig.fbiMemMb) {
        std::cerr << "ERROR: Requested resolution '" << res_name << "' requires " 
                  << std::fixed << std::setprecision(2) << req_mem 
                  << " MB VRAM, but only " << sysConfig.fbiMemMb << ".00 MB is available!" << std::endl;
        grGlideShutdown();
        std::exit(EXIT_FAILURE);
    }

    // 6. Print Startup Diagnostic Header
    const char* driverVersion = grGetString(GR_VERSION);
    const char* hardwareName = grGetString(GR_HARDWARE);
    const char* vendorName = grGetString(GR_VENDOR);
    const char* rendererName = grGetString(GR_RENDERER);
    const char* extString = grGetString(GR_EXTENSION);

    std::cout << "======================================================================" << std::endl;
    std::cout << toolName << " (Glide " << GLIDE_VERSION << ".x API)" << std::endl;
    std::cout << "======================================================================" << std::endl;
    std::cout << "Glide Version:   " << (driverVersion ? driverVersion : "Unknown") << std::endl;
    std::cout << "Resolution:      " << resolutions[res_index].name << " (" << std::fixed << std::setprecision(2) << req_mem << " MB VRAM required)" << std::endl;
    std::cout << "----------------------------------------------------------------------" << std::endl;
    std::cout << "Emulated Device: " << sysConfig.boardName << " (" << (hardwareName ? hardwareName : "Unknown") << ")" << std::endl;
    std::cout << "Device Vendor:   " << (vendorName ? vendorName : "Unknown") << std::endl;
    std::cout << "VRAM Size:       " << sysConfig.fbiMemMb << ".00 MB" << std::endl;
    std::cout << "Active TMUs:     " << sysConfig.tmuCount << " TMUs" << std::endl;
    std::cout << "SLI Detect:      " << (sysConfig.sli ? "Yes (Dual-Card Board)" : "No") << std::endl;
    std::cout << "----------------------------------------------------------------------" << std::endl;
    std::cout << "Active Driver:   " << (rendererName ? rendererName : "Unknown") << std::endl;
    std::cout << "----------------------------------------------------------------------" << std::endl;
    
    // Print Extension Checklist
    std::cout << "Driver Extension Checklist:" << std::endl;
    std::vector<std::string> targetExtensions;
#if GLIDE_VERSION == 3
    targetExtensions = {"CHROMARANGE", "STENCIL", "COMBINER", "TEXCOMBINER", "TBUFFER"};
#else
    targetExtensions = {"CHROMARANGE", "COLORMASK", "STENCIL", "BUFFERCLEAR", "LFBSTENCIL", "TBUFFER", "BLEND"};
#endif

    for (const auto& ext : targetExtensions) {
        bool supported = false;
        if (extString && extString[0] != '\0') {
            std::string s(extString);
            size_t pos = 0;
            while ((pos = s.find(ext, pos)) != std::string::npos) {
                bool leftWordBound = (pos == 0 || s[pos - 1] == ' ');
                bool rightWordBound = (pos + ext.length() == s.length() || s[pos + ext.length()] == ' ');
                if (leftWordBound && rightWordBound) {
                    supported = true;
                    break;
                }
                pos += ext.length();
            }
        }
        std::cout << "  [" << (supported ? "X" : " ") << "]  " << ext << std::endl;
    }
    std::cout << "----------------------------------------------------------------------" << std::endl;

    std::cout << "Interactive Keybindings:" << std::endl;
    for (const auto& binding : keybindings) {
        std::cout << "  " << binding << std::endl;
    }
    std::cout << "======================================================================" << std::endl;

    // 7. Construct and Return final validated parameters
    ToolRunConfig runConfig;
    runConfig.sysConfig = sysConfig;
    runConfig.resName = resolutions[res_index].name;
    runConfig.resEnum = resolutions[res_index].resolution;
    runConfig.width = resolutions[res_index].width;
    runConfig.height = resolutions[res_index].height;

    return runConfig;
}
```

---

## 4. Implementation Steps

1. **Write Refined Plan**: (This file saved to `docs/modernization_plan.md`).
2. **Write Shared Code**: Create `tools/DiagnosticInfo.h` and `tools/DiagnosticInfo.cpp`.
3. **Update Wrapper Extensions & Stubs**:
   * Implement `CHROMARANGE` in `grGetString(GR_EXTENSION)`.
   * Add stub functions and `grGetProcAddress` resolutions for the other Glide 2.x extensions in `src/glide2/glide2_entry.cpp`.
4. **Update Build System**: Configure `tools/CMakeLists.txt` to compile `tool_info2` and `tool_info3`.
5. **Refactor Diagnostic Tools**:
   * Update `glidegears.cpp` to call `Tools::InitializeAndParse` and use the returned `ToolRunConfig` struct, completely removing all parsing, resolution arrays, VRAM checks, and printing code.
   * Update `glidecube.cpp` to do the same.
6. **Build & Verify**: Compile everything and run both gears and cubes, verifying:
   * The `-l` flag lists the resolution matrix and exits cleanly.
   * Invalid resolution inputs are validated and handled correctly.
   * The new Extension Checklist and dynamic version display render perfectly.

#include <glide.h>
#include <tlib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

// Dynamically resolve grChromakeyRangeExt if available
typedef void (FX_CALL *grChromakeyRangeExtFunc)(GrColor_t minColor, GrColor_t maxColor, FxU32 mode);
static grChromakeyRangeExtFunc s_grChromakeyRangeExt = nullptr;

int main(int argc, char** argv) {
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;

    tlSetScreen(scrWidth, scrHeight);
    grGlideInit();
    assert(grSstQueryHardware(&hwconfig));
    grSstSelect(0);
    
    // Open standard 640x480 double-buffered window.
    // For visual desktop display, the user should make sure that "forceNoWindow"
    // is set to false in the glide_config.json file!
    assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));

    // Resolve the extension dynamically
    s_grChromakeyRangeExt = (grChromakeyRangeExtFunc)grGetProcAddress(const_cast<char*>("grChromakeyRangeExt"));
    if (!s_grChromakeyRangeExt) {
        std::printf("[WARNING] grChromakeyRangeExt extension NOT supported by the active Glide library!\n");
        std::printf("Range mode will not be available, falling back to standard chromakey.\n");
    }

    // Set up console text overlay
    tlConSet(0.0f, 0.0f, 1.0f, 1.0f, 60, 30, 0xffffff);
    
    // Initial Glide state: Gouraud/iterated color shading
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
                   GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_ITERATED,
                   GR_COMBINE_OTHER_NONE,
                   FXFALSE);

    bool chromaEnabled = true;
    bool rangeMode = (s_grChromakeyRangeExt != nullptr);

    std::printf("\n==================================================\n");
    std::printf("  3dfx Glide Chroma-Key Range Interactive Demo\n");
    std::printf("==================================================\n");
    std::printf("  Controls (press in the console or terminal):\n");
    std::printf("    [SPACE]  : Toggle Chroma-Keying On/Off\n");
    std::printf("    [R]      : Toggle between Exact Match and Range Match\n");
    std::printf("    [ESC]    : Exit Demo\n");
    std::printf("==================================================\n\n");

    bool running = true;
    while (running) {
        // Handle input if a key was pressed (bridged via console stdin / SDL window)
        if (tlKbHit()) {
            char key = tlGetCH();
            if (key == 27) { // ESC
                running = false;
                break;
            } else if (key == ' ') { // Space
                chromaEnabled = !chromaEnabled;
            } else if (key == 'r' || key == 'R') {
                if (s_grChromakeyRangeExt) {
                    rangeMode = !rangeMode;
                } else {
                    std::printf("Cannot toggle range mode: extension not supported!\n");
                }
            }
        }

        // Apply state based on selections
        if (chromaEnabled) {
            grChromakeyMode(GR_CHROMAKEY_ENABLE);
        } else {
            grChromakeyMode(GR_CHROMAKEY_DISABLE);
        }

        if (rangeMode && s_grChromakeyRangeExt) {
            // Range Match Mode (ABGR packed format: 0x00BBGGRR):
            // Min Color: Red=100 (0x64), Green=50 (0x32), Blue=50 (0x32) -> 0x00323264
            // Max Color: Red=150 (0x96), Green=200 (0xC8), Blue=200 (0xC8) -> 0x00C8C896
            // Mode: 0x10000000 (Range enabled, inclusive, all channels must intersect)
            s_grChromakeyRangeExt(0x00323264, 0x00C8C896, 0x10000000);
        } else {
            // Exact Match Mode:
            // Value: Red=128 (0x80), Green=120 (0x78), Blue=60 (0x3C) -> 0x003C7880
            grChromakeyValue(0x003c7880);
        }

        // Render the frame (solid black background)
        grBufferClear(0x00000000, 0, 0);

        // Setup smooth Gouraud shaded triangle
        GrVertex v0, v1, v2;

        // Left vertex (Red = 0, Green = 120, Blue = 60)
        v0.x = tlScaleX(0.15f); v0.y = tlScaleY(0.85f); v0.ooz = 1.0f; v0.oow = 1.0f;
        v0.r = 0.0f; v0.g = 120.0f; v0.b = 60.0f; v0.a = 255.0f;

        // Right vertex (Red = 255, Green = 120, Blue = 60)
        v1.x = tlScaleX(0.85f); v1.y = tlScaleY(0.85f); v1.ooz = 1.0f; v1.oow = 1.0f;
        v1.r = 255.0f; v1.g = 120.0f; v1.b = 60.0f; v1.a = 255.0f;

        // Top vertex (Red = 128, Green = 120, Blue = 60)
        v2.x = tlScaleX(0.5f);  v2.y = tlScaleY(0.15f); v2.ooz = 1.0f; v2.oow = 1.0f;
        v2.r = 128.0f; v2.g = 120.0f; v2.b = 60.0f; v2.a = 255.0f;

        grDrawTriangle(&v0, &v1, &v2);

        // Update console overlay text
        tlConOutput("=== Glide Chroma-Key Range Demo ===\n");
        if (chromaEnabled) {
            tlConOutput("Chroma-Keying: ENABLED \n");
        } else {
            tlConOutput("Chroma-Keying: DISABLED\n");
        }

        if (rangeMode && s_grChromakeyRangeExt) {
            tlConOutput("Mode: RANGE MATCH (Red 100 to 150)\n");
            tlConOutput("Expected: Wide vertical transparent band in middle of triangle.\n");
        } else {
            tlConOutput("Mode: EXACT MATCH (Red=128)\n");
            tlConOutput("Expected: Very thin single-pixel vertical line in middle.\n");
        }
        tlConOutput("\nPress SPACE to toggle keying, R to toggle mode, ESC to exit.\n");

        tlConRender();
        grBufferSwap(1);
    }

    grGlideShutdown();
    return 0;
}

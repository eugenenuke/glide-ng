#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>

namespace GlideTest {

class Test13 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;

public:
    std::string GetName() const override { return "test13"; }

    int GetTotalModes() const override { return 1; }

    void SetMode(int modeIndex) override {
        (void)modeIndex; // Only 1 mode
    }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));
        
        tlConSet(0.0f, 0.0f, 1.0f, 0.5f, 60, 15, 0xffffff);
        
        /* Set up Render State - constant color + iterated alpha */
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
                       GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_CONSTANT,
                       GR_COMBINE_OTHER_NONE,
                       FXFALSE);
        grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL,
                       GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_ITERATED,
                       GR_COMBINE_OTHER_NONE,
                       FXFALSE);
        grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                             GR_BLEND_ZERO, GR_BLEND_ZERO);
    }

    bool RenderFrame(int frameIndex) override {
        (void)frameIndex;
        
        grBufferClear(0x00000000, 0, 0);

        GrVertex vtxA, vtxB, vtxC;

        // Scale vertices to viewport
        vtxA.x = tlScaleX(0.5f), vtxA.y = tlScaleY(0.1f);
        vtxB.x = tlScaleX(0.8f), vtxB.y = tlScaleY(0.9f);
        vtxC.x = tlScaleX(0.2f), vtxC.y = tlScaleY(0.9f);
        
        // 1. Draw solid RED background triangle (opaque alpha)
        vtxA.a = vtxB.a = vtxC.a = 255.0f;
        grConstantColorValue(0x000000ff); // RED in ABGR packing (swapped to 0x000000ff -> RGB: 255,0,0)
        grDrawTriangle(&vtxA, &vtxB, &vtxC);

        // 2. Draw fading BLUE overlay triangle (iterated alpha, lower-left origin)
        grSstOrigin(GR_ORIGIN_LOWER_LEFT);

        vtxA.a = 0.0f; // Fade to transparent at top vertex
        vtxB.a = vtxC.a = 255.0f; // Opaque at bottom vertices
        grConstantColorValue(0x00ff0000); // BLUE in ABGR packing (swapped to 0x00ff0000 -> RGB: 0,0,255)
        grDrawTriangle(&vtxA, &vtxB, &vtxC);

        // Restore default coordinate origin
        grSstOrigin(GR_ORIGIN_UPPER_LEFT);

        // 3. Render console text overlay
        char statusText[128];
        sprintf(statusText, "Test13: Iterated Alpha Blend (Fading Blue over Red) \r");
        tlConOutput(statusText);
        tlConRender();

        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

// Register the test in the global suite
std::unique_ptr<BaseTest> CreateTest13() {
    return std::make_unique<Test13>();
}

} // namespace GlideTest

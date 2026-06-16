#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>

namespace GlideTest {

class Test07 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;

public:
    std::string GetName() const override { return "test07"; }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));
        
        tlConSet(0.0f, 0.0f, 1.0f, 1.0f, 60, 30, 0xffffff);
        
        /* Set up Render State - flat constant color + alpha blending */
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
                       GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_CONSTANT,
                       GR_COMBINE_OTHER_NONE,
                       FXFALSE);
        grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL,
                       GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_CONSTANT,
                       GR_COMBINE_OTHER_NONE,
                       FXFALSE);
        grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                             GR_BLEND_ZERO, GR_BLEND_ZERO);
        grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);

        tlConOutput("Press a key to quit\n");
    }

    bool RenderFrame(int frameIndex) override {
        (void)frameIndex;
        grBufferClear(0x00, 0, 0);

        GrVertex vtxA, vtxB, vtxC;

        // Triangle 1: Opaque Blue (0xFF0000FF)
        vtxA.x = tlScaleX(0.25f); vtxA.y = tlScaleY(0.21f);
        vtxB.x = tlScaleX(0.75f); vtxB.y = tlScaleY(0.21f);
        vtxC.x = tlScaleX(0.50f); vtxC.y = tlScaleY(0.79f);

        grConstantColorValue(0xFF0000FF);
        grDrawTriangle(&vtxA, &vtxB, &vtxC);

        // Triangle 2: Translucent Red (0x80FF0000) - 50% opacity
        vtxA.x = tlScaleX(0.86f); vtxA.y = tlScaleY(0.21f);
        vtxB.x = tlScaleX(0.86f); vtxB.y = tlScaleY(0.79f);
        vtxC.x = tlScaleX(0.14f); vtxC.y = tlScaleY(0.50f);

        grConstantColorValue(0x80FF0000);
        grDrawTriangle(&vtxA, &vtxB, &vtxC);

        tlConRender();
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest07() {
    return std::make_unique<Test07>();
}

} // namespace GlideTest

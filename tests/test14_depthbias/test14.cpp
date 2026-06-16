#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>

namespace GlideTest {

class Test14 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;
    
    short zBias = 0;
    short zDelta = 10;
    
    const short MAX_ZBIAS = 500;
    const short MIN_ZBIAS = -500;

public:
    std::string GetName() const override { return "test14"; }

    int GetTotalModes() const override { return 1; }

    int GetMaxFrames() const override { return 120; }
    std::vector<int> GetCaptureFrames() const override { return { 0, 15, 115 }; }

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
        
        /* Set up Render State - iterated alpha + z buffering */
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
                       GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_CONSTANT,
                       GR_COMBINE_OTHER_NONE,
                       FXFALSE);
        grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
        grDepthBufferFunction(GR_CMP_GREATER);
        grDepthMask(FXTRUE);
        
        zBias = 0;
        zDelta = 10;
    }

    bool RenderFrame(int frameIndex) override {
        (void)frameIndex;
        
        // Dynamically vary depth bias
        if ((zBias > MAX_ZBIAS) || (zBias < MIN_ZBIAS)) {
            zDelta = -zDelta;
        }
        zBias += zDelta;

        grBufferClear(0x00, 0, GR_ZDEPTHVALUE_FARTHEST);

        GrVertex vtxA, vtxB, vtxC;
        float zDist;

        // 1. Draw GRAY triangle (biased depth)
        vtxA.x = tlScaleX(0.25f), vtxA.y = tlScaleY(0.21f);
        vtxB.x = tlScaleX(0.75f), vtxB.y = tlScaleY(0.21f);
        vtxC.x = tlScaleX(0.5f),  vtxC.y = tlScaleY(0.79f);
        zDist = 10.0f;
        vtxA.ooz = vtxB.ooz = vtxC.ooz = (65535.0f / zDist);

        grConstantColorValue(0x00808080); // GRAY
        grDepthBiasLevel(zBias);
        grDrawTriangle(&vtxA, &vtxB, &vtxC);

        // 2. Draw GREEN triangle (static zero bias)
        vtxA.x = tlScaleX(0.86f), vtxA.y = tlScaleY(0.21f);
        vtxB.x = tlScaleX(0.86f), vtxB.y = tlScaleY(0.79f);
        vtxC.x = tlScaleX(0.14f), vtxC.y = tlScaleY(0.5f);
        zDist = 12.5f;
        vtxA.ooz = vtxB.ooz = (65535.0f / zDist);
        zDist = 7.5f;
        vtxC.ooz = (65535.0f / zDist);

        grConstantColorValue(0x0000ff00); // GREEN
        grDepthBiasLevel(0);
        grDrawTriangle(&vtxA, &vtxB, &vtxC);

        // Render console text overlay
        char statusText[128];
        std::sprintf(statusText, "DepthBiasLevel: %+.03d \r", zBias);
        tlConOutput(statusText);
        tlConRender();

        // Note: NO grBufferSwap(1) here! Handled by framework to prevent double-swaps.
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest14() {
    return std::make_unique<Test14>();
}

} // namespace GlideTest

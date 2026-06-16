#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>

namespace GlideTest {

class Test05 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;

public:
    std::string GetName() const override { return "test05"; }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));
        
        tlConSet(0.0f, 0.0f, 1.0f, 1.0f, 60, 30, 0xffffff);
        
        /* Set up Render State - flat constant color + Z-buffering */
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
                       GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_CONSTANT,
                       GR_COMBINE_OTHER_NONE,
                       FXFALSE);
        grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
        grDepthBufferFunction(GR_CMP_GREATER);
        grDepthMask(FXTRUE);

        tlConOutput("Press a key to quit\n");
    }

    bool RenderFrame(int frameIndex) override {
        (void)frameIndex;
        grBufferClear(0x00, 0, GR_ZDEPTHVALUE_FARTHEST);

        GrVertex vtxA, vtxB, vtxC;
        float zDist;

        // Triangle 1: Gray (0x00808080) at constant depth 10.0f
        vtxA.x = tlScaleX(0.25f); vtxA.y = tlScaleY(0.21f);
        vtxB.x = tlScaleX(0.75f); vtxB.y = tlScaleY(0.21f);
        vtxC.x = tlScaleX(0.50f); vtxC.y = tlScaleY(0.79f);

        zDist = 10.0f;
        vtxA.ooz = vtxB.ooz = vtxC.ooz = (65535.0f / zDist);

        grConstantColorValue(0x00808080);
        grDrawTriangle(&vtxA, &vtxB, &vtxC);

        // Triangle 2: Green (0x0000FF00) interpenetrating (depth 12.5f to 7.5f)
        vtxA.x = tlScaleX(0.86f); vtxA.y = tlScaleY(0.21f);
        vtxB.x = tlScaleX(0.86f); vtxB.y = tlScaleY(0.79f);
        vtxC.x = tlScaleX(0.14f); vtxC.y = tlScaleY(0.50f);

        zDist = 12.5f;
        vtxA.ooz = vtxB.ooz = (65535.0f / zDist);
        zDist = 7.5f;
        vtxC.ooz = (65535.0f / zDist);

        grConstantColorValue(0x0000FF00);
        grDrawTriangle(&vtxA, &vtxB, &vtxC);

        tlConRender();
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest05() {
    return std::make_unique<Test05>();
}

} // namespace GlideTest

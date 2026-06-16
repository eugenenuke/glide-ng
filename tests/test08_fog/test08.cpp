#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <cstdio>
#include <cassert>
#include <memory>

namespace GlideTest {

class Test08 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;
    GrFog_t fogtable[64];

public:
    std::string GetName() const override { return "test08"; }
    int GetTotalModes() const override { return 1; }
    void SetMode(int modeIndex) override { (void)modeIndex; }
    
    std::vector<int> GetCaptureFrames() const override {
        return {14}; // Capture at Frame 14
    }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));
        
        tlConSet(0.0f, 0.0f, 1.0f, 1.0f, 60, 30, 0xffffff);
        
        /* Set up Render State - gouraud shading + fog */
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
                       GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_ITERATED,
                       GR_COMBINE_OTHER_NONE,
                       FXFALSE);
        grFogMode(GR_FOG_WITH_TABLE);
        grFogColorValue(0xff00ff00); // Green
        guFogGenerateExp(fogtable, 0.01f);
        grFogTable(fogtable);

        tlConOutput("test08: Glide2 Table-Based Fogging\n");
    }

    bool RenderFrame(int frameIndex) override {
        (void)frameIndex;
        
        grBufferClear(0x00, 0, 0);

        GrVertex vtxA, vtxB, vtxC;
        float wDist;

        vtxA.x = tlScaleX(0.0f); vtxA.y = tlScaleY(0.0f);
        vtxA.r = 0.0f; vtxA.g = 0.0f; vtxA.b = 64.0f;
        vtxA.a = 255.0f;
        wDist = 20.0f;
        vtxA.oow = (1.0f / wDist);

        vtxB.x = tlScaleX(0.016f); vtxB.y = tlScaleY(1.0f);
        vtxB.r = 0.0f; vtxB.g = 0.0f; vtxB.b = 128.0f;
        vtxB.a = 255.0f;
        wDist = 2000.0f;
        vtxB.oow = (1.0f / wDist);

        vtxC.x = tlScaleX(1.0f); vtxC.y = tlScaleY(0.0208f);
        vtxC.r = 0.0f; vtxC.g = 0.0f; vtxC.b = 64.0f;
        vtxC.a = 255.0f;
        wDist = 20.0f;
        vtxC.oow = (1.0f / wDist);

        grDrawTriangle(&vtxA, &vtxB, &vtxC);

        tlConRender();
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest08() {
    return std::make_unique<Test08>();
}

} // namespace GlideTest

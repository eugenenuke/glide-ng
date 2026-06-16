#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>

namespace GlideTest {

class Test04 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;

public:
    std::string GetName() const override { return "test04"; }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));
        
        tlConSet(0.0f, 0.0f, 1.0f, 1.0f, 60, 30, 0xffffff);
        
        /* Set up Render State - gouraud shading */
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
                       GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_ITERATED,
                       GR_COMBINE_OTHER_NONE,
                       FXFALSE);

        tlConOutput("Press a key to quit\n");
    }

    bool RenderFrame(int frameIndex) override {
        (void)frameIndex;
        grBufferClear(0x0, 0, GR_WDEPTHVALUE_FARTHEST);

        GrVertex vtxA, vtxB, vtxC;
        vtxA.x = tlScaleX(0.3f); vtxA.y = tlScaleY(0.3f);
        vtxA.r = 255.0f; vtxA.g = 0.0f; vtxA.b = 0.0f;

        vtxB.x = tlScaleX(0.8f); vtxB.y = tlScaleY(0.4f);
        vtxB.r = 0.0f; vtxB.g = 255.0f; vtxB.b = 0.0f;

        vtxC.x = tlScaleX(0.5f); vtxC.y = tlScaleY(0.8f);
        vtxC.r = 0.0f; vtxC.g = 0.0f; vtxC.b = 255.0f;

        grDrawTriangle(&vtxA, &vtxB, &vtxC);

        tlConRender();
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest04() {
    return std::make_unique<Test04>();
}

} // namespace GlideTest

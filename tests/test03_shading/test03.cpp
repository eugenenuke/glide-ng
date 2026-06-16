#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>

namespace GlideTest {

class Test03 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;

public:
    std::string GetName() const override { return "test03"; }

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

        GrVertex vtxA, vtxB;
        vtxA.x = tlScaleX(0.0f); vtxA.y = tlScaleY(0.0f);
        vtxA.r = 255.0f; vtxA.g = 0.0f; vtxA.b = 0.0f;

        vtxB.x = tlScaleX(1.0f); vtxB.y = tlScaleY(1.0f);
        vtxB.r = 0.0f; vtxB.g = 255.0f; vtxB.b = 0.0f;

        grDrawLine(&vtxA, &vtxB);

        tlConRender();
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest03() {
    return std::make_unique<Test03>();
}

} // namespace GlideTest

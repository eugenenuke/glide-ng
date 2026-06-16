#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>

namespace GlideTest {

class Test02 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;

public:
    std::string GetName() const override { return "test02"; }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));
        
        // Note: test02.c sets console text color to red (0xff0000)
        tlConSet(0.0f, 0.0f, 1.0f, 1.0f, 60, 30, 0xff0000);
        
        /* Set up Render State - flat shading */
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
                       GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_CONSTANT,
                       GR_COMBINE_OTHER_NONE,
                       FXFALSE);
        grConstantColorValue(0xFFFFFF);

        tlConOutput("Press a key to quit\n");
    }

    bool RenderFrame(int frameIndex) override {
        (void)frameIndex;
        grBufferClear(0x0, 0, GR_WDEPTHVALUE_FARTHEST);

        GrVertex vtxA, vtxB;
        for (int i = 0; i < 100; i++) {
            float pos = ((float)i) / 100.0f;
            vtxA.x = tlScaleX(pos);
            vtxA.y = tlScaleY(0.0f);
            vtxB.x = tlScaleX(1.0f);
            vtxB.y = tlScaleY(pos);
            grDrawLine(&vtxA, &vtxB);
        }

        tlConRender();
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest02() {
    return std::make_unique<Test02>();
}

} // namespace GlideTest

#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>

namespace GlideTest {

class Test10 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;
    GrCullMode_t m_cullMode{GR_CULL_POSITIVE};

public:
    std::string GetName() const override { return "test10"; }

    int GetTotalModes() const override { return 2; }

    void SetMode(int modeIndex) override {
        if (modeIndex == 0) {
            m_cullMode = GR_CULL_POSITIVE;
        } else {
            m_cullMode = GR_CULL_NEGATIVE;
        }
    }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));
        
        tlConSet(0.0f, 0.0f, 1.0f, 1.0f, 60, 30, 0xffffff);
        
        /* Set up Render State - constant color combiner */
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
                       GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_CONSTANT,
                       GR_COMBINE_OTHER_NONE,
                       FXFALSE);

        tlConOutput("Press a key to quit\n");
    }

    bool RenderFrame(int frameIndex) override {
        (void)frameIndex;
        grBufferClear(0x0, 0, GR_WDEPTHVALUE_FARTHEST);

        // Apply active cull mode
        grCullMode(m_cullMode);

        // Define shared vertices for our triangles
        GrVertex vtxA, vtxB, vtxC;
        // Vertex A (Top-Center)
        vtxA.x = tlScaleX(0.5f); vtxA.y = tlScaleY(0.15f);
        vtxA.r = 255.0f; vtxA.g = 255.0f; vtxA.b = 255.0f; vtxA.a = 255.0f;

        // Vertex B (Bottom-Right)
        vtxB.x = tlScaleX(0.8f); vtxB.y = tlScaleY(0.85f);
        vtxB.r = 255.0f; vtxB.g = 255.0f; vtxB.b = 255.0f; vtxB.a = 255.0f;

        // Vertex C (Bottom-Left)
        vtxC.x = tlScaleX(0.2f); vtxC.y = tlScaleY(0.85f);
        vtxC.r = 255.0f; vtxC.g = 255.0f; vtxC.b = 255.0f; vtxC.a = 255.0f;

        // 1. Draw Positive-winding Clockwise (CW) triangle in BLUE
        // Winding: A -> B -> C (CW)
        grConstantColorValue(0x000000ff); // Blue
        grDrawTriangle(&vtxA, &vtxB, &vtxC);

        // 2. Draw Negative-winding Counter-Clockwise (CCW) triangle in RED
        // Winding: A -> C -> B (CCW)
        grConstantColorValue(0x00ff0000); // Red
        grDrawTriangle(&vtxA, &vtxC, &vtxB);

        // Render console text overlay
        if (m_cullMode == GR_CULL_POSITIVE) {
            tlConOutput("CULL POSITIVE: Only RED (CCW) triangle should render\r");
        } else {
            tlConOutput("CULL NEGATIVE: Only BLUE (CW) triangle should render\r");
        }
        tlConRender();
        
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest10() {
    return std::make_unique<Test10>();
}

} // namespace GlideTest

#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <cstdio>
#include <cassert>
#include <memory>

namespace GlideTest {

class Test09 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;
    uint32_t m_chromaColor = 0x00ff0000; // Default BLUE in ABGR

public:
    std::string GetName() const override { return "test09"; }
    int GetTotalModes() const override { return 2; }
    
    void SetMode(int modeIndex) override {
        if (modeIndex == 0) {
            m_chromaColor = 0x00ff0000; // BLUE
        } else {
            m_chromaColor = 0x000000ff; // RED
        }
        grChromakeyValue(m_chromaColor);
    }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));
        
        tlConSet(0.0f, 0.0f, 1.0f, 1.0f, 60, 30, 0xffffff);
        
        /* Set up Render State - gouraud shading + enable chromakey */
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
                       GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_CONSTANT,
                       GR_COMBINE_OTHER_NONE,
                       FXFALSE);
        grChromakeyMode(GR_CHROMAKEY_ENABLE);

        tlConOutput("test09: Glide2 Chroma Keying\n");
    }

    bool RenderFrame(int frameIndex) override {
        (void)frameIndex;
        
        grBufferClear(0x00, 0, 0);

        GrVertex vtxA, vtxB, vtxC;

        vtxA.x = tlScaleX(0.5f); vtxA.y = tlScaleY(0.1f);
        vtxB.x = tlScaleX(0.8f); vtxB.y = tlScaleY(0.9f);
        vtxC.x = tlScaleX(0.2f); vtxC.y = tlScaleY(0.9f);

        // 1. Draw RED triangle (Upper-Left origin -> pointing down)
        grConstantColorValue(0x000000ff); // RED
        grDrawTriangle(&vtxA, &vtxB, &vtxC);

        // 2. Flip origin to LOWER_LEFT
        grSstOrigin(GR_ORIGIN_LOWER_LEFT);

        // 3. Draw BLUE triangle (Lower-Left origin -> pointing up)
        grConstantColorValue(0x00ff0000); // BLUE
        grDrawTriangle(&vtxA, &vtxB, &vtxC);

        // 4. Restore origin to UPPER_LEFT for text overlay
        grSstOrigin(GR_ORIGIN_UPPER_LEFT);

        if (m_chromaColor == 0x00ff0000) {
            tlConOutput("Chromakey BLUE (RED triangle should be visible)  \r");
        } else {
            tlConOutput("Chromakey RED (BLUE triangle should be visible)   \r");
        }

        tlConRender();
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest09() {
    return std::make_unique<Test09>();
}

} // namespace GlideTest

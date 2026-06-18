#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>

namespace GlideTest {

class Test16 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;

    int m_maxFrames = 15;
    int m_captureFrameIndex = 14;
    bool m_renderSplash = false;
    bool m_shamelessPlug = false;

public:
    std::string GetName() const override { return "test16"; }

    int GetTotalModes() const override { return 2; }

    void SetMode(int modeIndex) override {
        if (modeIndex == 0) {
            // Mode 0: Watermark enabled on top of red/blue triangle grid
            m_maxFrames = 15;
            m_captureFrameIndex = 14;
            m_renderSplash = false;
            m_shamelessPlug = true;
        } else {
            // Mode 1: 3D spinning logo animated frame-by-frame
            m_maxFrames = 75;
            m_captureFrameIndex = 35;
            m_renderSplash = true;
            m_shamelessPlug = false;
        }
    }

    int GetMaxFrames() const override { return m_maxFrames; }
    int GetCaptureFrameIndex() const override { return m_captureFrameIndex; }
    std::vector<int> GetCaptureFrames() const override { return { m_captureFrameIndex }; }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));
        
        tlConSet(0.0f, 0.0f, 1.0f, 0.5f, 60, 15, 0xffffff);
        
        /* Set up Render State - constant color combiner */
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
                       GR_COMBINE_FACTOR_NONE,
                       GR_COMBINE_LOCAL_CONSTANT,
                       GR_COMBINE_OTHER_NONE,
                       FXFALSE);
    }

    bool RenderFrame(int frameIndex) override {
        #define BLUE  0x00ff0000
        #define RED   0x000000ff
        #define BLACK 0x00000000

        grBufferClear(BLACK, 0, GR_ZDEPTHVALUE_FARTHEST);

        if (!m_renderSplash) {
            // Mode 0: Draw 10x10 grid of alternating red/blue triangles
            GrVertex vtxA, vtxB, vtxC, vtxD;
            for (int y = 0; y < 10; y++) {
                for (int x = 0; x < 10; x++) {
                    vtxA.x = vtxB.x = tlScaleX(static_cast<float>(x) / 10.0f);
                    vtxA.y = vtxD.y = tlScaleY(static_cast<float>(y) / 10.0f);
                    vtxB.y = vtxC.y = tlScaleY((static_cast<float>(y) / 10.0f) + 0.1f);
                    vtxC.x = vtxD.x = tlScaleX((static_cast<float>(x) / 10.0f) + 0.1f);

                    grConstantColorValue(RED);
                    grDrawTriangle(&vtxA, &vtxB, &vtxC);
                    grConstantColorValue(BLUE);
                    grDrawTriangle(&vtxA, &vtxC, &vtxD);
                }
            }
        }

        // Configure shameless plug state dynamically
        grGlideShamelessPlug(m_shamelessPlug ? 1 : 0);

        if (m_renderSplash) {
            // Mode 1: Draw the 3D spinning logo at the current frame
            // We pass frameIndex + 1 (1 to 74) to grSplash
            grSplash(0.0f, 0.0f, scrWidth, scrHeight, frameIndex + 1);
        }

        tlConRender();
        return true;
    }

    void Shutdown() override {
        // Clean up shameless plug state on exit to avoid leakage
        grGlideShamelessPlug(0);
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest16() {
    return std::make_unique<Test16>();
}

} // namespace GlideTest

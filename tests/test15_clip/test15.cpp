#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>

namespace GlideTest {

class Test15 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;
    
    float m_clipX = 0.2f;
    float m_clipY = 0.2f;
    float m_clipSize = 0.6f;

public:
    std::string GetName() const override { return "test15"; }

    int GetTotalModes() const override { return 2; }

    void SetMode(int modeIndex) override {
        if (modeIndex == 0) {
            // Mode 0: Centered large clip window
            m_clipX = 0.2f;
            m_clipY = 0.2f;
            m_clipSize = 0.6f;
        } else {
            // Mode 1: Small offset clip window
            m_clipX = 0.4f;
            m_clipY = 0.1f;
            m_clipSize = 0.3f;
        }
    }

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
        (void)frameIndex;
        
        // 1. Reset clip window to full screen and clear the buffers
        grClipWindow(0, 0, static_cast<FxU32>(scrWidth), static_cast<FxU32>(scrHeight));
        grBufferClear(0x00000000, 0, GR_ZDEPTHVALUE_FARTHEST);

        // 2. Compute and set active clip window bounds
        int minx = static_cast<int>(tlScaleX(m_clipX));
        int miny = static_cast<int>(tlScaleY(m_clipY));
        
        int maxx = (m_clipX + m_clipSize > 1.0f) ? 
            static_cast<int>(tlScaleX(1.0f)) : 
            static_cast<int>(tlScaleX(m_clipX + m_clipSize));
            
        int maxy = (m_clipY + m_clipSize > 1.0f) ? 
            static_cast<int>(tlScaleY(1.0f)) : 
            static_cast<int>(tlScaleY(m_clipY + m_clipSize));
            
        grClipWindow(minx, miny, maxx, maxy);

        // 3. Draw a 10x10 grid of gray triangles covering the entire screen
        GrVertex vtxA, vtxB, vtxC;
        for (int y = 0; y < 10; y++) {
            for (int x = 0; x < 10; x++) {
                vtxA.x = vtxB.x = tlScaleX(static_cast<float>(x) / 10.0f);
                vtxA.y = tlScaleY(static_cast<float>(y) / 10.0f);
                vtxB.y = vtxC.y = tlScaleY((static_cast<float>(y) / 10.0f) + 0.1f);
                vtxC.x = tlScaleX((static_cast<float>(x) / 10.0f) + 0.1f);
                
                // Initialize color/alpha attributes for Gouraud-interpolated pipeline compatibility (if any)
                vtxA.r = vtxB.r = vtxC.r = 128.0f;
                vtxA.g = vtxB.g = vtxC.g = 128.0f;
                vtxA.b = vtxB.b = vtxC.b = 128.0f;
                vtxA.a = vtxB.a = vtxC.a = 255.0f;

                grConstantColorValue(0x00808080); // Gray (0x00RRGGBB in ABGR little endian memory -> 0x00808080 is Gray!)
                grDrawTriangle(&vtxA, &vtxB, &vtxC);
            }
        }

        // 4. Render console text overlay (always reset clip to full screen before text render!)
        grClipWindow(0, 0, static_cast<FxU32>(scrWidth), static_cast<FxU32>(scrHeight));
        
        char statusText[128];
        std::sprintf(statusText, "CLIP WINDOW: (%d,%d) to (%d,%d)\r", minx, miny, maxx, maxy);
        tlConOutput(statusText);
        tlConRender();
        
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest15() {
    return std::make_unique<Test15>();
}

} // namespace GlideTest

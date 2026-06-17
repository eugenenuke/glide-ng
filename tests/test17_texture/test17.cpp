#include "../framework/TestRunner.h"
#include <glide.h>
#include <glideutl.h>
#include "tlib.h"
#include <cstdio>
#include <cassert>
#include <memory>
#include <vector>
#include <cstdlib>

namespace GlideTest {

class Test17 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;
    int m_currentMode = 0;
    TlTexture texture;

public:
    std::string GetName() const override { return "test17"; }
    int GetTotalModes() const override { return 5; }

    std::vector<int> GetCaptureFrames() const override {
        return {0}; // Capture the first frame of each mode
    }

    void SetMode(int modeIndex) override {
        m_currentMode = modeIndex;
    }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));
        
        tlConSet(0.0f, 0.0f, 1.0f, 1.0f, 60, 30, 0xffffff);

        // 1. Load texture data using the framework's tlLoadTexture utility
        if (!tlLoadTexture("ext_tests/build_tests/miro.3df", &texture.info, &texture.tableType, &texture.tableData)) {
            std::fprintf(stderr, "test17: Failed to load miro.3df using tlLoadTexture\n");
            std::exit(1);
        }

        // 2. Download texture data to TMU0
        grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        if (texture.tableType != NO_TABLE) {
            grTexDownloadTable(GR_TMU0, texture.tableType, &texture.tableData);
        }

        // 3. Bind TMU0
        grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
        grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);

        // Set green constant color (FLATLIT mode uses it)
        grConstantColorValue(0x00ff0000); // ABGR representation of red

        tlConOutput("test17: Texture Mapping and Multi-mode Register Combiners\n");
    }

    bool RenderFrame(int frameIndex) override {
        (void)frameIndex;
        std::printf("DEBUG: Test17::RenderFrame executed. frameIndex=%d, m_currentMode=%d\n", frameIndex, m_currentMode);
        
        grBufferClear(0x00, 0, 0);

        // Configure combiner based on current mode
        switch(m_currentMode) {
        case 0: // DECAL
            grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER,
                            GR_COMBINE_FACTOR_ONE,
                            GR_COMBINE_LOCAL_NONE,
                            GR_COMBINE_OTHER_TEXTURE,
                            FXFALSE);
            break;
        case 1: // FLATLIT
            grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER,
                            GR_COMBINE_FACTOR_LOCAL,
                            GR_COMBINE_LOCAL_CONSTANT,
                            GR_COMBINE_OTHER_TEXTURE,
                            FXFALSE);
            break;
        case 2: // RGBLIT
            grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER,
                            GR_COMBINE_FACTOR_LOCAL,
                            GR_COMBINE_LOCAL_ITERATED,
                            GR_COMBINE_OTHER_TEXTURE,
                            FXFALSE);
            break;
        case 3: // WHITELIT
            grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER,
                            GR_COMBINE_FACTOR_LOCAL_ALPHA,
                            GR_COMBINE_LOCAL_NONE,
                            GR_COMBINE_OTHER_TEXTURE,
                            FXFALSE);
            grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL,
                            GR_COMBINE_FACTOR_NONE,
                            GR_COMBINE_LOCAL_ITERATED,
                            GR_COMBINE_OTHER_TEXTURE,
                            FXFALSE);
            break;
        case 4: // SPECALPHA
            grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA,
                            GR_COMBINE_FACTOR_LOCAL,
                            GR_COMBINE_LOCAL_ITERATED,
                            GR_COMBINE_OTHER_TEXTURE,
                            FXFALSE);
            grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL,
                            GR_COMBINE_FACTOR_NONE,
                            GR_COMBINE_LOCAL_ITERATED,
                            GR_COMBINE_OTHER_TEXTURE,
                            FXFALSE);
            break;
        }

        // Draw quad (A-B, C-D) using two triangles
        GrVertex vtxA, vtxB, vtxC, vtxD;
        vtxA.oow = 1.0f;
        vtxB = vtxC = vtxD = vtxA;

        vtxA.x = vtxC.x = tlScaleX(0.2f);
        vtxB.x = vtxD.x = tlScaleX(0.8f);
        vtxA.y = vtxB.y = tlScaleY(0.2f);
        vtxC.y = vtxD.y = tlScaleY(0.8f);

        vtxA.tmuvtx[0].sow = vtxC.tmuvtx[0].sow = 0.0f;
        vtxB.tmuvtx[0].sow = vtxD.tmuvtx[0].sow = 255.0f;
        vtxA.tmuvtx[0].tow = vtxB.tmuvtx[0].tow = 0.0f;
        vtxC.tmuvtx[0].tow = vtxD.tmuvtx[0].tow = 255.0f;

        vtxA.r = 255.0f; vtxA.g =   0.0f; vtxA.b =   0.0f; vtxA.a = 255.0f;
        vtxB.r =   0.0f; vtxB.g = 255.0f; vtxB.b =   0.0f; vtxB.a = 128.0f;
        vtxC.r =   0.0f; vtxC.g =   0.0f; vtxC.b = 255.0f; vtxC.a = 128.0f;
        vtxD.r =   0.0f; vtxD.g =   0.0f; vtxD.b =   0.0f; vtxD.a =   0.0f;

        grDrawTriangle(&vtxA, &vtxD, &vtxC);
        grDrawTriangle(&vtxA, &vtxB, &vtxD);

        tlConRender();
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest17() {
    return std::make_unique<Test17>();
}

} // namespace GlideTest

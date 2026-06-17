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

class Test18 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;
    TlTexture texture;

public:
    std::string GetName() const override { return "test18"; }
    int GetTotalModes() const override { return 1; }

    std::vector<int> GetCaptureFrames() const override {
        return {0}; // Capture frame 0
    }

    void SetMode(int modeIndex) override {
        // test18 only has a single mode
        (void)modeIndex;
    }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));
        
        tlConSet(0.0f, 0.0f, 1.0f, 0.5f, 60, 15, 0xffffff);

        // 1. Load texture data (alpha.3df)
        if (!tlLoadTexture("ext_tests/build_tests/alpha.3df", &texture.info, &texture.tableType, &texture.tableData)) {
            std::fprintf(stderr, "test18: Failed to load alpha.3df using tlLoadTexture\n");
            std::exit(1);
        }

        // 2. Download texture data to TMU0
        grTexDownloadMipMap(GR_TMU0, grTexMinAddress(GR_TMU0), GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        if (texture.tableType != NO_TABLE) {
            grTexDownloadTable(GR_TMU0, texture.tableType, &texture.tableData);
        }

        // 3. Bind TMU0
        grTexSource(GR_TMU0, grTexMinAddress(GR_TMU0), GR_MIPMAPLEVELMASK_BOTH, &texture.info);

        // 4. Configure texture filtering and mipmapping
        grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
        grTexMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXFALSE);

        tlConOutput("test18: Alpha Texture and Blending Test\n");
    }

    bool RenderFrame(int frameIndex) override {
        (void)frameIndex;
        
        grBufferClear(0, 0, GR_ZDEPTHVALUE_FARTHEST);

        // --- Stage 1: Draw 10x10 grid of background squares ---
        // Configure combiner for constant color drawing
        grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
                        GR_COMBINE_FACTOR_NONE,
                        GR_COMBINE_LOCAL_CONSTANT,
                        GR_COMBINE_OTHER_NONE,
                        FXFALSE);
        grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL,
                        GR_COMBINE_FACTOR_NONE,
                        GR_COMBINE_LOCAL_CONSTANT,
                        GR_COMBINE_OTHER_NONE,
                        FXFALSE);
        
        // Disable alpha blending for the background (overwrite)
        grAlphaBlendFunction(GR_BLEND_ONE,
                              GR_BLEND_ZERO,
                              GR_BLEND_ZERO,
                              GR_BLEND_ZERO);

        #define RED  0x000000ff
        #define BLUE 0x00ff0000

        for (int y = 0; y < 10; y++) {
            for (int x = 0; x < 10; x++) {
                GrVertex vtxA, vtxB, vtxC, vtxD;
                vtxA.oow = vtxB.oow = vtxC.oow = vtxD.oow = 1.0f;

                /* 
                   A-D
                   |\|
                   B-C
                 */
                vtxA.x = vtxB.x = tlScaleX(((float)x) / 10.0f);
                vtxA.y = vtxD.y = tlScaleY(((float)y) / 10.0f);
                vtxB.y = vtxC.y = tlScaleY((((float)y) / 10.0f) + 0.1f);
                vtxC.x = vtxD.x = tlScaleX((((float)x) / 10.0f) + 0.1f);
                
                grConstantColorValue(RED);
                grDrawTriangle(&vtxA, &vtxB, &vtxC);
                grConstantColorValue(BLUE);
                grDrawTriangle(&vtxA, &vtxC, &vtxD);
            }
        }

        // --- Stage 2: Draw the large central transparent textured quad ---
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

        // Route texture color and texture alpha directly to combiner outputs
        grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER,
                        GR_COMBINE_FACTOR_ONE,
                        GR_COMBINE_LOCAL_NONE,
                        GR_COMBINE_OTHER_TEXTURE,
                        FXFALSE);
        grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER,
                        GR_COMBINE_FACTOR_ONE,
                        GR_COMBINE_LOCAL_NONE,
                        GR_COMBINE_OTHER_TEXTURE,
                        FXFALSE);

        // Enable classic source alpha blending
        grAlphaBlendFunction(GR_BLEND_SRC_ALPHA,
                              GR_BLEND_ONE_MINUS_SRC_ALPHA,
                              GR_BLEND_ZERO,
                              GR_BLEND_ZERO);

        grDrawTriangle(&vtxA, &vtxD, &vtxC);
        grDrawTriangle(&vtxA, &vtxB, &vtxD);

        tlConRender();
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest18() {
    return std::make_unique<Test18>();
}

} // namespace GlideTest

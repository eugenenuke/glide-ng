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

class Test19 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;
    TlTexture texture;

public:
    std::string GetName() const override { return "test19"; }
    int GetTotalModes() const override { return 1; }

    std::vector<int> GetCaptureFrames() const override {
        return {0, 1, 2, 3}; // Capture all 4 swept frames
    }

    void SetMode(int modeIndex) override {
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

        // 1. Load texture data (miro.3df)
        if (!tlLoadTexture("ext_tests/build_tests/miro.3df", &texture.info, &texture.tableType, &texture.tableData)) {
            std::fprintf(stderr, "test19: Failed to load miro.3df using tlLoadTexture\n");
            std::exit(1);
        }

        // 2. Download texture data to TMU0
        grTexDownloadMipMap(GR_TMU0, grTexMinAddress(GR_TMU0), GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        if (texture.tableType != NO_TABLE) {
            grTexDownloadTable(GR_TMU0, texture.tableType, &texture.tableData);
        }

        // 3. Bind TMU0
        grTexSource(GR_TMU0, grTexMinAddress(GR_TMU0), GR_MIPMAPLEVELMASK_BOTH, &texture.info);

        // 4. Configure Decal Texture Combiner (disable mipmapping)
        grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER,
                        GR_COMBINE_FACTOR_ONE,
                        GR_COMBINE_LOCAL_NONE,
                        GR_COMBINE_OTHER_TEXTURE,
                        FXFALSE);
        
        grTexCombine(GR_TMU0,
                      GR_COMBINE_FUNCTION_LOCAL,
                      GR_COMBINE_FACTOR_NONE,
                      GR_COMBINE_FUNCTION_LOCAL,
                      GR_COMBINE_FACTOR_NONE,
                      FXFALSE, FXFALSE);

        grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);

        tlConOutput("test19: Texture Filtering and Clamping Test\n");
    }

    bool RenderFrame(int frameIndex) override {
        // Map frameIndex to sweep parameters
        // Frame 0: Magnify, Point
        // Frame 1: Magnify, Bilinear
        // Frame 2: Minify, Point
        // Frame 3: Minify, Bilinear
        bool minify = (frameIndex >= 2);
        bool bilerp = (frameIndex % 2 == 1);

        // Configure dynamic filter mode
        grTexFilterMode(GR_TMU0,
                        bilerp ? GR_TEXTUREFILTER_BILINEAR : GR_TEXTUREFILTER_POINT_SAMPLED,
                        bilerp ? GR_TEXTUREFILTER_BILINEAR : GR_TEXTUREFILTER_POINT_SAMPLED);

        grBufferClear(0, 0, GR_ZDEPTHVALUE_FARTHEST);

        GrVertex vtxA, vtxB, vtxC, vtxD;
        vtxA.oow = 1.0f;
        vtxB = vtxC = vtxD = vtxA;

        if (minify) {
            vtxA.x = vtxC.x = tlScaleX(0.0f);
            vtxB.x = vtxD.x = tlScaleX(1.0f);
            vtxA.y = vtxB.y = tlScaleY(0.0f);
            vtxC.y = vtxD.y = tlScaleY(1.0f);
        } else { // magnify
            vtxA.x = vtxC.x = tlScaleX(0.45f);
            vtxB.x = vtxD.x = tlScaleX(0.55f);
            vtxA.y = vtxB.y = tlScaleY(0.45f);
            vtxC.y = vtxD.y = tlScaleY(0.55f);
        }

        vtxA.tmuvtx[0].sow = vtxC.tmuvtx[0].sow = 0.0f;
        vtxB.tmuvtx[0].sow = vtxD.tmuvtx[0].sow = 255.0f;
        vtxA.tmuvtx[0].tow = vtxB.tmuvtx[0].tow = 0.0f;
        vtxC.tmuvtx[0].tow = vtxD.tmuvtx[0].tow = 255.0f;

        grDrawTriangle(&vtxA, &vtxD, &vtxC);
        grDrawTriangle(&vtxA, &vtxB, &vtxD);

        if (bilerp) {
            tlConOutput("Mode: BILINEAR\n");
        } else {
            tlConOutput("Mode: POINT_SAMPLED\n");
        }

        tlConRender();
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest19() {
    return std::make_unique<Test19>();
}

} // namespace GlideTest

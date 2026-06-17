#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>
#include <cmath>
#include <vector>

namespace GlideTest {

struct Vertex3D {
    float x, y, z, w;
    float s, t;
};

class Test29 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;
    int m_currentMode = 0;
    TlTexture texture;

    // Matches legacy test29.c vertices exactly
    Vertex3D srcVerts[4] = {
        { -0.5f, 0.0f,  0.5f, 1.0f, 0.0f, 0.0f },   // 0 = back-left
        {  0.5f, 0.0f,  0.5f, 1.0f, 1.0f, 0.0f },   // 1 = back-right
        {  0.5f, 0.0f, -0.5f, 1.0f, 1.0f, 1.0f },   // 2 = front-right
        { -0.5f, 0.0f, -0.5f, 1.0f, 0.0f, 1.0f }    // 3 = front-left
    };

public:
    std::string GetName() const override { return "test29"; }
    int GetTotalModes() const override { return 2; } // Mode 0: Separate W, Mode 1: Default W

    std::vector<int> GetCaptureFrames() const override {
        return {0, 1, 2}; // 3 sweep steps
    }

    void SetMode(int modeIndex) override {
        m_currentMode = modeIndex;
    }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        // Matches legacy test29.c window origin exactly: GR_ORIGIN_LOWER_LEFT!
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_LOWER_LEFT, 2, 1));
        
        tlConSet(0.0f, 0.0f, 1.0f, 1.0f, 60, 15, 0xffffff);

        // Load decal texture
        if (!tlLoadTexture("ext_tests/build_tests/decal1.3df", &texture.info, &texture.tableType, &texture.tableData)) {
            std::fprintf(stderr, "test29: Failed to load decal1.3df\n");
            std::exit(1);
        }

        grTexDownloadMipMap(GR_TMU0, grTexMinAddress(GR_TMU0), GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        if (texture.tableType != NO_TABLE) {
            grTexDownloadTable(GR_TMU0, texture.tableType, &texture.tableData);
        }

        grTexSource(GR_TMU0, grTexMinAddress(GR_TMU0), GR_MIPMAPLEVELMASK_BOTH, &texture.info);
        grTexMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXFALSE);
        grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
        grTexCombine(GR_TMU0,
                      GR_COMBINE_FUNCTION_LOCAL,
                      GR_COMBINE_FACTOR_NONE,
                      GR_COMBINE_FUNCTION_LOCAL,
                      GR_COMBINE_FACTOR_NONE,
                      FXFALSE, FXFALSE);

        grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER,
                        GR_COMBINE_FACTOR_ONE,
                        GR_COMBINE_LOCAL_NONE,
                        GR_COMBINE_OTHER_TEXTURE,
                        FXFALSE);

        grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
        grDepthBufferFunction(GR_CMP_LESS);
        grDepthMask(FXTRUE);

        tlConOutput("test29: Legacy Origin LOWER_LEFT Test\n");
    }

    bool RenderFrame(int frameIndex) override {
        float distance = 1.2f + static_cast<float>(frameIndex) * 0.6f;
        bool separateW = (m_currentMode == 0);

        if (separateW) {
            grHints(GR_HINT_STWHINT, GR_STWHINT_W_DIFF_TMU0);
        } else {
            grHints(GR_HINT_STWHINT, 0);
        }

        grBufferClear(0x00404040, 0, GR_WDEPTHVALUE_FARTHEST);

        GrVertex v[4];

        float angle = -20.0f * 3.14159265f / 180.0f;
        float cos_t = std::cos(angle);
        float sin_t = std::sin(angle);

        for (int i = 0; i < 4; ++i) {
            // Rotate around X
            float rx = srcVerts[i].x;
            float ry = srcVerts[i].y * cos_t - srcVerts[i].z * sin_t;
            float rz = srcVerts[i].y * sin_t + srcVerts[i].z * cos_t;

            // Translate
            float tx = rx;
            float ty = ry - 0.3f;
            float tz = rz + distance;

            // Project
            float px = tx / tz;
            float py = ty / tz;

            // Scale to screen using legacy bottom-left math!
            v[i].x = (px + 1.0f) * 0.5f * scrWidth;
            v[i].y = (py + 1.0f) * 0.5f * scrHeight;
            v[i].oow = 1.0f / tz;

            if (separateW) {
                v[i].tmuvtx[0].oow = 1.0f;
                v[i].tmuvtx[0].sow = srcVerts[i].s * 255.0f * v[i].tmuvtx[0].oow;
                v[i].tmuvtx[0].tow = srcVerts[i].t * 255.0f * v[i].tmuvtx[0].oow;
            } else {
                v[i].tmuvtx[0].oow = v[i].oow;
                v[i].tmuvtx[0].sow = srcVerts[i].s * 255.0f * v[i].tmuvtx[0].oow;
                v[i].tmuvtx[0].tow = srcVerts[i].t * 255.0f * v[i].tmuvtx[0].oow;
            }
        }

        // Draw quad using legacy test29.c triangle indices!
        grDrawTriangle(&v[0], &v[1], &v[2]);
        grDrawTriangle(&v[0], &v[2], &v[3]);

        if (separateW) {
            tlConOutput("Hint: SEPARATE_W (Linear Tex)\n");
        } else {
            tlConOutput("Hint: DEFAULT_W (Perspective Tex)\n");
        }

        tlConRender();
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest29() {
    return std::make_unique<Test29>();
}

} // namespace GlideTest

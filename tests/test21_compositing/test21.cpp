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

class Test21 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;

    TlTexture baseTexture;
    unsigned long baseTextureAddr;
    TlTexture lightTexture;
    unsigned long lightTextureAddr;
    TlTexture detailTexture;
    unsigned long detailTextureAddr;

    TlVertex3D srcVerts[4];
    int m_modeIndex = 0; // 0 = LIGHTMAP, 1 = SPECULAR, 2 = DETAIL

public:
    std::string GetName() const override { return "test21"; }
    int GetTotalModes() const override { return 3; }
    int GetMaxFrames() const override { return 31; }

    std::vector<int> GetCaptureFrames() const override {
        return {0, 15, 30}; // Capture three frames to verify depth/distance animation
    }

    void SetMode(int modeIndex) override {
        m_modeIndex = modeIndex;
    }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        
        // Note: The original test21.c uses GR_ORIGIN_LOWER_LEFT
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_LOWER_LEFT, 2, 1));
        
        tlConSet(0.0f, 0.0f, 1.0f, 0.5f, 60, 15, 0xffffff);

        /* Set up Render State - decal - bilinear - nearest mipmapping  */
        grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER,
                        GR_COMBINE_FACTOR_ONE,
                        GR_COMBINE_LOCAL_NONE,
                        GR_COMBINE_OTHER_TEXTURE,
                        FXFALSE);
        grTexMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXFALSE);
        grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);

        /* Load texture data into system ram */
        assert(tlLoadTexture("ext_tests/build_tests/decal1.3df", 
                             &baseTexture.info, 
                             &baseTexture.tableType, 
                             &baseTexture.tableData));
        assert(tlLoadTexture("ext_tests/build_tests/light.3df", 
                             &lightTexture.info, 
                             &lightTexture.tableType, 
                             &lightTexture.tableData));
        assert(tlLoadTexture("ext_tests/build_tests/lava.3df", 
                             &detailTexture.info, 
                             &detailTexture.tableType, 
                             &detailTexture.tableData));

        /* Download texture data to TMU */
        baseTextureAddr = grTexMinAddress(GR_TMU0);
        grTexDownloadMipMap(GR_TMU0,
                             baseTextureAddr,
                             GR_MIPMAPLEVELMASK_BOTH,
                             &baseTexture.info);
        if (baseTexture.tableType != NO_TABLE) {
            grTexDownloadTable(GR_TMU0,
                                baseTexture.tableType,
                                &baseTexture.tableData);
        }

        lightTextureAddr = 
            baseTextureAddr +
            grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH,
                                     &baseTexture.info);
        grTexDownloadMipMap(GR_TMU0,
                             lightTextureAddr,
                             GR_MIPMAPLEVELMASK_BOTH,
                             &lightTexture.info);
        if (lightTexture.tableType != NO_TABLE) {
            grTexDownloadTable(GR_TMU0,
                                lightTexture.tableType,
                                &lightTexture.tableData);
        }

        detailTextureAddr = 
            lightTextureAddr +
            grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH,
                                     &lightTexture.info);
        grTexDownloadMipMap(GR_TMU0,
                             detailTextureAddr,
                             GR_MIPMAPLEVELMASK_BOTH,
                             &detailTexture.info);
        if (detailTexture.tableType != NO_TABLE) {
            grTexDownloadTable(GR_TMU0,
                                detailTexture.tableType,
                                &detailTexture.tableData);
        }

        /* Initialize Source 3D data - Rectangle on X/Z Plane Centered about Y Axis */
        srcVerts[0].x = -0.5f, srcVerts[0].y = 0.0f, srcVerts[0].z =  0.5f, srcVerts[0].w = 1.0f;
        srcVerts[1].x =  0.5f, srcVerts[1].y = 0.0f, srcVerts[1].z =  0.5f, srcVerts[1].w = 1.0f;
        srcVerts[2].x = -0.5f, srcVerts[2].y = 0.0f, srcVerts[2].z = -0.5f, srcVerts[2].w = 1.0f;
        srcVerts[3].x =  0.5f, srcVerts[3].y = 0.0f, srcVerts[3].z = -0.5f, srcVerts[3].w = 1.0f;

        srcVerts[0].s = 0.0f, srcVerts[0].t = 0.0f;
        srcVerts[1].s = 1.0f, srcVerts[1].t = 0.0f;
        srcVerts[2].s = 0.0f, srcVerts[2].t = 1.0f;
        srcVerts[3].s = 1.0f, srcVerts[3].t = 1.0f;

        tlConOutput("test21: Texture Compositing / Multi-pass Rendering\n");
    }

    bool RenderFrame(int frameIndex) override {
        grBufferClear(0x00404040, 0, GR_ZDEPTHVALUE_FARTHEST);

        GrVertex vtxA, vtxB, vtxC, vtxD;
        TlVertex3D xfVerts[4];
        TlVertex3D prjVerts[4];

        vtxA.oow = 1.0f;
        vtxB = vtxC = vtxD = vtxA;

        // Animate distance over frames
        float distance = 1.0f + (frameIndex + 1) * 0.01f;

        tlSetMatrix(tlIdentity());
        tlMultMatrix(tlXRotation(-20.0f));
        tlMultMatrix(tlTranslation(0.0f, -0.3f, distance));

        tlTransformVertices(xfVerts, srcVerts, 4);
        tlProjectVertices(prjVerts, xfVerts, 4);

        vtxA.x = tlScaleX(prjVerts[0].x);
        vtxA.y = tlScaleY(prjVerts[0].y);
        vtxA.oow = 1.0f / prjVerts[0].w;

        vtxB.x = tlScaleX(prjVerts[1].x);
        vtxB.y = tlScaleY(prjVerts[1].y); 
        vtxB.oow = 1.0f / prjVerts[1].w;
        
        vtxC.x = tlScaleX(prjVerts[2].x);
        vtxC.y = tlScaleY(prjVerts[2].y);
        vtxC.oow = 1.0f / prjVerts[2].w;

        vtxD.x = tlScaleX(prjVerts[3].x); 
        vtxD.y = tlScaleY(prjVerts[3].y);
        vtxD.oow = 1.0f / prjVerts[3].w;

        vtxA.tmuvtx[0].sow = prjVerts[0].s * 255.0f * vtxA.oow;
        vtxA.tmuvtx[0].tow = prjVerts[0].t * 255.0f * vtxA.oow;

        vtxB.tmuvtx[0].sow = prjVerts[1].s * 255.0f * vtxB.oow;
        vtxB.tmuvtx[0].tow = prjVerts[1].t * 255.0f * vtxB.oow;

        vtxC.tmuvtx[0].sow = prjVerts[2].s * 255.0f * vtxC.oow;
        vtxC.tmuvtx[0].tow = prjVerts[2].t * 255.0f * vtxC.oow;

        vtxD.tmuvtx[0].sow = prjVerts[3].s * 255.0f * vtxD.oow;
        vtxD.tmuvtx[0].tow = prjVerts[3].t * 255.0f * vtxD.oow;

        // --- Render First Pass ---
        switch (m_modeIndex) {
        case 0: // LIGHTMAP
        case 1: // SPECULAR
            grTexCombine(GR_TMU0,
                          GR_COMBINE_FUNCTION_LOCAL,
                          GR_COMBINE_FACTOR_NONE,
                          GR_COMBINE_FUNCTION_LOCAL,
                          GR_COMBINE_FACTOR_NONE,
                          FXFALSE,
                          FXFALSE);
            break;
        case 2: // DETAIL
            grTexCombine(GR_TMU0,
                          GR_COMBINE_FUNCTION_BLEND_LOCAL,
                          GR_COMBINE_FACTOR_DETAIL_FACTOR,
                          GR_COMBINE_FUNCTION_LOCAL,
                          GR_COMBINE_FACTOR_NONE,
                          FXFALSE,
                          FXFALSE);
            grTexDetailControl(GR_TMU0, 2, 7, 1.0f);
            break;
        }
        grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO,
                              GR_BLEND_ONE, GR_BLEND_ZERO);
        grTexSource(GR_TMU0,
                     baseTextureAddr,
                     GR_MIPMAPLEVELMASK_BOTH,
                     &baseTexture.info);

        grDrawTriangle(&vtxA, &vtxB, &vtxD);
        grDrawTriangle(&vtxA, &vtxD, &vtxC);

        // --- Render Second Pass ---
        switch (m_modeIndex) {
        case 0: // LIGHTMAP
            grAlphaBlendFunction(GR_BLEND_DST_COLOR, GR_BLEND_ZERO,
                                  GR_BLEND_ZERO, GR_BLEND_ZERO);
            grTexSource(GR_TMU0,
                         lightTextureAddr,
                         GR_MIPMAPLEVELMASK_BOTH,
                         &lightTexture.info);
            tlConOutput("LIGHTMAP - TEXTURE MODULATE  \n");
            break;
        case 2: // DETAIL
            grTexCombine(GR_TMU0,
                          GR_COMBINE_FUNCTION_BLEND_LOCAL,
                          GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR,
                          GR_COMBINE_FUNCTION_LOCAL,
                          GR_COMBINE_FACTOR_NONE,
                          FXFALSE,
                          FXFALSE);
            grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE,
                                  GR_BLEND_ZERO, GR_BLEND_ZERO);
            grTexSource(GR_TMU0,
                         detailTextureAddr,
                         GR_MIPMAPLEVELMASK_BOTH,
                         &detailTexture.info);
            tlConOutput("DETAIL   - BLEND ON LOD      \n");
            break;
        case 1: // SPECULAR
            grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE,
                                  GR_BLEND_ZERO, GR_BLEND_ZERO);
            grTexSource(GR_TMU0,
                         lightTextureAddr,
                         GR_MIPMAPLEVELMASK_BOTH,
                         &lightTexture.info);
            tlConOutput("SPECULAR - TEXTURE ACCUMULATE\n");
            break;
        }

        grDrawTriangle(&vtxA, &vtxB, &vtxD);
        grDrawTriangle(&vtxA, &vtxD, &vtxC);

        tlConRender();
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest21() {
    return std::make_unique<Test21>();
}

} // namespace GlideTest

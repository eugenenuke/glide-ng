/*
 * glide_envtest.c - Unified 3dfx Glide 2.x / 3.x Environment Variable Verifier
 *
 * Pure C99, 100% authentic Glide API calls.
 * Builds cleanly for Linux (glide2_envtest, glide3_envtest), DOS/32 CauseWay
 * (build/dos/glide_envtest.exe), and Windows (Win32 dynamic glide2x.dll).
 *
 * Renders a deterministic 4-quadrant visual test card and performs LFB RGB565
 * pixel readbacks + hardware queries so every authentic 3dfx FX_GLIDE_* /
 * SST_* / SSTV2_* and GLIDE_NG_* override can be verified visually and
 * programmatically:
 *
 *   TL: 16x16 mipmapped B/W seam quad (POINT_SAMPLED, magenta vertex color) +
 *       minified mipmap strip (Mip0=B/W, Mip1=Green, Mip2=Blue, Mip3=Red).
 *       Probes SST_TEXMAP_DISABLE, SSTV2_FORCE_BILINEAR, SSTV2_FORCE_TRILINEAR.
 *   TR: Smooth RGB gradient + flat mid-tone (124,122,124) swatch with
 *       GR_DITHER_4x4. Probes FX_GLIDE_NO_DITHER_SUB.
 *   BL: Overlapping W-buffered near-red / far-blue depth quads + 5-step gray
 *       gamma wedge (with app gamma reset to 1.0).
 *       Probes FX_GLIDE_ALLOC_AUX and SST_GAMMA / SSTV2_GAMMA.
 *   BR: Signature panel for FX_GLIDE_IGNORE_REOPEN (--test-reopen), optional
 *       out-of-bounds triangle for FX_GLIDE_BOUNDS_CHECK (--test-bounds),
 *       plus hardware query & frame-pacing verification.
 */

#define GLIDE_TOOL_LOADER_IMPLEMENTATION 1
#include "glide_tool_platform.h"
#include "glide2_dos_ovl.h"

#define GLIDE_NG_ENV_VARS_DEFINE_TABLE 1
#include "glide_ng/env_vars.h"

#include <stddef.h>

#ifndef GR_WDEPTHVALUE_FARTHEST
#  define GR_WDEPTHVALUE_FARTHEST 0xFFFF
#endif

#if GLIDE_VERSION == 3
#  define ENVTEST_LOD_16     GR_LOD_LOG2_16
#  define ENVTEST_LOD_1      GR_LOD_LOG2_1
#  define ENVTEST_ASPECT_1x1 GR_ASPECT_LOG2_1x1
#else
#  define ENVTEST_LOD_16     GR_LOD_16
#  define ENVTEST_LOD_1      GR_LOD_1
#  define ENVTEST_ASPECT_1x1 GR_ASPECT_1x1
#endif

#if GLIDE_VERSION == 3
typedef struct EnvVertex {
  float x, y;
  float ooz;
  float oow;
  float r, g, b, a;
  float s0, t0;
} EnvVertex;
#else
typedef GrVertex EnvVertex;
#endif

static void set_vert(EnvVertex* v, float x, float y, float oow,
                     float r, float g, float b, float s0, float t0) {
#if GLIDE_VERSION == 3
  v->x = x;
  v->y = y;
  v->ooz = 0.0f;
  v->oow = oow;
  v->r = r;
  v->g = g;
  v->b = b;
  v->a = 255.0f;
  v->s0 = s0 * oow;
  v->t0 = t0 * oow;
#else
  v->x = x;
  v->y = y;
  v->z = 0.0f;
  v->ooz = 0.0f;
  v->oow = oow;
  v->r = r;
  v->g = g;
  v->b = b;
  v->a = 255.0f;
  v->tmuvtx[0].sow = s0 * oow;
  v->tmuvtx[0].tow = t0 * oow;
  v->tmuvtx[0].oow = oow;
#if GLIDE_NUM_TMU > 1
  v->tmuvtx[1].sow = 0.0f;
  v->tmuvtx[1].tow = 0.0f;
  v->tmuvtx[1].oow = oow;
#endif
#endif
}

static void draw_quad(float x0, float y0, float x1, float y1, float oow,
                      float r0, float g0, float b0,
                      float r1, float g1, float b1,
                      float s0, float t0, float s1, float t1) {
  EnvVertex tl, tr, br, bl;
  set_vert(&tl, x0, y0, oow, r0, g0, b0, s0, t0);
  set_vert(&tr, x1, y0, oow, r1, g1, b1, s1, t0);
  set_vert(&br, x1, y1, oow, r1, g1, b1, s1, t1);
  set_vert(&bl, x0, y1, oow, r0, g0, b0, s0, t1);
  grDrawTriangle(&tl, &tr, &br);
  grDrawTriangle(&tl, &br, &bl);
}

#if GLIDE_VERSION == 3
static void init_vertex_layout(void) {
  grVertexLayout(GR_PARAM_XY,  (FxI32)offsetof(EnvVertex, x),   GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_Z,   (FxI32)offsetof(EnvVertex, ooz), GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_Q,   (FxI32)offsetof(EnvVertex, oow), GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_RGB, (FxI32)offsetof(EnvVertex, r),   GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_A,   (FxI32)offsetof(EnvVertex, a),   GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_ST0, (FxI32)offsetof(EnvVertex, s0),  GR_PARAM_ENABLE);
}
#endif

static void init_tex_info(GrTexInfo* info, void* data) {
  memset(info, 0, sizeof(*info));
#if GLIDE_VERSION == 3
  info->smallLodLog2 = ENVTEST_LOD_1;
  info->largeLodLog2 = ENVTEST_LOD_16;
  info->aspectRatioLog2 = ENVTEST_ASPECT_1x1;
#else
  info->smallLod = ENVTEST_LOD_1;
  info->largeLod = ENVTEST_LOD_16;
  info->aspectRatio = ENVTEST_ASPECT_1x1;
#endif
  info->format = GR_TEXFMT_RGB_565;
  info->data = data;
}

static GrColor_t pack_abgr(unsigned int r, unsigned int g, unsigned int b) {
  return (GrColor_t)(0xFF000000U | ((b & 0xFFU) << 16) |
                     ((g & 0xFFU) << 8) | (r & 0xFFU));
}

static void unpack_rgb565(FxU16 p, int* r, int* g, int* b) {
  int r5 = (int)((p >> 11) & 0x1FU);
  int g6 = (int)((p >> 5) & 0x3FU);
  int b5 = (int)(p & 0x1FU);
  *r = (r5 << 3) | (r5 >> 2);
  *g = (g6 << 2) | (g6 >> 4);
  *b = (b5 << 3) | (b5 >> 2);
}

static int parse_res_arg(const char* s, GrScreenResolution_t* out_enum,
                         int* out_w, int* out_h) {
  if (!s) return 0;
  if (strcmp(s, "512") == 0 || gtool_strcasecmp(s, "512x384") == 0) {
    *out_enum = GR_RESOLUTION_512x384; *out_w = 512; *out_h = 384; return 1;
  }
  if (strcmp(s, "640") == 0 || gtool_strcasecmp(s, "640x480") == 0) {
    *out_enum = GR_RESOLUTION_640x480; *out_w = 640; *out_h = 480; return 1;
  }
  if (strcmp(s, "800") == 0 || gtool_strcasecmp(s, "800x600") == 0) {
    *out_enum = GR_RESOLUTION_800x600; *out_w = 800; *out_h = 600; return 1;
  }
  if (strcmp(s, "1024") == 0 || gtool_strcasecmp(s, "1024x768") == 0) {
    *out_enum = GR_RESOLUTION_1024x768; *out_w = 1024; *out_h = 768; return 1;
  }
  return 0;
}

static void print_usage(const char* argv0) {
  printf("Usage: %s [options]\n", argv0 ? argv0 : "glide_envtest");
  printf("3dfx Glide %d.x Environment Variable & Override Verifier (%s)\n\n",
         GLIDE_VERSION, gtool_platform_name());
  printf("Options:\n");
  printf("  --verify              Run deterministic probe pass & print verdict report\n");
  printf("  --frames <N>          Render N frames then exit (default: 5 with --verify)\n");
  printf("  --seconds <N>         Render for N seconds then exit (interactive mode)\n");
  printf("  --res <512|640|800|1024> Resolution (default: 640)\n");
  printf("  --test-reopen         Issue redundant grSstWinOpen to test FX_GLIDE_IGNORE_REOPEN\n");
  printf("  --test-bounds         Submit out-of-bounds triangle to test FX_GLIDE_BOUNDS_CHECK\n");
  printf("  --splash              Call grSplash() on startup to test FX_GLIDE_NO_SPLASH\n");
  printf("  --shameless-plug      Request app-level shameless plug to test FX_GLIDE_SHAMELESS_PLUG\n");
  printf("  --pause               Wait for keypress before exiting\n");
  printf("  -h, --help            Show this help message\n");
}

/* 16x16 + 8x8 + 4x4 + 2x2 + 1x1 RGB565 mipmap chain = 341 texels */
static FxU16 g_mip_tex_data[341];

static void build_mip_texture(void) {
  int idx = 0;
  int x, y;
  /* Mip 0: 16x16, left half black (0x0000), right half white (0xFFFF) */
  for (y = 0; y < 16; ++y) {
    for (x = 0; x < 16; ++x) {
      g_mip_tex_data[idx++] = (x < 8) ? 0x0000U : 0xFFFFU;
    }
  }
  /* Mip 1: 8x8 solid green (0x07E0) */
  for (y = 0; y < 8 * 8; ++y) g_mip_tex_data[idx++] = 0x07E0U;
  /* Mip 2: 4x4 solid blue (0x001F) */
  for (y = 0; y < 4 * 4; ++y) g_mip_tex_data[idx++] = 0x001FU;
  /* Mip 3: 2x2 solid red (0xF800) */
  for (y = 0; y < 2 * 2; ++y) g_mip_tex_data[idx++] = 0xF800U;
  /* Mip 4: 1x1 solid yellow (0xFFE0) */
  g_mip_tex_data[idx++] = 0xFFE0U;
}

static void reset_base_state(int w, int h) {
  grClipWindow(0, 0, (FxU32)w, (FxU32)h);
  grCullMode(GR_CULL_DISABLE);
  grColorMask(FXTRUE, FXTRUE);
  grAlphaTestFunction(GR_CMP_ALWAYS);
  grAlphaTestReferenceValue(0);
  grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO,
                       GR_BLEND_ONE, GR_BLEND_ZERO);
  grChromakeyMode(GR_CHROMAKEY_DISABLE);
  grFogMode(GR_FOG_DISABLE);
  grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
  grDepthBufferFunction(GR_CMP_LESS);
  grDepthMask(FXFALSE);
  grDitherMode(GR_DITHER_DISABLE);
  grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
}

static void render_test_card(int w, int h, FxU32 tex_addr,
                             GrTexInfo* tex_info, int test_bounds) {
  float hw = (float)(w / 2);
  float hh = (float)(h / 2);
  float pad = 12.0f;
  int step;

  reset_base_state(w, h);
  grDepthMask(FXTRUE);
  grBufferClear(pack_abgr(16, 20, 28), 0xFF, GR_WDEPTHVALUE_FARTHEST);
  grDepthMask(FXFALSE);

  /* ------------------------------------------------------------------------
   * Quadrant TL: Texture Filter, Trilinear Mipmap & Texmap Disable
   * ------------------------------------------------------------------------ */
  grTexSource(GR_TMU0, tex_addr, GR_MIPMAPLEVELMASK_BOTH, tex_info);
  grTexCombine(GR_TMU0,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
                  GR_TEXTUREFILTER_POINT_SAMPLED);
  grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
  grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);
  grTexLodBiasValue(GR_TMU0, 0.0f);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

  /* TL-top: Magnified B/W seam quad tinted magenta (255, 0, 255) */
  draw_quad(pad, pad, hw - pad, hh * 0.68f, 1.0f,
            255.0f, 0.0f, 255.0f,
            255.0f, 0.0f, 255.0f,
            0.0f, 0.0f, 256.0f, 256.0f);

  /* TL-bottom: Minified mipmap strip (LOD ~1.4..2.0; Mip0=B/W, Mip1=G, Mip2=B) */
  grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
  draw_quad(pad, hh * 0.74f, hw - pad, hh - pad, 1.0f,
            255.0f, 255.0f, 255.0f,
            255.0f, 255.0f, 255.0f,
            0.0f, 0.0f, 256.0f * 48.0f, 256.0f * 8.0f);

  /* ------------------------------------------------------------------------
   * Quadrant TR: 16-bit 4x4 Dithering Probe (FX_GLIDE_NO_DITHER_SUB)
   * ------------------------------------------------------------------------ */
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  grDitherMode(GR_DITHER_4x4);

  /* TR-top: Smooth dark-to-mid RGB gradient (visual banding check) */
  draw_quad(hw + pad, pad, (float)w - pad, hh * 0.48f, 1.0f,
            24.0f, 36.0f, 52.0f,
            180.0f, 140.0f, 200.0f,
            0.0f, 0.0f, 0.0f, 0.0f);

  /* TR-bottom: Flat mid-tone (124, 122, 124) — halfway between 5-bit steps */
  draw_quad(hw + pad, hh * 0.54f, (float)w - pad, hh - pad, 1.0f,
            124.0f, 122.0f, 124.0f,
            124.0f, 122.0f, 124.0f,
            0.0f, 0.0f, 0.0f, 0.0f);

  grDitherMode(GR_DITHER_DISABLE);

  /* ------------------------------------------------------------------------
   * Quadrant BL: Depth/Aux Buffer (FX_GLIDE_ALLOC_AUX) + Gamma Step Wedge
   * ------------------------------------------------------------------------ */
  /* BL-left: W-buffered overlapping quads (Near Red drawn FIRST, Far Blue SECOND) */
  grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
  grDepthBufferFunction(GR_CMP_LESS);
  grDepthMask(FXTRUE);

  /* Near Red (oow=0.10 -> w=10) */
  draw_quad(pad, hh + pad, hw * 0.52f, (float)h - pad, 0.10f,
            240.0f, 32.0f, 32.0f,
            240.0f, 32.0f, 32.0f,
            0.0f, 0.0f, 0.0f, 0.0f);
  /* Far Blue (oow=0.01 -> w=100) overlapping center of Red quad */
  draw_quad(pad + 16.0f, hh + pad + 16.0f, hw * 0.52f - 16.0f,
            (float)h - pad - 16.0f, 0.01f,
            32.0f, 64.0f, 240.0f,
            32.0f, 64.0f, 240.0f,
            0.0f, 0.0f, 0.0f, 0.0f);

  grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
  grDepthMask(FXFALSE);

  /* BL-right: 5-step gray gamma calibration wedge */
  {
    float gx0 = hw * 0.56f;
    float gx1 = hw - pad;
    float gy0 = hh + pad;
    float gy1 = (float)h - pad;
    float band_h = (gy1 - gy0) / 5.0f;
    for (step = 0; step < 5; ++step) {
      float c = (float)(step * 64);
      if (c > 255.0f) c = 255.0f;
      draw_quad(gx0, gy0 + band_h * (float)step,
                gx1, gy0 + band_h * (float)(step + 1), 1.0f,
                c, c, c, c, c, c, 0.0f, 0.0f, 0.0f, 0.0f);
    }
  }

  /* ------------------------------------------------------------------------
   * Quadrant BR: Signature Block (Reopen probe) & Optional Bounds-Check Tri
   * ------------------------------------------------------------------------ */
  /* Dark panel + bright orange signature patch in center of BR */
  draw_quad(hw + pad, hh + pad, (float)w - pad, (float)h - pad, 1.0f,
            24.0f, 34.0f, 48.0f,
            24.0f, 34.0f, 48.0f,
            0.0f, 0.0f, 0.0f, 0.0f);
  draw_quad(hw + pad + 28.0f, hh + pad + 28.0f,
            (float)w - pad - 28.0f, (float)h - pad - 28.0f, 1.0f,
            255.0f, 160.0f, 0.0f,
            255.0f, 160.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f);

  if (test_bounds) {
    EnvVertex a, b, c;
    /* Place triangle completely off-screen so it triggers FX_GLIDE_BOUNDS_CHECK
     * without painting over any of the four probe quadrants. */
    set_vert(&a, (float)w + 80.0f,  (float)h + 60.0f,  1.0f, 255.0f, 255.0f, 0.0f, 0.0f, 0.0f);
    set_vert(&b, (float)w + 220.0f, (float)h + 60.0f,  1.0f, 255.0f, 255.0f, 0.0f, 0.0f, 0.0f);
    set_vert(&c, (float)w + 150.0f, (float)h + 190.0f, 1.0f, 255.0f, 255.0f, 0.0f, 0.0f, 0.0f);
    grDrawTriangle(&a, &b, &c);
  }
}

typedef struct ProbeSummary {
  int readback_ok;
  /* TL: left-half pixel (black if textured, magenta if SST_TEXMAP_DISABLE) */
  int tl_left_r, tl_left_g, tl_left_b;
  int texmap_disabled;
  /* TL: seam blended pixel count (0 if point-sampled, >0 if bilinear/trilinear) */
  int seam_blended_pixels;
  int bilinear_active;
  /* TL-bottom: minified strip average RGB (green/blue > red if trilinear active) */
  int mip_avg_r, mip_avg_g, mip_avg_b;
  int trilinear_active;
  /* TR-bottom: 16x16 flat dither swatch R5 min/max spread */
  int dither_r5_min, dither_r5_max;
  int dither_active;
  /* BL-left: depth overlap center pixel (red if aux=1, blue if aux=0) */
  int depth_r, depth_g, depth_b;
  int depth_active;
  /* BR: reopen signature test */
  int reopen_tested;
  int reopen_succeeded;
  int reopen_preserved;
  int reopen_after_r, reopen_after_g, reopen_after_b;
} ProbeSummary;

static void probe_backbuffer(int w, int h, ProbeSummary* out) {
  int hw = w / 2;
  int hh = h / 2;
  int pad = 12;
  FxU16 row_buf[128];
  FxU16 block_buf[16 * 16];
  FxU16 px = 0;
  int i;

  memset(out, 0, sizeof(*out));
  out->readback_ok = 1;

  /* 1. TL-left interior pixel -> detects SST_TEXMAP_DISABLE */
  {
    FxU32 sx = (FxU32)(pad + (hw - 2 * pad) / 4);
    FxU32 sy = (FxU32)(pad + (int)(hh * 0.30f));
    if (!grLfbReadRegion(GR_BUFFER_BACKBUFFER, sx, sy, 1, 1, 2, &px)) {
      out->readback_ok = 0;
      return;
    }
    unpack_rgb565(px, &out->tl_left_r, &out->tl_left_g, &out->tl_left_b);
    out->texmap_disabled = (out->tl_left_r > 200 && out->tl_left_b > 200 &&
                            out->tl_left_g < 40);
  }

  /* 2. TL horizontal scan across vertical seam -> detects SSTV2_FORCE_BILINEAR */
  {
    int quad_w = hw - 2 * pad;
    int seam_x = pad + quad_w / 2;
    int scan_w = 32;
    int start_x = seam_x - scan_w / 2;
    FxU32 sy = (FxU32)(pad + (int)(hh * 0.30f));
    if (start_x < pad) start_x = pad;
    if (grLfbReadRegion(GR_BUFFER_BACKBUFFER, (FxU32)start_x, sy,
                        (FxU32)scan_w, 1, (FxU32)(scan_w * 2), row_buf)) {
      int blended = 0;
      for (i = 0; i < scan_w; ++i) {
        int r, g, b;
        unpack_rgb565(row_buf[i], &r, &g, &b);
        if (r >= 24 && r <= 230 && b >= 24 && b <= 230) {
          blended++;
        }
      }
      out->seam_blended_pixels = blended;
      out->bilinear_active = (!out->texmap_disabled && blended >= 2);
    }
  }

  /* 3. TL-bottom minified strip -> detects SSTV2_FORCE_TRILINEAR */
  {
    FxU32 sx = (FxU32)(pad + 16);
    FxU32 sy = (FxU32)((int)(hh * 0.84f));
    int scan_w = 64;
    if (grLfbReadRegion(GR_BUFFER_BACKBUFFER, sx, sy, (FxU32)scan_w, 1,
                        (FxU32)(scan_w * 2), row_buf)) {
      long sr = 0, sg = 0, sb = 0;
      for (i = 0; i < scan_w; ++i) {
        int r, g, b;
        unpack_rgb565(row_buf[i], &r, &g, &b);
        sr += r; sg += g; sb += b;
      }
      out->mip_avg_r = (int)(sr / scan_w);
      out->mip_avg_g = (int)(sg / scan_w);
      out->mip_avg_b = (int)(sb / scan_w);
      /* Mip0 is B/W (R==G==B); Mip1 is Green, Mip2 is Blue -> G or B >> R */
      out->trilinear_active =
          (!out->texmap_disabled &&
           ((out->mip_avg_g > out->mip_avg_r + 35) ||
            (out->mip_avg_b > out->mip_avg_r + 35)));
    }
  }

  /* 4. TR-bottom 16x16 flat swatch -> detects FX_GLIDE_NO_DITHER_SUB */
  {
    FxU32 sx = (FxU32)(hw + pad + 24);
    FxU32 sy = (FxU32)((int)(hh * 0.65f));
    if (grLfbReadRegion(GR_BUFFER_BACKBUFFER, sx, sy, 16, 16, 16 * 2,
                        block_buf)) {
      int rmin = 31, rmax = 0;
      for (i = 0; i < 16 * 16; ++i) {
        int r5 = (int)((block_buf[i] >> 11) & 0x1FU);
        if (r5 < rmin) rmin = r5;
        if (r5 > rmax) rmax = r5;
      }
      out->dither_r5_min = rmin;
      out->dither_r5_max = rmax;
      out->dither_active = (rmax > rmin);
    }
  }

  /* 5. BL-left depth overlap center -> detects FX_GLIDE_ALLOC_AUX=0 */
  {
    FxU32 sx = (FxU32)(pad + (int)(hw * 0.25f));
    FxU32 sy = (FxU32)(hh + hh / 2);
    if (grLfbReadRegion(GR_BUFFER_BACKBUFFER, sx, sy, 1, 1, 2, &px)) {
      unpack_rgb565(px, &out->depth_r, &out->depth_g, &out->depth_b);
      out->depth_active = (out->depth_r > 180 && out->depth_b < 80);
    }
  }
}

static void print_active_env_vars(void) {
  size_t i;
  size_t count = sizeof(kGlideKnownEnvVars) / sizeof(kGlideKnownEnvVars[0]);
  int printed = 0;
  (void)kGlideEnvScanPrefixes;
  printf(" Active Env Vars:\n");
  for (i = 0; i < count; ++i) {
    const char* val = getenv(kGlideKnownEnvVars[i].envName);
    if (val && (val[0] || strcmp(kGlideKnownEnvVars[i].envName,
                                 GLIDE_FX_ENV_REPORT_REAL_HW) == 0)) {
      printf("   %-28s = %s  [%s]\n",
             kGlideKnownEnvVars[i].envName, val[0] ? val : "(empty)",
             kGlideKnownEnvVars[i].isAuthentic3dfx ? "3dfx" : "glide-ng");
      printed++;
    }
  }
  if (!printed) {
    printf("   (none set in environment; using defaults / glide-ng.ini)\n");
  }
}

int main(int argc, char** argv) {
  GrScreenResolution_t res_enum = GR_RESOLUTION_640x480;
  int width = 640;
  int height = 480;
  int verify_mode = 0;
  int target_frames = 0;
  double target_seconds = 0.0;
  int test_reopen = 0;
  int test_bounds = 0;
  int run_splash = 0;
  int req_plug = 0;
  int pause_on_exit = 0;
  int i;

  char runtime_source[64];
  char loader_err[192];
  char glide_ver[80];
  int fb_ram_mb = 0;
  int tmu_count = 0;
  int tmu_ram_mb = 0;
  FxU32 tmu0_min = 0, tmu0_max = 0, tmu1_max = 0;
  GrTexInfo tex_info;
  ProbeSummary probe;
  double t_start = 0.0, t_end = 0.0;
  int frames = 0;
#if GLIDE_VERSION == 3
  GrContext_t ctx = 0;
#endif

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--verify") == 0) {
      verify_mode = 1;
    } else if (strcmp(argv[i], "--test-reopen") == 0) {
      test_reopen = 1;
    } else if (strcmp(argv[i], "--test-bounds") == 0) {
      test_bounds = 1;
    } else if (strcmp(argv[i], "--splash") == 0) {
      run_splash = 1;
    } else if (strcmp(argv[i], "--shameless-plug") == 0) {
      req_plug = 1;
    } else if (strcmp(argv[i], "--pause") == 0) {
      pause_on_exit = 1;
    } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      target_frames = atoi(argv[++i]);
      if (target_frames < 1) target_frames = 1;
    } else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
      target_seconds = atof(argv[++i]);
      if (target_seconds < 0.1) target_seconds = 0.1;
    } else if (strcmp(argv[i], "--res") == 0 && i + 1 < argc) {
      if (!parse_res_arg(argv[++i], &res_enum, &width, &height)) {
        fprintf(stderr, "ERROR: Invalid resolution '%s'\n", argv[i]);
        return 1;
      }
    } else {
      fprintf(stderr, "ERROR: Unknown option '%s' (use --help)\n", argv[i]);
      return 1;
    }
  }

  if (verify_mode && target_frames <= 0) {
    target_frames = 5;
  }

  runtime_source[0] = '\0';
  loader_err[0] = '\0';
  if (!gtool_glide_load(runtime_source, sizeof(runtime_source),
                        loader_err, sizeof(loader_err))) {
    fprintf(stderr, "ERROR: %s\n", loader_err[0] ? loader_err : "Failed to load Glide");
    return 1;
  }

  grGlideInit();

  /* Query hardware configuration before window open */
  glide_ver[0] = '\0';
#if GLIDE_VERSION == 3
  {
    const char* vstr = grGetString(GR_VERSION);
    FxI32 v = 0;
    snprintf(glide_ver, sizeof(glide_ver), "%s", vstr ? vstr : "Glide 3.x");
    if (grGet(GR_MEMORY_FB, 4, &v) >= 4) fb_ram_mb = (int)(v / (1024 * 1024));
    if (grGet(GR_MEMORY_TMU, 4, &v) >= 4) tmu_ram_mb = (int)(v / (1024 * 1024));
    if (grGet(GR_NUM_TMU, 4, &v) >= 4) tmu_count = (int)v;
  }
#else
  {
    GrHwConfiguration hw;
    memset(&hw, 0, sizeof(hw));
    grGlideGetVersion(glide_ver);
    if (grSstQueryHardware(&hw) && hw.num_sst > 0) {
      if (hw.SSTs[0].type == GR_SSTTYPE_VOODOO) {
        fb_ram_mb = hw.SSTs[0].sstBoard.VoodooConfig.fbRam;
        tmu_count = hw.SSTs[0].sstBoard.VoodooConfig.nTexelfx;
        tmu_ram_mb = hw.SSTs[0].sstBoard.VoodooConfig.tmuConfig[0].tmuRam;
      } else if (hw.SSTs[0].type == GR_SSTTYPE_SST96) {
        fb_ram_mb = hw.SSTs[0].sstBoard.SST96Config.fbRam;
        tmu_count = hw.SSTs[0].sstBoard.SST96Config.nTexelfx;
        tmu_ram_mb = hw.SSTs[0].sstBoard.SST96Config.tmuConfig.tmuRam;
      } else {
        fb_ram_mb = hw.SSTs[0].sstBoard.Voodoo2Config.fbRam;
        tmu_count = hw.SSTs[0].sstBoard.Voodoo2Config.nTexelfx;
        tmu_ram_mb = hw.SSTs[0].sstBoard.Voodoo2Config.tmuConfig[0].tmuRam;
      }
    }
    grSstSelect(0);
  }
#endif

  tmu0_min = grTexMinAddress(GR_TMU0);
  tmu0_max = grTexMaxAddress(GR_TMU0);
  tmu1_max = grTexMaxAddress(GR_TMU1);

#if GLIDE_VERSION == 3
  ctx = grSstWinOpen(0, res_enum, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR,
                     GR_ORIGIN_UPPER_LEFT, 2, 1);
  if (!ctx) {
    fprintf(stderr, "ERROR: grSstWinOpen (Glide 3.x) failed\n");
    grGlideShutdown();
    gtool_glide_unload();
    return 1;
  }
  init_vertex_layout();
#else
  if (!grSstWinOpen(0, res_enum, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR,
                    GR_ORIGIN_UPPER_LEFT, 2, 1)) {
    fprintf(stderr, "ERROR: grSstWinOpen (Glide 2.x) failed\n");
    grGlideShutdown();
    gtool_glide_unload();
    return 1;
  }
#endif

  /* Deliberately request linear gamma 1.0f so SST_GAMMA / SSTV2_GAMMA can be
   * verified to override application gamma calls. */
#if GLIDE_VERSION == 3
  {
    FxU32 ramp[256];
    for (i = 0; i < 256; ++i) ramp[i] = (FxU32)i;
    grLoadGammaTable(256, ramp, ramp, ramp);
  }
  if (req_plug) {
    grEnable(GR_SHAMELESS_PLUG);
  }
  if (run_splash) {
    grSplash(0.0f, 0.0f, (float)width, (float)height, 0);
  }
#else
#  if defined(__DOS__) || defined(__MSDOS__) || defined(_WIN32)
  if (grGammaCorrectionValue) grGammaCorrectionValue(1.0f);
  if (grGlideShamelessPlug) grGlideShamelessPlug(req_plug ? FXTRUE : FXFALSE);
  if (run_splash && grSplash) {
    grSplash(0.0f, 0.0f, (float)width, (float)height, 0);
  }
#  else
  grGammaCorrectionValue(1.0f);
  grGlideShamelessPlug(req_plug ? FXTRUE : FXFALSE);
  if (run_splash) {
    grSplash(0.0f, 0.0f, (float)width, (float)height, 0);
  }
#  endif
#endif

  build_mip_texture();
  init_tex_info(&tex_info, g_mip_tex_data);
  grTexDownloadMipMap(GR_TMU0, tmu0_min, GR_MIPMAPLEVELMASK_BOTH, &tex_info);

  memset(&probe, 0, sizeof(probe));
  gtool_console_begin();
  t_start = gtool_time_sec();

  while (1) {
    int key;
    render_test_card(width, height, tmu0_min, &tex_info,
                     (test_bounds && frames == 0));

    if (frames == 0) {
      probe_backbuffer(width, height, &probe);
    }

    grBufferSwap(0);
    frames++;

    /* Optional redundant grSstWinOpen test right after frame 0 */
    if (frames == 1 && test_reopen) {
      FxU32 br_x = (FxU32)(width / 2 + width / 4);
      FxU32 br_y = (FxU32)(height / 2 + height / 4);
      FxU16 after_px = 0;

      /* Paint bright orange signature directly into current backbuffer */
      render_test_card(width, height, tmu0_min, &tex_info, 0);
      probe.reopen_tested = 1;
#if GLIDE_VERSION == 3
      {
        GrContext_t ctx2 = grSstWinOpen(0, res_enum, GR_REFRESH_60Hz,
                                        GR_COLORFORMAT_ABGR,
                                        GR_ORIGIN_UPPER_LEFT, 2, 1);
        probe.reopen_succeeded = (ctx2 != 0);
        if (ctx2) {
          ctx = ctx2;
          init_vertex_layout();
        }
      }
#else
      probe.reopen_succeeded =
          (grSstWinOpen(0, res_enum, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR,
                        GR_ORIGIN_UPPER_LEFT, 2, 1) == FXTRUE);
#endif
      if (grLfbReadRegion(GR_BUFFER_BACKBUFFER, br_x, br_y, 1, 1, 2,
                          &after_px)) {
        unpack_rgb565(after_px, &probe.reopen_after_r,
                      &probe.reopen_after_g, &probe.reopen_after_b);
        /* Orange signature: R >= 220, G in 110..200, B <= 40 */
        probe.reopen_preserved =
            (probe.reopen_after_r >= 220 &&
             probe.reopen_after_g >= 110 && probe.reopen_after_g <= 205 &&
             probe.reopen_after_b <= 40);
      }
      /* Re-upload mipmap texture in case reopen actually recreated the context */
      grTexDownloadMipMap(GR_TMU0, tmu0_min, GR_MIPMAPLEVELMASK_BOTH, &tex_info);
    }

    key = gtool_poll_key();
    if (key == 27 || key == 'q' || key == 'Q') {
      break;
    }
    if (target_frames > 0 && frames >= target_frames) {
      break;
    }
    if (target_seconds > 0.0 && (gtool_time_sec() - t_start) >= target_seconds) {
      break;
    }
  }

  t_end = gtool_time_sec();
  gtool_console_end();

#if GLIDE_VERSION == 3
  grSstWinClose(ctx);
#else
  grSstWinClose();
#endif
  grGlideShutdown();
  gtool_glide_unload();

  {
    double elapsed = t_end - t_start;
    double avg_fps = (elapsed > 1e-6) ? ((double)frames / elapsed) : 0.0;
    printf("==============================================================================\n");
    printf(" glide_envtest - 3dfx Glide %d.x EnvVar Verifier (%dx%d)   [%s]\n",
           GLIDE_VERSION, width, height, gtool_platform_name());
    printf("==============================================================================\n");
    printf(" Runtime  : %s  |  Glide: %s\n", runtime_source, glide_ver);
    printf(" Hardware : FBI=%d MB, TMUs=%d, TMU_RAM=%d MB (TMU0=0x%lX, TMU1=0x%lX)\n",
           fb_ram_mb, tmu_count, tmu_ram_mb,
           (unsigned long)tmu0_max, (unsigned long)tmu1_max);
    print_active_env_vars();
    printf("------------------------------------------------------------------------------\n");
    printf(" Probe Verdicts (LFB RGB565 Readback):\n");
    if (!probe.readback_ok) {
      printf("   ERROR: grLfbReadRegion(GR_BUFFER_BACKBUFFER) failed!\n");
    } else {
      printf("   Texmap    : %-26s (TL-left RGB=%d,%d,%d)\n",
             probe.texmap_disabled ? "DISABLED (SST_TEXMAP_DISABLE)"
                                   : "ENABLED (normal texture)",
             probe.tl_left_r, probe.tl_left_g, probe.tl_left_b);
      printf("   Filtering : %-26s (seam blended px=%d)\n",
             probe.texmap_disabled
                 ? "N/A (texmap disabled)"
                 : (probe.bilinear_active
                        ? "BILINEAR+ (forced)"
                        : "POINT_SAMPLED (app default)"),
             probe.seam_blended_pixels);
      printf("   Mipmap    : %-26s (strip avg RGB=%d,%d,%d)\n",
             probe.texmap_disabled
                 ? "N/A (texmap disabled)"
                 : (probe.trilinear_active
                        ? "TRILINEAR (forced Mip1/2)"
                        : "DISABLED (Mip0 B/W)"),
             probe.mip_avg_r, probe.mip_avg_g, probe.mip_avg_b);
      printf("   Dithering : %-26s (R5 range=%d..%d)\n",
             probe.dither_active ? "4x4 ACTIVE (app default)"
                                 : "DISABLED (NO_DITHER_SUB)",
             probe.dither_r5_min, probe.dither_r5_max);
      printf("   Depth/Aux : %-26s (overlap RGB=%d,%d,%d)\n",
             probe.depth_active ? "ACTIVE (near red wins)"
                                : "DISABLED (ALLOC_AUX=0)",
             probe.depth_r, probe.depth_g, probe.depth_b);
      if (probe.reopen_tested) {
        printf("   Reopen    : %-26s (ok=%d, post RGB=%d,%d,%d)\n",
               probe.reopen_preserved ? "IGNORED (buffer preserved)"
                                      : "RECREATED (buffer reset)",
               probe.reopen_succeeded,
               probe.reopen_after_r, probe.reopen_after_g,
               probe.reopen_after_b);
      }
      if (test_bounds) {
        printf("   BoundsChk : SUBMITTED out-of-bounds tri (see [BoundsCheck] in glide-ng.log)\n");
      }
    }
    printf("   Timing    : %d frames in %.3fs (%.1f FPS)\n",
           frames, elapsed, avg_fps);
    printf("==============================================================================\n");
  }

  if (pause_on_exit) {
    gtool_wait_keypress();
  }
  return probe.readback_ok ? 0 : 1;
}

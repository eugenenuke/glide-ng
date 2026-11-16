/*
 * glide_bench.h - Modular 3dfx Glide 2.x / 3.x Benchmark Suite
 *
 * Pure C99, 100% authentic Glide API calls.
 * Builds identically for:
 *   - Linux:   glide2_bench (libglide2x.so), glide3_bench (libglide3x.so)
 *   - DOS/32:  glide_bench.exe (Open Watcom v2 + CauseWay + GLIDE2X.OVL)
 *   - Windows: glide_bench.exe (Win32 dynamic glide2x.dll)
 *
 * Adding a new benchmark scene:
 *   1. Implement setup / render_frame / teardown in bench_scenes.c
 *   2. Add ONE entry to kBenchScenes[] in bench_scenes.c
 */
#ifndef GLIDE_BENCH_H
#define GLIDE_BENCH_H

#include "glide_tool_platform.h"
#include "glide2_dos_ovl.h"

#include <math.h>
#include <stddef.h>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

#ifndef GR_WDEPTHVALUE_FARTHEST
#  define GR_WDEPTHVALUE_FARTHEST 0xFFFF
#endif
#ifndef GR_ZDEPTHVALUE_FARTHEST
#  define GR_ZDEPTHVALUE_FARTHEST 0x0000
#endif

#if GLIDE_VERSION == 3
#  define BENCH_FOG_TABLE_MODE GR_FOG_WITH_TABLE_ON_Q
#  define BENCH_LOD_256        GR_LOD_LOG2_256
#  define BENCH_LOD_128        GR_LOD_LOG2_128
#  define BENCH_LOD_64         GR_LOD_LOG2_64
#  define BENCH_LOD_32         GR_LOD_LOG2_32
#  define BENCH_LOD_16         GR_LOD_LOG2_16
#  define BENCH_LOD_8          GR_LOD_LOG2_8
#  define BENCH_ASPECT_1x1     GR_ASPECT_LOG2_1x1
#  define BENCH_ASPECT_2x1     GR_ASPECT_LOG2_2x1
#else
#  define BENCH_FOG_TABLE_MODE GR_FOG_WITH_TABLE
#  define BENCH_LOD_256        GR_LOD_256
#  define BENCH_LOD_128        GR_LOD_128
#  define BENCH_LOD_64         GR_LOD_64
#  define BENCH_LOD_32         GR_LOD_32
#  define BENCH_LOD_16         GR_LOD_16
#  define BENCH_LOD_8          GR_LOD_8
#  define BENCH_ASPECT_1x1     GR_ASPECT_1x1
#  define BENCH_ASPECT_2x1     GR_ASPECT_2x1
#endif

/* --- Zero-overhead vertex type for both Glide 2.x and Glide 3.x --- */
#if GLIDE_VERSION == 3
typedef struct BenchVertex {
  float x, y;
  float ooz;
  float oow;
  float r, g, b, a;
  float s0, t0;
} BenchVertex;
#else
typedef GrVertex BenchVertex;
#endif

static inline void bench_set_vert(BenchVertex* v,
                                  float x, float y,
                                  float ooz, float oow,
                                  float r, float g, float b, float a,
                                  float sow, float tow) {
#if GLIDE_VERSION == 3
  v->x = x;
  v->y = y;
  v->ooz = ooz;
  v->oow = oow;
  v->r = r;
  v->g = g;
  v->b = b;
  v->a = a;
  v->s0 = sow;
  v->t0 = tow;
#else
  v->x = x;
  v->y = y;
  v->z = 0.0f;
  v->r = r;
  v->g = g;
  v->b = b;
  v->ooz = ooz;
  v->a = a;
  v->oow = oow;
  v->tmuvtx[0].sow = sow;
  v->tmuvtx[0].tow = tow;
  v->tmuvtx[0].oow = oow;
#if GLIDE_NUM_TMU > 1
  v->tmuvtx[1].sow = sow;
  v->tmuvtx[1].tow = tow;
  v->tmuvtx[1].oow = oow;
#endif
#endif
}

static inline void bench_init_vertex_layout(void) {
#if GLIDE_VERSION == 3
  grVertexLayout(GR_PARAM_XY,  (FxI32)offsetof(BenchVertex, x),   GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_Z,   (FxI32)offsetof(BenchVertex, ooz), GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_Q,   (FxI32)offsetof(BenchVertex, oow), GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_RGB, (FxI32)offsetof(BenchVertex, r),   GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_A,   (FxI32)offsetof(BenchVertex, a),   GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_ST0, (FxI32)offsetof(BenchVertex, s0),  GR_PARAM_ENABLE);
#endif
}

static inline void bench_init_tex_info(GrTexInfo* info,
                                       GrLOD_t small_lod,
                                       GrLOD_t large_lod,
                                       GrAspectRatio_t aspect,
                                       GrTextureFormat_t fmt,
                                       void* data) {
  memset(info, 0, sizeof(*info));
#if GLIDE_VERSION == 3
  info->smallLodLog2 = small_lod;
  info->largeLodLog2 = large_lod;
  info->aspectRatioLog2 = aspect;
#else
  info->smallLod = small_lod;
  info->largeLod = large_lod;
  info->aspectRatio = aspect;
#endif
  info->format = fmt;
  info->data = data;
}

static inline void bench_download_palette(FxU32 palette[256]) {
#if GLIDE_VERSION == 3
  grTexDownloadTable(GR_TEXTABLE_PALETTE, palette);
#else
  grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, palette);
#endif
}

static inline void bench_draw_quad(const BenchVertex* tl,
                                   const BenchVertex* tr,
                                   const BenchVertex* br,
                                   const BenchVertex* bl) {
  grDrawTriangle(tl, tr, br);
  grDrawTriangle(tl, br, bl);
}

static inline void bench_reset_pipeline(int width, int height) {
  grClipWindow(0, 0, (FxU32)width, (FxU32)height);
  grCullMode(GR_CULL_DISABLE);
  grDitherMode(GR_DITHER_4x4);
  grColorMask(FXTRUE, FXTRUE);
  grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
  grDepthBufferFunction(GR_CMP_LESS);
  grDepthMask(FXFALSE);
  grAlphaTestFunction(GR_CMP_ALWAYS);
  grAlphaTestReferenceValue(0);
  grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO,
                       GR_BLEND_ONE, GR_BLEND_ZERO);
  grChromakeyMode(GR_CHROMAKEY_DISABLE);
  grFogMode(GR_FOG_DISABLE);
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR,
                  GR_TEXTUREFILTER_BILINEAR);
  grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
  grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);
}

/* --- Shared benchmark context passed to scenes & HUD --- */
typedef struct BenchContext {
  int width;
  int height;
  FxU32 tmu0_min;
  FxU32 tmu0_max;
  FxU32 scene_tmu_limit; /* Top address available to scenes (below HUD atlas) */
  int num_tmus;
  int hud_enabled;
  FxU32 hud_tex_addr;
  GrTexInfo hud_tex_info;
  void* scene_state;
} BenchContext;

/* --- Modular Scene Descriptor (add a new test by appending to kBenchScenes) --- */
typedef struct BenchScene {
  const char* name;
  const char* title;
  const char* metric_unit;
  double      score_weight;
  double      ref_fps;      /* 30.0 FPS = 1000 GlideMark pts */
  int  (*setup)(BenchContext* ctx);
  void (*render_frame)(BenchContext* ctx, int frame_idx, double t_sec,
                       double* out_work_units);
  void (*teardown)(BenchContext* ctx);
} BenchScene;

/* Scene registry (implemented in bench_scenes.c) */
extern const BenchScene* bench_get_scenes(int* out_count);

/* Pure-Glide textured-quad HUD (implemented in bench_hud.c; never touches LFB) */
extern int  bench_hud_init(BenchContext* ctx);
extern void bench_hud_draw(BenchContext* ctx,
                           const char* scene_name,
                           int scene_idx,
                           int scene_total,
                           double inst_fps,
                           double avg_ms,
                           double elapsed_s,
                           double target_s);

#endif /* GLIDE_BENCH_H */

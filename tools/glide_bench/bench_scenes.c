/*
 * bench_scenes.c - Modular Scene Registry for glide_bench
 *
 * All 6 scenes precompute geometry & textures in setup() to keep guest CPU
 * math near-zero (critical under DOSBox) so measurements isolate Glide
 * rendering, state dispatch, texture streaming, and LFB throughput.
 *
 * Adding a new benchmark test:
 *   1. Implement setup / render_frame / teardown below.
 *   2. Append ONE BenchScene row to kBenchScenes[] at the bottom of this file.
 */

#include "glide_bench.h"

/* =========================================================================
 * Helper: Procedural texture generators
 * ========================================================================= */

static void gen_checker_rgb565(FxU16* dst, int size, int tile,
                               FxU16 c0, FxU16 c1, int add_chroma_holes) {
  int y, x;
  for (y = 0; y < size; ++y) {
    for (x = 0; x < size; ++x) {
      int chk = (((x / tile) ^ (y / tile)) & 1);
      FxU16 col = chk ? c0 : c1;
      if (add_chroma_holes && ((x & 15) >= 6 && (x & 15) <= 9) &&
          ((y & 15) >= 6 && (y & 15) <= 9)) {
        col = 0xF81FU; /* Magenta chroma key (RGB565) */
      }
      dst[y * size + x] = col;
    }
  }
}

static void gen_radial_rgb565(FxU16* dst, int size, int phase) {
  int y, x;
  float half = (float)size * 0.5f;
  for (y = 0; y < size; ++y) {
    for (x = 0; x < size; ++x) {
      float dx = ((float)x - half) / half;
      float dy = ((float)y - half) / half;
      float d = (float)sqrt((double)(dx * dx + dy * dy));
      float v = 1.0f - d;
      unsigned r5, g6, b5;
      if (v < 0.0f) v = 0.0f;
      if (phase == 0) {
        r5 = (unsigned)(v * 31.0f);
        g6 = (unsigned)(v * v * 58.0f);
        b5 = (unsigned)(v * 20.0f);
      } else {
        r5 = (unsigned)(v * 16.0f);
        g6 = (unsigned)(v * 52.0f);
        b5 = (unsigned)(v * 31.0f);
      }
      dst[y * size + x] = (FxU16)((r5 << 11) | (g6 << 5) | b5);
    }
  }
}

/* =========================================================================
 * Scene 1: fillrate - 16x full-screen textured alpha-blended overdraw
 * ========================================================================= */
#define FILL_LAYERS 16

typedef struct FillrateState {
  FxU16 tex[128 * 128];
  GrTexInfo ti;
  FxU32 tex_addr;
  BenchVertex layers[FILL_LAYERS][4];
} FillrateState;

static int fillrate_setup(BenchContext* ctx) {
  FillrateState* st = (FillrateState*)calloc(1, sizeof(FillrateState));
  int i;
  float w = (float)ctx->width;
  float h = (float)ctx->height;
  if (!st) return 0;

  gen_checker_rgb565(st->tex, 128, 16, 0x3A9FU, 0x118CU, 0);
  bench_init_tex_info(&st->ti, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_RGB_565, st->tex);
  st->tex_addr = ctx->tmu0_min;
  grTexDownloadMipMap(GR_TMU0, st->tex_addr, GR_MIPMAPLEVELMASK_BOTH, &st->ti);

  for (i = 0; i < FILL_LAYERS; ++i) {
    float r = 140.0f + (float)((i * 29) & 111);
    float g = 140.0f + (float)((i * 53) & 111);
    float b = 140.0f + (float)((i * 71) & 111);
    float a = 96.0f;
    float u0 = (float)(i * 8);
    float v0 = (float)(i * 6);
    bench_set_vert(&st->layers[i][0], 0.0f, 0.0f, 0.0f, 1.0f, r, g, b, a, u0,          v0);
    bench_set_vert(&st->layers[i][1], w,    0.0f, 0.0f, 1.0f, r, g, b, a, u0 + 255.0f, v0);
    bench_set_vert(&st->layers[i][2], w,    h,    0.0f, 1.0f, r, g, b, a, u0 + 255.0f, v0 + 255.0f);
    bench_set_vert(&st->layers[i][3], 0.0f, h,    0.0f, 1.0f, r, g, b, a, u0,          v0 + 255.0f);
  }

  ctx->scene_state = st;
  return 1;
}

static void fillrate_render(BenchContext* ctx, int frame_idx, double t_sec,
                            double* out_work_units) {
  FillrateState* st = (FillrateState*)ctx->scene_state;
  int i;
  float uv_shift = (float)((frame_idx * 3) & 255);
  (void)t_sec;

  bench_reset_pipeline(ctx->width, ctx->height);
  grBufferClear(0x00101828UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);

  grTexSource(GR_TMU0, st->tex_addr, GR_MIPMAPLEVELMASK_BOTH, &st->ti);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
  grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                       GR_BLEND_ONE, GR_BLEND_ZERO);

  for (i = 0; i < FILL_LAYERS; ++i) {
    BenchVertex q[4];
    int k;
    memcpy(q, st->layers[i], sizeof(q));
    for (k = 0; k < 4; ++k) {
#if GLIDE_VERSION == 3
      q[k].s0 += uv_shift;
      q[k].t0 += uv_shift * 0.5f;
#else
      q[k].tmuvtx[0].sow += uv_shift;
      q[k].tmuvtx[0].tow += uv_shift * 0.5f;
#endif
    }
    bench_draw_quad(&q[0], &q[1], &q[2], &q[3]);
  }

  *out_work_units =
      ((double)FILL_LAYERS * (double)ctx->width * (double)ctx->height) / 1e6;
}

static void fillrate_teardown(BenchContext* ctx) {
  free(ctx->scene_state);
  ctx->scene_state = NULL;
}

/* =========================================================================
 * Scene 2: tris - 4,050-triangle precomputed 3D waving mesh (1 uniform batch)
 * ========================================================================= */
#define MESH_N      45  /* 45x45 quads = 4,050 triangles per frame */
#define MESH_PHASES 8   /* Precomputed animation ring -> 0 trig per frame! */
#define MESH_VERTS  ((MESH_N + 1) * (MESH_N + 1))

typedef struct TrisState {
  FxU16 tex[128 * 128];
  GrTexInfo ti;
  FxU32 tex_addr;
  BenchVertex* phases[MESH_PHASES];
} TrisState;

static int tris_setup(BenchContext* ctx) {
  TrisState* st = (TrisState*)calloc(1, sizeof(TrisState));
  int p, gy, gx;
  float w = (float)ctx->width;
  float h = (float)ctx->height;
  if (!st) return 0;

  gen_checker_rgb565(st->tex, 128, 16, 0xD69AU, 0x4A69U, 0);
  bench_init_tex_info(&st->ti, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_RGB_565, st->tex);
  st->tex_addr = ctx->tmu0_min;
  grTexDownloadMipMap(GR_TMU0, st->tex_addr, GR_MIPMAPLEVELMASK_BOTH, &st->ti);

  for (p = 0; p < MESH_PHASES; ++p) {
    float phase_rad = (float)p * (2.0f * (float)M_PI / (float)MESH_PHASES);
    st->phases[p] = (BenchVertex*)calloc(MESH_VERTS, sizeof(BenchVertex));
    if (!st->phases[p]) {
      int k;
      for (k = 0; k < p; ++k) free(st->phases[k]);
      free(st);
      return 0;
    }
    for (gy = 0; gy <= MESH_N; ++gy) {
      for (gx = 0; gx <= MESH_N; ++gx) {
        float u = (float)gx / (float)MESH_N;
        float v = (float)gy / (float)MESH_N;
        float wave = (float)(sin((double)(u * 12.0f + phase_rad)) *
                             cos((double)(v * 10.0f - phase_rad)));
        float z_cam = 2.4f + v * 3.0f + wave * 0.25f;
        float oow = 1.0f / z_cam;
        float ooz = 65535.0f * oow;
        float sx = (0.06f + u * 0.88f) * w;
        float sy = (0.14f + v * 0.78f - wave * 0.035f) * h;
        float shade = 145.0f + wave * 95.0f;
        float r = shade * (0.65f + 0.35f * u);
        float g = shade * (0.55f + 0.45f * (1.0f - v));
        float b = shade * (0.75f + 0.25f * v);
        float sow = (u * 255.0f) * oow;
        float tow = (v * 255.0f) * oow;
        bench_set_vert(&st->phases[p][gy * (MESH_N + 1) + gx],
                       sx, sy, ooz, oow, r, g, b, 255.0f, sow, tow);
      }
    }
  }

  ctx->scene_state = st;
  return 1;
}

static void tris_render(BenchContext* ctx, int frame_idx, double t_sec,
                        double* out_work_units) {
  TrisState* st = (TrisState*)ctx->scene_state;
  const BenchVertex* verts = st->phases[(frame_idx >> 1) & (MESH_PHASES - 1)];
  int gy, gx;
  (void)t_sec;

  bench_reset_pipeline(ctx->width, ctx->height);
  grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
  grDepthBufferFunction(GR_CMP_LESS);
  grDepthMask(FXTRUE);
  grBufferClear(0x00141824UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);

  grTexSource(GR_TMU0, st->tex_addr, GR_MIPMAPLEVELMASK_BOTH, &st->ti);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

  for (gy = 0; gy < MESH_N; ++gy) {
    int row0 = gy * (MESH_N + 1);
    int row1 = row0 + (MESH_N + 1);
    for (gx = 0; gx < MESH_N; ++gx) {
      const BenchVertex* v00 = &verts[row0 + gx];
      const BenchVertex* v10 = &verts[row0 + gx + 1];
      const BenchVertex* v11 = &verts[row1 + gx + 1];
      const BenchVertex* v01 = &verts[row1 + gx];
      grDrawTriangle(v00, v10, v11);
      grDrawTriangle(v00, v11, v01);
    }
  }

  *out_work_units = (double)(MESH_N * MESH_N * 2) / 1000.0; /* KTri */
}

static void tris_teardown(BenchContext* ctx) {
  TrisState* st = (TrisState*)ctx->scene_state;
  int p;
  if (st) {
    for (p = 0; p < MESH_PHASES; ++p) free(st->phases[p]);
    free(st);
  }
  ctx->scene_state = NULL;
}

/* =========================================================================
 * Scene 3: state - 120 small state-switching batches (~1,200 tris/frame)
 * ========================================================================= */
#define STATE_BATCHES      120
#define STATE_TRIS_PER_B   10
#define STATE_NUM_TEX      4

typedef struct StateBenchState {
  FxU16 tex[STATE_NUM_TEX][64 * 64];
  GrTexInfo ti[STATE_NUM_TEX];
  FxU32 tex_addr[STATE_NUM_TEX];
  BenchVertex tris[STATE_BATCHES][STATE_TRIS_PER_B][3];
} StateBenchState;

static int state_setup(BenchContext* ctx) {
  static const FxU16 kPalA[STATE_NUM_TEX] = { 0xF800U, 0x07E0U, 0x001FU, 0xFFE0U };
  static const FxU16 kPalB[STATE_NUM_TEX] = { 0x7800U, 0x03E0U, 0x0010U, 0x7BE0U };
  StateBenchState* st = (StateBenchState*)calloc(1, sizeof(StateBenchState));
  FxU32 addr = ctx->tmu0_min;
  int i, t;
  float w = (float)ctx->width;
  float h = (float)ctx->height;
  if (!st) return 0;

  for (i = 0; i < STATE_NUM_TEX; ++i) {
    FxU32 req;
    gen_checker_rgb565(st->tex[i], 64, 8, kPalA[i], kPalB[i], (i & 1));
    bench_init_tex_info(&st->ti[i], BENCH_LOD_64, BENCH_LOD_64,
                        BENCH_ASPECT_1x1, GR_TEXFMT_RGB_565, st->tex[i]);
    req = grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &st->ti[i]);
    st->tex_addr[i] = addr;
    grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &st->ti[i]);
    addr = (addr + req + 7U) & ~7U;
  }

  for (i = 0; i < STATE_BATCHES; ++i) {
    int col = i % 12;
    int row = i / 12;
    float cx = ((float)col + 0.5f) * (w / 12.0f);
    float cy = 56.0f + ((float)row + 0.5f) * ((h - 64.0f) / 10.0f);
    float rad = (w / 26.0f);
    for (t = 0; t < STATE_TRIS_PER_B; ++t) {
      float a0 = (float)t * (2.0f * (float)M_PI / (float)STATE_TRIS_PER_B);
      float a1 = (float)(t + 1) * (2.0f * (float)M_PI / (float)STATE_TRIS_PER_B);
      float r = 120.0f + (float)((i * 37) & 127);
      float g = 120.0f + (float)((i * 61) & 127);
      float b = 120.0f + (float)((i * 89) & 127);
      float oow = 0.25f + (float)(i & 7) * 0.04f;
      float ooz = 65535.0f * oow;
      bench_set_vert(&st->tris[i][t][0], cx, cy, ooz, oow, r, g, b, 190.0f,
                     128.0f * oow, 128.0f * oow);
      bench_set_vert(&st->tris[i][t][1],
                     cx + (float)cos((double)a0) * rad,
                     cy + (float)sin((double)a0) * rad,
                     ooz, oow, r, g, b, 190.0f, 255.0f * oow, 0.0f);
      bench_set_vert(&st->tris[i][t][2],
                     cx + (float)cos((double)a1) * rad,
                     cy + (float)sin((double)a1) * rad,
                     ooz, oow, r, g, b, 190.0f, 0.0f, 255.0f * oow);
    }
  }

  ctx->scene_state = st;
  return 1;
}

static void state_render(BenchContext* ctx, int frame_idx, double t_sec,
                         double* out_work_units) {
  StateBenchState* st = (StateBenchState*)ctx->scene_state;
  int i, t;
  (void)t_sec;

  bench_reset_pipeline(ctx->width, ctx->height);
  grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
  grBufferClear(0x00181220UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);
  grChromakeyValue(0x00FF00FFUL);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);

  for (i = 0; i < STATE_BATCHES; ++i) {
    int mode = (i + frame_idx) % 6;
    int tex_idx = (i + frame_idx) & (STATE_NUM_TEX - 1);

    grTexSource(GR_TMU0, st->tex_addr[tex_idx], GR_MIPMAPLEVELMASK_BOTH,
                &st->ti[tex_idx]);
    grTexFilterMode(GR_TMU0,
                    (mode & 1) ? GR_TEXTUREFILTER_BILINEAR
                               : GR_TEXTUREFILTER_POINT_SAMPLED,
                    (mode & 1) ? GR_TEXTUREFILTER_BILINEAR
                               : GR_TEXTUREFILTER_POINT_SAMPLED);

    if (mode == 0 || mode == 3) {
      grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                     GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE,
                     FXFALSE);
      grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO,
                           GR_BLEND_ONE, GR_BLEND_ZERO);
      grChromakeyMode(GR_CHROMAKEY_DISABLE);
      grDepthMask(FXTRUE);
      grDepthBufferFunction(GR_CMP_LESS);
    } else if (mode == 1 || mode == 4) {
      grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                     GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE,
                     FXFALSE);
      grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                           GR_BLEND_ONE, GR_BLEND_ZERO);
      grChromakeyMode(GR_CHROMAKEY_ENABLE);
      grDepthMask(FXFALSE);
      grDepthBufferFunction(GR_CMP_LEQUAL);
    } else {
      grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                     GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE,
                     FXFALSE);
      grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE,
                           GR_BLEND_ONE, GR_BLEND_ZERO);
      grChromakeyMode(GR_CHROMAKEY_DISABLE);
      grDepthMask(FXFALSE);
      grDepthBufferFunction(GR_CMP_LESS);
    }

    for (t = 0; t < STATE_TRIS_PER_B; ++t) {
      grDrawTriangle(&st->tris[i][t][0], &st->tris[i][t][1],
                     &st->tris[i][t][2]);
    }
  }

  *out_work_units = (double)STATE_BATCHES / 1000.0; /* KDraw (KBatch) */
}

static void state_teardown(BenchContext* ctx) {
  free(ctx->scene_state);
  ctx->scene_state = NULL;
}

/* =========================================================================
 * Scene 4: tex - 64 resident P_8 128x128 textures + palette swaps + streaming
 * ========================================================================= */
#define TEX_SLOTS       64
#define TEX_DIM         128
#define TEX_PAL_COUNT   4
#define TEX_REUPLOADS   4

typedef struct TexBenchState {
  FxU8 tex_data[TEX_SLOTS][TEX_DIM * TEX_DIM];
  GrTexInfo ti[TEX_SLOTS];
  FxU32 tex_addr[TEX_SLOTS];
  FxU32 palettes[TEX_PAL_COUNT][256];
  BenchVertex quads[TEX_SLOTS][4];
  int active_slots;
} TexBenchState;

static int tex_setup(BenchContext* ctx) {
  TexBenchState* st = (TexBenchState*)calloc(1, sizeof(TexBenchState));
  FxU32 addr = ctx->tmu0_min;
  int i, p, y, x;
  float w = (float)ctx->width;
  float h = (float)ctx->height;
  float cell_w = w / 8.0f;
  float cell_h = (h - 56.0f) / 8.0f;
  if (!st) return 0;

  /* 4 distinct 256-entry ARGB palettes */
  for (p = 0; p < TEX_PAL_COUNT; ++p) {
    for (i = 0; i < 256; ++i) {
      unsigned r = (unsigned)((i + p * 64) & 255);
      unsigned g = (unsigned)(((i * 3) + p * 40) & 255);
      unsigned b = (unsigned)((255 - i + p * 32) & 255);
      st->palettes[p][i] = (0xFFUL << 24) | ((FxU32)r << 16) |
                           ((FxU32)g << 8) | (FxU32)b;
    }
  }
  bench_download_palette(st->palettes[0]);

  st->active_slots = 0;
  for (i = 0; i < TEX_SLOTS; ++i) {
    FxU32 req;
    int col = i & 7;
    int row = i >> 3;
    float x0 = (float)col * cell_w + 1.0f;
    float y0 = 52.0f + (float)row * cell_h + 1.0f;
    float x1 = x0 + cell_w - 2.0f;
    float y1 = y0 + cell_h - 2.0f;

    for (y = 0; y < TEX_DIM; ++y) {
      for (x = 0; x < TEX_DIM; ++x) {
        st->tex_data[i][y * TEX_DIM + x] =
            (FxU8)(((x ^ y) + i * 7 + ((x + y) >> 1)) & 255);
      }
    }
    bench_init_tex_info(&st->ti[i], BENCH_LOD_128, BENCH_LOD_128,
                        BENCH_ASPECT_1x1, GR_TEXFMT_P_8, st->tex_data[i]);
    req = grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &st->ti[i]);
    if (addr + req > ctx->scene_tmu_limit) {
      break;
    }
    st->tex_addr[i] = addr;
    grTexDownloadMipMap(GR_TMU0, addr, GR_MIPMAPLEVELMASK_BOTH, &st->ti[i]);
    addr = (addr + req + 7U) & ~7U;

    bench_set_vert(&st->quads[i][0], x0, y0, 0.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 0.0f,   0.0f);
    bench_set_vert(&st->quads[i][1], x1, y0, 0.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 255.0f, 0.0f);
    bench_set_vert(&st->quads[i][2], x1, y1, 0.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 255.0f, 255.0f);
    bench_set_vert(&st->quads[i][3], x0, y1, 0.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 0.0f,   255.0f);
    st->active_slots++;
  }

  if (st->active_slots == 0) {
    free(st);
    return 0;
  }

  ctx->scene_state = st;
  return 1;
}

static void tex_render(BenchContext* ctx, int frame_idx, double t_sec,
                       double* out_work_units) {
  TexBenchState* st = (TexBenchState*)ctx->scene_state;
  int i, u;
  int slots = st->active_slots;
  (void)t_sec;

  bench_reset_pipeline(ctx->width, ctx->height);
  grBufferClear(0x00080C14UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

  /* Stream 4 dynamic P_8 textures per frame (animated first row) */
  for (u = 0; u < TEX_REUPLOADS; ++u) {
    int slot = (frame_idx * TEX_REUPLOADS + u) % slots;
    st->tex_data[slot][0] = (FxU8)((frame_idx + u) & 255);
    st->tex_data[slot][TEX_DIM * 64 + 64] = (FxU8)((frame_idx * 5 + u) & 255);
    grTexDownloadMipMap(GR_TMU0, st->tex_addr[slot], GR_MIPMAPLEVELMASK_BOTH,
                        &st->ti[slot]);
  }

  /* Draw all resident textures, swapping palette 4 times per frame */
  for (i = 0; i < slots; ++i) {
    if ((i & 15) == 0) {
      int pal_idx = ((i >> 4) + frame_idx) & (TEX_PAL_COUNT - 1);
      bench_download_palette(st->palettes[pal_idx]);
    }
    grTexSource(GR_TMU0, st->tex_addr[i], GR_MIPMAPLEVELMASK_BOTH, &st->ti[i]);
    bench_draw_quad(&st->quads[i][0], &st->quads[i][1],
                    &st->quads[i][2], &st->quads[i][3]);
  }

  /* Throughput in MB/frame uploaded (4 x 16KB tex + 4 x 1KB palettes) */
  *out_work_units =
      (double)(TEX_REUPLOADS * (TEX_DIM * TEX_DIM) + TEX_PAL_COUNT * 1024) /
      (1024.0 * 1024.0);
}

static void tex_teardown(BenchContext* ctx) {
  free(ctx->scene_state);
  ctx->scene_state = NULL;
}

/* =========================================================================
 * Scene 5: lfb - 3D background + sparse persistent GR_LFB_WRITE_ONLY HUD
 * ========================================================================= */
#define LFB_BG_QUADS 64 /* 128 triangles background */

typedef struct LfbBenchState {
  FxU16 tex[64 * 64];
  GrTexInfo ti;
  FxU32 tex_addr;
  BenchVertex bg_quads[LFB_BG_QUADS][4];
} LfbBenchState;

static int lfb_setup(BenchContext* ctx) {
  LfbBenchState* st = (LfbBenchState*)calloc(1, sizeof(LfbBenchState));
  int i;
  float w = (float)ctx->width;
  float h = (float)ctx->height;
  float cw = w / 8.0f;
  float ch = h / 8.0f;
  if (!st) return 0;

  gen_checker_rgb565(st->tex, 64, 8, 0x24B0U, 0x1248U, 0);
  bench_init_tex_info(&st->ti, BENCH_LOD_64, BENCH_LOD_64,
                      BENCH_ASPECT_1x1, GR_TEXFMT_RGB_565, st->tex);
  st->tex_addr = ctx->tmu0_min;
  grTexDownloadMipMap(GR_TMU0, st->tex_addr, GR_MIPMAPLEVELMASK_BOTH, &st->ti);

  for (i = 0; i < LFB_BG_QUADS; ++i) {
    float x0 = (float)(i & 7) * cw;
    float y0 = (float)(i >> 3) * ch;
    float r = 160.0f + (float)((i * 19) & 95);
    float g = 160.0f + (float)((i * 31) & 95);
    float b = 180.0f + (float)((i * 43) & 75);
    bench_set_vert(&st->bg_quads[i][0], x0,      y0,      0.0f, 1.0f, r, g, b, 255.0f, 0.0f,   0.0f);
    bench_set_vert(&st->bg_quads[i][1], x0 + cw, y0,      0.0f, 1.0f, r, g, b, 255.0f, 255.0f, 0.0f);
    bench_set_vert(&st->bg_quads[i][2], x0 + cw, y0 + ch, 0.0f, 1.0f, r, g, b, 255.0f, 255.0f, 255.0f);
    bench_set_vert(&st->bg_quads[i][3], x0,      y0 + ch, 0.0f, 1.0f, r, g, b, 255.0f, 0.0f,   255.0f);
  }

  ctx->scene_state = st;
  return 1;
}

static void lfb_fill_band_565(FxU8* base, FxU32 stride, int width,
                              int y_start, int y_end, FxU16 c_main,
                              FxU16 c_bar, int bar_w) {
  int y, x;
  FxU32 main32 = ((FxU32)c_main << 16) | (FxU32)c_main;
  FxU32 bar32  = ((FxU32)c_bar << 16)  | (FxU32)c_bar;
  int pairs = width >> 1;
  int bar_pairs = bar_w >> 1;
  if (bar_pairs > pairs) bar_pairs = pairs;

  for (y = y_start; y < y_end; ++y) {
    FxU32* row32 = (FxU32*)(base + (size_t)y * stride);
    int is_bar_row = (y >= y_start + 8 && y < y_end - 8);
    if (is_bar_row) {
      for (x = 0; x < bar_pairs; ++x) row32[x] = bar32;
      for (; x < pairs; ++x) row32[x] = main32;
    } else {
      for (x = 0; x < pairs; ++x) row32[x] = main32;
    }
  }
}

static void lfb_render(BenchContext* ctx, int frame_idx, double t_sec,
                       double* out_work_units) {
  LfbBenchState* st = (LfbBenchState*)ctx->scene_state;
  GrLfbInfo_t lfb;
  int i;
  int top_rows = (ctx->height >= 600) ? 56 : 48;
  int bot_rows = (ctx->height >= 600) ? 72 : 64;
  int hud_rows = top_rows + bot_rows;
  (void)t_sec;

  bench_reset_pipeline(ctx->width, ctx->height);
  grBufferClear(0x000A101CUL, 0xFF, GR_WDEPTHVALUE_FARTHEST);
  grTexSource(GR_TMU0, st->tex_addr, GR_MIPMAPLEVELMASK_BOTH, &st->ti);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

  for (i = 0; i < LFB_BG_QUADS; ++i) {
    bench_draw_quad(&st->bg_quads[i][0], &st->bg_quads[i][1],
                    &st->bg_quads[i][2], &st->bg_quads[i][3]);
  }

  /* Sparse persistent LFB write-only HUD (top + bottom dashboard bars) */
  memset(&lfb, 0, sizeof(lfb));
  lfb.size = sizeof(lfb);
  if (grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE, &lfb) &&
      lfb.lfbPtr != NULL && lfb.strideInBytes >= (FxU32)(ctx->width * 2)) {
    FxU8* base = (FxU8*)lfb.lfbPtr;
    int bar_w = 32 + ((frame_idx * 9) % (ctx->width - 48));
    FxU16 accent = (FxU16)(0x07E0U | (((unsigned)frame_idx & 31U) << 11));
    lfb_fill_band_565(base, lfb.strideInBytes, ctx->width,
                      0, top_rows, 0x18E3U, accent, bar_w);
    lfb_fill_band_565(base, lfb.strideInBytes, ctx->width,
                      ctx->height - bot_rows, ctx->height, 0x2104U, 0xFD20U,
                      ctx->width - bar_w);
    grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
  }

  *out_work_units =
      ((double)hud_rows * (double)ctx->width * 2.0) / (1024.0 * 1024.0); /* MB */
}

static void lfb_teardown(BenchContext* ctx) {
  free(ctx->scene_state);
  ctx->scene_state = NULL;
}

/* =========================================================================
 * Scene 6: multipass - 2-pass FOG_ADD2/MULT2 + PREFOG_COLOR + mipmaps + chroma
 * ========================================================================= */
#define MP_GRID_N      20 /* 20x20 quads = 800 tris x 2 passes = 1,600 tris */
#define MP_VERTS       ((MP_GRID_N + 1) * (MP_GRID_N + 1))
#define MP_MIP_TEXELS  (128*128 + 64*64 + 32*32 + 16*16)
#ifndef GR_FOG_TABLE_SIZE
#define GR_FOG_TABLE_SIZE 64
#endif

typedef struct MultipassState {
  FxU16 base_mips[MP_MIP_TEXELS];
  FxU16 glow_mips[MP_MIP_TEXELS];
  GrTexInfo base_ti;
  GrTexInfo glow_ti;
  FxU32 base_addr;
  FxU32 glow_addr;
  GrFog_t fog_table[GR_FOG_TABLE_SIZE];
  BenchVertex verts[MP_VERTS];
} MultipassState;

static void fill_mip_chain_checker(FxU16* dst, int add_chroma) {
  int sizes[4] = { 128, 64, 32, 16 };
  int tiles[4] = { 16, 8, 4, 2 };
  int lv;
  FxU16* p = dst;
  for (lv = 0; lv < 4; ++lv) {
    gen_checker_rgb565(p, sizes[lv], tiles[lv], 0x5ACB, 0x2945,
                       add_chroma && (lv == 0));
    p += sizes[lv] * sizes[lv];
  }
}

static void fill_mip_chain_glow(FxU16* dst) {
  int sizes[4] = { 128, 64, 32, 16 };
  int lv;
  FxU16* p = dst;
  for (lv = 0; lv < 4; ++lv) {
    gen_radial_rgb565(p, sizes[lv], lv & 1);
    p += sizes[lv] * sizes[lv];
  }
}

static int multipass_setup(BenchContext* ctx) {
  MultipassState* st = (MultipassState*)calloc(1, sizeof(MultipassState));
  FxU32 addr = ctx->tmu0_min;
  FxU32 req;
  int i, gy, gx;
  float w = (float)ctx->width;
  float h = (float)ctx->height;
  if (!st) return 0;

  fill_mip_chain_checker(st->base_mips, 1);
  fill_mip_chain_glow(st->glow_mips);

  bench_init_tex_info(&st->base_ti, BENCH_LOD_16, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_RGB_565, st->base_mips);
  bench_init_tex_info(&st->glow_ti, BENCH_LOD_16, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_RGB_565, st->glow_mips);

  req = grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &st->base_ti);
  st->base_addr = addr;
  grTexDownloadMipMap(GR_TMU0, st->base_addr, GR_MIPMAPLEVELMASK_BOTH,
                      &st->base_ti);
  addr = (addr + req + 7U) & ~7U;

  st->glow_addr = addr;
  grTexDownloadMipMap(GR_TMU0, st->glow_addr, GR_MIPMAPLEVELMASK_BOTH,
                      &st->glow_ti);

  /* Smooth exponential fog table (no guFogGenerateExp dependency for Glide 3) */
  for (i = 0; i < GR_FOG_TABLE_SIZE; ++i) {
    float frac = (float)i / (float)(GR_FOG_TABLE_SIZE - 1);
    float f = 1.0f - (float)exp((double)(-2.2f * frac * frac));
    int fi = (int)(f * 255.0f);
    if (fi < 0) fi = 0;
    if (fi > 255) fi = 255;
    st->fog_table[i] = (GrFog_t)fi;
  }

  for (gy = 0; gy <= MP_GRID_N; ++gy) {
    for (gx = 0; gx <= MP_GRID_N; ++gx) {
      float u = (float)gx / (float)MP_GRID_N;
      float v = (float)gy / (float)MP_GRID_N;
      float depth_w = 1.5f + v * 48.0f;
      float oow = 1.0f / depth_w;
      float ooz = 65535.0f * oow;
      float sx = (0.04f + u * 0.92f) * w;
      float sy = (0.12f + v * 0.84f) * h;
      bench_set_vert(&st->verts[gy * (MP_GRID_N + 1) + gx],
                     sx, sy, ooz, oow,
                     220.0f, 210.0f, 200.0f, 255.0f,
                     (u * 255.0f) * oow, (v * 255.0f) * oow);
    }
  }

  ctx->scene_state = st;
  return 1;
}

static void multipass_draw_mesh(const BenchVertex* verts) {
  int gy, gx;
  for (gy = 0; gy < MP_GRID_N; ++gy) {
    int r0 = gy * (MP_GRID_N + 1);
    int r1 = r0 + (MP_GRID_N + 1);
    for (gx = 0; gx < MP_GRID_N; ++gx) {
      grDrawTriangle(&verts[r0 + gx], &verts[r0 + gx + 1], &verts[r1 + gx + 1]);
      grDrawTriangle(&verts[r0 + gx], &verts[r1 + gx + 1], &verts[r1 + gx]);
    }
  }
}

static void multipass_render(BenchContext* ctx, int frame_idx, double t_sec,
                             double* out_work_units) {
  MultipassState* st = (MultipassState*)ctx->scene_state;
  FxU32 fog_col = 0x00382820UL + (((FxU32)frame_idx & 31U) << 8);
  (void)t_sec;

  bench_reset_pipeline(ctx->width, ctx->height);
  grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
  grDepthBufferFunction(GR_CMP_LESS);
  grDepthMask(FXTRUE);
  grBufferClear(fog_col, 0xFF, GR_WDEPTHVALUE_FARTHEST);

  grFogColorValue(fog_col);
  grFogTable(st->fog_table);
  grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR,
                  GR_TEXTUREFILTER_BILINEAR);
  grTexMipMapMode(GR_TMU0, GR_MIPMAP_NEAREST, FXFALSE);
  grTexLodBiasValue(GR_TMU0, 0.5f);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

  /* Pass 1: Base bilinear mipmapped geometry + chromakey + GR_FOG_ADD2 */
  grTexSource(GR_TMU0, st->base_addr, GR_MIPMAPLEVELMASK_BOTH, &st->base_ti);
  grChromakeyValue(0x00FF00FFUL);
  grChromakeyMode(GR_CHROMAKEY_ENABLE);
  grFogMode(BENCH_FOG_TABLE_MODE | GR_FOG_ADD2);
  grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO,
                       GR_BLEND_ONE, GR_BLEND_ZERO);
  multipass_draw_mesh(st->verts);

  /* Pass 2: Additive lightmap/glow with GR_FOG_MULT2 + GR_BLEND_PREFOG_COLOR */
  grTexSource(GR_TMU0, st->glow_addr, GR_MIPMAPLEVELMASK_BOTH, &st->glow_ti);
  grChromakeyMode(GR_CHROMAKEY_DISABLE);
  grDepthMask(FXFALSE);
  grDepthBufferFunction(GR_CMP_LEQUAL);
  grFogMode(BENCH_FOG_TABLE_MODE | GR_FOG_MULT2);
  grAlphaBlendFunction(GR_BLEND_PREFOG_COLOR, GR_BLEND_ONE,
                       GR_BLEND_ONE, GR_BLEND_ZERO);
  multipass_draw_mesh(st->verts);

  *out_work_units = (double)(MP_GRID_N * MP_GRID_N * 2 * 2) / 1000.0; /* KTri */
}

static void multipass_teardown(BenchContext* ctx) {
  free(ctx->scene_state);
  ctx->scene_state = NULL;
}

/* =========================================================================
 * Scene 7: fog - Heavy Table Fog across all 64 W-table intervals
 * ========================================================================= */
#define FOG_LAYERS 12

typedef struct FogBenchState {
  FxU16 tex[128 * 128];
  GrTexInfo ti;
  FxU32 tex_addr;
  GrFog_t fog_table[GR_FOG_TABLE_SIZE];
  BenchVertex layers[FOG_LAYERS][4];
} FogBenchState;

static int fog_setup(BenchContext* ctx) {
  FogBenchState* st = (FogBenchState*)calloc(1, sizeof(FogBenchState));
  int i;
  float w = (float)ctx->width;
  float h = (float)ctx->height;
  if (!st) return 0;

  gen_checker_rgb565(st->tex, 128, 16, 0x5B9FU, 0x224CU, 0);
  bench_init_tex_info(&st->ti, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_RGB_565, st->tex);
  st->tex_addr = ctx->tmu0_min;
  grTexDownloadMipMap(GR_TMU0, st->tex_addr, GR_MIPMAPLEVELMASK_BOTH, &st->ti);

  for (i = 0; i < GR_FOG_TABLE_SIZE; ++i) {
    float frac = (float)i / (float)(GR_FOG_TABLE_SIZE - 1);
    float f = 1.0f - (float)exp((double)(-2.8f * frac * frac));
    int fi = (int)(f * 255.0f);
    if (fi < 0) fi = 0;
    if (fi > 255) fi = 255;
    st->fog_table[i] = (GrFog_t)fi;
  }

  for (i = 0; i < FOG_LAYERS; ++i) {
    /* Span near (W=1.2) to very far (W=2048..16384) across each quad */
    float w_near = 1.2f + (float)i * 2.5f;
    float w_far  = 64.0f * (float)(1 << (i % 8));
    float oow_n  = 1.0f / w_near;
    float oow_f  = 1.0f / w_far;
    float ooz_n  = 65535.0f * oow_n;
    float ooz_f  = 65535.0f * oow_f;
    float r = 180.0f + (float)((i * 19) & 63);
    float g = 170.0f + (float)((i * 37) & 63);
    float b = 160.0f + (float)((i * 53) & 63);
    bench_set_vert(&st->layers[i][0], 0.0f, 0.0f, ooz_n, oow_n, r, g, b, 160.0f, 0.0f,            0.0f);
    bench_set_vert(&st->layers[i][1], w,    0.0f, ooz_f, oow_f, r, g, b, 160.0f, 255.0f * oow_f,  0.0f);
    bench_set_vert(&st->layers[i][2], w,    h,    ooz_f, oow_f, r, g, b, 160.0f, 255.0f * oow_f,  255.0f * oow_f);
    bench_set_vert(&st->layers[i][3], 0.0f, h,    ooz_n, oow_n, r, g, b, 160.0f, 0.0f,            255.0f * oow_n);
  }

  ctx->scene_state = st;
  return 1;
}

static void fog_render(BenchContext* ctx, int frame_idx, double t_sec,
                       double* out_work_units) {
  FogBenchState* st = (FogBenchState*)ctx->scene_state;
  FxU32 fog_col = 0x00485868UL + (((FxU32)frame_idx & 15U) << 8);
  int i;
  (void)t_sec;

  bench_reset_pipeline(ctx->width, ctx->height);
  grBufferClear(fog_col, 0xFF, GR_WDEPTHVALUE_FARTHEST);

  grFogColorValue(fog_col);
  grFogTable(st->fog_table);
  grFogMode(BENCH_FOG_TABLE_MODE);

  grTexSource(GR_TMU0, st->tex_addr, GR_MIPMAPLEVELMASK_BOTH, &st->ti);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
  grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                       GR_BLEND_ONE, GR_BLEND_ZERO);

  for (i = 0; i < FOG_LAYERS; ++i) {
    bench_draw_quad(&st->layers[i][0], &st->layers[i][1],
                    &st->layers[i][2], &st->layers[i][3]);
  }

  *out_work_units =
      ((double)ctx->width * (double)ctx->height * (double)FOG_LAYERS) / 1e6;
}

static void fog_teardown(BenchContext* ctx) {
  free(ctx->scene_state);
  ctx->scene_state = NULL;
}

/* =========================================================================
 * Scene 8: multitex - Dual-TMU (TMU0 + TMU1) Multitexturing + Detail
 * ========================================================================= */
#define MTEX_LAYERS 10

typedef struct MultitexState {
  FxU16 tex0[128 * 128];
  FxU16 tex1[128 * 128];
  GrTexInfo ti0;
  GrTexInfo ti1;
  FxU32 addr0;
  FxU32 addr1;
  BenchVertex layers[MTEX_LAYERS][4];
} MultitexState;

static int multitex_setup(BenchContext* ctx) {
  MultitexState* st;
  int i;
  float w = (float)ctx->width;
  float h = (float)ctx->height;

  if (ctx->num_tmus < 2) return 0;
  st = (MultitexState*)calloc(1, sizeof(MultitexState));
  if (!st) return 0;

  gen_checker_rgb565(st->tex0, 128, 16, 0x73AEU, 0x39E7U, 0);
  gen_radial_rgb565(st->tex1, 128, 0);
  bench_init_tex_info(&st->ti0, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_RGB_565, st->tex0);
  bench_init_tex_info(&st->ti1, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_RGB_565, st->tex1);
  st->addr0 = ctx->tmu0_min;
  st->addr1 = ctx->tmu1_min;
  grTexDownloadMipMap(GR_TMU0, st->addr0, GR_MIPMAPLEVELMASK_BOTH, &st->ti0);
  grTexDownloadMipMap(GR_TMU1, st->addr1, GR_MIPMAPLEVELMASK_BOTH, &st->ti1);

  for (i = 0; i < MTEX_LAYERS; ++i) {
    float shift = (float)(i * 11);
    float r = 200.0f;
    float g = 210.0f;
    float b = 220.0f;
    float a = 128.0f;
    bench_set_vert(&st->layers[i][0], 0.0f, 0.0f, 0.0f, 1.0f, r, g, b, a, shift,          shift);
    bench_set_vert(&st->layers[i][1], w,    0.0f, 0.0f, 1.0f, r, g, b, a, shift + 255.0f, shift);
    bench_set_vert(&st->layers[i][2], w,    h,    0.0f, 1.0f, r, g, b, a, shift + 255.0f, shift + 255.0f);
    bench_set_vert(&st->layers[i][3], 0.0f, h,    0.0f, 1.0f, r, g, b, a, shift,          shift + 255.0f);
    bench_set_vert_st1(&st->layers[i][0], 0.0f,   0.0f);
    bench_set_vert_st1(&st->layers[i][1], 255.0f, 0.0f);
    bench_set_vert_st1(&st->layers[i][2], 255.0f, 255.0f);
    bench_set_vert_st1(&st->layers[i][3], 0.0f,   255.0f);
  }

  ctx->scene_state = st;
  return 1;
}

static void multitex_render(BenchContext* ctx, int frame_idx, double t_sec,
                            double* out_work_units) {
  MultitexState* st = (MultitexState*)ctx->scene_state;
  int i;
  float uv = (float)((frame_idx * 2) & 255);
  (void)t_sec;

  bench_reset_pipeline(ctx->width, ctx->height);
  grBufferClear(0x00101820UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);

  grTexSource(GR_TMU0, st->addr0, GR_MIPMAPLEVELMASK_BOTH, &st->ti0);
  grTexSource(GR_TMU1, st->addr1, GR_MIPMAPLEVELMASK_BOTH, &st->ti1);
  grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
  grTexFilterMode(GR_TMU1, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
  grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
  grTexClampMode(GR_TMU1, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
  /* TMU1 outputs its local lightmap; TMU0 multiplies TMU0 * TMU1 */
  grTexCombine(GR_TMU1, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_SCALE_OTHER,
               GR_COMBINE_FACTOR_LOCAL,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
  grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                       GR_BLEND_ONE, GR_BLEND_ZERO);

  for (i = 0; i < MTEX_LAYERS; ++i) {
    BenchVertex q[4];
    int k;
    for (k = 0; k < 4; ++k) {
      q[k] = st->layers[i][k];
#if GLIDE_VERSION == 3
      q[k].s0 += uv;
      q[k].t1 += uv * 0.5f;
#else
      q[k].tmuvtx[0].sow += uv;
#if GLIDE_NUM_TMU > 1
      q[k].tmuvtx[1].tow += uv * 0.5f;
#endif
#endif
    }
    bench_draw_quad(&q[0], &q[1], &q[2], &q[3]);
  }

  *out_work_units =
      ((double)ctx->width * (double)ctx->height * (double)MTEX_LAYERS) / 1e6;
}

static void multitex_teardown(BenchContext* ctx) {
  if (ctx->num_tmus >= 2) {
    grTexCombine(GR_TMU1, GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE,
                 FXFALSE, FXFALSE);
  }
  free(ctx->scene_state);
  ctx->scene_state = NULL;
}

/* =========================================================================
 * Scene 9: lfb_read - Full-Frame LFB ReadRegion + WriteRegion (RGB565)
 * ========================================================================= */
typedef struct LfbReadState {
  FxU16* buf;
  int buf_w;
  int buf_h;
  BenchVertex bg[4];
} LfbReadState;

static int lfb_read_setup(BenchContext* ctx) {
  LfbReadState* st = (LfbReadState*)calloc(1, sizeof(LfbReadState));
  float w = (float)ctx->width;
  float h = (float)ctx->height;
  if (!st) return 0;

  st->buf_w = ctx->width;
  st->buf_h = ctx->height;
  st->buf = (FxU16*)malloc((size_t)st->buf_w * (size_t)st->buf_h * sizeof(FxU16));
  if (!st->buf) {
    free(st);
    return 0;
  }

  bench_set_vert(&st->bg[0], 0.0f, 0.0f, 0.0f, 1.0f, 220.0f, 60.0f,  40.0f, 255.0f, 0.0f, 0.0f);
  bench_set_vert(&st->bg[1], w,    0.0f, 0.0f, 1.0f, 40.0f,  220.0f, 80.0f, 255.0f, 0.0f, 0.0f);
  bench_set_vert(&st->bg[2], w,    h,    0.0f, 1.0f, 60.0f,  80.0f, 230.0f, 255.0f, 0.0f, 0.0f);
  bench_set_vert(&st->bg[3], 0.0f, h,    0.0f, 1.0f, 230.0f, 210.0f, 50.0f, 255.0f, 0.0f, 0.0f);

  ctx->scene_state = st;
  return 1;
}

static void lfb_read_render(BenchContext* ctx, int frame_idx, double t_sec,
                            double* out_work_units) {
  LfbReadState* st = (LfbReadState*)ctx->scene_state;
  FxU32 stride = (FxU32)st->buf_w * 2U;
  (void)t_sec;

  bench_reset_pipeline(ctx->width, ctx->height);
  grBufferClear(0x00101020UL + ((FxU32)(frame_idx & 31) << 16), 0xFF,
                GR_WDEPTHVALUE_FARTHEST);
  bench_draw_quad(&st->bg[0], &st->bg[1], &st->bg[2], &st->bg[3]);

  /* Full-frame readback (BGRA32 -> RGB565) + writeback (RGB565 -> BGRA32) */
  grLfbReadRegion(GR_BUFFER_BACKBUFFER, 0, 0,
                  (FxU32)st->buf_w, (FxU32)st->buf_h, stride, st->buf);
#if GLIDE_VERSION == 3
  grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 0, 0, GR_LFB_SRC_FMT_565,
                   (FxU32)st->buf_w, (FxU32)st->buf_h, FXFALSE,
                   (FxI32)stride, st->buf);
#else
  grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 0, 0, GR_LFB_SRC_FMT_565,
                   (FxU32)st->buf_w, (FxU32)st->buf_h, (FxI32)stride, st->buf);
#endif

  /* Total bytes transferred (read + write) in MB */
  *out_work_units =
      (double)((size_t)st->buf_w * (size_t)st->buf_h * 4U) / (1024.0 * 1024.0);
}

static void lfb_read_teardown(BenchContext* ctx) {
  LfbReadState* st = (LfbReadState*)ctx->scene_state;
  if (st) {
    free(st->buf);
    free(st);
  }
  ctx->scene_state = NULL;
}

#if GLIDE_VERSION == 3

/* =========================================================================
 * Scene 10 (Glide 3 VSA-100): stencil - PIXEXT Hardware Stencil Shadows
 * ========================================================================= */
#define STENCIL_VOLUMES 8

typedef struct StencilBenchState {
  BenchVertex bg[4];
  BenchVertex vols[STENCIL_VOLUMES][4];
} StencilBenchState;

static int stencil_setup(BenchContext* ctx) {
  StencilBenchState* st;
  int i;
  float w = (float)ctx->width;
  float h = (float)ctx->height;

  if (!bench_has_ext(ctx, "PIXEXT") || !ctx->fn_stencilFunc ||
      !ctx->fn_stencilMask || !ctx->fn_stencilOp || !ctx->fn_bufferClearExt) {
    return 0;
  }
  st = (StencilBenchState*)calloc(1, sizeof(StencilBenchState));
  if (!st) return 0;

  bench_set_vert(&st->bg[0], 0.0f, 0.0f, 32768.0f, 0.5f, 180.0f, 190.0f, 210.0f, 255.0f, 0.0f, 0.0f);
  bench_set_vert(&st->bg[1], w,    0.0f, 32768.0f, 0.5f, 180.0f, 190.0f, 210.0f, 255.0f, 0.0f, 0.0f);
  bench_set_vert(&st->bg[2], w,    h,    32768.0f, 0.5f, 120.0f, 140.0f, 170.0f, 255.0f, 0.0f, 0.0f);
  bench_set_vert(&st->bg[3], 0.0f, h,    32768.0f, 0.5f, 120.0f, 140.0f, 170.0f, 255.0f, 0.0f, 0.0f);

  for (i = 0; i < STENCIL_VOLUMES; ++i) {
    float pad = (float)i * 0.04f;
    float x0 = (0.05f + pad) * w;
    float y0 = (0.05f + pad) * h;
    float x1 = (0.95f - pad) * w;
    float y1 = (0.95f - pad) * h;
    float z  = 40000.0f + (float)i * 1500.0f;
    float q  = z / 65535.0f;
    bench_set_vert(&st->vols[i][0], x0, y0, z, q, 255.0f, 255.0f, 255.0f, 128.0f, 0.0f, 0.0f);
    bench_set_vert(&st->vols[i][1], x1, y0, z, q, 255.0f, 255.0f, 255.0f, 128.0f, 0.0f, 0.0f);
    bench_set_vert(&st->vols[i][2], x1, y1, z, q, 255.0f, 255.0f, 255.0f, 128.0f, 0.0f, 0.0f);
    bench_set_vert(&st->vols[i][3], x0, y1, z, q, 255.0f, 255.0f, 255.0f, 128.0f, 0.0f, 0.0f);
  }

  ctx->scene_state = st;
  return 1;
}

static void stencil_render(BenchContext* ctx, int frame_idx, double t_sec,
                           double* out_work_units) {
  StencilBenchState* st = (StencilBenchState*)ctx->scene_state;
  int i;
  (void)frame_idx;
  (void)t_sec;

  bench_reset_pipeline(ctx->width, ctx->height);
  bench_reset_vsa_state(ctx);
  grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
  grDepthBufferFunction(GR_CMP_LESS);
  grDepthMask(FXTRUE);
  ctx->fn_bufferClearExt(0x00182028UL, 0xFF, GR_WDEPTHVALUE_FARTHEST, 0);

  /* Pass 1: Base lit geometry */
  bench_draw_quad(&st->bg[0], &st->bg[1], &st->bg[2], &st->bg[3]);

  /* Pass 2: Shadow volume stencil increments/decrements (color write masked) */
  grEnable(GR_STENCIL_MODE_EXT);
  grColorMask(FXFALSE, FXFALSE);
  grDepthMask(FXFALSE);
  ctx->fn_stencilMask(0xFF);
  ctx->fn_stencilFunc(GR_CMP_ALWAYS, 1, 0xFF);
  for (i = 0; i < STENCIL_VOLUMES; ++i) {
    ctx->fn_stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP,
                      (i & 1) ? GR_STENCILOP_DECR_WRAP : GR_STENCILOP_INCR_WRAP);
    bench_draw_quad(&st->vols[i][0], &st->vols[i][1],
                    &st->vols[i][2], &st->vols[i][3]);
  }

  /* Pass 3: Darken pixels where stencil != 0 */
  grColorMask(FXTRUE, FXTRUE);
  grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
  ctx->fn_stencilFunc(GR_CMP_NOTEQUAL, 0, 0xFF);
  ctx->fn_stencilOp(GR_STENCILOP_KEEP, GR_STENCILOP_KEEP, GR_STENCILOP_KEEP);
  grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                       GR_BLEND_ONE, GR_BLEND_ZERO);
  for (i = 0; i < 4; ++i) {
    bench_draw_quad(&st->bg[0], &st->bg[1], &st->bg[2], &st->bg[3]);
  }

  grDisable(GR_STENCIL_MODE_EXT);
  *out_work_units =
      ((double)ctx->width * (double)ctx->height * (double)(1 + STENCIL_VOLUMES + 4)) / 1e6;
}

static void stencil_teardown(BenchContext* ctx) {
  bench_reset_vsa_state(ctx);
  free(ctx->scene_state);
  ctx->scene_state = NULL;
}

/* =========================================================================
 * Scene 11 (Glide 3 VSA-100): ext_combine - COMBINE + grAlphaBlendFunctionExt
 * ========================================================================= */
#define EXTCOMB_LAYERS 10

typedef struct ExtCombineState {
  FxU16 tex0[128 * 128];
  FxU16 tex1[128 * 128];
  GrTexInfo ti0;
  GrTexInfo ti1;
  FxU32 addr0;
  FxU32 addr1;
  BenchVertex layers[EXTCOMB_LAYERS][4];
} ExtCombineState;

static int ext_combine_setup(BenchContext* ctx) {
  ExtCombineState* st;
  int i;
  float w = (float)ctx->width;
  float h = (float)ctx->height;

  if (!bench_has_ext(ctx, "COMBINE") || !ctx->fn_colorCombineExt ||
      !ctx->fn_alphaCombineExt || !ctx->fn_texColorCombineExt ||
      !ctx->fn_constColorExt || !ctx->fn_alphaBlendExt) {
    return 0;
  }
  st = (ExtCombineState*)calloc(1, sizeof(ExtCombineState));
  if (!st) return 0;

  gen_checker_rgb565(st->tex0, 128, 16, 0xC4A8U, 0x294AU, 0);
  gen_radial_rgb565(st->tex1, 128, 1);
  bench_init_tex_info(&st->ti0, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_RGB_565, st->tex0);
  bench_init_tex_info(&st->ti1, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_RGB_565, st->tex1);
  st->addr0 = ctx->tmu0_min;
  grTexDownloadMipMap(GR_TMU0, st->addr0, GR_MIPMAPLEVELMASK_BOTH, &st->ti0);
  if (ctx->num_tmus >= 2) {
    st->addr1 = ctx->tmu1_min;
    grTexDownloadMipMap(GR_TMU1, st->addr1, GR_MIPMAPLEVELMASK_BOTH, &st->ti1);
  }

  for (i = 0; i < EXTCOMB_LAYERS; ++i) {
    float r = 160.0f + (float)((i * 23) & 79);
    float g = 140.0f + (float)((i * 41) & 95);
    float b = 150.0f + (float)((i * 59) & 79);
    float a = 96.0f;
    bench_set_vert(&st->layers[i][0], 0.0f, 0.0f, 0.0f, 1.0f, r, g, b, a, 0.0f,   0.0f);
    bench_set_vert(&st->layers[i][1], w,    0.0f, 0.0f, 1.0f, r, g, b, a, 255.0f, 0.0f);
    bench_set_vert(&st->layers[i][2], w,    h,    0.0f, 1.0f, r, g, b, a, 255.0f, 255.0f);
    bench_set_vert(&st->layers[i][3], 0.0f, h,    0.0f, 1.0f, r, g, b, a, 0.0f,   255.0f);
  }

  ctx->scene_state = st;
  return 1;
}

static void ext_combine_render(BenchContext* ctx, int frame_idx, double t_sec,
                               double* out_work_units) {
  ExtCombineState* st = (ExtCombineState*)ctx->scene_state;
  int i;
  FxU32 cval = 0x80406080UL + ((FxU32)(frame_idx & 31) << 8);
  (void)t_sec;

  bench_reset_pipeline(ctx->width, ctx->height);
  bench_reset_vsa_state(ctx);
  grBufferClear(0x00181828UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);

  grTexSource(GR_TMU0, st->addr0, GR_MIPMAPLEVELMASK_BOTH, &st->ti0);
  ctx->fn_constColorExt(GR_TMU0, cval);
  if (ctx->num_tmus >= 2) {
    grTexSource(GR_TMU1, st->addr1, GR_MIPMAPLEVELMASK_BOTH, &st->ti1);
    ctx->fn_constColorExt(GR_TMU1, 0xFF80C0FFUL);
    ctx->fn_texColorCombineExt(GR_TMU1,
                               GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
                               GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
                               GR_CMBX_TMU_CCOLOR, FXFALSE,
                               GR_CMBX_ZERO, FXFALSE, 0, FXFALSE);
    ctx->fn_texColorCombineExt(GR_TMU0,
                               GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
                               GR_CMBX_OTHER_TEXTURE_RGB, GR_FUNC_MODE_X,
                               GR_CMBX_TMU_CALPHA, FXFALSE,
                               GR_CMBX_OTHER_TEXTURE_RGB, FXFALSE, 0, FXFALSE);
  } else {
    ctx->fn_texColorCombineExt(GR_TMU0,
                               GR_CMBX_LOCAL_TEXTURE_RGB, GR_FUNC_MODE_X,
                               GR_CMBX_TMU_CCOLOR, GR_FUNC_MODE_X,
                               GR_CMBX_TMU_CALPHA, FXFALSE,
                               GR_CMBX_ZERO, FXFALSE, 0, FXFALSE);
  }

  grConstantColorValue(0x80A0C0E0UL);
  ctx->fn_colorCombineExt(GR_CMBX_TEXTURE_RGB, GR_FUNC_MODE_X,
                          GR_CMBX_ITRGB, GR_FUNC_MODE_ONE_MINUS_X,
                          GR_CMBX_CONSTANT_ALPHA, FXFALSE,
                          GR_CMBX_ITRGB, FXFALSE, 1, FXFALSE);
  ctx->fn_alphaCombineExt(GR_CMBX_ITALPHA, GR_FUNC_MODE_X,
                          GR_CMBX_ZERO, GR_FUNC_MODE_ZERO,
                          GR_CMBX_ZERO, FXTRUE,
                          GR_CMBX_ZERO, FXFALSE, 0, FXFALSE);

  for (i = 0; i < EXTCOMB_LAYERS; ++i) {
    if ((i & 3) == 3) {
      ctx->fn_alphaBlendExt(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_OP_REVSUB,
                            GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_OP_ADD);
    } else {
      ctx->fn_alphaBlendExt(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                            GR_BLEND_OP_ADD,
                            GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_OP_ADD);
    }
    bench_draw_quad(&st->layers[i][0], &st->layers[i][1],
                    &st->layers[i][2], &st->layers[i][3]);
  }

  *out_work_units =
      ((double)ctx->width * (double)ctx->height * (double)EXTCOMB_LAYERS) / 1e6;
}

static void ext_combine_teardown(BenchContext* ctx) {
  bench_reset_pipeline(ctx->width, ctx->height);
  bench_reset_vsa_state(ctx);
  free(ctx->scene_state);
  ctx->scene_state = NULL;
}

/* =========================================================================
 * Scene 12 (Glide 3 VSA-100): texfmt - DXT1 / DXT5 / FXT1 / ARGB8888 Stream
 * ========================================================================= */
#define TEXFMT_SZ 128

typedef struct TexFmtBenchState {
  FxU8  dxt1[(TEXFMT_SZ / 4) * (TEXFMT_SZ / 4) * 8];
  FxU8  dxt5[(TEXFMT_SZ / 4) * (TEXFMT_SZ / 4) * 16];
  FxU8  fxt1[(TEXFMT_SZ / 8) * (TEXFMT_SZ / 4) * 16];
  FxU32 argb[TEXFMT_SZ * TEXFMT_SZ];
  GrTexInfo ti_dxt1, ti_dxt5, ti_fxt1, ti_argb;
  FxU32 addr_dxt1, addr_dxt5, addr_fxt1, addr_argb;
  BenchVertex quads[4][4];
} TexFmtBenchState;

static int texfmt_setup(BenchContext* ctx) {
  TexFmtBenchState* st;
  FxU32 addr, req;
  int i, b;
  float hw = (float)ctx->width * 0.5f;
  float hh = (float)ctx->height * 0.5f;

  if (!bench_has_ext(ctx, "TEXFMT")) return 0;
  st = (TexFmtBenchState*)calloc(1, sizeof(TexFmtBenchState));
  if (!st) return 0;

  /* Valid DXT1 blocks (128x128 = 1024 blocks x 8 bytes) */
  for (b = 0; b < (TEXFMT_SZ / 4) * (TEXFMT_SZ / 4); ++b) {
    FxU8* p = &st->dxt1[b * 8];
    FxU16 c0 = (FxU16)(0xF800U | (((b * 7) & 63) << 5) | ((b * 3) & 31));
    FxU16 c1 = (FxU16)(0x001FU | (((b * 5) & 63) << 5));
    p[0] = (FxU8)(c0 & 0xFF); p[1] = (FxU8)(c0 >> 8);
    p[2] = (FxU8)(c1 & 0xFF); p[3] = (FxU8)(c1 >> 8);
    p[4] = 0xE4; p[5] = 0x1B; p[6] = 0xB1; p[7] = 0x4E;
  }

  /* Valid DXT5 blocks (128x128 = 1024 blocks x 16 bytes) */
  for (b = 0; b < (TEXFMT_SZ / 4) * (TEXFMT_SZ / 4); ++b) {
    FxU8* p = &st->dxt5[b * 16];
    p[0] = 240; p[1] = 48;
    p[2] = 0x88; p[3] = 0x44; p[4] = 0x22; p[5] = 0x11; p[6] = 0x66; p[7] = 0x99;
    memcpy(p + 8, &st->dxt1[b * 8], 8);
  }

  /* Valid FXT1 blocks (128x128 = 512 8x4 blocks x 16 bytes; CC_MIXED mode) */
  for (b = 0; b < (TEXFMT_SZ / 8) * (TEXFMT_SZ / 4); ++b) {
    FxU8* p = &st->fxt1[b * 16];
    p[0] = 0x55; p[1] = 0xAA; p[2] = 0x33; p[3] = 0xCC;
    p[4] = 0x0F; p[5] = 0xF0; p[6] = 0x69; p[7] = 0x96;
    p[8]  = (FxU8)(0x40 | (b & 0x1F));
    p[9]  = (FxU8)(0x80 | ((b * 3) & 0x3F));
    p[10] = 0x5A; p[11] = 0xA5;
    p[12] = 0x7C; p[13] = 0x3E;
    p[14] = 0x1F;
    p[15] = 0x20; /* CC_MIXED (bits 127..125 = 001 -> byte 15 high bits 001) */
  }

  /* ARGB_8888 (128x128) */
  for (i = 0; i < TEXFMT_SZ * TEXFMT_SZ; ++i) {
    int x = i % TEXFMT_SZ;
    int y = i / TEXFMT_SZ;
    st->argb[i] = 0xFF000000UL | ((FxU32)(x * 2) << 16) |
                  ((FxU32)(y * 2) << 8) | (FxU32)((x ^ y) * 2);
  }

  bench_init_tex_info(&st->ti_dxt1, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_ARGB_CMP_DXT1, st->dxt1);
  bench_init_tex_info(&st->ti_dxt5, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_ARGB_CMP_DXT5, st->dxt5);
  bench_init_tex_info(&st->ti_fxt1, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_ARGB_CMP_FXT1, st->fxt1);
  bench_init_tex_info(&st->ti_argb, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_ARGB_8888, st->argb);

  addr = ctx->tmu0_min;
  st->addr_dxt1 = addr;
  req = grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &st->ti_dxt1);
  addr = (addr + req + 15U) & ~15U;
  st->addr_dxt5 = addr;
  req = grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &st->ti_dxt5);
  addr = (addr + req + 15U) & ~15U;
  st->addr_fxt1 = addr;
  req = grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &st->ti_fxt1);
  addr = (addr + req + 15U) & ~15U;
  st->addr_argb = addr;
  req = grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &st->ti_argb);
  if (addr + req > ctx->scene_tmu_limit) {
    free(st);
    return 0;
  }

  for (i = 0; i < 4; ++i) {
    float x0 = (i & 1) ? hw : 0.0f;
    float y0 = (i & 2) ? hh : 0.0f;
    bench_set_vert(&st->quads[i][0], x0,      y0,      0.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 0.0f,   0.0f);
    bench_set_vert(&st->quads[i][1], x0 + hw, y0,      0.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 255.0f, 0.0f);
    bench_set_vert(&st->quads[i][2], x0 + hw, y0 + hh, 0.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 255.0f, 255.0f);
    bench_set_vert(&st->quads[i][3], x0,      y0 + hh, 0.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 0.0f,   255.0f);
  }

  ctx->scene_state = st;
  return 1;
}

static void texfmt_render(BenchContext* ctx, int frame_idx, double t_sec,
                          double* out_work_units) {
  TexFmtBenchState* st = (TexFmtBenchState*)ctx->scene_state;
  FxU8 mut = (FxU8)(frame_idx & 0xFF);
  (void)t_sec;

  /* Mutate 1 byte per texture so wrapper cache cannot skip upload/decode */
  st->dxt1[4] = mut;
  st->dxt5[12] = mut;
  st->fxt1[0] = mut;
  st->argb[0] = 0xFF000000UL | ((FxU32)mut << 16) | ((FxU32)(255 - mut) << 8) | mut;

  bench_reset_pipeline(ctx->width, ctx->height);
  grBufferClear(0x00101018UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

  grTexDownloadMipMap(GR_TMU0, st->addr_dxt1, GR_MIPMAPLEVELMASK_BOTH, &st->ti_dxt1);
  grTexSource(GR_TMU0, st->addr_dxt1, GR_MIPMAPLEVELMASK_BOTH, &st->ti_dxt1);
  bench_draw_quad(&st->quads[0][0], &st->quads[0][1], &st->quads[0][2], &st->quads[0][3]);

  grTexDownloadMipMap(GR_TMU0, st->addr_dxt5, GR_MIPMAPLEVELMASK_BOTH, &st->ti_dxt5);
  grTexSource(GR_TMU0, st->addr_dxt5, GR_MIPMAPLEVELMASK_BOTH, &st->ti_dxt5);
  bench_draw_quad(&st->quads[1][0], &st->quads[1][1], &st->quads[1][2], &st->quads[1][3]);

  grTexDownloadMipMap(GR_TMU0, st->addr_fxt1, GR_MIPMAPLEVELMASK_BOTH, &st->ti_fxt1);
  grTexSource(GR_TMU0, st->addr_fxt1, GR_MIPMAPLEVELMASK_BOTH, &st->ti_fxt1);
  bench_draw_quad(&st->quads[2][0], &st->quads[2][1], &st->quads[2][2], &st->quads[2][3]);

  grTexDownloadMipMap(GR_TMU0, st->addr_argb, GR_MIPMAPLEVELMASK_BOTH, &st->ti_argb);
  grTexSource(GR_TMU0, st->addr_argb, GR_MIPMAPLEVELMASK_BOTH, &st->ti_argb);
  bench_draw_quad(&st->quads[3][0], &st->quads[3][1], &st->quads[3][2], &st->quads[3][3]);

  /* Decoded equivalent texel bandwidth (4 x 128x128 x 4B = 0.25 MB/frame) */
  *out_work_units =
      (double)(4U * TEXFMT_SZ * TEXFMT_SZ * 4U) / (1024.0 * 1024.0);
}

static void texfmt_teardown(BenchContext* ctx) {
  free(ctx->scene_state);
  ctx->scene_state = NULL;
}

/* =========================================================================
 * Scene 13 (Glide 3 Banshee+): rtt - TEXTUREBUFFER Render-to-Texture
 * ========================================================================= */
#define RTT_SZ 128

typedef struct RttBenchState {
  GrTexInfo rtt_ti;
  FxU32 rtt_addr;
  BenchVertex screen_quads[4][4];
} RttBenchState;

static int rtt_setup(BenchContext* ctx) {
  RttBenchState* st;
  FxU32 req;
  int i;
  float hw = (float)ctx->width * 0.5f;
  float hh = (float)ctx->height * 0.5f;

  if (!bench_has_ext(ctx, "TEXTUREBUFFER") || !ctx->fn_textureBufferExt) {
    return 0;
  }
  st = (RttBenchState*)calloc(1, sizeof(RttBenchState));
  if (!st) return 0;

  bench_init_tex_info(&st->rtt_ti, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_RGB_565, NULL);
  st->rtt_addr = ctx->tmu0_min;
  req = grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &st->rtt_ti);
  if (st->rtt_addr + req > ctx->scene_tmu_limit) {
    free(st);
    return 0;
  }

  for (i = 0; i < 4; ++i) {
    float x0 = (i & 1) ? hw : 0.0f;
    float y0 = (i & 2) ? hh : 0.0f;
    bench_set_vert(&st->screen_quads[i][0], x0,      y0,      0.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 0.0f,   0.0f);
    bench_set_vert(&st->screen_quads[i][1], x0 + hw, y0,      0.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 255.0f, 0.0f);
    bench_set_vert(&st->screen_quads[i][2], x0 + hw, y0 + hh, 0.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 255.0f, 255.0f);
    bench_set_vert(&st->screen_quads[i][3], x0,      y0 + hh, 0.0f, 1.0f, 255.0f, 255.0f, 255.0f, 255.0f, 0.0f,   255.0f);
  }

  ctx->scene_state = st;
  return 1;
}

static void rtt_render(BenchContext* ctx, int frame_idx, double t_sec,
                       double* out_work_units) {
  RttBenchState* st = (RttBenchState*)ctx->scene_state;
  float ang = (float)t_sec * 2.5f + (float)frame_idx * 0.05f;
  float c = (float)cos((double)ang) * 44.0f;
  float s = (float)sin((double)ang) * 44.0f;
  BenchVertex tri[3];
  int i;

  bench_reset_pipeline(ctx->width, ctx->height);
  bench_reset_vsa_state(ctx);
  grBufferClear(0x00101828UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);

  /* Pass 1: Render dynamic 3D triangle into 128x128 TEXTUREBUFFER */
  ctx->fn_textureBufferExt(GR_TMU0, st->rtt_addr,
                           BENCH_LOD_128, BENCH_LOD_128, BENCH_ASPECT_1x1,
                           GR_TEXFMT_RGB_565, GR_MIPMAPLEVELMASK_BOTH);
  grRenderBuffer(GR_BUFFER_TEXTUREBUFFER_EXT);
  grClipWindow(0, 0, RTT_SZ, RTT_SZ);
  grBufferClear(0x00201038UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);
  bench_set_vert(&tri[0], 64.0f + c, 64.0f + s, 0.0f, 1.0f, 255.0f, 60.0f,  40.0f, 255.0f, 0.0f, 0.0f);
  bench_set_vert(&tri[1], 64.0f - s, 64.0f + c, 0.0f, 1.0f, 40.0f,  255.0f, 80.0f, 255.0f, 0.0f, 0.0f);
  bench_set_vert(&tri[2], 64.0f - c, 64.0f - s, 0.0f, 1.0f, 60.0f,  120.0f, 255.0f, 255.0f, 0.0f, 0.0f);
  grDrawTriangle(&tri[0], &tri[1], &tri[2]);

  /* Pass 2: Return to backbuffer and map rendered texture across 4 quads */
  grRenderBuffer(GR_BUFFER_BACKBUFFER);
  grClipWindow(0, 0, (FxU32)ctx->width, (FxU32)ctx->height);
  grTexSource(GR_TMU0, st->rtt_addr, GR_MIPMAPLEVELMASK_BOTH, &st->rtt_ti);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
  for (i = 0; i < 4; ++i) {
    bench_draw_quad(&st->screen_quads[i][0], &st->screen_quads[i][1],
                    &st->screen_quads[i][2], &st->screen_quads[i][3]);
  }

  *out_work_units = 1.0; /* RTT passes/frame (reported as RTT/s) */
}

static void rtt_teardown(BenchContext* ctx) {
  bench_reset_vsa_state(ctx);
  free(ctx->scene_state);
  ctx->scene_state = NULL;
}

/* =========================================================================
 * Scene 14 (Glide 3 VSA-100): tbuffer - 4x Spatial AA + T-Buffer Mask + Resolve
 * ========================================================================= */
typedef struct TBufferBenchState {
  FxU16 tex[128 * 128];
  GrTexInfo ti;
  FxU32 tex_addr;
  BenchVertex mesh[4][4];
} TBufferBenchState;

static int tbuffer_setup(BenchContext* ctx) {
  TBufferBenchState* st;
  int i;
  float w = (float)ctx->width;
  float h = (float)ctx->height;

  if (!bench_has_ext(ctx, "PIXEXT") || !ctx->fn_tbufWriteMask ||
      ctx->aa_samples < 2) {
    return 0;
  }
  st = (TBufferBenchState*)calloc(1, sizeof(TBufferBenchState));
  if (!st) return 0;

  gen_checker_rgb565(st->tex, 128, 8, 0xE6B0U, 0x194BU, 0);
  bench_init_tex_info(&st->ti, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_1x1, GR_TEXFMT_RGB_565, st->tex);
  st->tex_addr = ctx->tmu0_min;
  grTexDownloadMipMap(GR_TMU0, st->tex_addr, GR_MIPMAPLEVELMASK_BOTH, &st->ti);

  for (i = 0; i < 4; ++i) {
    float dx = (float)(i - 2) * 3.0f;
    float dy = (float)(i - 1) * 2.0f;
    bench_set_vert(&st->mesh[i][0], 0.08f * w + dx, 0.08f * h + dy, 0.0f, 1.0f, 240.0f, 220.0f, 200.0f, 255.0f, 0.0f,   0.0f);
    bench_set_vert(&st->mesh[i][1], 0.92f * w + dx, 0.12f * h + dy, 0.0f, 1.0f, 220.0f, 240.0f, 210.0f, 255.0f, 255.0f, 0.0f);
    bench_set_vert(&st->mesh[i][2], 0.88f * w + dx, 0.92f * h + dy, 0.0f, 1.0f, 200.0f, 210.0f, 245.0f, 255.0f, 255.0f, 255.0f);
    bench_set_vert(&st->mesh[i][3], 0.12f * w + dx, 0.88f * h + dy, 0.0f, 1.0f, 245.0f, 200.0f, 210.0f, 255.0f, 0.0f,   255.0f);
  }

  ctx->scene_state = st;
  return 1;
}

static void tbuffer_render(BenchContext* ctx, int frame_idx, double t_sec,
                           double* out_work_units) {
  TBufferBenchState* st = (TBufferBenchState*)ctx->scene_state;
  int s;
  int samples = (ctx->aa_samples >= 4) ? 4 : 2;
  (void)frame_idx;
  (void)t_sec;

  bench_reset_pipeline(ctx->width, ctx->height);
  grEnable(GR_AA_MULTI_SAMPLE);
  ctx->fn_tbufWriteMask(0x0Fu);
  grBufferClear(0x00102030UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);

  grTexSource(GR_TMU0, st->tex_addr, GR_MIPMAPLEVELMASK_BOTH, &st->ti);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

  /* Render 1 jittered frame per T-Buffer sample slot (DoF / Motion Blur) */
  for (s = 0; s < samples; ++s) {
    ctx->fn_tbufWriteMask(1U << (unsigned)s);
    grBufferClear(0x00102030UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);
    bench_draw_quad(&st->mesh[s][0], &st->mesh[s][1],
                    &st->mesh[s][2], &st->mesh[s][3]);
  }
  ctx->fn_tbufWriteMask(0x0Fu);

  *out_work_units =
      ((double)ctx->width * (double)ctx->height * (double)samples) / 1e6;
}

static void tbuffer_teardown(BenchContext* ctx) {
  bench_reset_vsa_state(ctx);
  free(ctx->scene_state);
  ctx->scene_state = NULL;
}

#endif /* GLIDE_VERSION == 3 */

/* =========================================================================
 * Scene Registry Table (Append new benchmark tests here!)
 * ========================================================================= */
static const BenchScene kBenchScenes[] = {
  /* Core 6 scenes (GlideMark baseline — unchanged for cross-milestone parity) */
  { "fillrate",    "Fillrate (16x Alpha Overdraw)",      "MPix/s",  1.0, 30.0, BENCH_CAT_CORE,   0, fillrate_setup,  fillrate_render,  fillrate_teardown  },
  { "tris",        "Triangles (4K-Tri Waving Mesh)",     "KTri/s",  1.0, 30.0, BENCH_CAT_CORE,   0, tris_setup,      tris_render,      tris_teardown      },
  { "state",       "State Thrash (120 Batches/Frame)",   "KDraw/s", 1.0, 30.0, BENCH_CAT_CORE,   0, state_setup,     state_render,     state_teardown     },
  { "tex",         "Texture Stream (64x P8 + Palettes)", "MB/s",    1.0, 30.0, BENCH_CAT_CORE,   0, tex_setup,       tex_render,       tex_teardown       },
  { "lfb",         "LFB HUD (3D + Sparse 565 Write)",    "MB/s",    1.0, 30.0, BENCH_CAT_CORE,   0, lfb_setup,       lfb_render,       lfb_teardown       },
  { "multipass",   "Multipass (Fog ADD2/MULT2 + Mips)",  "KTri/s",  1.0, 30.0, BENCH_CAT_CORE,   0, multipass_setup, multipass_render, multipass_teardown },
  /* Extended Glide 2.x + 3.x scenes */
  { "fog",         "Table Fog (12x Full-Screen W-Fog)",  "MPix/s",  1.0, 30.0, BENCH_CAT_EXT,    0, fog_setup,       fog_render,       fog_teardown       },
  { "multitex",    "Dual-TMU (10x TMU0*TMU1 Bilinear)",  "MPix/s",  1.0, 30.0, BENCH_CAT_EXT,    0, multitex_setup,  multitex_render,  multitex_teardown  },
  { "lfb_read",    "LFB Roundtrip (Full-Frame 565 R+W)", "MB/s",    1.0, 30.0, BENCH_CAT_EXT,    0, lfb_read_setup,  lfb_read_render,  lfb_read_teardown  }
#if GLIDE_VERSION == 3
  ,
  /* Glide 3.x H3 / VSA-100 extension scenes */
  { "stencil",     "VSA-100 Stencil (PIXEXT Shadows)",   "MPix/s",  1.0, 30.0, BENCH_CAT_VSA100, 0, stencil_setup,     stencil_render,     stencil_teardown     },
  { "ext_combine", "VSA-100 Combine (COMBINE+BlendExt)", "MPix/s",  1.0, 30.0, BENCH_CAT_VSA100, 0, ext_combine_setup, ext_combine_render, ext_combine_teardown },
  { "texfmt",      "VSA-100 TEXFMT (DXT1/5+FXT1+8888)",  "MB/s",    1.0, 30.0, BENCH_CAT_VSA100, 0, texfmt_setup,      texfmt_render,      texfmt_teardown      },
  { "rtt",         "TEXTUREBUFFER RTT (128x128 Live)",   "RTT/s",   1.0, 30.0, BENCH_CAT_VSA100, 0, rtt_setup,         rtt_render,         rtt_teardown         },
  { "tbuffer",     "VSA-100 T-Buffer (4x AA + Resolve)", "MPix/s",  1.0, 30.0, BENCH_CAT_VSA100, 4, tbuffer_setup,     tbuffer_render,     tbuffer_teardown     }
#endif
};

const BenchScene* bench_get_scenes(int* out_count) {
  if (out_count) {
    *out_count = (int)(sizeof(kBenchScenes) / sizeof(kBenchScenes[0]));
  }
  return kBenchScenes;
}

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
 * Scene Registry Table (Append new benchmark tests here!)
 * ========================================================================= */
static const BenchScene kBenchScenes[] = {
  { "fillrate",  "Fillrate (16x Alpha Overdraw)",      "MPix/s",  1.0, 30.0, fillrate_setup,  fillrate_render,  fillrate_teardown  },
  { "tris",      "Triangles (4K-Tri Waving Mesh)",     "KTri/s",  1.0, 30.0, tris_setup,      tris_render,      tris_teardown      },
  { "state",     "State Thrash (120 Batches/Frame)",   "KDraw/s", 1.0, 30.0, state_setup,     state_render,     state_teardown     },
  { "tex",       "Texture Stream (64x P8 + Palettes)", "MB/s",    1.0, 30.0, tex_setup,       tex_render,       tex_teardown       },
  { "lfb",       "LFB HUD (3D + Sparse 565 Write)",    "MB/s",    1.0, 30.0, lfb_setup,       lfb_render,       lfb_teardown       },
  { "multipass", "Multipass (Fog ADD2/MULT2 + Mips)",  "KTri/s",  1.0, 30.0, multipass_setup, multipass_render, multipass_teardown }
};

const BenchScene* bench_get_scenes(int* out_count) {
  if (out_count) {
    *out_count = (int)(sizeof(kBenchScenes) / sizeof(kBenchScenes[0]));
  }
  return kBenchScenes;
}

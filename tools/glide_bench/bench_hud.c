/*
 * bench_hud.c - Pure-Glide Textured-Quad HUD Overlay for glide_bench
 *
 * Uses a compact embedded 5x7 ASCII bitmap font uploaded once as a 128x64
 * GR_TEXFMT_ARGB_4444 texture at the top of TMU0 memory. NEVER locks or
 * touches LFB so it has zero framebuffer read/write stall interference.
 */

#include "glide_bench.h"

#define HUD_ATLAS_W 128
#define HUD_ATLAS_H 64
#define HUD_CELL_W  8
#define HUD_CELL_H  8
#define HUD_COLS    16  /* 128 / 8 */

/* 5x7 font bitmaps for ASCII 32..95 (64 glyphs; lowercase mapped to upper).
 * Each glyph is 7 rows; bits 4..0 represent columns left-to-right.
 */
static const unsigned char kFont5x7[64][7] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 32 ' ' */
  {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* 33 '!' */
  {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* 34 '"' */
  {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, /* 35 '#' */
  {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, /* 36 '$' */
  {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, /* 37 '%' */
  {0x08,0x14,0x14,0x08,0x15,0x12,0x0D}, /* 38 '&' */
  {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, /* 39 '\'' */
  {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* 40 '(' */
  {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* 41 ')' */
  {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, /* 42 '*' */
  {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* 43 '+' */
  {0x00,0x00,0x00,0x00,0x00,0x04,0x08}, /* 44 ',' */
  {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* 45 '-' */
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, /* 46 '.' */
  {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, /* 47 '/' */
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 48 '0' */
  {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 49 '1' */
  {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, /* 50 '2' */
  {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, /* 51 '3' */
  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 52 '4' */
  {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 53 '5' */
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 54 '6' */
  {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 55 '7' */
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 56 '8' */
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 57 '9' */
  {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, /* 58 ':' */
  {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}, /* 59 ';' */
  {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, /* 60 '<' */
  {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* 61 '=' */
  {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, /* 62 '>' */
  {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, /* 63 '?' */
  {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, /* 64 '@' */
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* 65 'A' */
  {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* 66 'B' */
  {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* 67 'C' */
  {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, /* 68 'D' */
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* 69 'E' */
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* 70 'F' */
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* 71 'G' */
  {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* 72 'H' */
  {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* 73 'I' */
  {0x07,0x02,0x02,0x02,0x12,0x12,0x0C}, /* 74 'J' */
  {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* 75 'K' */
  {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* 76 'L' */
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* 77 'M' */
  {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* 78 'N' */
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* 79 'O' */
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* 80 'P' */
  {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* 81 'Q' */
  {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* 82 'R' */
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* 83 'S' */
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* 84 'T' */
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* 85 'U' */
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* 86 'V' */
  {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* 87 'W' */
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* 88 'X' */
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* 89 'Y' */
  {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* 90 'Z' */
  {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, /* 91 '[' */
  {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, /* 92 '\\' */
  {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, /* 93 ']' */
  {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, /* 94 '^' */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}  /* 95 '_' */
};

static FxU16 g_hud_atlas[HUD_ATLAS_W * HUD_ATLAS_H];

int bench_hud_init(BenchContext* ctx) {
  int ch, row, col;
  FxU32 req_bytes;

  memset(g_hud_atlas, 0, sizeof(g_hud_atlas));

  for (ch = 0; ch < 64; ++ch) {
    int cx = (ch % HUD_COLS) * HUD_CELL_W;
    int cy = (ch / HUD_COLS) * HUD_CELL_H;
    for (row = 0; row < 7; ++row) {
      unsigned char bits = kFont5x7[ch][row];
      for (col = 0; col < 5; ++col) {
        if (bits & (1U << (4 - col))) {
          /* Crisp white glyph pixel (ARGB 4444 = 0xFFFF) */
          g_hud_atlas[(cy + row) * HUD_ATLAS_W + (cx + col)] = 0xFFFFU;
        }
      }
    }
  }

  bench_init_tex_info(&ctx->hud_tex_info, BENCH_LOD_128, BENCH_LOD_128,
                      BENCH_ASPECT_2x1, GR_TEXFMT_ARGB_4444, g_hud_atlas);
  req_bytes = grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH,
                                      &ctx->hud_tex_info);
  if (ctx->tmu0_max <= ctx->tmu0_min + req_bytes + 4096U) {
    ctx->hud_enabled = 0;
    return 0;
  }

  /* Place HUD atlas at top of TMU0 (8-byte aligned), reserving below for scenes */
  ctx->hud_tex_addr = (ctx->tmu0_max - req_bytes - 256U) & ~7U;
  ctx->scene_tmu_limit = ctx->hud_tex_addr;

  grTexDownloadMipMap(GR_TMU0, ctx->hud_tex_addr, GR_MIPMAPLEVELMASK_BOTH,
                      &ctx->hud_tex_info);
  return 1;
}

static void hud_draw_string(float x, float y, float scale,
                            float r, float g, float b, const char* text) {
  float cx = x;
  float adv = 6.0f * scale;
  float gw = 5.0f * scale;
  float gh = 7.0f * scale;

  /* Glide 2.x/3.x 128x64 (2x1 aspect) texture coordinates:
   * S spans [0..256] across 128 texels (2.0 per texel),
   * T spans [0..128] across 64 texels (2.0 per texel).
   */
  while (*text) {
    unsigned char uc = (unsigned char)*text++;
    int idx;
    float s0, t0, s1, t1;
    BenchVertex v0, v1, v2, v3;

    if (uc >= 'a' && uc <= 'z') uc = (unsigned char)(uc - 'a' + 'A');
    if (uc == '|') uc = 'I';
    if (uc < 32 || uc > 95) uc = ' ';

    if (uc != ' ') {
      idx = (int)(uc - 32);
      s0 = (float)((idx % HUD_COLS) * HUD_CELL_W) * 2.0f;
      t0 = (float)((idx / HUD_COLS) * HUD_CELL_H) * 2.0f;
      s1 = s0 + 5.0f * 2.0f;
      t1 = t0 + 7.0f * 2.0f;

      bench_set_vert(&v0, cx,      y,      0.0f, 1.0f, r, g, b, 255.0f, s0, t0);
      bench_set_vert(&v1, cx + gw, y,      0.0f, 1.0f, r, g, b, 255.0f, s1, t0);
      bench_set_vert(&v2, cx + gw, y + gh, 0.0f, 1.0f, r, g, b, 255.0f, s1, t1);
      bench_set_vert(&v3, cx,      y + gh, 0.0f, 1.0f, r, g, b, 255.0f, s0, t1);
      bench_draw_quad(&v0, &v1, &v2, &v3);
    }
    cx += adv;
  }
}

void bench_hud_draw(BenchContext* ctx,
                    const char* scene_name,
                    int scene_idx,
                    int scene_total,
                    double inst_fps,
                    double avg_ms,
                    double elapsed_s,
                    double target_s) {
  char line1[96];
  char line2[96];
  float bx0 = 8.0f, by0 = 8.0f;
  float bx1 = (float)ctx->width - 8.0f;
  float by1 = 44.0f;
  float scale = (ctx->width >= 800) ? 2.0f : 1.5f;
  BenchVertex q0, q1, q2, q3;

  if (!ctx->hud_enabled) return;
  if (scale > 1.5f) by1 = 52.0f;

  /* Configure clean 2D overlay state (no depth, no fog, no chromakey) */
  grClipWindow(0, 0, (FxU32)ctx->width, (FxU32)ctx->height);
  grCullMode(GR_CULL_DISABLE);
  grColorMask(FXTRUE, FXTRUE);
  grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
  grDepthMask(FXFALSE);
  grFogMode(GR_FOG_DISABLE);
  grChromakeyMode(GR_CHROMAKEY_DISABLE);
  grAlphaTestFunction(GR_CMP_ALWAYS);

  /* 1) Semi-transparent dark backdrop quad */
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
  grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                       GR_BLEND_ONE, GR_BLEND_ZERO);

  bench_set_vert(&q0, bx0, by0, 0.0f, 1.0f, 10.0f, 14.0f, 24.0f, 192.0f, 0.0f, 0.0f);
  bench_set_vert(&q1, bx1, by0, 0.0f, 1.0f, 10.0f, 14.0f, 24.0f, 192.0f, 0.0f, 0.0f);
  bench_set_vert(&q2, bx1, by1, 0.0f, 1.0f, 10.0f, 14.0f, 24.0f, 192.0f, 0.0f, 0.0f);
  bench_set_vert(&q3, bx0, by1, 0.0f, 1.0f, 10.0f, 14.0f, 24.0f, 192.0f, 0.0f, 0.0f);
  bench_draw_quad(&q0, &q1, &q2, &q3);

  /* 2) Bind font atlas and draw crisp textured glyph quads */
  grTexSource(GR_TMU0, ctx->hud_tex_addr, GR_MIPMAPLEVELMASK_BOTH,
              &ctx->hud_tex_info);
  grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED,
                  GR_TEXTUREFILTER_POINT_SAMPLED);
  grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
  grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
               FXFALSE, FXFALSE);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                 GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
  grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

  snprintf(line1, sizeof(line1), "GLIDE BENCH [%d/%d] %-10s %dx%d",
           scene_idx + 1, scene_total, scene_name, ctx->width, ctx->height);
  if (target_s > 0.0) {
    snprintf(line2, sizeof(line2), "FPS: %6.1f  AVG: %5.2f MS  T: %.1f/%.0fS",
             inst_fps, avg_ms, elapsed_s, target_s);
  } else {
    snprintf(line2, sizeof(line2), "FPS: %6.1f  AVG: %5.2f MS  T: %.1fS",
             inst_fps, avg_ms, elapsed_s);
  }

  hud_draw_string(bx0 + 8.0f, by0 + 5.0f, scale, 96.0f, 224.0f, 255.0f, line1);
  hud_draw_string(bx0 + 8.0f, by0 + 6.0f + 9.0f * scale, scale,
                  255.0f, 232.0f, 112.0f, line2);
}

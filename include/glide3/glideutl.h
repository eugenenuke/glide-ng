/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * glide-ng clean-room ABI header: Glide 3.x utility structures (3DF texture
 * descriptors) and utility function declarations.
 */
#ifndef __GLIDEUTL_H__
#define __GLIDEUTL_H__

#ifdef __cplusplus
extern "C" {
#endif

/* 3DF texture file metadata and palette/NCC table structures */
typedef struct {
  FxU32             width, height;
  int               small_lod, large_lod;
  GrAspectRatio_t   aspect_ratio;
  GrTextureFormat_t format;
} Gu3dfHeader;

typedef struct {
  FxU8  yRGB[16];
  FxI16 iRGB[4][3];
  FxI16 qRGB[4][3];
  FxU32 packed_data[12];
} GuNccTable;

typedef struct {
  FxU32 data[256];
} GuTexPalette;

typedef union {
  GuNccTable   nccTable;
  GuTexPalette palette;
} GuTexTable;

typedef struct {
  Gu3dfHeader header;
  GuTexTable  table;
  void       *data;
  FxU32       mem_required;
} Gu3dfInfo;

#ifndef FX_GLIDE_NO_FUNC_PROTO

/* Gamma correction */
FX_ENTRY void FX_CALL
guGammaCorrectionRGB(FxFloat red, FxFloat green, FxFloat blue);

/* Fog table generation */
FX_ENTRY float FX_CALL
guFogTableIndexToW(int i);

FX_ENTRY void FX_CALL
guFogGenerateExp(GrFog_t *fogtable, float density);

FX_ENTRY void FX_CALL
guFogGenerateExp2(GrFog_t *fogtable, float density);

FX_ENTRY void FX_CALL
guFogGenerateLinear(GrFog_t *fogtable, float nearZ, float farZ);

/* 3DF texture loading and RLE16 encoding */
FX_ENTRY FxBool FX_CALL
gu3dfGetInfo(const char *filename, Gu3dfInfo *info);

FX_ENTRY FxBool FX_CALL
gu3dfLoad(const char *filename, Gu3dfInfo *data);

FX_ENTRY int FX_CALL
guEncodeRLE16(void *dst, void *src, FxU32 width, FxU32 height);

#endif /* FX_GLIDE_NO_FUNC_PROTO */

#ifdef __cplusplus
}
#endif

#endif /* __GLIDEUTL_H__ */

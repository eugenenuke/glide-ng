/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * glide-ng clean-room ABI header: Glide 2.x utility function declarations
 * and multipass texture combine constants.
 */
#ifndef __GLIDEUTL_H__
#define __GLIDEUTL_H__

#ifdef __cplusplus
extern "C" {
#endif

#define GLIDE_NUM_VIRTUAL_TMU 2

typedef FxU32 GrMPTextureCombineFnc_t;
#define GR_MPTEXTURECOMBINE_ADD        0x0
#define GR_MPTEXTURECOMBINE_MULTIPLY   0x1
#define GR_MPTEXTURECOMBINE_DETAIL0    0x2
#define GR_MPTEXTURECOMBINE_DETAIL1    0x3
#define GR_MPTEXTURECOMBINE_TRILINEAR0 0x4
#define GR_MPTEXTURECOMBINE_TRILINEAR1 0x5
#define GR_MPTEXTURECOMBINE_SUBTRACT   0x6

#ifndef FX_GLIDE_NO_FUNC_PROTO

/* Clipped primitive helpers */
FX_ENTRY void FX_CALL
guAADrawTriangleWithClip(const GrVertex *a, const GrVertex *b,
                         const GrVertex *c);

FX_ENTRY void FX_CALL
guDrawTriangleWithClip(const GrVertex *a, const GrVertex *b,
                       const GrVertex *c);

FX_ENTRY void FX_CALL
guDrawPolygonVertexListWithClip(int nverts, const GrVertex vlist[]);

/* Combiner and encoding helpers */
FX_ENTRY void FX_CALL
guAlphaSource(GrAlphaSource_t mode);

FX_ENTRY void FX_CALL
guColorCombineFunction(GrColorCombineFnc_t fnc);

FX_ENTRY int FX_CALL
guEncodeRLE16(void *dst, void *src, FxU32 width, FxU32 height);

FX_ENTRY FxU16 * FX_CALL
guTexCreateColorMipMap(void);

#ifdef GLIDE3
FX_ENTRY void FX_CALL
guGammaCorrectionRGB(FxFloat red, FxFloat green, FxFloat blue);
#endif

/* Fog table generation */
FX_ENTRY float FX_CALL
guFogTableIndexToW(int i);

FX_ENTRY void FX_CALL
guFogGenerateExp(GrFog_t fogtable[], float density);

FX_ENTRY void FX_CALL
guFogGenerateExp2(GrFog_t fogtable[], float density);

FX_ENTRY void FX_CALL
guFogGenerateLinear(GrFog_t fogtable[], float nearZ, float farZ);

/* Byte-order conversion */
FX_ENTRY FxU32 FX_CALL
guEndianSwapWords(FxU32 value);

FX_ENTRY FxU16 FX_CALL
guEndianSwapBytes(FxU16 value);

/* 3DF texture file loaders */
FX_ENTRY FxBool FX_CALL
gu3dfGetInfo(const char *filename, Gu3dfInfo *info);

FX_ENTRY FxBool FX_CALL
gu3dfLoad(const char *filename, Gu3dfInfo *data);

/* Multipass TMU helpers */
FX_ENTRY void FX_CALL
guMPInit(void);

FX_ENTRY void FX_CALL
guMPTexCombineFunction(GrMPTextureCombineFnc_t tc_fnc);

FX_ENTRY void FX_CALL
guMPTexSource(GrChipID_t virtual_tmu, GrMipMapId_t mmid);

FX_ENTRY void FX_CALL
guMPDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c);

/* Legacy framebuffer region and movie capture utilities */
FX_ENTRY void FX_CALL
guFbReadRegion(const int src_x, const int src_y, const int w, const int h,
               const void *dst, const int strideInBytes);

FX_ENTRY void FX_CALL
guFbWriteRegion(const int dst_x, const int dst_y, const int w, const int h,
                const void *src, const int strideInBytes);

FX_ENTRY void FX_CALL
guMovieSetName(const char *name);

FX_ENTRY void FX_CALL
guMovieStart(void);

FX_ENTRY void FX_CALL
guMovieStop(void);

#endif /* FX_GLIDE_NO_FUNC_PROTO */

#ifdef __cplusplus
}
#endif

#endif /* __GLIDEUTL_H__ */

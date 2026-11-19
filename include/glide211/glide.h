/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * glide-ng clean-room ABI header: Glide 2.11 (glide.dll / libglide.so)
 * pre-2.3 SST-1 types, constants, and C function prototypes.
 */
#ifndef __GLIDE211_GLIDE_H__
#define __GLIDE211_GLIDE_H__

/* Shared Glide 2.x fundamental types, enumerations, structures, and utilities */
#include "../glide2/glide.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed SST-1 linear framebuffer scanline stride in pixels (Glide 2.11 ABI) */
#define GR_LFB_STRIDE_PIXELS_211 1024

#ifndef FX_GLIDE_NO_FUNC_PROTO

/* Glide 2.11 SST-1 initialization and VGA pass-through control */
FX_ENTRY FxBool FX_CALL
grSstOpen(GrScreenResolution_t screen_resolution,
          GrScreenRefresh_t    refresh_rate,
          GrColorFormat_t      color_format,
          GrOriginLocation_t   origin_location,
          GrSmoothingMode_t    smoothing_filter,
          int                  num_buffers);

FX_ENTRY void FX_CALL
grSstPassthruMode(GrPassthruMode_t mode);

/* Glide 2.11 legacy linear framebuffer API (fixed 1024-pixel stride) */
FX_ENTRY void FX_CALL
grLfbBegin(void);

FX_ENTRY void FX_CALL
grLfbEnd(void);

FX_ENTRY void FX_CALL
grLfbWriteMode(GrLfbWriteMode_t mode);

FX_ENTRY void FX_CALL
grLfbOrigin(GrOriginLocation_t origin);

FX_ENTRY void FX_CALL
grLfbBypassMode(GrLfbBypassMode_t mode);

FX_ENTRY const void * FX_CALL
grLfbGetReadPtr(GrBuffer_t buffer);

FX_ENTRY void * FX_CALL
grLfbGetWritePtr(GrBuffer_t buffer);

#endif /* FX_GLIDE_NO_FUNC_PROTO */

#ifdef __cplusplus
}
#endif

#endif /* __GLIDE211_GLIDE_H__ */

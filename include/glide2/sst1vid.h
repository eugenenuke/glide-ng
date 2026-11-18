/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * glide-ng clean-room ABI header: Glide 2.x display resolution and refresh
 * rate constants.
 */
#ifndef __SST1VID_H__
#define __SST1VID_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef FxI32 GrScreenRefresh_t;
#define GR_REFRESH_60Hz         0x0
#define GR_REFRESH_70Hz         0x1
#define GR_REFRESH_72Hz         0x2
#define GR_REFRESH_75Hz         0x3
#define GR_REFRESH_80Hz         0x4
#define GR_REFRESH_90Hz         0x5
#define GR_REFRESH_100Hz        0x6
#define GR_REFRESH_85Hz         0x7
#define GR_REFRESH_120Hz        0x8
#define GR_REFRESH_NONE         0xff

typedef FxI32 GrScreenResolution_t;
#define GR_RESOLUTION_320x200   0x0
#define GR_RESOLUTION_320x240   0x1
#define GR_RESOLUTION_400x256   0x2
#define GR_RESOLUTION_512x384   0x3
#define GR_RESOLUTION_640x200   0x4
#define GR_RESOLUTION_640x350   0x5
#define GR_RESOLUTION_640x400   0x6
#define GR_RESOLUTION_640x480   0x7
#define GR_RESOLUTION_800x600   0x8
#define GR_RESOLUTION_960x720   0x9
#define GR_RESOLUTION_856x480   0xa
#define GR_RESOLUTION_512x256   0xb
#define GR_RESOLUTION_1024x768  0xC
#define GR_RESOLUTION_1280x1024 0xD
#define GR_RESOLUTION_1600x1200 0xE
#define GR_RESOLUTION_400x300   0xF
#define GR_RESOLUTION_NONE      0xff

#ifdef GR_RESOLUTION_MAX
#undef GR_RESOLUTION_MAX
#endif
#ifdef GR_RESOLUTION_MIN
#undef GR_RESOLUTION_MIN
#endif
#define GR_RESOLUTION_MIN       GR_RESOLUTION_320x200
#define GR_RESOLUTION_MAX       GR_RESOLUTION_1600x1200

#ifdef __cplusplus
}
#endif

#endif /* __SST1VID_H__ */

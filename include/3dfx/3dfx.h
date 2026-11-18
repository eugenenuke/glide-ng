/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * glide-ng clean-room ABI header: fundamental scalar, color, and calling
 * convention definitions for Glide 2.x and Glide 3.x binary compatibility.
 */
#ifndef __3DFX_H__
#define __3DFX_H__

/* Fixed-width and platform scalar typedefs */
typedef unsigned char       FxU8;
typedef signed   char       FxI8;
typedef unsigned short      FxU16;
typedef signed   short      FxI16;
#if defined(__DOS__) || defined(__MSDOS__) || defined(_WIN32) || defined(macintosh)
typedef signed   long       FxI32;
typedef unsigned long       FxU32;
#else
typedef signed   int        FxI32;
typedef unsigned int        FxU32;
#endif

typedef long long           FxI64;
typedef unsigned long long  FxU64;

typedef unsigned long       AnyPtr;
typedef int                 FxBool;
typedef float               FxFloat;
typedef double              FxDouble;

/* Color representations */
typedef unsigned long                FxColor_t;
typedef struct { float r, g, b, a; } FxColor4;

/* Boolean constants */
#define FXTRUE    1
#define FXFALSE   0

/* Utility macros */
#define FXUNUSED(a) ((void)(a))
#define FXBIT(i)    (1 << (i))

/* Symbol linkage and ABI calling convention macros */
#if defined(_MSC_VER) || defined(__MSC__)
#  if defined(MSVC16)
#    define FX_ENTRY
#    define FX_CALL
#  else
#    define FX_ENTRY extern
#    define FX_CALL  __stdcall
#  endif
#elif defined(__WATCOMC__) || defined(__MINGW32__) || defined(_WIN32) || \
      defined(__IBMC__) || defined(__IBMCPP__)
#  define FX_ENTRY extern
#  define FX_CALL  __stdcall
#elif defined(__DJGPP__) || defined(__unix__) || defined(__linux__) || \
      defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
      defined(__APPLE__) || defined(macintosh)
#  define FX_ENTRY extern
#  define FX_CALL
#else
#  define FX_ENTRY extern
#  define FX_CALL
#endif

/* 32-bit DOS Watcom DPMI/register helpers */
#if defined(__WATCOMC__)
#  undef FP_SEG
#  undef FP_OFF
#  define REGW(a, b)          ((a).w.b)
#  define REGB(a, b)          ((a).h.b)
#  define INT86(a, b, c)      int386((a), (b), (c))
#  define INT86X(a, b, c, d)  int386x((a), (b), (c), (d))
#  define RM_SEG(a)           ((((FxU32)(a)) & 0x000F0000U) >> 4)
#  define RM_OFF(a)           ((FxU16)(a))
#endif

#endif /* __3DFX_H__ */

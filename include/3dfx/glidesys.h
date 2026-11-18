/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * glide-ng clean-room ABI header: compile-time platform, endianness, and
 * TMU count feature macros.
 */
#ifndef __GLIDESYS_H__
#define __GLIDESYS_H__

/* Endianness bitmask (bits 30..31) */
#define GLIDE_ENDIAN_SHIFT      30
#define GLIDE_ENDIAN_LITTLE     (0x1 << GLIDE_ENDIAN_SHIFT)
#define GLIDE_ENDIAN_BIG        (0x2 << GLIDE_ENDIAN_SHIFT)

/* Operating system bitmask (bits 0..6) */
#define GLIDE_OS_SHIFT          0
#define GLIDE_OS_UNIX           0x1
#define GLIDE_OS_DOS32          0x2
#define GLIDE_OS_WIN32          0x4
#define GLIDE_OS_MACOS          0x8
#define GLIDE_OS_OS2            0x10
#define GLIDE_OS_OTHER          0x40

/* SST target bitmask (bits 7..8) */
#define GLIDE_SST_SHIFT         7
#define GLIDE_SST_HW            (0x2 << GLIDE_SST_SHIFT)

/* Hardware architecture bitmask (bits 9..13) */
#define GLIDE_HW_SHIFT          9
#define GLIDE_HW_SST1           (0x1 << GLIDE_HW_SHIFT)
#define GLIDE_HW_SST96          (0x2 << GLIDE_HW_SHIFT)
#define GLIDE_HW_H3             (0x4 << GLIDE_HW_SHIFT)
#define GLIDE_HW_CVG            (0x10 << GLIDE_HW_SHIFT)

/* Normalize Win32 preprocessor macro */
#ifndef __WIN32__
#  if defined(_WIN32) || defined(WIN32) || defined(__NT__)
#    define __WIN32__
#  endif
#endif

/* Detect target byte order */
#if defined(__IRIX__) || defined(__sparc__) || defined(MACOS)
#  define GLIDE_ENDIAN          GLIDE_ENDIAN_BIG
#else
#  define GLIDE_ENDIAN          GLIDE_ENDIAN_LITTLE
#endif

/* Detect target operating system */
#if defined(__unix__) || defined(__linux__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__) || defined(__IRIX__) || \
    defined(__sparc__) || defined(__APPLE__)
#  define GLIDE_OS              GLIDE_OS_UNIX
#elif defined(__DOS__) || defined(__MSDOS__)
#  define GLIDE_OS              GLIDE_OS_DOS32
#elif defined(__WIN32__)
#  define GLIDE_OS              GLIDE_OS_WIN32
#elif defined(macintosh)
#  define GLIDE_OS              GLIDE_OS_MACOS
#else
#  error "Unsupported target OS for Glide ABI headers"
#endif

#define GLIDE_SST               GLIDE_SST_HW

/* Detect selected hardware target */
#if defined(SST96)
#  define GLIDE_HW              GLIDE_HW_SST96
#elif defined(H3)
#  define GLIDE_HW              GLIDE_HW_H3
#elif defined(CVG)
#  define GLIDE_HW              GLIDE_HW_CVG
#else
#  define GLIDE_HW              GLIDE_HW_SST1
#endif

#define GLIDE_PLATFORM          (GLIDE_ENDIAN | GLIDE_OS | GLIDE_SST | GLIDE_HW)

/* Number of texture mapping units exposed in GrVertex (default: 2) */
#ifndef GLIDE_NUM_TMU
#  define GLIDE_NUM_TMU         2
#endif

#if ((GLIDE_NUM_TMU < 0) || (GLIDE_NUM_TMU > 3))
#  error "GLIDE_NUM_TMU must be between 0 and 3"
#endif

#endif /* __GLIDESYS_H__ */

/*
 * Portable Glide 2.x / 3.x API binding header for glide-ng CLI tools.
 *
 * - On Linux: directly includes <glide.h> (Glide 2.x or Glide 3.x depending on
 *   GLIDE_VERSION) and links libglide2x.so / libglide3x.so. Uses dlsym for
 *   optional grGetString() on Glide 2.x so pure Glide 2.x libraries work too.
 * - On DOS (Open Watcom v2 + CauseWay): includes <glide.h> with
 *   FX_GLIDE_NO_FUNC_PROTO and dynamically loads GLIDE2X.OVL (current dir,
 *   then Z:\GLIDE2X.OVL) via CauseWay cwdll.h. Resolves __stdcall exports
 *   using uppercase _GR...@N first, falling back to mixed-case _gr...@N for
 *   authentic 3dfx hardware GLIDE2X.OVL compatibility.
 * - On Windows (_WIN32): dynamically loads glide2x.dll (or glide3x.dll) via
 *   LoadLibraryA / GetProcAddress.
 *
 * Define GLIDE_TOOL_LOADER_IMPLEMENTATION in exactly ONE .c file per binary.
 */
#ifndef GLIDE2_DOS_OVL_H
#define GLIDE2_DOS_OVL_H

#ifndef GLIDE_VERSION
#  define GLIDE_VERSION 2
#endif

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if defined(__DOS__) || defined(__MSDOS__) || defined(_WIN32)
#  define FX_GLIDE_NO_FUNC_PROTO 1
#  include <glide.h>
#  if defined(__DOS__) || defined(__MSDOS__)
#    include <cwdll.h>
#  else
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#  endif
#else
#  if defined(__linux__) || defined(__unix__)
#    ifndef _GNU_SOURCE
#      define _GNU_SOURCE
#    endif
#    include <dlfcn.h>
#  endif
#  include <glide.h>
#endif

/* Glide 3.x / extended Glide 2.x grGetString pnames (safe constants even on
 * pure Glide 2.x headers where these may not be defined). */
#ifndef GR_EXTENSION
#  define GR_EXTENSION 0xa0
#endif
#ifndef GR_HARDWARE
#  define GR_HARDWARE  0xa1
#endif
#ifndef GR_RENDERER
#  define GR_RENDERER  0xa2
#endif
#ifndef GR_VENDOR
#  define GR_VENDOR    0xa3
#endif
#ifndef GR_VERSION
#  define GR_VERSION   0xa4
#endif

#if defined(__DOS__) || defined(__MSDOS__) || defined(_WIN32)

/* Every Glide 2.x entry point used across glide_info and glide_bench.
 * Format: X(ret, name, upper_sym, mixed_sym, params)
 * Byte counts verified directly via `wdump -x` against DOSBox/OpenGlide
 * GLIDE2X.OVL and authentic 3dfx Glide 2.x __stdcall ABI.
 */
#define GLIDE2_OVL_REQUIRED_FUNCS_X(X)                                                                 \
  X(void,   grGlideInit,               "_GRGLIDEINIT@0",               "_grGlideInit@0",               (void)) \
  X(void,   grGlideShutdown,           "_GRGLIDESHUTDOWN@0",           "_grGlideShutdown@0",           (void)) \
  X(void,   grGlideGetVersion,         "_GRGLIDEGETVERSION@4",         "_grGlideGetVersion@4",         (char version[80])) \
  X(FxBool, grSstQueryHardware,        "_GRSSTQUERYHARDWARE@4",        "_grSstQueryHardware@4",        (GrHwConfiguration* hwconfig)) \
  X(void,   grSstSelect,               "_GRSSTSELECT@4",               "_grSstSelect@4",               (int which_sst)) \
  X(FxBool, grSstWinOpen,              "_GRSSTWINOPEN@28",             "_grSstWinOpen@28",             (FxU32 hWnd, GrScreenResolution_t res, GrScreenRefresh_t ref, GrColorFormat_t fmt, GrOriginLocation_t org, int nCol, int nAux)) \
  X(void,   grSstWinClose,             "_GRSSTWINCLOSE@0",             "_grSstWinClose@0",             (void)) \
  X(FxU32,  grSstScreenWidth,          "_GRSSTSCREENWIDTH@0",          "_grSstScreenWidth@0",          (void)) \
  X(FxU32,  grSstScreenHeight,         "_GRSSTSCREENHEIGHT@0",         "_grSstScreenHeight@0",         (void)) \
  X(void,   grBufferClear,             "_GRBUFFERCLEAR@12",            "_grBufferClear@12",            (GrColor_t color, GrAlpha_t alpha, FxU16 depth)) \
  X(void,   grBufferSwap,              "_GRBUFFERSWAP@4",              "_grBufferSwap@4",              (int swap_interval)) \
  X(void,   grRenderBuffer,            "_GRRENDERBUFFER@4",            "_grRenderBuffer@4",            (GrBuffer_t buffer)) \
  X(void,   grClipWindow,              "_GRCLIPWINDOW@16",             "_grClipWindow@16",             (FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy)) \
  X(void,   grCullMode,                "_GRCULLMODE@4",                "_grCullMode@4",                (GrCullMode_t mode)) \
  X(void,   grDitherMode,              "_GRDITHERMODE@4",              "_grDitherMode@4",              (GrDitherMode_t mode)) \
  X(void,   grColorMask,               "_GRCOLORMASK@8",               "_grColorMask@8",               (FxBool rgb, FxBool a)) \
  X(void,   grDepthBufferMode,         "_GRDEPTHBUFFERMODE@4",         "_grDepthBufferMode@4",         (GrDepthBufferMode_t mode)) \
  X(void,   grDepthBufferFunction,     "_GRDEPTHBUFFERFUNCTION@4",     "_grDepthBufferFunction@4",     (GrCmpFnc_t func)) \
  X(void,   grDepthMask,               "_GRDEPTHMASK@4",               "_grDepthMask@4",               (FxBool mask)) \
  X(void,   grColorCombine,            "_GRCOLORCOMBINE@20",           "_grColorCombine@20",           (GrCombineFunction_t func, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, FxBool invert)) \
  X(void,   grAlphaCombine,            "_GRALPHACOMBINE@20",           "_grAlphaCombine@20",           (GrCombineFunction_t func, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, FxBool invert)) \
  X(void,   grAlphaBlendFunction,      "_GRALPHABLENDFUNCTION@16",     "_grAlphaBlendFunction@16",     (GrAlphaBlendFnc_t rgb_sf, GrAlphaBlendFnc_t rgb_df, GrAlphaBlendFnc_t alpha_sf, GrAlphaBlendFnc_t alpha_df)) \
  X(void,   grAlphaTestFunction,       "_GRALPHATESTFUNCTION@4",       "_grAlphaTestFunction@4",       (GrCmpFnc_t func)) \
  X(void,   grAlphaTestReferenceValue, "_GRALPHATESTREFERENCEVALUE@4", "_grAlphaTestReferenceValue@4", (GrAlpha_t value)) \
  X(void,   grConstantColorValue,      "_GRCONSTANTCOLORVALUE@4",      "_grConstantColorValue@4",      (GrColor_t value)) \
  X(void,   grChromakeyMode,           "_GRCHROMAKEYMODE@4",           "_grChromakeyMode@4",           (GrChromakeyMode_t mode)) \
  X(void,   grChromakeyValue,          "_GRCHROMAKEYVALUE@4",          "_grChromakeyValue@4",          (GrColor_t value)) \
  X(void,   grFogMode,                 "_GRFOGMODE@4",                 "_grFogMode@4",                 (GrFogMode_t mode)) \
  X(void,   grFogColorValue,           "_GRFOGCOLORVALUE@4",           "_grFogColorValue@4",           (GrColor_t fogcolor)) \
  X(void,   grFogTable,                "_GRFOGTABLE@4",                "_grFogTable@4",                (const GrFog_t ft[GR_FOG_TABLE_SIZE])) \
  X(void,   grDrawTriangle,            "_GRDRAWTRIANGLE@12",           "_grDrawTriangle@12",           (const GrVertex* a, const GrVertex* b, const GrVertex* c)) \
  X(void,   grDrawPolygonVertexList,   "_GRDRAWPOLYGONVERTEXLIST@8",   "_grDrawPolygonVertexList@8",   (int nverts, const GrVertex vlist[])) \
  X(FxU32,  grTexMinAddress,           "_GRTEXMINADDRESS@4",           "_grTexMinAddress@4",           (GrChipID_t tmu)) \
  X(FxU32,  grTexMaxAddress,           "_GRTEXMAXADDRESS@4",           "_grTexMaxAddress@4",           (GrChipID_t tmu)) \
  X(FxU32,  grTexCalcMemRequired,      "_GRTEXCALCMEMREQUIRED@16",     "_grTexCalcMemRequired@16",     (GrLOD_t lodmin, GrLOD_t lodmax, GrAspectRatio_t aspect, GrTextureFormat_t fmt)) \
  X(FxU32,  grTexTextureMemRequired,   "_GRTEXTEXTUREMEMREQUIRED@8",   "_grTexTextureMemRequired@8",   (FxU32 evenOdd, GrTexInfo* info)) \
  X(void,   grTexDownloadMipMap,       "_GRTEXDOWNLOADMIPMAP@16",      "_grTexDownloadMipMap@16",      (GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo* info)) \
  X(void,   grTexDownloadTable,        "_GRTEXDOWNLOADTABLE@12",       "_grTexDownloadTable@12",       (GrChipID_t tmu, GrTexTable_t type, void* data)) \
  X(void,   grTexSource,               "_GRTEXSOURCE@16",              "_grTexSource@16",              (GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo* info)) \
  X(void,   grTexCombine,              "_GRTEXCOMBINE@28",             "_grTexCombine@28",             (GrChipID_t tmu, GrCombineFunction_t rgb_fn, GrCombineFactor_t rgb_fac, GrCombineFunction_t a_fn, GrCombineFactor_t a_fac, FxBool rgb_inv, FxBool a_inv)) \
  X(void,   grTexFilterMode,           "_GRTEXFILTERMODE@12",          "_grTexFilterMode@12",          (GrChipID_t tmu, GrTextureFilterMode_t minfilter, GrTextureFilterMode_t magfilter)) \
  X(void,   grTexClampMode,            "_GRTEXCLAMPMODE@12",           "_grTexClampMode@12",           (GrChipID_t tmu, GrTextureClampMode_t s_clamp, GrTextureClampMode_t t_clamp)) \
  X(void,   grTexMipMapMode,           "_GRTEXMIPMAPMODE@12",          "_grTexMipMapMode@12",          (GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend)) \
  X(void,   grTexLodBiasValue,         "_GRTEXLODBIASVALUE@8",         "_grTexLodBiasValue@8",         (GrChipID_t tmu, float bias)) \
  X(FxBool, grLfbLock,                 "_GRLFBLOCK@24",                "_grLfbLock@24",                (GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode, GrOriginLocation_t origin, FxBool pixelPipeline, GrLfbInfo_t* info)) \
  X(FxBool, grLfbUnlock,               "_GRLFBUNLOCK@8",               "_grLfbUnlock@8",               (GrLock_t type, GrBuffer_t buffer)) \
  X(FxBool, grLfbReadRegion,           "_GRLFBREADREGION@28",          "_grLfbReadRegion@28",          (GrBuffer_t src_buffer, FxU32 src_x, FxU32 src_y, FxU32 src_width, FxU32 src_height, FxU32 dst_stride, void* dst_data)) \
  X(FxBool, grLfbWriteRegion,          "_GRLFBWRITEREGION@32",         "_grLfbWriteRegion@32",         (GrBuffer_t dst_buffer, FxU32 dst_x, FxU32 dst_y, GrLfbSrcFmt_t src_format, FxU32 src_width, FxU32 src_height, FxI32 src_stride, void* src_data)) \
  X(void,   grDisableAllEffects,       "_GRDISABLEALLEFFECTS@0",       "_grDisableAllEffects@0",       (void)) \
  X(void,   grSstIdle,                 "_GRSSTIDLE@0",                 "_grSstIdle@0",                 (void))

/* Optional Glide 2.x entry points (may be absent on older 2.1/2.4 OVL builds) */
#define GLIDE2_OVL_OPTIONAL_FUNCS_X(X)                                                                 \
  X(FxBool,      grSstQueryBoards,     "_GRSSTQUERYBOARDS@4",          "_grSstQueryBoards@4",          (GrHwConfiguration* hwconfig)) \
  X(FxU32,       guTexMemQueryAvail,   "_GUTEXMEMQUERYAVAIL@4",        "_guTexMemQueryAvail@4",        (GrChipID_t tmu)) \
  X(void,        guTexMemReset,        "_GUTEXMEMRESET@0",             "_guTexMemReset@0",             (void)) \
  X(void,        guFogGenerateExp,     "_GUFOGGENERATEEXP@8",          "_guFogGenerateExp@8",          (GrFog_t fogtable[GR_FOG_TABLE_SIZE], float density)) \
  X(const char*, grGetString,          "_GRGETSTRING@4",               "_grGetString@4",               (FxU32 pname))

#define GLIDE2_DECLARE_PFN_(ret, name, u, m, params) \
  typedef ret (FX_CALL *pfn_##name) params;          \
  extern pfn_##name g_g2ovl_##name;

GLIDE2_OVL_REQUIRED_FUNCS_X(GLIDE2_DECLARE_PFN_)
GLIDE2_OVL_OPTIONAL_FUNCS_X(GLIDE2_DECLARE_PFN_)
#undef GLIDE2_DECLARE_PFN_

/* Route standard Glide 2.x call sites to resolved function pointers */
#define grGlideInit               g_g2ovl_grGlideInit
#define grGlideShutdown           g_g2ovl_grGlideShutdown
#define grGlideGetVersion         g_g2ovl_grGlideGetVersion
#define grSstQueryHardware        g_g2ovl_grSstQueryHardware
#define grSstSelect               g_g2ovl_grSstSelect
#define grSstWinOpen              g_g2ovl_grSstWinOpen
#define grSstWinClose             g_g2ovl_grSstWinClose
#define grSstScreenWidth          g_g2ovl_grSstScreenWidth
#define grSstScreenHeight         g_g2ovl_grSstScreenHeight
#define grBufferClear             g_g2ovl_grBufferClear
#define grBufferSwap              g_g2ovl_grBufferSwap
#define grRenderBuffer            g_g2ovl_grRenderBuffer
#define grClipWindow              g_g2ovl_grClipWindow
#define grCullMode                g_g2ovl_grCullMode
#define grDitherMode              g_g2ovl_grDitherMode
#define grColorMask               g_g2ovl_grColorMask
#define grDepthBufferMode         g_g2ovl_grDepthBufferMode
#define grDepthBufferFunction     g_g2ovl_grDepthBufferFunction
#define grDepthMask               g_g2ovl_grDepthMask
#define grColorCombine            g_g2ovl_grColorCombine
#define grAlphaCombine            g_g2ovl_grAlphaCombine
#define grAlphaBlendFunction      g_g2ovl_grAlphaBlendFunction
#define grAlphaTestFunction       g_g2ovl_grAlphaTestFunction
#define grAlphaTestReferenceValue g_g2ovl_grAlphaTestReferenceValue
#define grConstantColorValue      g_g2ovl_grConstantColorValue
#define grChromakeyMode           g_g2ovl_grChromakeyMode
#define grChromakeyValue          g_g2ovl_grChromakeyValue
#define grFogMode                 g_g2ovl_grFogMode
#define grFogColorValue           g_g2ovl_grFogColorValue
#define grFogTable                g_g2ovl_grFogTable
#define grDrawTriangle            g_g2ovl_grDrawTriangle
#define grDrawPolygonVertexList   g_g2ovl_grDrawPolygonVertexList
#define grTexMinAddress           g_g2ovl_grTexMinAddress
#define grTexMaxAddress           g_g2ovl_grTexMaxAddress
#define grTexCalcMemRequired      g_g2ovl_grTexCalcMemRequired
#define grTexTextureMemRequired   g_g2ovl_grTexTextureMemRequired
#define grTexDownloadMipMap       g_g2ovl_grTexDownloadMipMap
#define grTexDownloadTable        g_g2ovl_grTexDownloadTable
#define grTexSource               g_g2ovl_grTexSource
#define grTexCombine              g_g2ovl_grTexCombine
#define grTexFilterMode           g_g2ovl_grTexFilterMode
#define grTexClampMode            g_g2ovl_grTexClampMode
#define grTexMipMapMode           g_g2ovl_grTexMipMapMode
#define grTexLodBiasValue         g_g2ovl_grTexLodBiasValue
#define grLfbLock                 g_g2ovl_grLfbLock
#define grLfbUnlock               g_g2ovl_grLfbUnlock
#define grLfbReadRegion           g_g2ovl_grLfbReadRegion
#define grLfbWriteRegion          g_g2ovl_grLfbWriteRegion
#define grDisableAllEffects       g_g2ovl_grDisableAllEffects
#define grSstIdle                 g_g2ovl_grSstIdle
#define grSstQueryBoards          g_g2ovl_grSstQueryBoards
#define guTexMemQueryAvail        g_g2ovl_guTexMemQueryAvail
#define guTexMemReset             g_g2ovl_guTexMemReset
#define guFogGenerateExp          g_g2ovl_guFogGenerateExp

extern int gtool_glide_load(char* source_out, size_t source_sz,
                            char* err_out, size_t err_sz);
extern void gtool_glide_unload(void);
extern const char* gtool_glide2_try_get_string(FxU32 pname);

#ifdef GLIDE_TOOL_LOADER_IMPLEMENTATION

#define GLIDE2_DEFINE_PFN_(ret, name, u, m, params) pfn_##name g_g2ovl_##name = NULL;
GLIDE2_OVL_REQUIRED_FUNCS_X(GLIDE2_DEFINE_PFN_)
GLIDE2_OVL_OPTIONAL_FUNCS_X(GLIDE2_DEFINE_PFN_)
#undef GLIDE2_DEFINE_PFN_

#if defined(__DOS__) || defined(__MSDOS__)
static void* g_g2ovl_handle = NULL;
static unsigned short g_g2ovl_sel = 0;
static int g_g2ovl_used_cwdll = 0;

/* Safe CauseWay LE DLL loader:
 * CauseWay's stock cwdll.lib LoadLibrary() unconditionally executes a far call
 * to the DLL's initial CS:EIP even when the LE header specifies initial EIP=0
 * (no entry point), which is the case for DOSBox/OpenGlide GLIDE2X.OVL.
 * Using CauseWay INT 31h AX=FF2Ah (LoadModule) + AX=FF08h (GetSelDet32) lets
 * us skip the entry-point call when CS:EIP==0, while delegating to standard
 * LoadLibrary() if an authentic 3dfx hardware OVL specifies a real entry point.
 */
static void* cw_safe_load_ovl(const char* path, unsigned short* out_sel,
                              int* out_used_cwdll) {
  union REGS regs;
  struct SREGS sregs;
  unsigned short mod_sel;
  unsigned short entry_cs;
  unsigned long entry_eip;

  if (out_used_cwdll) *out_used_cwdll = 0;
  if (out_sel) *out_sel = 0;

  memset(&regs, 0, sizeof(regs));
  memset(&sregs, 0, sizeof(sregs));
  segread(&sregs);
  regs.w.ax = 0xFF2AU;
  regs.x.edx = (unsigned long)path;
  int386x(0x31, &regs, &regs, &sregs);
  if (regs.x.cflag) {
    return NULL;
  }

  mod_sel = regs.w.si;
  entry_cs = regs.w.cx;
  entry_eip = regs.x.edx;

  if (entry_cs != 0) {
    /* Module has a real DLL entry point (e.g. authentic 3dfx HW OVL):
     * release probe load and let CauseWay LoadLibrary() run its initializer.
     */
    (void)entry_eip;
    memset(&regs, 0, sizeof(regs));
    regs.w.ax = 0xFF0FU;
    regs.w.bx = mod_sel;
    int386(0x31, &regs, &regs);
    if (out_used_cwdll) *out_used_cwdll = 1;
    return LoadLibrary(path);
  }

  /* Get linear base address of module descriptor (handle for GetProcAddress) */
  memset(&regs, 0, sizeof(regs));
  regs.w.ax = 0xFF08U;
  regs.w.bx = mod_sel;
  int386(0x31, &regs, &regs);
  if (regs.x.cflag) {
    memset(&regs, 0, sizeof(regs));
    regs.w.ax = 0xFF0FU;
    regs.w.bx = mod_sel;
    int386(0x31, &regs, &regs);
    return NULL;
  }

  if (out_sel) {
    *out_sel = mod_sel;
  }
  return (void*)regs.x.edx;
}

static void cw_safe_free_ovl(unsigned short mod_sel) {
  union REGS regs;
  if (!mod_sel) return;
  memset(&regs, 0, sizeof(regs));
  regs.w.ax = 0xFF0FU;
  regs.w.bx = mod_sel;
  int386(0x31, &regs, &regs);
}

static void* gtool_ovl_resolve(void* mod, const char* upper_name,
                               const char* mixed_name) {
  void* p = GetProcAddress(mod, upper_name);
  if (!p && mixed_name) {
    p = GetProcAddress(mod, mixed_name);
  }
  return p;
}
#else /* _WIN32 */
static HMODULE g_g2ovl_handle = NULL;

static void* gtool_ovl_resolve(HMODULE mod, const char* upper_name,
                               const char* mixed_name) {
  FARPROC p = NULL;
  if (mixed_name) {
    p = GetProcAddress(mod, mixed_name);
  }
  if (!p && upper_name) {
    p = GetProcAddress(mod, upper_name);
  }
  return (void*)p;
}
#endif

int gtool_glide_load(char* source_out, size_t source_sz,
                     char* err_out, size_t err_sz) {
  const char* loaded_from = NULL;
#if defined(__DOS__) || defined(__MSDOS__)
  static const char* const kCandidates[] = {
    "GLIDE2X.OVL",
    "Z:\\GLIDE2X.OVL",
    "Z:\\SYSTEM\\GLIDE2X.OVL",
    NULL
  };
  int i;
  for (i = 0; kCandidates[i] != NULL; ++i) {
    g_g2ovl_handle =
        cw_safe_load_ovl(kCandidates[i], &g_g2ovl_sel, &g_g2ovl_used_cwdll);
    if (g_g2ovl_handle != NULL) {
      loaded_from = kCandidates[i];
      break;
    }
  }
  if (!g_g2ovl_handle) {
    if (err_out && err_sz > 0) {
      snprintf(err_out, err_sz,
               "Failed to load GLIDE2X.OVL (tried current dir and Z:\\GLIDE2X.OVL)");
    }
    return 0;
  }
#else
  g_g2ovl_handle = LoadLibraryA("glide2x.dll");
  loaded_from = "glide2x.dll";
  if (!g_g2ovl_handle) {
    if (err_out && err_sz > 0) {
      snprintf(err_out, err_sz, "Failed to load glide2x.dll");
    }
    return 0;
  }
#endif

  if (source_out && source_sz > 0) {
    snprintf(source_out, source_sz, "%s", loaded_from);
  }

#define GLIDE2_RESOLVE_REQ_(ret, name, u, m, params)                                    \
  do {                                                                                  \
    void* _sym = gtool_ovl_resolve(g_g2ovl_handle, u, m);                               \
    if (!_sym) {                                                                        \
      if (err_out && err_sz > 0) {                                                      \
        snprintf(err_out, err_sz, "Missing required symbol %s in %s", #name,            \
                 loaded_from);                                                          \
      }                                                                                 \
      gtool_glide_unload();                                                             \
      return 0;                                                                         \
    }                                                                                   \
    memcpy(&g_g2ovl_##name, &_sym, sizeof(g_g2ovl_##name));                             \
  } while (0);

#define GLIDE2_RESOLVE_OPT_(ret, name, u, m, params)                                    \
  do {                                                                                  \
    void* _sym = gtool_ovl_resolve(g_g2ovl_handle, u, m);                               \
    if (_sym) {                                                                         \
      memcpy(&g_g2ovl_##name, &_sym, sizeof(g_g2ovl_##name));                           \
    } else {                                                                            \
      g_g2ovl_##name = NULL;                                                            \
    }                                                                                   \
  } while (0);

  GLIDE2_OVL_REQUIRED_FUNCS_X(GLIDE2_RESOLVE_REQ_)
  GLIDE2_OVL_OPTIONAL_FUNCS_X(GLIDE2_RESOLVE_OPT_)
#undef GLIDE2_RESOLVE_REQ_
#undef GLIDE2_RESOLVE_OPT_

  return 1;
}

void gtool_glide_unload(void) {
  if (g_g2ovl_handle) {
#if defined(__DOS__) || defined(__MSDOS__)
    if (g_g2ovl_used_cwdll) {
      FreeLibrary(g_g2ovl_handle);
      g_g2ovl_used_cwdll = 0;
    } else {
      cw_safe_free_ovl(g_g2ovl_sel);
      g_g2ovl_sel = 0;
    }
#else
    FreeLibrary(g_g2ovl_handle);
#endif
    g_g2ovl_handle = NULL;
  }
}

const char* gtool_glide2_try_get_string(FxU32 pname) {
  if (g_g2ovl_grGetString) {
    return g_g2ovl_grGetString(pname);
  }
  return NULL;
}

#endif /* GLIDE_TOOL_LOADER_IMPLEMENTATION */

#else /* Linux / POSIX direct linking */

static inline int gtool_glide_load(char* source_out, size_t source_sz,
                                   char* err_out, size_t err_sz) {
  (void)err_out;
  (void)err_sz;
  if (source_out && source_sz > 0) {
#if GLIDE_VERSION == 3
    snprintf(source_out, source_sz, "libglide3x.so");
#else
    snprintf(source_out, source_sz, "libglide2x.so");
#endif
  }
  return 1;
}

static inline void gtool_glide_unload(void) {}

#if GLIDE_VERSION == 2
static inline const char* gtool_glide2_try_get_string(FxU32 pname) {
  typedef const char* (FX_CALL *pfn_get_str)(FxU32);
  pfn_get_str fn = NULL;
  void* sym = dlsym(RTLD_DEFAULT, "grGetString");
  if (!sym) return NULL;
  memcpy(&fn, &sym, sizeof(fn));
  return fn ? fn(pname) : NULL;
}
#endif

#endif /* DOS / Win32 vs Linux */

#endif /* GLIDE2_DOS_OVL_H */

/*
 * glide_info - Unified 3dfx Glide 2.x / 3.x System Diagnostic Tool
 *
 * Pure C99, 100% authentic Glide API calls (zero glide-ng-only assumptions).
 * Builds cleanly for:
 *   - Linux:   glide2_info (libglide2x.so), glide3_info (libglide3x.so)
 *   - DOS/32:  glide_info.exe (Open Watcom v2 + CauseWay + dynamic GLIDE2X.OVL)
 *   - Windows: glide_info.exe (Win32 dynamic glide2x.dll)
 *
 * Modes:
 *   default        Non-graphical compact summary (<= 23 lines x <= 78 cols)
 *   -v / --verbose Full multi-board, per-TMU, capability & env var report
 *   -p / --probe   Active HW probe (resolutions, LFB 565 lock/write/readback, TMU)
 *   -o <file>      Write full verbose report (+ probe if -p) directly to file
 *   --pause        Wait for keypress before exiting (handy on DOS)
 */

#define GLIDE_TOOL_LOADER_IMPLEMENTATION 1
#include "glide_tool_platform.h"
#include "glide2_dos_ovl.h"

#define GLIDE_NG_ENV_VARS_DEFINE_TABLE 1
#include "glide_ng/env_vars.h"

#include <stdarg.h>

#define MAX_ACTIVE_ENVS 128
#define MAX_PROBE_RES   4
#define MAX_EXT_PROCS   24

#ifndef MAX_NUM_SST
#  define MAX_NUM_SST 4
#endif
#ifndef GR_WDEPTHVALUE_FARTHEST
#  define GR_WDEPTHVALUE_FARTHEST 0xFFFF
#endif

typedef struct ExtProcResult {
  const char* group;
  const char* token; /* Expected token in GR_EXTENSION, or NULL */
  const char* proc;
  int advertised;    /* 1 if token appears in GR_EXTENSION */
  int resolved;      /* 1 = resolved, 0 = NULL, -1 = API unavailable */
} ExtProcResult;

typedef struct ActiveEnvEntry {
  char name[64];
  char value[192];
  const char* tag; /* "3dfx", "wrapper", or "env" */
} ActiveEnvEntry;

typedef struct ResProbeResult {
  const char* label;
  GrScreenResolution_t res_enum;
  FxU32 expected_w;
  FxU32 expected_h;
  int opened;
  FxU32 actual_w;
  FxU32 actual_h;
} ResProbeResult;

typedef struct ProbeResults {
  int executed;
  ResProbeResult res[MAX_PROBE_RES];
  int lfb_lock_ok;
  int lfb_lock_readback_ok;
  FxU16 lfb_lock_got;
  FxU32 lfb_lock_stride;
  int lfb_write_region_ok;
  int lfb_write_readback_ok;
  FxU16 lfb_write_got;
  int tmu_probe_count;
  int tmu_upload_ok[3];
  FxU32 tmu_calc_bytes[3];
} ProbeResults;

typedef struct ReportData {
  char runtime_source[64];
  char glide_version[80];
  int has_get_string;
  char str_version[128];
  char str_hardware[128];
  char str_vendor[128];
  char str_renderer[128];
  char str_extensions[256];

  int num_boards;
  int board_type[MAX_NUM_SST];
  int fb_ram_mb[MAX_NUM_SST];
  int fbi_rev[MAX_NUM_SST];
  int num_tmus[MAX_NUM_SST];
  int sli_detect[MAX_NUM_SST];
  int tmu_ram_mb[MAX_NUM_SST][3];
  int tmu_rev[MAX_NUM_SST][3];

  FxU32 tmu_min_addr[3];
  FxU32 tmu_max_addr[3];
  int has_gu_tex_avail;
  FxU32 gu_tex_avail[3];

#if GLIDE_VERSION == 3
  FxI32 g3_mem_fb;
  FxI32 g3_mem_tmu;
  FxI32 g3_mem_uma;
  FxI32 g3_num_fb;
  FxI32 g3_max_tex_size;
  FxI32 g3_max_aspect;
  FxI32 g3_bits_depth;
  FxI32 g3_bits_rgba[4];
  FxI32 g3_fog_entries;
  FxI32 g3_gamma_entries;
  FxI32 g3_tex_align;
  FxI32 g3_supports_passthru;
  FxI32 g3_wdepth[2];
  FxI32 g3_zdepth[2];
  int g3_num_resolutions;
  GrResolution g3_resolutions[64];
#endif

  ActiveEnvEntry envs[MAX_ACTIVE_ENVS];
  int num_envs;

  int has_get_proc_addr;
  int num_ext_procs;
  ExtProcResult ext_procs[MAX_EXT_PROCS];

  ProbeResults probe;
} ReportData;

static void copy_str(char* dst, size_t dst_sz, const char* src) {
  if (!dst || dst_sz == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  snprintf(dst, dst_sz, "%s", src);
}

static int ext_has_token(const char* ext_str, const char* token) {
  size_t tlen;
  const char* p;
  if (!ext_str || !token || token[0] == '\0') return 0;
  tlen = strlen(token);
  p = ext_str;
  while ((p = strstr(p, token)) != NULL) {
    int left_ok = (p == ext_str || p[-1] == ' ' || p[-1] == '\t');
    int right_ok = (p[tlen] == '\0' || p[tlen] == ' ' || p[tlen] == '\t');
    if (left_ok && right_ok) return 1;
    p += tlen;
  }
  return 0;
}

static void print_trunc(FILE* out, const char* prefix, const char* val,
                        int max_col) {
  int plen = (int)strlen(prefix);
  int avail = max_col - plen;
  int vlen;
  if (!val) val = "";
  vlen = (int)strlen(val);
  if (avail <= 4) {
    fprintf(out, "%s\n", prefix);
    return;
  }
  if (vlen <= avail) {
    fprintf(out, "%s%s\n", prefix, val);
  } else {
    fprintf(out, "%s%.*s...\n", prefix, avail - 3, val);
  }
}

static void print_wrapped(FILE* out, const char* first_prefix,
                          const char* cont_prefix, const char* val,
                          int max_col, int max_lines) {
  const char* p = (val && val[0] != '\0') ? val : "(none)";
  int line_idx = 0;

  while (*p == ' ') ++p;
  if (*p == '\0') {
    fprintf(out, "%s(none)\n", first_prefix);
    return;
  }

  while (*p != '\0' && line_idx < max_lines) {
    const char* pfx = (line_idx == 0) ? first_prefix : cont_prefix;
    int avail = max_col - (int)strlen(pfx);
    int rem = (int)strlen(p);
    int take;

    if (avail <= 4) {
      fprintf(out, "%s\n", pfx);
      return;
    }

    if (rem <= avail) {
      fprintf(out, "%s%s\n", pfx, p);
      return;
    }

    if (line_idx + 1 >= max_lines) {
      fprintf(out, "%s%.*s...\n", pfx, avail - 3, p);
      return;
    }

    /* Break on the last space within avail columns so tokens stay intact */
    take = avail;
    while (take > 0 && p[take] != ' ') {
      --take;
    }
    if (take <= 0) {
      take = avail;
    }
    fprintf(out, "%s%.*s\n", pfx, take, p);
    p += take;
    while (*p == ' ') ++p;
    ++line_idx;
  }
}

static void probe_extension_procs(ReportData* r) {
  typedef struct ExtSpec {
    const char* group;
    const char* token;
    const char* proc;
  } ExtSpec;

#if GLIDE_VERSION == 3
  static const ExtSpec kSpecs[] = {
    { "CHROMARANGE",   "CHROMARANGE",   "grChromaRangeModeExt" },
    { "CHROMARANGE",   "CHROMARANGE",   "grChromaRangeExt" },
    { "TEXCHROMA",     "TEXCHROMA",     "grTexChromaModeExt" },
    { "TEXCHROMA",     "TEXCHROMA",     "grTexChromaRangeExt" },
    { "PIXEXT",        "PIXEXT",        "grSstWinOpenExt" },
    { "PIXEXT",        "PIXEXT",        "grTBufferWriteMaskExt" },
    { "PIXEXT",        "PIXEXT",        "grColorMaskExt" },
    { "PIXEXT",        "PIXEXT",        "grBufferClearExt" },
    { "PIXEXT",        "PIXEXT",        "grStencilFuncExt" },
    { "PIXEXT",        "PIXEXT",        "grStencilMaskExt" },
    { "PIXEXT",        "PIXEXT",        "grStencilOpExt" },
    { "PIXEXT",        "PIXEXT",        "grLfbConstantStencilExt" },
    { "PIXEXT",        "PIXEXT",        "grAlphaBlendFunctionExt" },
    { "COMBINE",       "COMBINE",       "grColorCombineExt" },
    { "COMBINE",       "COMBINE",       "grAlphaCombineExt" },
    { "COMBINE",       "COMBINE",       "grTexColorCombineExt" },
    { "COMBINE",       "COMBINE",       "grTexAlphaCombineExt" },
    { "COMBINE",       "COMBINE",       "grConstantColorValueExt" },
    { "TEXTUREBUFFER", "TEXTUREBUFFER", "grTextureBufferExt" },
    { "TEXTUREBUFFER", "TEXTUREBUFFER", "grTextureAuxBufferExt" },
    { "TEXTUREBUFFER", "TEXTUREBUFFER", "grAuxBufferExt" },
    { "GETGAMMA",      "GETGAMMA",      "grGetGammaTableExt" }
  };
#else
  static const ExtSpec kSpecs[] = {
    { "CHROMARANGE", "CHROMARANGE", "grChromakeyRangeExt" },
    { "TEXCHROMA",   "TEXCHROMA",   "grTexChromaModeExt" },
    { "TEXCHROMA",   "TEXCHROMA",   "grTexChromaRangeExt" },
    { "Glide 2.61",  NULL,          "grSplashCb" },
    { "PIXEXT",      NULL,          "grTBufferWriteMaskExt" },
    { "PIXEXT",      NULL,          "grColorMaskExt" },
    { "PIXEXT",      NULL,          "grBufferClearExt" },
    { "PIXEXT",      NULL,          "grStencilFuncExt" },
    { "PIXEXT",      NULL,          "grStencilMaskExt" },
    { "PIXEXT",      NULL,          "grStencilOpExt" },
    { "PIXEXT",      NULL,          "grLfbConstantStencilExt" },
    { "PIXEXT",      NULL,          "grAlphaBlendFunctionExt" }
  };
#endif
  size_t count = sizeof(kSpecs) / sizeof(kSpecs[0]);
  size_t i;

  r->num_ext_procs = 0;
  r->has_get_proc_addr = 0;

  for (i = 0; i < count && r->num_ext_procs < MAX_EXT_PROCS; ++i) {
    int res = gtool_try_get_proc_address(kSpecs[i].proc);
    ExtProcResult* e = &r->ext_procs[r->num_ext_procs++];
    e->group = kSpecs[i].group;
    e->token = kSpecs[i].token;
    e->proc = kSpecs[i].proc;
    e->advertised =
        (kSpecs[i].token && r->has_get_string)
            ? ext_has_token(r->str_extensions, kSpecs[i].token)
            : 0;
    e->resolved = res;
    if (res >= 0) {
      r->has_get_proc_addr = 1;
    }
  }
}

#if GLIDE_VERSION == 3
static void count_group_procs(const ReportData* r, const char* group,
                              int* out_ok, int* out_total) {
  int i;
  int ok = 0;
  int total = 0;
  for (i = 0; i < r->num_ext_procs; ++i) {
    if (strcmp(r->ext_procs[i].group, group) == 0) {
      ++total;
      if (r->ext_procs[i].resolved > 0) ++ok;
    }
  }
  if (out_ok) *out_ok = ok;
  if (out_total) *out_total = total;
}
#endif

#if GLIDE_VERSION == 2
static const char* sst_type_name(int type, int fbi_rev) {
  switch (type) {
    case GR_SSTTYPE_VOODOO:
      if ((fbi_rev & 0x11000) == 0x11000) return "Voodoo3 / VSA-100 (H3+)";
      if ((fbi_rev & 0x01000) == 0x01000) return "Voodoo Banshee (H3)";
      if ((fbi_rev & 0x180) != 0) return "Voodoo2 (CVG)";
      return "Voodoo Graphics (SST-1)";
    case GR_SSTTYPE_SST96:   return "Voodoo Rush (SST-96)";
    case GR_SSTTYPE_AT3D:    return "Alliance AT3D";
    case GR_SSTTYPE_Voodoo2: return "Voodoo2 (CVG)";
    default:                 return "Unknown SST";
  }
}
#endif

static void format_rev(char* out, size_t out_sz, int rev) {
  if (rev >= 0x80) {
    snprintf(out, out_sz, "0x%X", (unsigned)rev);
  } else {
    snprintf(out, out_sz, "%d", rev);
  }
}

#if GLIDE_VERSION == 3
static const char* g3_res_name(GrScreenResolution_t r) {
  switch (r) {
    case GR_RESOLUTION_320x200:   return "320x200";
    case GR_RESOLUTION_320x240:   return "320x240";
    case GR_RESOLUTION_400x256:   return "400x256";
    case GR_RESOLUTION_400x300:   return "400x300";
    case GR_RESOLUTION_512x256:   return "512x256";
    case GR_RESOLUTION_512x384:   return "512x384";
    case GR_RESOLUTION_640x200:   return "640x200";
    case GR_RESOLUTION_640x350:   return "640x350";
    case GR_RESOLUTION_640x400:   return "640x400";
    case GR_RESOLUTION_640x480:   return "640x480";
    case GR_RESOLUTION_800x600:   return "800x600";
    case GR_RESOLUTION_856x480:   return "856x480";
    case GR_RESOLUTION_960x720:   return "960x720";
    case GR_RESOLUTION_1024x768:  return "1024x768";
    case GR_RESOLUTION_1152x864:  return "1152x864";
    case GR_RESOLUTION_1280x960:  return "1280x960";
    case GR_RESOLUTION_1280x1024: return "1280x1024";
    case GR_RESOLUTION_1600x1024: return "1600x1024";
    case GR_RESOLUTION_1600x1200: return "1600x1200";
    case GR_RESOLUTION_1792x1344: return "1792x1344";
    case GR_RESOLUTION_1856x1392: return "1856x1392";
    case GR_RESOLUTION_1920x1440: return "1920x1440";
    case GR_RESOLUTION_2048x1536: return "2048x1536";
    case GR_RESOLUTION_2048x2048: return "2048x2048";
    default:                      return "Other";
  }
}

static const char* g3_refresh_name(GrScreenRefresh_t r) {
  switch (r) {
    case GR_REFRESH_60Hz:  return "60Hz";
    case GR_REFRESH_70Hz:  return "70Hz";
    case GR_REFRESH_72Hz:  return "72Hz";
    case GR_REFRESH_75Hz:  return "75Hz";
    case GR_REFRESH_80Hz:  return "80Hz";
    case GR_REFRESH_90Hz:  return "90Hz";
    case GR_REFRESH_100Hz: return "100Hz";
    case GR_REFRESH_85Hz:  return "85Hz";
    case GR_REFRESH_120Hz: return "120Hz";
    default:               return "?Hz";
  }
}
#endif

static int has_active_env(const ReportData* r, const char* name) {
  int i;
  for (i = 0; i < r->num_envs; ++i) {
    if (gtool_strcasecmp(r->envs[i].name, name) == 0) {
      return 1;
    }
  }
  return 0;
}

static void add_active_env(ReportData* r, const char* name, const char* val,
                           const char* tag) {
  ActiveEnvEntry* e;
  if (!name || !val || val[0] == '\0') return;
  if (r->num_envs >= MAX_ACTIVE_ENVS) return;
  if (has_active_env(r, name)) return;
  e = &r->envs[r->num_envs++];
  copy_str(e->name, sizeof(e->name), name);
  copy_str(e->value, sizeof(e->value), val);
  e->tag = tag;
}

/* Dynamically gather active Glide-related env vars:
 * 1) Check every single var registered in include/glide_ng/env_vars.h
 *    (FX_GLIDE_REPORT_REAL_HW follows strict 3dfx != NULL semantics, so an
 *    explicitly empty value is still active and shown as "(empty)").
 * 2) Scan process environ for any additional FX_GLIDE_ / SST_ / SSTV2_ /
 *    SSTH3_ / GLIDE_NG_ variables so authentic 3dfx / third-party vars appear.
 */
static void collect_active_envs(ReportData* r) {
  size_t i;
  size_t known_count = sizeof(kGlideKnownEnvVars) / sizeof(kGlideKnownEnvVars[0]);
  size_t prefix_count =
      sizeof(kGlideEnvScanPrefixes) / sizeof(kGlideEnvScanPrefixes[0]);
  char** envp;

  r->num_envs = 0;

  for (i = 0; i < known_count; ++i) {
    const char* v = getenv(kGlideKnownEnvVars[i].envName);
    if (v && (v[0] != '\0' ||
              strcmp(kGlideKnownEnvVars[i].envName,
                     GLIDE_FX_ENV_REPORT_REAL_HW) == 0)) {
      add_active_env(r, kGlideKnownEnvVars[i].envName,
                     v[0] != '\0' ? v : "(empty)",
                     kGlideKnownEnvVars[i].isAuthentic3dfx ? "3dfx" : "wrapper");
    }
  }

  envp = gtool_get_environ();
  if (envp) {
    while (*envp) {
      const char* entry = *envp++;
      const char* eq = strchr(entry, '=');
      size_t nlen;
      if (!eq || eq == entry) continue;
      nlen = (size_t)(eq - entry);
      if (eq[1] == '\0') continue;

      for (i = 0; i < prefix_count; ++i) {
        size_t plen = strlen(kGlideEnvScanPrefixes[i]);
        if (nlen >= plen &&
            gtool_strncasecmp(entry, kGlideEnvScanPrefixes[i], plen) == 0) {
          char name_buf[64];
          size_t copy_len =
              (nlen < sizeof(name_buf) - 1) ? nlen : (sizeof(name_buf) - 1);
          memcpy(name_buf, entry, copy_len);
          name_buf[copy_len] = '\0';
          add_active_env(r, name_buf, eq + 1,
                         (gtool_strncasecmp(name_buf, "GLIDE_NG_", 9) == 0)
                             ? "wrapper"
                             : "3dfx");
          break;
        }
      }
    }
  }
}

static void collect_glide_info(ReportData* r) {
  int tmu;
  int max_tmu;

#if GLIDE_VERSION == 2
  GrHwConfiguration hw;
  const char* s;
  int b;

  memset(&hw, 0, sizeof(hw));
  grGlideGetVersion(r->glide_version);
  if (r->glide_version[0] == '\0') {
    copy_str(r->glide_version, sizeof(r->glide_version), "2.x (unknown)");
  }

  if (grSstQueryHardware(&hw) && hw.num_sst > 0) {
    r->num_boards = hw.num_sst;
    if (r->num_boards > MAX_NUM_SST) r->num_boards = MAX_NUM_SST;
    for (b = 0; b < r->num_boards; ++b) {
      r->board_type[b] = (int)hw.SSTs[b].type;
      if (hw.SSTs[b].type == GR_SSTTYPE_VOODOO) {
        const GrVoodooConfig_t* vc = &hw.SSTs[b].sstBoard.VoodooConfig;
        int k;
        r->fb_ram_mb[b] = vc->fbRam;
        r->fbi_rev[b] = vc->fbiRev;
        r->num_tmus[b] = vc->nTexelfx;
        r->sli_detect[b] = vc->sliDetect ? 1 : 0;
        for (k = 0; k < vc->nTexelfx && k < GLIDE_NUM_TMU; ++k) {
          r->tmu_ram_mb[b][k] = vc->tmuConfig[k].tmuRam;
          r->tmu_rev[b][k] = vc->tmuConfig[k].tmuRev;
        }
      } else if (hw.SSTs[b].type == GR_SSTTYPE_SST96) {
        const GrSst96Config_t* rc = &hw.SSTs[b].sstBoard.SST96Config;
        int k;
        r->fb_ram_mb[b] = rc->fbRam;
        r->fbi_rev[b] = (rc->tmuConfig.tmuRev >= 0x1000) ? rc->tmuConfig.tmuRev : 1;
        r->num_tmus[b] = rc->nTexelfx > 0 ? rc->nTexelfx : 1;
        r->sli_detect[b] = 0;
        for (k = 0; k < r->num_tmus[b] && k < GLIDE_NUM_TMU; ++k) {
          r->tmu_ram_mb[b][k] = rc->tmuConfig.tmuRam;
          r->tmu_rev[b][k] = rc->tmuConfig.tmuRev;
        }
      } else if (hw.SSTs[b].type == GR_SSTTYPE_Voodoo2) {
        const GrVoodoo2Config_t* v2 = &hw.SSTs[b].sstBoard.Voodoo2Config;
        int k;
        r->fb_ram_mb[b] = v2->fbRam;
        r->fbi_rev[b] = v2->fbiRev;
        r->num_tmus[b] = v2->nTexelfx;
        r->sli_detect[b] = v2->sliDetect ? 1 : 0;
        for (k = 0; k < v2->nTexelfx && k < GLIDE_NUM_TMU; ++k) {
          r->tmu_ram_mb[b][k] = v2->tmuConfig[k].tmuRam;
          r->tmu_rev[b][k] = v2->tmuConfig[k].tmuRev;
        }
      }
    }
    grSstSelect(0);
  }

  /* Optional grGetString (not part of pure 3dfx Glide 2.x; present in glide-ng) */
  s = gtool_glide2_try_get_string(GR_VERSION);
  if (s) {
    r->has_get_string = 1;
    copy_str(r->str_version, sizeof(r->str_version), s);
    copy_str(r->str_hardware, sizeof(r->str_hardware),
             gtool_glide2_try_get_string(GR_HARDWARE));
    copy_str(r->str_vendor, sizeof(r->str_vendor),
             gtool_glide2_try_get_string(GR_VENDOR));
    copy_str(r->str_renderer, sizeof(r->str_renderer),
             gtool_glide2_try_get_string(GR_RENDERER));
    copy_str(r->str_extensions, sizeof(r->str_extensions),
             gtool_glide2_try_get_string(GR_EXTENSION));
  }

  max_tmu = (r->num_boards > 0 && r->num_tmus[0] > 0) ? r->num_tmus[0] : 1;
  if (max_tmu > 3) max_tmu = 3;
  for (tmu = 0; tmu < max_tmu; ++tmu) {
    r->tmu_min_addr[tmu] = grTexMinAddress((GrChipID_t)tmu);
    r->tmu_max_addr[tmu] = grTexMaxAddress((GrChipID_t)tmu);
  }

#if defined(__DOS__) || defined(__MSDOS__) || defined(_WIN32)
  if (g_g2ovl_guTexMemQueryAvail != NULL) {
    r->has_gu_tex_avail = 1;
    for (tmu = 0; tmu < max_tmu; ++tmu) {
      r->gu_tex_avail[tmu] = guTexMemQueryAvail((GrChipID_t)tmu);
    }
  }
#else
  r->has_gu_tex_avail = 1;
  for (tmu = 0; tmu < max_tmu; ++tmu) {
    r->gu_tex_avail[tmu] = guTexMemQueryAvail((GrChipID_t)tmu);
  }
#endif

#else /* GLIDE_VERSION == 3 */
  {
    FxI32 boards = 0;
    FxI32 ntmu = 0;
    FxI32 rev_fb = 0;
    FxI32 rev_tmu = 0;
    GrResolution tmpl;
    FxI32 bytes;

    r->has_get_string = 1;
    copy_str(r->str_version, sizeof(r->str_version), grGetString(GR_VERSION));
    copy_str(r->glide_version, sizeof(r->glide_version), r->str_version);
    copy_str(r->str_hardware, sizeof(r->str_hardware),
             grGetString(GR_HARDWARE));
    copy_str(r->str_vendor, sizeof(r->str_vendor), grGetString(GR_VENDOR));
    copy_str(r->str_renderer, sizeof(r->str_renderer),
             grGetString(GR_RENDERER));
    copy_str(r->str_extensions, sizeof(r->str_extensions),
             grGetString(GR_EXTENSION));

    grGet(GR_NUM_BOARDS, 4, &boards);
    grGet(GR_NUM_TMU, 4, &ntmu);
    grGet(GR_MEMORY_FB, 4, &r->g3_mem_fb);
    grGet(GR_MEMORY_TMU, 4, &r->g3_mem_tmu);
    grGet(GR_MEMORY_UMA, 4, &r->g3_mem_uma);
    grGet(GR_NUM_FB, 4, &r->g3_num_fb);
    grGet(GR_MAX_TEXTURE_SIZE, 4, &r->g3_max_tex_size);
    grGet(GR_MAX_TEXTURE_ASPECT_RATIO, 4, &r->g3_max_aspect);
    grGet(GR_BITS_DEPTH, 4, &r->g3_bits_depth);
    grGet(GR_BITS_RGBA, 16, r->g3_bits_rgba);
    grGet(GR_FOG_TABLE_ENTRIES, 4, &r->g3_fog_entries);
    grGet(GR_GAMMA_TABLE_ENTRIES, 4, &r->g3_gamma_entries);
    grGet(GR_TEXTURE_ALIGN, 4, &r->g3_tex_align);
    grGet(GR_SUPPORTS_PASSTHRU, 4, &r->g3_supports_passthru);
    grGet(GR_WDEPTH_MIN_MAX, 8, r->g3_wdepth);
    grGet(GR_ZDEPTH_MIN_MAX, 8, r->g3_zdepth);
    grGet(GR_REVISION_FB, 4, &rev_fb);
    grGet(GR_REVISION_TMU, 4, &rev_tmu);

    r->num_boards = (boards > 0) ? (int)boards : 1;
    if (r->num_boards > MAX_NUM_SST) r->num_boards = MAX_NUM_SST;
    r->fb_ram_mb[0] = (int)(r->g3_mem_fb / (1024 * 1024));
    r->fbi_rev[0] = (int)rev_fb;
    r->sli_detect[0] = (r->g3_num_fb > 1) ? 1 : 0;
    r->num_tmus[0] = (ntmu > 0) ? (int)ntmu : 1;
    if (r->num_tmus[0] > 3) r->num_tmus[0] = 3;
    for (tmu = 0; tmu < r->num_tmus[0]; ++tmu) {
      r->tmu_ram_mb[0][tmu] = (int)(r->g3_mem_tmu / (1024 * 1024));
      r->tmu_rev[0][tmu] = (int)rev_tmu;
    }

    grSstSelect(0);
    max_tmu = r->num_tmus[0];
    for (tmu = 0; tmu < max_tmu; ++tmu) {
      r->tmu_min_addr[tmu] = grTexMinAddress((GrChipID_t)tmu);
      r->tmu_max_addr[tmu] = grTexMaxAddress((GrChipID_t)tmu);
    }

    tmpl.resolution = (GrScreenResolution_t)GR_QUERY_ANY;
    tmpl.refresh = (GrScreenRefresh_t)GR_QUERY_ANY;
    tmpl.numColorBuffers = GR_QUERY_ANY;
    tmpl.numAuxBuffers = GR_QUERY_ANY;
    bytes = grQueryResolutions(&tmpl, NULL);
    if (bytes > 0) {
      GrResolution* tmp = (GrResolution*)malloc((size_t)bytes);
      if (tmp) {
        FxI32 filled = grQueryResolutions(&tmpl, tmp);
        int n = (int)(filled / (FxI32)sizeof(GrResolution));
        if (n > 64) n = 64;
        if (n > 0) {
          memcpy(r->g3_resolutions, tmp, (size_t)n * sizeof(GrResolution));
          r->g3_num_resolutions = n;
        }
        free(tmp);
      }
    }
  }
#endif

  probe_extension_procs(r);
}

/* Active hardware/wrapper probe (-p / --probe):
 * 1) Open & verify 512x384, 640x480, 800x600, 1024x768
 * 2) On 640x480: LFB 565 lock/write/swap/readback + grLfbWriteRegion roundtrip
 * 3) On 640x480: TMU0 (and TMU1 if present) 64x64 RGB565 download + bind
 */
static void run_active_probe(ReportData* r) {
  static const struct {
    const char* label;
    GrScreenResolution_t res_enum;
    FxU32 w;
    FxU32 h;
  } kResList[MAX_PROBE_RES] = {
    { "512x384",  GR_RESOLUTION_512x384,  512,  384 },
    { "640x480",  GR_RESOLUTION_640x480,  640,  480 },
    { "800x600",  GR_RESOLUTION_800x600,  800,  600 },
    { "1024x768", GR_RESOLUTION_1024x768, 1024, 768 }
  };
  ProbeResults* p = &r->probe;
  int i;
  int max_tmu;

  memset(p, 0, sizeof(*p));
  p->executed = 1;

  for (i = 0; i < MAX_PROBE_RES; ++i) {
    p->res[i].label = kResList[i].label;
    p->res[i].res_enum = kResList[i].res_enum;
    p->res[i].expected_w = kResList[i].w;
    p->res[i].expected_h = kResList[i].h;
  }

  for (i = 0; i < MAX_PROBE_RES; ++i) {
#if GLIDE_VERSION == 3
    GrContext_t ctx = grSstWinOpen(0, kResList[i].res_enum, GR_REFRESH_60Hz,
                                   GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT,
                                   2, 1);
    if (ctx != 0) {
      FxI32 vp[4] = {0, 0, 0, 0};
      p->res[i].opened = 1;
      if (grGet(GR_VIEWPORT, 16, vp) >= 16) {
        p->res[i].actual_w = (FxU32)vp[2];
        p->res[i].actual_h = (FxU32)vp[3];
      }
      grBufferClear(0x00102030UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);
      grBufferSwap(0);
      grSstWinClose(ctx);
    }
#else
    if (grSstWinOpen(0, kResList[i].res_enum, GR_REFRESH_60Hz,
                     GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
      p->res[i].opened = 1;
      p->res[i].actual_w = grSstScreenWidth();
      p->res[i].actual_h = grSstScreenHeight();
      grBufferClear(0x00102030UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);
      grBufferSwap(0);
      grSstWinClose();
    }
#endif
  }

  /* Open 640x480 once for LFB & TMU upload probes */
  {
#if GLIDE_VERSION == 3
    GrContext_t ctx = grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                                   GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT,
                                   2, 1);
    int opened = (ctx != 0);
#else
    int opened = grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                              GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT,
                              2, 1) ? 1 : 0;
#endif
    if (opened) {
      GrLfbInfo_t lfb;
      FxU16 readback_px = 0;
      FxU16 write_block[16 * 16];
      static FxU16 tex64[64 * 64];
      int k;

      /* 1) LFB Lock (RGB565 write -> swap -> readback) */
      grBufferClear(0x00000000UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);
      memset(&lfb, 0, sizeof(lfb));
      lfb.size = sizeof(lfb);
      if (grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                    GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                    &lfb) &&
          lfb.lfbPtr != NULL && lfb.strideInBytes >= 1280) {
        FxU8* base = (FxU8*)lfb.lfbPtr;
        int y, x;
        p->lfb_lock_ok = 1;
        p->lfb_lock_stride = lfb.strideInBytes;
        for (y = 32; y < 64; ++y) {
          FxU16* row = (FxU16*)(base + (size_t)y * lfb.strideInBytes);
          for (x = 32; x < 64; ++x) {
            row[x] = 0xF800U; /* Pure RGB565 Red */
          }
        }
        grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
        grBufferSwap(0);
        if (grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 48, 48, 1, 1, 2,
                            &readback_px)) {
          /* Accept minor precision tolerance (e.g. 565->888->565) */
          unsigned r5 = (readback_px >> 11) & 0x1FU;
          unsigned g6 = (readback_px >> 5) & 0x3FU;
          unsigned b5 = readback_px & 0x1FU;
          p->lfb_lock_got = readback_px;
          if (r5 >= 29 && g6 <= 2 && b5 <= 2) {
            p->lfb_lock_readback_ok = 1;
          }
        }
      }

      /* 2) grLfbWriteRegion (RGB565 green block -> swap -> readback) */
      for (k = 0; k < 16 * 16; ++k) {
        write_block[k] = 0x07E0U; /* Pure RGB565 Green */
      }
      grBufferClear(0x00000000UL, 0xFF, GR_WDEPTHVALUE_FARTHEST);
#if GLIDE_VERSION == 3
      if (grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 96, 96, GR_LFB_SRC_FMT_565,
                           16, 16, FXFALSE, 32, write_block))
#else
      if (grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 96, 96, GR_LFB_SRC_FMT_565,
                           16, 16, 32, write_block))
#endif
      {
        p->lfb_write_region_ok = 1;
        grBufferSwap(0);
        readback_px = 0;
        if (grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 104, 104, 1, 1, 2,
                            &readback_px)) {
          unsigned r5 = (readback_px >> 11) & 0x1FU;
          unsigned g6 = (readback_px >> 5) & 0x3FU;
          unsigned b5 = readback_px & 0x1FU;
          p->lfb_write_got = readback_px;
          if (r5 <= 2 && g6 >= 60 && b5 <= 2) {
            p->lfb_write_readback_ok = 1;
          }
        }
      }

      /* 3) TMU0 / TMU1 texture download & bind probe */
      for (k = 0; k < 64 * 64; ++k) {
        int tx = k & 63;
        int ty = k >> 6;
        tex64[k] = (((tx ^ ty) & 8) != 0) ? 0xFFE0U : 0x001FU;
      }
      max_tmu = (r->num_boards > 0 && r->num_tmus[0] > 0) ? r->num_tmus[0] : 1;
      if (max_tmu > 3) max_tmu = 3;
      p->tmu_probe_count = max_tmu;

      for (i = 0; i < max_tmu; ++i) {
        GrTexInfo ti;
        FxU32 req_bytes;
        FxU32 min_addr = grTexMinAddress((GrChipID_t)i);
        FxU32 max_addr = grTexMaxAddress((GrChipID_t)i);
        memset(&ti, 0, sizeof(ti));
#if GLIDE_VERSION == 3
        ti.smallLodLog2 = GR_LOD_LOG2_64;
        ti.largeLodLog2 = GR_LOD_LOG2_64;
        ti.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
#else
        ti.smallLod = GR_LOD_64;
        ti.largeLod = GR_LOD_64;
        ti.aspectRatio = GR_ASPECT_1x1;
#endif
        ti.format = GR_TEXFMT_RGB_565;
        ti.data = tex64;
        req_bytes = grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &ti);
        p->tmu_calc_bytes[i] = req_bytes;
        if (req_bytes > 0 && max_addr > min_addr &&
            (max_addr - min_addr) >= req_bytes) {
          grTexDownloadMipMap((GrChipID_t)i, min_addr,
                              GR_MIPMAPLEVELMASK_BOTH, &ti);
          grTexSource((GrChipID_t)i, min_addr, GR_MIPMAPLEVELMASK_BOTH, &ti);
          p->tmu_upload_ok[i] = 1;
        }
      }

      grBufferSwap(0);
#if GLIDE_VERSION == 3
      grSstWinClose(ctx);
#else
      grSstWinClose();
#endif
    }
  }
}

static void print_probe_summary(FILE* out, const ProbeResults* p, int verbose) {
  int i;
  if (!p->executed) return;
  fprintf(out, "------------------------------------------------------------------------------\n");
  fprintf(out, " Active HW Probe\n");
  if (!verbose) {
    fprintf(out, "  Modes     : ");
    for (i = 0; i < MAX_PROBE_RES; ++i) {
      fprintf(out, "%s[%s]%s", p->res[i].label,
              (p->res[i].opened && p->res[i].actual_w == p->res[i].expected_w &&
               p->res[i].actual_h == p->res[i].expected_h)
                  ? "OK"
                  : (p->res[i].opened ? "MISMATCH" : "FAIL"),
              (i + 1 < MAX_PROBE_RES) ? " " : "\n");
    }
    fprintf(out, "  LFB 565   : Lock[%s] Readback[%s] WriteReg[%s] Readback[%s]\n",
            p->lfb_lock_ok ? "OK" : "FAIL",
            p->lfb_lock_readback_ok ? "OK" : "FAIL",
            p->lfb_write_region_ok ? "OK" : "FAIL",
            p->lfb_write_readback_ok ? "OK" : "FAIL");
    fprintf(out, "  TMU Upload: ");
    for (i = 0; i < p->tmu_probe_count; ++i) {
      fprintf(out, "TMU%d[%s %luB]%s", i, p->tmu_upload_ok[i] ? "OK" : "FAIL",
              (unsigned long)p->tmu_calc_bytes[i],
              (i + 1 < p->tmu_probe_count) ? "  " : "\n");
    }
  } else {
    for (i = 0; i < MAX_PROBE_RES; ++i) {
      fprintf(out, "  Mode %-8s : %s (reported %lux%lu)\n", p->res[i].label,
              p->res[i].opened ? "OPEN OK" : "FAILED",
              (unsigned long)p->res[i].actual_w,
              (unsigned long)p->res[i].actual_h);
    }
    fprintf(out,
            "  LFB Lock 565  : %s (stride=%lu B) | Readback: %s (got 0x%04X, exp 0xF800)\n",
            p->lfb_lock_ok ? "OK" : "FAIL", (unsigned long)p->lfb_lock_stride,
            p->lfb_lock_readback_ok ? "OK" : "FAIL",
            (unsigned)p->lfb_lock_got);
    fprintf(out,
            "  LFB WriteReg  : %s | Readback: %s (got 0x%04X, exp 0x07E0)\n",
            p->lfb_write_region_ok ? "OK" : "FAIL",
            p->lfb_write_readback_ok ? "OK" : "FAIL",
            (unsigned)p->lfb_write_got);
    for (i = 0; i < p->tmu_probe_count; ++i) {
      fprintf(out, "  TMU%d 64x64    : %s (grTexTextureMemRequired = %lu bytes)\n",
              i, p->tmu_upload_ok[i] ? "UPLOAD+BIND OK" : "FAIL",
              (unsigned long)p->tmu_calc_bytes[i]);
    }
  }
}

/* Compact report: strictly <= 23 lines x <= 78 cols (when not running -p) */
static void print_compact_report(FILE* out, const ReportData* r) {
  char buf[256];
  int ntmu = (r->num_boards > 0) ? r->num_tmus[0] : 0;
  int i;

  fprintf(out, "==============================================================================\n");
  fprintf(out, " glide_info - 3dfx Glide %d.x Diagnostic Tool         [%-20s]\n",
          GLIDE_VERSION, gtool_platform_name());
  fprintf(out, "==============================================================================\n");

  print_trunc(out, " Runtime    : ", r->runtime_source, 78);
  print_trunc(out, " Glide Ver  : ", r->glide_version, 78);

  if (r->num_boards > 0) {
    snprintf(buf, sizeof(buf), "%d detected (SST #0 active)", r->num_boards);
    print_trunc(out, " Boards     : ", buf, 78);

#if GLIDE_VERSION == 2
    {
      char rev_buf[24];
      format_rev(rev_buf, sizeof(rev_buf), r->fbi_rev[0]);
      snprintf(buf, sizeof(buf), "%s [FBI rev %s, SLI: %s]",
               sst_type_name(r->board_type[0], r->fbi_rev[0]), rev_buf,
               r->sli_detect[0] ? "Yes" : "No");
    }
#else
    snprintf(buf, sizeof(buf), "%s", r->str_hardware);
#endif
    print_trunc(out, " Hardware   : ", buf, 78);

    snprintf(buf, sizeof(buf), "%d MB", r->fb_ram_mb[0]);
    print_trunc(out, " FrameBuf   : ", buf, 78);

    {
      int pos = snprintf(buf, sizeof(buf), "%dx (", ntmu);
      for (i = 0; i < ntmu && i < 3; ++i) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%sTMU%d: %d MB",
                        (i > 0) ? ", " : "", i, r->tmu_ram_mb[0][i]);
      }
      snprintf(buf + pos, sizeof(buf) - (size_t)pos, ")");
      print_trunc(out, " TMUs       : ", buf, 78);
    }

    {
      int pos = 0;
      for (i = 0; i < ntmu && i < 2; ++i) {
        FxU32 kb = (r->tmu_max_addr[i] > r->tmu_min_addr[i])
                       ? ((r->tmu_max_addr[i] - r->tmu_min_addr[i]) / 1024U)
                       : 0U;
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        "%sTMU%d 0x%lX-0x%lX (%luKB)", (i > 0) ? " | " : "", i,
                        (unsigned long)r->tmu_min_addr[i],
                        (unsigned long)r->tmu_max_addr[i], (unsigned long)kb);
      }
      print_trunc(out, " TMU Range  : ", (ntmu > 0) ? buf : "N/A", 78);
    }

    if (r->has_gu_tex_avail) {
      int pos = 0;
      for (i = 0; i < ntmu && i < 3; ++i) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        "%sTMU%d %lu KB", (i > 0) ? " | " : "", i,
                        (unsigned long)(r->gu_tex_avail[i] / 1024U));
      }
      print_trunc(out, " guTexAvail : ", buf, 78);
    }
  } else {
    fprintf(out, " Boards     : 0 detected (no Glide hardware found)\n");
  }

  if (r->has_get_string) {
    print_trunc(out, " Vendor     : ", r->str_vendor, 78);
    print_trunc(out, " Renderer   : ", r->str_renderer, 78);
    print_wrapped(out, " Extensions : ", "              ",
                  r->str_extensions[0] ? r->str_extensions : "(none)", 78, 3);
  } else {
    fprintf(out, " Extended   : N/A (pure Glide 2.x; grGetString not exported)\n");
  }

#if GLIDE_VERSION == 3
  {
    int pix_ok = 0, pix_tot = 0;
    int cmb_ok = 0, cmb_tot = 0;
    int tbf_ok = 0, tbf_tot = 0;
    count_group_procs(r, "PIXEXT", &pix_ok, &pix_tot);
    count_group_procs(r, "COMBINE", &cmb_ok, &cmb_tot);
    count_group_procs(r, "TEXTUREBUFFER", &tbf_ok, &tbf_tot);
    if (pix_ok > 0 || cmb_ok > 0 || tbf_ok > 0) {
      snprintf(buf, sizeof(buf),
               "PIXEXT %d/%d | COMBINE %d/%d | TEXBUF %d/%d",
               pix_ok, pix_tot, cmb_ok, cmb_tot, tbf_ok, tbf_tot);
    } else {
      snprintf(buf, sizeof(buf),
               "Inactive on this card profile (0/%d PIXEXT procs)", pix_tot);
    }
    print_trunc(out, " VSA-100    : ", buf, 78);
  }
#endif

  fprintf(out, "------------------------------------------------------------------------------\n");
  if (r->num_envs == 0) {
    fprintf(out, " Active Env : (none)\n");
  } else {
    /* Fit up to 3 lines cleanly inside the 23-line DOS screen budget */
    char line[80];
    int line_count = 0;
    int shown = 0;
    line[0] = '\0';
    for (i = 0; i < r->num_envs; ++i) {
      char pair[260];
      int cur_len = (int)strlen(line);
      int add_len;
      snprintf(pair, sizeof(pair), "%s=%s", r->envs[i].name, r->envs[i].value);
      add_len = (int)strlen(pair) + (cur_len > 0 ? 2 : 0);
      if (cur_len > 0 && cur_len + add_len > 63) {
        fprintf(out, "%s%s\n", (line_count == 0) ? " Active Env : " : "              ",
                line);
        line_count++;
        line[0] = '\0';
        if (line_count >= 2) break;
      }
      {
        size_t len = strlen(line);
        size_t avail = sizeof(line) - 1 - len;
        if (len > 0 && avail >= 2) {
          memcpy(line + len, ", ", 2);
          len += 2;
          avail -= 2;
        }
        {
          size_t plen = strlen(pair);
          if (plen > avail) plen = avail;
          if (plen > 0) memcpy(line + len, pair, plen);
          line[len + plen] = '\0';
        }
      }
      shown++;
    }
    if (line[0] != '\0') {
      fprintf(out, "%s%s\n", (line_count == 0) ? " Active Env : " : "              ",
              line);
    }
    if (shown < r->num_envs) {
      fprintf(out, "              (+%d more; use -v for full list)\n",
              r->num_envs - shown);
    }
  }

  print_probe_summary(out, &r->probe, 0);

  fprintf(out, "------------------------------------------------------------------------------\n");
  fprintf(out, " Tip: -v (verbose), -p (HW probe), -o <file> (full dump), --pause\n");
  fprintf(out, "==============================================================================\n");
}

static void print_verbose_report(FILE* out, const ReportData* r) {
  int b, tmu, i;

  fprintf(out, "==============================================================================\n");
  fprintf(out, " glide_info - 3dfx Glide %d.x Full Diagnostic Report  [%-20s]\n",
          GLIDE_VERSION, gtool_platform_name());
  fprintf(out, "==============================================================================\n");
  fprintf(out, "\n[Runtime & API]\n");
  fprintf(out, "  Runtime Source : %s\n", r->runtime_source);
  fprintf(out, "  Glide API      : %d.x\n", GLIDE_VERSION);
  fprintf(out, "  Glide Version  : %s\n", r->glide_version);
  if (r->has_get_string) {
    fprintf(out, "  GR_VERSION     : %s\n", r->str_version);
    fprintf(out, "  GR_HARDWARE    : %s\n", r->str_hardware);
    fprintf(out, "  GR_VENDOR      : %s\n", r->str_vendor);
    fprintf(out, "  GR_RENDERER    : %s\n", r->str_renderer);
    print_wrapped(out, "  GR_EXTENSION   : ", "                   ",
                  r->str_extensions[0] ? r->str_extensions : "(none)", 78, 8);
  } else {
    fprintf(out, "  grGetString    : N/A (pure Glide 2.x; not exported)\n");
  }

  fprintf(out, "\n[Extensions & Dynamic Entry Points (grGetProcAddress)]\n");
  if (!r->has_get_proc_addr) {
    fprintf(out, "  grGetProcAddress : N/A (pure Glide 2.x; not exported)\n");
  } else {
    int gstart = 0;
    while (gstart < r->num_ext_procs) {
      const char* gname = r->ext_procs[gstart].group;
      const char* gtok = r->ext_procs[gstart].token;
      int gend = gstart;
      int ok_cnt = 0;
      int tot_cnt = 0;
      int adv = r->ext_procs[gstart].advertised;
      int k;

      while (gend < r->num_ext_procs &&
             strcmp(r->ext_procs[gend].group, gname) == 0) {
        if (r->ext_procs[gend].resolved > 0) ++ok_cnt;
        ++tot_cnt;
        ++gend;
      }

      if (gtok) {
        fprintf(out, "  %-14s [%s] (%d/%d procs resolved)\n", gname,
                adv ? "ADVERTISED" : "not listed", ok_cnt, tot_cnt);
      } else {
        fprintf(out, "  %-14s (%d/%d procs resolved)\n", gname, ok_cnt,
                tot_cnt);
      }

      for (k = gstart; k < gend; k += 2) {
        const ExtProcResult* p0 = &r->ext_procs[k];
        if (k + 1 < gend) {
          const ExtProcResult* p1 = &r->ext_procs[k + 1];
          fprintf(out, "    [%s] %-30s [%s] %s\n",
                  p0->resolved > 0 ? "OK" : "--", p0->proc,
                  p1->resolved > 0 ? "OK" : "--", p1->proc);
        } else {
          fprintf(out, "    [%s] %s\n",
                  p0->resolved > 0 ? "OK" : "--", p0->proc);
        }
      }
      gstart = gend;
    }
  }

  fprintf(out, "\n[Detected Hardware (%d board%s)]\n", r->num_boards,
          (r->num_boards == 1) ? "" : "s");
  for (b = 0; b < r->num_boards; ++b) {
    char rev_fb_buf[24];
    format_rev(rev_fb_buf, sizeof(rev_fb_buf), r->fbi_rev[b]);
    fprintf(out, "  Board #%d       : %s\n", b,
#if GLIDE_VERSION == 2
            sst_type_name(r->board_type[b], r->fbi_rev[b])
#else
            r->str_hardware
#endif
    );
    fprintf(out, "    FrameBuffer  : %d MB (FBI rev %s, SLI: %s)\n",
            r->fb_ram_mb[b], rev_fb_buf, r->sli_detect[b] ? "Yes" : "No");
    fprintf(out, "    TMU Count    : %d\n", r->num_tmus[b]);
    for (tmu = 0; tmu < r->num_tmus[b] && tmu < 3; ++tmu) {
      char rev_tmu_buf[24];
      format_rev(rev_tmu_buf, sizeof(rev_tmu_buf), r->tmu_rev[b][tmu]);
      fprintf(out, "    TMU%d         : %d MB (rev %s)", tmu,
              r->tmu_ram_mb[b][tmu], rev_tmu_buf);
      if (b == 0) {
        FxU32 span = (r->tmu_max_addr[tmu] > r->tmu_min_addr[tmu])
                         ? (r->tmu_max_addr[tmu] - r->tmu_min_addr[tmu])
                         : 0U;
        fprintf(out, " | addr 0x%08lX..0x%08lX (%lu KB)",
                (unsigned long)r->tmu_min_addr[tmu],
                (unsigned long)r->tmu_max_addr[tmu],
                (unsigned long)(span / 1024U));
        if (r->has_gu_tex_avail) {
          fprintf(out, " | guAvail %lu KB",
                  (unsigned long)(r->gu_tex_avail[tmu] / 1024U));
        }
      }
      fprintf(out, "\n");
    }
  }

#if GLIDE_VERSION == 3
  fprintf(out, "\n[Glide 3.x Capabilities (grGet)]\n");
  fprintf(out, "  GR_MEMORY_FB               : %ld bytes (%ld MB)\n",
          (long)r->g3_mem_fb, (long)(r->g3_mem_fb / (1024 * 1024)));
  fprintf(out, "  GR_MEMORY_TMU              : %ld bytes (%ld MB)\n",
          (long)r->g3_mem_tmu, (long)(r->g3_mem_tmu / (1024 * 1024)));
  fprintf(out, "  GR_MEMORY_UMA / NUM_FB     : %ld MB / %ld\n",
          (long)(r->g3_mem_uma / (1024 * 1024)), (long)r->g3_num_fb);
  fprintf(out, "  GR_MAX_TEXTURE_SIZE        : %ldx%ld\n",
          (long)r->g3_max_tex_size, (long)r->g3_max_tex_size);
  fprintf(out, "  GR_MAX_TEXTURE_ASPECT_RATIO: %ld (max %ld:1)\n",
          (long)r->g3_max_aspect, (long)(1L << r->g3_max_aspect));
  fprintf(out, "  GR_BITS_DEPTH / RGBA       : %ld-bit / %ld-%ld-%ld-%ld (%ld-bit)\n",
          (long)r->g3_bits_depth,
          (long)r->g3_bits_rgba[0], (long)r->g3_bits_rgba[1],
          (long)r->g3_bits_rgba[2], (long)r->g3_bits_rgba[3],
          (long)(r->g3_bits_rgba[0] + r->g3_bits_rgba[1] +
                 r->g3_bits_rgba[2] + r->g3_bits_rgba[3]));
  fprintf(out, "  GR_FOG / GAMMA ENTRIES     : %ld / %ld\n",
          (long)r->g3_fog_entries, (long)r->g3_gamma_entries);
  fprintf(out, "  GR_TEXTURE_ALIGN / PASSTHRU: %ld bytes / %s\n",
          (long)r->g3_tex_align, r->g3_supports_passthru ? "Yes" : "No");
  fprintf(out, "  GR_WDEPTH_MIN_MAX          : %ld .. %ld\n",
          (long)r->g3_wdepth[0], (long)r->g3_wdepth[1]);
  fprintf(out, "  GR_ZDEPTH_MIN_MAX          : %ld .. %ld\n",
          (long)r->g3_zdepth[0], (long)r->g3_zdepth[1]);

  fprintf(out, "\n[Supported Resolutions (grQueryResolutions: %d)]\n",
          r->g3_num_resolutions);
  for (i = 0; i < r->g3_num_resolutions; i += 2) {
    char col0[48];
    snprintf(col0, sizeof(col0), "%-10s @ %-5s (%dc/%da)",
             g3_res_name(r->g3_resolutions[i].resolution),
             g3_refresh_name(r->g3_resolutions[i].refresh),
             r->g3_resolutions[i].numColorBuffers,
             r->g3_resolutions[i].numAuxBuffers);
    if (i + 1 < r->g3_num_resolutions) {
      fprintf(out, "  %-34s  %-10s @ %-5s (%dc/%da)\n", col0,
              g3_res_name(r->g3_resolutions[i + 1].resolution),
              g3_refresh_name(r->g3_resolutions[i + 1].refresh),
              r->g3_resolutions[i + 1].numColorBuffers,
              r->g3_resolutions[i + 1].numAuxBuffers);
    } else {
      fprintf(out, "  %s\n", col0);
    }
  }
#endif

  fprintf(out, "\n[Active Glide Environment Variables (%d set)]\n", r->num_envs);
  if (r->num_envs == 0) {
    fprintf(out, "  (none set)\n");
  } else {
    for (i = 0; i < r->num_envs; ++i) {
      fprintf(out, "  [%-7s] %s=%s\n", r->envs[i].tag, r->envs[i].name,
              r->envs[i].value);
    }
  }

  print_probe_summary(out, &r->probe, 1);
  fprintf(out, "==============================================================================\n");
}

static void print_usage(const char* argv0) {
  printf("Usage: %s [options]\n", argv0 ? argv0 : "glide_info");
  printf("3dfx Glide %d.x hardware & environment diagnostic tool (%s)\n\n",
         GLIDE_VERSION, gtool_platform_name());
  printf("Options:\n");
  printf("  -v, --verbose     Print full multi-board, per-TMU & env var report\n");
  printf("  -p, --probe       Run active HW probe (modes, LFB 565, TMU upload)\n");
  printf("  -o <file>         Write full verbose report (+ probe if -p) to file\n");
  printf("  --pause           Wait for a keypress before exiting\n");
  printf("  -h, --help        Show this help message\n");
}

int main(int argc, char** argv) {
  int verbose = 0;
  int do_probe = 0;
  int do_pause = 0;
  const char* out_path = NULL;
  int i;
  ReportData report;
  char err_buf[256];

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
      verbose = 1;
    } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--probe") == 0) {
      do_probe = 1;
    } else if (strcmp(argv[i], "--pause") == 0) {
      do_pause = 1;
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ||
               strcmp(argv[i], "/?") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
      out_path = argv[++i];
    } else {
      fprintf(stderr, "Unknown option: %s (use -h for help)\n", argv[i]);
      return 1;
    }
  }

  memset(&report, 0, sizeof(report));
  err_buf[0] = '\0';

  if (!gtool_glide_load(report.runtime_source, sizeof(report.runtime_source),
                        err_buf, sizeof(err_buf))) {
    fprintf(stderr, "ERROR: %s\n", err_buf[0] ? err_buf : "Glide load failed");
    if (do_pause) gtool_wait_keypress();
    return 1;
  }

  grGlideInit();
  collect_active_envs(&report);
  collect_glide_info(&report);
  if (do_probe) {
    run_active_probe(&report);
  }
  grGlideShutdown();
  gtool_glide_unload();

  if (verbose) {
    print_verbose_report(stdout, &report);
  } else {
    print_compact_report(stdout, &report);
  }

  if (out_path) {
    FILE* fp = fopen(out_path, "w");
    if (!fp) {
      fprintf(stderr, "ERROR: Failed to open output file '%s'\n", out_path);
      if (do_pause) gtool_wait_keypress();
      return 1;
    }
    print_verbose_report(fp, &report);
    fclose(fp);
    printf("Full report saved to: %s\n", out_path);
  }

  if (do_pause) {
    gtool_wait_keypress();
  }
  return 0;
}

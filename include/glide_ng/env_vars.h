/*
 * Single source of truth for all environment variables recognized by glide-ng
 * and authentic 3dfx Glide. Pure C89/C99/C++ compatible so both the C++ core
 * (IniConfigLoader) and portable C tools (Linux/DOS/Windows glide_info) stay
 * strictly in sync with zero duplicated lists.
 *
 * When adding or implementing a new environment variable, add it here first!
 */
#ifndef GLIDE_NG_ENV_VARS_H
#define GLIDE_NG_ENV_VARS_H

/* Implemented glide-ng wrapper environment variable names */
#define GLIDE_NG_ENV_CONFIG              "GLIDE_NG_CONFIG"
#define GLIDE_NG_ENV_BACKEND             "GLIDE_NG_BACKEND"
#define GLIDE_NG_ENV_HEADLESS            "GLIDE_NG_HEADLESS"
#define GLIDE_NG_ENV_CARD_MODEL          "GLIDE_NG_CARD_MODEL"
#define GLIDE_NG_ENV_API_VERSION         "GLIDE_NG_API_VERSION"
#define GLIDE_NG_ENV_FBI_MEM_MB          "GLIDE_NG_FBI_MEM_MB"
#define GLIDE_NG_ENV_TMU_COUNT           "GLIDE_NG_TMU_COUNT"
#define GLIDE_NG_ENV_TMU_MEM_MB          "GLIDE_NG_TMU_MEM_MB"
#define GLIDE_NG_ENV_SLI                 "GLIDE_NG_SLI"
#define GLIDE_NG_ENV_VENDOR              "GLIDE_NG_VENDOR"
#define GLIDE_NG_ENV_RENDERER            "GLIDE_NG_RENDERER"
#define GLIDE_NG_ENV_VSYNC               "GLIDE_NG_VSYNC"
#define GLIDE_NG_ENV_MAX_FPS             "GLIDE_NG_MAX_FPS"
#define GLIDE_NG_ENV_WINDOW_SCALE        "GLIDE_NG_WINDOW_SCALE"
#define GLIDE_NG_ENV_MSAA                "GLIDE_NG_MSAA"
#define GLIDE_NG_ENV_PRESENTATION_FILTER "GLIDE_NG_PRESENTATION_FILTER"
#define GLIDE_NG_ENV_NO_SPLASH           "GLIDE_NG_NO_SPLASH"
#define GLIDE_NG_ENV_SHAMELESS_PLUG      "GLIDE_NG_SHAMELESS_PLUG"
#define GLIDE_NG_ENV_LOG_LEVEL           "GLIDE_NG_LOG_LEVEL"
#define GLIDE_NG_ENV_LOG_CONSOLE         "GLIDE_NG_LOG_CONSOLE"
#define GLIDE_NG_ENV_LOG_FILE            "GLIDE_NG_LOG_FILE"
#define GLIDE_NG_ENV_LOG_CATEGORIES      "GLIDE_NG_LOG_CATEGORIES"
#define GLIDE_NG_ENV_TRACE_FRAMES        "GLIDE_NG_TRACE_FRAMES"
#define GLIDE_NG_ENV_PERF_LOG            "GLIDE_NG_PERF_LOG"
#define GLIDE_NG_ENV_HUD                 "GLIDE_NG_HUD"
#define GLIDE_NG_ENV_SW_THREADS          "GLIDE_NG_SW_THREADS"
#define GLIDE_NG_ENV_SW_SIMD             "GLIDE_NG_SW_SIMD"
#define GLIDE_NG_ENV_SW_TILE_SIZE        "GLIDE_NG_SW_TILE_SIZE"
#define GLIDE_NG_ENV_SW_AA               "GLIDE_NG_SW_AA"
#define GLIDE_NG_ENV_VK_CACHE_PATH       "GLIDE_NG_VK_CACHE_PATH"
#define GLIDE_NG_ENV_GLES_VBO_SIZE       "GLIDE_NG_GLES_VBO_SIZE"

/* Implemented authentic 3dfx Glide environment variable names */
#define GLIDE_FX_ENV_NO_SPLASH           "FX_GLIDE_NO_SPLASH"
#define GLIDE_FX_ENV_SHAMELESS_PLUG      "FX_GLIDE_SHAMELESS_PLUG"
#define GLIDE_FX_ENV_SWAPINTERVAL        "FX_GLIDE_SWAPINTERVAL"

/* X-macro registry for glide-ng environment variables:
 *   X(SYMBOL_SUFFIX, ENV_NAME, INI_KEY, CATEGORY)
 */
#define GLIDE_NG_ENV_VARS_X(X)                                                                      \
  X(CONFIG,              GLIDE_NG_ENV_CONFIG,              "core.config",              "core")      \
  X(BACKEND,             GLIDE_NG_ENV_BACKEND,             "core.backend",             "core")      \
  X(HEADLESS,            GLIDE_NG_ENV_HEADLESS,            "core.headless",            "core")      \
  X(CARD_MODEL,          GLIDE_NG_ENV_CARD_MODEL,          "hardware.card_model",      "hardware")  \
  X(API_VERSION,         GLIDE_NG_ENV_API_VERSION,         "hardware.api_version",     "hardware")  \
  X(FBI_MEM_MB,          GLIDE_NG_ENV_FBI_MEM_MB,          "hardware.fbi_memory_mb",   "hardware")  \
  X(TMU_COUNT,           GLIDE_NG_ENV_TMU_COUNT,           "hardware.tmu_count",       "hardware")  \
  X(TMU_MEM_MB,          GLIDE_NG_ENV_TMU_MEM_MB,          "hardware.tmu_memory_mb",   "hardware")  \
  X(SLI,                 GLIDE_NG_ENV_SLI,                 "hardware.sli",             "hardware")  \
  X(VENDOR,              GLIDE_NG_ENV_VENDOR,              "hardware.vendor",          "hardware")  \
  X(RENDERER,            GLIDE_NG_ENV_RENDERER,            "hardware.renderer",        "hardware")  \
  X(VSYNC,               GLIDE_NG_ENV_VSYNC,               "display.vsync",            "display")   \
  X(MAX_FPS,             GLIDE_NG_ENV_MAX_FPS,             "display.max_fps",          "display")   \
  X(WINDOW_SCALE,        GLIDE_NG_ENV_WINDOW_SCALE,        "display.window_scale",     "display")   \
  X(MSAA,                GLIDE_NG_ENV_MSAA,                "display.msaa",             "display")   \
  X(PRESENTATION_FILTER, GLIDE_NG_ENV_PRESENTATION_FILTER, "display.presentation_filter", "display") \
  X(NO_SPLASH,           GLIDE_NG_ENV_NO_SPLASH,           "glide.no_splash",          "glide")     \
  X(SHAMELESS_PLUG,      GLIDE_NG_ENV_SHAMELESS_PLUG,      "glide.shameless_plug",     "glide")     \
  X(LOG_LEVEL,           GLIDE_NG_ENV_LOG_LEVEL,           "logging.level",            "logging")   \
  X(LOG_CONSOLE,         GLIDE_NG_ENV_LOG_CONSOLE,         "logging.console",          "logging")   \
  X(LOG_FILE,            GLIDE_NG_ENV_LOG_FILE,            "logging.file",             "logging")   \
  X(LOG_CATEGORIES,      GLIDE_NG_ENV_LOG_CATEGORIES,      "logging.categories",       "logging")   \
  X(TRACE_FRAMES,        GLIDE_NG_ENV_TRACE_FRAMES,        "logging.trace_frames",     "logging")   \
  X(PERF_LOG,            GLIDE_NG_ENV_PERF_LOG,            "logging.perf_log",         "logging")   \
  X(HUD,                 GLIDE_NG_ENV_HUD,                 "hud.elements",             "hud")       \
  X(SW_THREADS,          GLIDE_NG_ENV_SW_THREADS,          "software.threads",         "software")  \
  X(SW_SIMD,             GLIDE_NG_ENV_SW_SIMD,             "software.simd",            "software")  \
  X(SW_TILE_SIZE,        GLIDE_NG_ENV_SW_TILE_SIZE,        "software.tile_size",       "software")  \
  X(SW_AA,               GLIDE_NG_ENV_SW_AA,               "software.aa",              "software")  \
  X(VK_CACHE_PATH,       GLIDE_NG_ENV_VK_CACHE_PATH,       "vulkan.cache_path",        "vulkan")    \
  X(GLES_VBO_SIZE,       GLIDE_NG_ENV_GLES_VBO_SIZE,       "opengl_es.vbo_size_mb",    "opengl_es")

/* X-macro registry for authentic 3dfx Glide environment variables:
 *   X(SYMBOL_SUFFIX, ENV_NAME, INI_KEY, CATEGORY)
 */
#define GLIDE_FX_ENV_VARS_X(X)                                                                      \
  X(NO_SPLASH,           GLIDE_FX_ENV_NO_SPLASH,           "glide.no_splash",          "3dfx")      \
  X(SHAMELESS_PLUG,      GLIDE_FX_ENV_SHAMELESS_PLUG,      "glide.shameless_plug",     "3dfx")      \
  X(SWAPINTERVAL,        GLIDE_FX_ENV_SWAPINTERVAL,        "glide.swap_interval",      "3dfx")

/* Prefixes scanned dynamically by diagnostic tools so user-set 3dfx/SST
 * variables appear automatically even on third-party wrappers or real HW.
 */
#define GLIDE_ENV_SCAN_PREFIXES_X(X) \
  X("FX_GLIDE_")                     \
  X("SST_")                          \
  X("SSTV2_")                        \
  X("GLIDE_NG_")

typedef struct GlideEnvVarSpec {
  const char* envName;
  const char* iniKey;
  const char* category;
  int isAuthentic3dfx;
} GlideEnvVarSpec;

/* Opt-in static table definition for tools/tests that enumerate known vars. */
#ifdef GLIDE_NG_ENV_VARS_DEFINE_TABLE
static const GlideEnvVarSpec kGlideKnownEnvVars[] = {
#define GLIDE_NG_SPEC_ROW_(sym, env, ini, cat) { env, ini, cat, 0 },
#define GLIDE_FX_SPEC_ROW_(sym, env, ini, cat) { env, ini, cat, 1 },
  GLIDE_FX_ENV_VARS_X(GLIDE_FX_SPEC_ROW_)
  GLIDE_NG_ENV_VARS_X(GLIDE_NG_SPEC_ROW_)
#undef GLIDE_NG_SPEC_ROW_
#undef GLIDE_FX_SPEC_ROW_
};

static const char* const kGlideEnvScanPrefixes[] = {
#define GLIDE_PREFIX_ROW_(p) p,
  GLIDE_ENV_SCAN_PREFIXES_X(GLIDE_PREFIX_ROW_)
#undef GLIDE_PREFIX_ROW_
};
#endif /* GLIDE_NG_ENV_VARS_DEFINE_TABLE */

#endif /* GLIDE_NG_ENV_VARS_H */

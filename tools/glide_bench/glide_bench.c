/*
 * glide_bench.c - Unified 3dfx Glide 2.x / 3.x Benchmark Runner
 *
 * Pure C99, 100% authentic Glide API calls.
 * Builds cleanly for Linux (glide2_bench, glide3_bench), DOS/32 CauseWay
 * (build/dos/glide_bench.exe), and Windows.
 */

#define GLIDE_TOOL_LOADER_IMPLEMENTATION 1
#include "glide_bench.h"

#define MAX_SELECTED_SCENES 32
#define MAX_FRAME_SAMPLES   32768

typedef struct SceneResult {
  const BenchScene* scene;
  int ran;
  int frames;
  double elapsed_s;
  double avg_fps;
  double min_fps;
  double p1_low_fps;
  double avg_ms;
  double p99_ms;
  double throughput;
  double score;
} SceneResult;

static int cmp_double(const void* a, const void* b) {
  double da = *(const double*)a;
  double db = *(const double*)b;
  if (da < db) return -1;
  if (da > db) return 1;
  return 0;
}

static int parse_res_arg(const char* s, GrScreenResolution_t* out_enum,
                         int* out_w, int* out_h) {
  if (!s) return 0;
  if (strcmp(s, "512") == 0 || gtool_strcasecmp(s, "512x384") == 0) {
    *out_enum = GR_RESOLUTION_512x384; *out_w = 512; *out_h = 384; return 1;
  }
  if (strcmp(s, "640") == 0 || gtool_strcasecmp(s, "640x480") == 0) {
    *out_enum = GR_RESOLUTION_640x480; *out_w = 640; *out_h = 480; return 1;
  }
  if (strcmp(s, "800") == 0 || gtool_strcasecmp(s, "800x600") == 0) {
    *out_enum = GR_RESOLUTION_800x600; *out_w = 800; *out_h = 600; return 1;
  }
  if (strcmp(s, "1024") == 0 || gtool_strcasecmp(s, "1024x768") == 0) {
    *out_enum = GR_RESOLUTION_1024x768; *out_w = 1024; *out_h = 768; return 1;
  }
  if (strcmp(s, "1280") == 0 || gtool_strcasecmp(s, "1280x1024") == 0) {
    *out_enum = GR_RESOLUTION_1280x1024; *out_w = 1280; *out_h = 1024; return 1;
  }
  if (strcmp(s, "1600") == 0 || gtool_strcasecmp(s, "1600x1200") == 0) {
    *out_enum = GR_RESOLUTION_1600x1200; *out_w = 1600; *out_h = 1200; return 1;
  }
  return 0;
}

static const char* cat_tag(int cat) {
  if (cat == BENCH_CAT_CORE) return "core";
  if (cat == BENCH_CAT_EXT) return "ext";
  return "vsa100";
}

static void print_usage(const char* argv0) {
  int count = 0;
  int i;
  const BenchScene* scenes = bench_get_scenes(&count);

  printf("Usage: %s [options]\n", argv0 ? argv0 : "glide_bench");
  printf("3dfx Glide %d.x Benchmark Suite (%s)\n\n",
         GLIDE_VERSION, gtool_platform_name());
  printf("Options:\n");
  printf("  --scene <set|list>   core, extended, vsa100, all, or comma-separated (default: all)\n");
  printf("  --res <512|640|800|1024|1280|1600> Resolution (default: 640)\n");
#if GLIDE_VERSION == 3
  printf("  --aa <0|2|4>         VSA-100 Spatial AA samples for all scenes (default: 0)\n");
#endif
  printf("  --seconds <N>        Duration per scene in seconds (default: 5)\n");
  printf("  --frames <N>         Fixed frame count per scene (overrides --seconds)\n");
  printf("  --loop               Loop continuously until ESC or Q is pressed\n");
  printf("  --no-hud             Disable pure-Glide textured-quad in-scene HUD\n");
  printf("  -o <file>            Save human-readable summary report to file\n");
  printf("  --csv <file>         Export per-scene metrics to CSV file\n");
  printf("  --list               List registered benchmark scenes and exit\n");
  printf("  --pause              Wait for keypress before exiting\n");
  printf("  -h, --help           Show this help message\n\n");
  printf("Registered scenes (%d):\n", count);
  for (i = 0; i < count; ++i) {
    printf("  %-12s [%-6s]  %s [%s]\n", scenes[i].name,
           cat_tag(scenes[i].category), scenes[i].title,
           scenes[i].metric_unit);
  }
}

static int run_single_scene(BenchContext* ctx,
                            const BenchScene* scene,
                            int scene_idx,
                            int scene_total,
                            double target_seconds,
                            int target_frames,
                            int loop_mode,
                            double* frame_ms_buf,
                            SceneResult* out_res,
                            int* out_aborted) {
  double t_start, t_prev, t_now;
  double total_work = 0.0;
  double inst_fps = 0.0;
  double avg_ms = 0.0;
  double work_units = 0.0;
  int frames = 0;
  int sample_count = 0;

  memset(out_res, 0, sizeof(*out_res));
  out_res->scene = scene;

  if (!scene->setup(ctx)) {
    return 0;
  }

  /* 1-frame untimed warmup (compiles shaders / caches pipelines) */
  scene->render_frame(ctx, 0, 0.0, &work_units);
  if (ctx->hud_enabled) {
    bench_hud_draw(ctx, scene->name, scene_idx, scene_total,
                   0.0, 0.0, 0.0, target_frames > 0 ? 0.0 : target_seconds);
  }
  grBufferSwap(0);
#if GLIDE_VERSION == 3
  grFinish();
#else
  grSstIdle();
#endif

  t_start = gtool_time_sec();
  t_prev = t_start;

  while (1) {
    double dt_s;
    double elapsed_s;
    int key;

    work_units = 0.0;
    scene->render_frame(ctx, frames + 1, t_prev - t_start, &work_units);
    if (ctx->hud_enabled) {
      bench_hud_draw(ctx, scene->name, scene_idx, scene_total,
                     inst_fps, avg_ms, t_prev - t_start,
                     (target_frames > 0 || loop_mode) ? 0.0 : target_seconds);
    }
    grBufferSwap(0);

    t_now = gtool_time_sec();
    dt_s = t_now - t_prev;
    if (dt_s < 1e-7) dt_s = 1e-7;
    t_prev = t_now;

    if (sample_count < MAX_FRAME_SAMPLES) {
      frame_ms_buf[sample_count++] = dt_s * 1000.0;
    }
    total_work += work_units;
    frames++;

    elapsed_s = t_now - t_start;
    if (elapsed_s > 0.0) {
      inst_fps = (double)frames / elapsed_s;
      avg_ms = (elapsed_s * 1000.0) / (double)frames;
    }

    key = gtool_poll_key();
    if (key == 27 || key == 'q' || key == 'Q') {
      if (out_aborted) *out_aborted = 1;
      break;
    }

    if (!loop_mode) {
      if (target_frames > 0) {
        if (frames >= target_frames) break;
      } else {
        if (elapsed_s >= target_seconds) break;
      }
    }
  }

  scene->teardown(ctx);

  {
    double total_s = t_prev - t_start;
    if (total_s < 1e-6) total_s = 1e-6;
    out_res->ran = 1;
    out_res->frames = frames;
    out_res->elapsed_s = total_s;
    out_res->avg_fps = (double)frames / total_s;
    out_res->avg_ms = (total_s * 1000.0) / (double)frames;
    out_res->throughput = total_work / total_s;

    if (sample_count > 0) {
      int p99_idx;
      double worst_ms;
      qsort(frame_ms_buf, (size_t)sample_count, sizeof(double), cmp_double);
      p99_idx = (int)ceil((double)sample_count * 0.99) - 1;
      if (p99_idx < 0) p99_idx = 0;
      if (p99_idx >= sample_count) p99_idx = sample_count - 1;
      out_res->p99_ms = frame_ms_buf[p99_idx];
      worst_ms = frame_ms_buf[sample_count - 1];
      out_res->p1_low_fps = (out_res->p99_ms > 1e-4) ? (1000.0 / out_res->p99_ms) : 0.0;
      out_res->min_fps = (worst_ms > 1e-4) ? (1000.0 / worst_ms) : 0.0;
    }

    /* 3DMark-style scene score: 30 FPS reference = 1000 pts */
    out_res->score = 1000.0 * (out_res->avg_fps / scene->ref_fps);
  }
  return 1;
}

static void print_summary(FILE* out,
                          const char* runtime_source,
                          const char* glide_ver,
                          const char* hw_str,
                          const char* renderer_str,
                          int width, int height,
                          const SceneResult* results,
                          int num_results) {
  int i;
  int ran_count = 0;
  int core_count = 0;
  int near_60_count = 0;
  double log_fps_sum = 0.0;
  double log_score_sum = 0.0;
  double weight_sum = 0.0;
  double core_log_fps_sum = 0.0;
  double core_log_score_sum = 0.0;
  double core_weight_sum = 0.0;
  double geomean_fps = 0.0;
  double glidemark_score = 0.0;

  fprintf(out, "==============================================================================\n");
  fprintf(out, " glide_bench - 3dfx Glide %d.x Suite (%dx%d)          [%-18s]\n",
          GLIDE_VERSION, width, height, gtool_platform_name());
  fprintf(out, "==============================================================================\n");
  fprintf(out, " Runtime  : %s  |  Glide: %s\n", runtime_source, glide_ver);
  if (hw_str && hw_str[0]) {
    fprintf(out, " Hardware : %.64s\n", hw_str);
  }
  if (renderer_str && renderer_str[0]) {
    fprintf(out, " Renderer : %.64s\n", renderer_str);
  }
  fprintf(out, "------------------------------------------------------------------------------\n");
  fprintf(out, " %-11s %8s %8s %8s %8s %16s %8s\n",
          "Scene", "Avg FPS", "1% Low", "Avg ms", "P99 ms", "Throughput", "Score");
  fprintf(out, "------------------------------------------------------------------------------\n");

  for (i = 0; i < num_results; ++i) {
    char tp_buf[32];
    if (!results[i].scene) {
      continue;
    }
    if (!results[i].ran) {
      fprintf(out, " %-11s   [SKIP - unsupported on active card/config]\n",
              results[i].scene->name);
      continue;
    }
    snprintf(tp_buf, sizeof(tp_buf), "%7.1f %-7s",
             results[i].throughput, results[i].scene->metric_unit);
    fprintf(out, " %-11s %8.1f %8.1f %8.2f %8.2f %16s %8.0f\n",
            results[i].scene->name,
            results[i].avg_fps,
            results[i].p1_low_fps,
            results[i].avg_ms,
            results[i].p99_ms,
            tp_buf,
            results[i].score);

    if (results[i].avg_fps > 0.0) {
      double w = results[i].scene->score_weight;
      log_fps_sum += w * log(results[i].avg_fps);
      log_score_sum += w * log(results[i].score > 0.0 ? results[i].score : 1.0);
      weight_sum += w;
      if (results[i].scene->category == BENCH_CAT_CORE) {
        core_log_fps_sum += w * log(results[i].avg_fps);
        core_log_score_sum += w * log(results[i].score > 0.0 ? results[i].score : 1.0);
        core_weight_sum += w;
        core_count++;
      }
    }
    if (results[i].avg_fps >= 58.0 && results[i].avg_fps <= 62.5) {
      near_60_count++;
    }
    ran_count++;
  }

  if (ran_count > 0 && weight_sum > 0.0) {
    geomean_fps = exp(log_fps_sum / weight_sum);
    glidemark_score = exp(log_score_sum / weight_sum);
  }

  fprintf(out, "------------------------------------------------------------------------------\n");
  if (core_count == 6 && ran_count > 6 && core_weight_sum > 0.0) {
    double core_fps = exp(core_log_fps_sum / core_weight_sum);
    double core_score = exp(core_log_score_sum / core_weight_sum);
    fprintf(out, " Core GlideMark (6 scenes) : %7.0f pts  (geomean %.1f FPS)\n",
            core_score, core_fps);
  }
  fprintf(out, " Composite Geomean FPS     : %7.1f FPS (%d scene%s)\n",
          geomean_fps, ran_count, (ran_count == 1) ? "" : "s");
  fprintf(out, " GlideMark Score (%dx%d) : %7.0f pts  (ref: 30 FPS = 1000 pts)\n",
          width, height, glidemark_score);

  if (ran_count > 0 && (near_60_count >= 2 ||
                        (ran_count == 1 && near_60_count == 1))) {
    fprintf(out, " Note: FPS near 60 Hz (check VSync / GLIDE_NG_VSYNC=0, GLIDE_NG_MAX_FPS=0).\n");
  }
  fprintf(out, "==============================================================================\n");
}

static int write_csv(const char* path, int width, int height,
                     const SceneResult* results, int num_results) {
  FILE* fp = fopen(path, "w");
  int i;
  if (!fp) return 0;
  fprintf(fp, "scene,res_w,res_h,frames,elapsed_s,avg_fps,min_fps,p1_low_fps,avg_ms,p99_ms,throughput,unit,score\n");
  for (i = 0; i < num_results; ++i) {
    if (!results[i].scene || !results[i].ran) continue;
    fprintf(fp, "%s,%d,%d,%d,%.4f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%s,%.1f\n",
            results[i].scene->name, width, height, results[i].frames,
            results[i].elapsed_s, results[i].avg_fps, results[i].min_fps,
            results[i].p1_low_fps, results[i].avg_ms, results[i].p99_ms,
            results[i].throughput, results[i].scene->metric_unit,
            results[i].score);
  }
  fclose(fp);
  return 1;
}

#if GLIDE_VERSION == 3
static void resolve_g3_ext_procs(BenchContext* ctx) {
  GrProc p;
#define LOAD_G3_PROC(field, sym)                     \
  do {                                               \
    p = grGetProcAddress((char*)(sym));              \
    if (p) {                                         \
      memcpy(&ctx->field, &p, sizeof(ctx->field));   \
    }                                                \
  } while (0)
  LOAD_G3_PROC(fn_winOpenExt,       "grSstWinOpenExt");
  LOAD_G3_PROC(fn_tbufWriteMask,    "grTBufferWriteMaskExt");
  LOAD_G3_PROC(fn_colorMaskExt,     "grColorMaskExt");
  LOAD_G3_PROC(fn_stencilFunc,      "grStencilFuncExt");
  LOAD_G3_PROC(fn_stencilMask,      "grStencilMaskExt");
  LOAD_G3_PROC(fn_stencilOp,        "grStencilOpExt");
  LOAD_G3_PROC(fn_bufferClearExt,   "grBufferClearExt");
  LOAD_G3_PROC(fn_colorCombineExt,  "grColorCombineExt");
  LOAD_G3_PROC(fn_alphaCombineExt,  "grAlphaCombineExt");
  LOAD_G3_PROC(fn_texColorCombineExt, "grTexColorCombineExt");
  LOAD_G3_PROC(fn_texAlphaCombineExt, "grTexAlphaCombineExt");
  LOAD_G3_PROC(fn_constColorExt,    "grConstantColorValueExt");
  LOAD_G3_PROC(fn_alphaBlendExt,    "grAlphaBlendFunctionExt");
  LOAD_G3_PROC(fn_textureBufferExt, "grTextureBufferExt");
#undef LOAD_G3_PROC
}

static GrContext_t open_g3_context(BenchContext* ctx,
                                   GrScreenResolution_t res_enum,
                                   int requested_aa) {
  GrContext_t h = 0;
  ctx->aa_samples = 0;
  if (requested_aa >= 2 && bench_has_ext(ctx, "PIXEXT") && ctx->fn_winOpenExt) {
    GrPixelFormat_t pf = (requested_aa >= 4) ? GR_PIXFMT_AA_4_RGB_565
                                             : GR_PIXFMT_AA_2_RGB_565;
    h = ctx->fn_winOpenExt(0, res_enum, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR,
                           GR_ORIGIN_UPPER_LEFT, pf, 2, 1);
    if (h != 0) {
      ctx->aa_samples = (requested_aa >= 4) ? 4 : 2;
      return h;
    }
    if (requested_aa >= 4) {
      h = ctx->fn_winOpenExt(0, res_enum, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR,
                             GR_ORIGIN_UPPER_LEFT, GR_PIXFMT_AA_2_RGB_565, 2, 1);
      if (h != 0) {
        ctx->aa_samples = 2;
        return h;
      }
    }
  }
  return grSstWinOpen(0, res_enum, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR,
                      GR_ORIGIN_UPPER_LEFT, 2, 1);
}
#endif

int main(int argc, char** argv) {
  const char* scene_arg = "all";
  GrScreenResolution_t res_enum = GR_RESOLUTION_640x480;
  int width = 640;
  int height = 480;
  int requested_aa = 0;
  double target_seconds = 5.0;
  int target_frames = 0;
  int loop_mode = 0;
  int hud_enabled = 1;
  int do_pause = 0;
  const char* out_path = NULL;
  const char* csv_path = NULL;

  int all_count = 0;
  const BenchScene* all_scenes = bench_get_scenes(&all_count);
  const BenchScene* selected[MAX_SELECTED_SCENES];
  int num_selected = 0;
  SceneResult results[MAX_SELECTED_SCENES];
  int num_executed = 0;

  char runtime_src[64];
  char err_buf[256];
  char glide_ver[80];
  char hw_str[128];
  char renderer_str[128];
  double* frame_ms_buf = NULL;
  BenchContext ctx;
  int i;
  int aborted = 0;
  int detected_tmus = 1;
#if GLIDE_VERSION == 3
  GrContext_t g3_ctx = 0;
  int active_aa = 0;
#endif

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--scene") == 0 && i + 1 < argc) {
      scene_arg = argv[++i];
    } else if (strcmp(argv[i], "--res") == 0 && i + 1 < argc) {
      if (!parse_res_arg(argv[++i], &res_enum, &width, &height)) {
        fprintf(stderr, "ERROR: Invalid --res '%s' (use 512, 640, 800, 1024, 1280, or 1600)\n",
                argv[i]);
        return 1;
      }
#if GLIDE_VERSION == 3
    } else if (strcmp(argv[i], "--aa") == 0 && i + 1 < argc) {
      requested_aa = atoi(argv[++i]);
      if (requested_aa != 0 && requested_aa != 2 && requested_aa != 4) {
        fprintf(stderr, "ERROR: Invalid --aa '%s' (use 0, 2, or 4)\n", argv[i]);
        return 1;
      }
#endif
    } else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
      target_seconds = atof(argv[++i]);
      if (target_seconds <= 0.0) target_seconds = 1.0;
    } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      target_frames = atoi(argv[++i]);
      if (target_frames < 1) target_frames = 1;
    } else if (strcmp(argv[i], "--loop") == 0) {
      loop_mode = 1;
    } else if (strcmp(argv[i], "--no-hud") == 0) {
      hud_enabled = 0;
    } else if (strcmp(argv[i], "--pause") == 0) {
      do_pause = 1;
    } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
      out_path = argv[++i];
    } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
      csv_path = argv[++i];
    } else if (strcmp(argv[i], "--list") == 0) {
      for (i = 0; i < all_count; ++i) {
        printf("%-12s [%-6s]  %s [%s]\n", all_scenes[i].name,
               cat_tag(all_scenes[i].category), all_scenes[i].title,
               all_scenes[i].metric_unit);
      }
      return 0;
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 ||
               strcmp(argv[i], "/?") == 0) {
      print_usage(argv[0]);
      return 0;
    } else {
      fprintf(stderr, "Unknown option: %s (use -h for help)\n", argv[i]);
      return 1;
    }
  }

  /* Resolve requested scenes (supports core/extended/vsa100/all or comma list) */
  if (gtool_strcasecmp(scene_arg, "all") == 0) {
    for (i = 0; i < all_count && num_selected < MAX_SELECTED_SCENES; ++i) {
      selected[num_selected++] = &all_scenes[i];
    }
  } else if (gtool_strcasecmp(scene_arg, "core") == 0) {
    for (i = 0; i < all_count && num_selected < MAX_SELECTED_SCENES; ++i) {
      if (all_scenes[i].category == BENCH_CAT_CORE) {
        selected[num_selected++] = &all_scenes[i];
      }
    }
  } else if (gtool_strcasecmp(scene_arg, "extended") == 0 ||
             gtool_strcasecmp(scene_arg, "ext") == 0) {
    for (i = 0; i < all_count && num_selected < MAX_SELECTED_SCENES; ++i) {
      if (all_scenes[i].category != BENCH_CAT_CORE) {
        selected[num_selected++] = &all_scenes[i];
      }
    }
  } else if (gtool_strcasecmp(scene_arg, "vsa100") == 0) {
    for (i = 0; i < all_count && num_selected < MAX_SELECTED_SCENES; ++i) {
      if (all_scenes[i].category == BENCH_CAT_VSA100) {
        selected[num_selected++] = &all_scenes[i];
      }
    }
  } else {
    char copy[256];
    char* tok;
    snprintf(copy, sizeof(copy), "%s", scene_arg);
    tok = strtok(copy, ",");
    while (tok && num_selected < MAX_SELECTED_SCENES) {
      int found = 0;
      for (i = 0; i < all_count; ++i) {
        if (gtool_strcasecmp(tok, all_scenes[i].name) == 0) {
          selected[num_selected++] = &all_scenes[i];
          found = 1;
          break;
        }
      }
      if (!found) {
        fprintf(stderr, "ERROR: Unknown scene '%s' (use --list)\n", tok);
        return 1;
      }
      tok = strtok(NULL, ",");
    }
  }

  if (num_selected == 0) {
    fprintf(stderr, "ERROR: No scenes selected.\n");
    return 1;
  }

  frame_ms_buf = (double*)malloc(sizeof(double) * MAX_FRAME_SAMPLES);
  if (!frame_ms_buf) {
    fprintf(stderr, "ERROR: Out of memory allocating frame timing buffer.\n");
    return 1;
  }

  runtime_src[0] = '\0';
  err_buf[0] = '\0';
  glide_ver[0] = '\0';
  hw_str[0] = '\0';
  renderer_str[0] = '\0';
  memset(&ctx, 0, sizeof(ctx));
  memset(results, 0, sizeof(results));

  if (!gtool_glide_load(runtime_src, sizeof(runtime_src), err_buf, sizeof(err_buf))) {
    fprintf(stderr, "ERROR: %s\n", err_buf[0] ? err_buf : "Glide load failed");
    free(frame_ms_buf);
    if (do_pause) gtool_wait_keypress();
    return 1;
  }

  grGlideInit();

  /* Collect concise hardware/runtime metadata before opening window */
#if GLIDE_VERSION == 2
  {
    GrHwConfiguration hw;
    const char* s;
    (void)requested_aa;
    memset(&hw, 0, sizeof(hw));
    grGlideGetVersion(glide_ver);
    if (!glide_ver[0]) snprintf(glide_ver, sizeof(glide_ver), "2.x");
    if (!grSstQueryHardware(&hw) || hw.num_sst <= 0) {
      fprintf(stderr, "ERROR: No Glide 2.x hardware detected.\n");
      grGlideShutdown();
      gtool_glide_unload();
      free(frame_ms_buf);
      if (do_pause) gtool_wait_keypress();
      return 1;
    }
    grSstSelect(0);
    if (hw.SSTs[0].type == GR_SSTTYPE_VOODOO) {
      detected_tmus = hw.SSTs[0].sstBoard.VoodooConfig.nTexelfx;
    } else if (hw.SSTs[0].type == GR_SSTTYPE_SST96) {
      detected_tmus = hw.SSTs[0].sstBoard.SST96Config.nTexelfx;
    } else if (hw.SSTs[0].type == GR_SSTTYPE_Voodoo2) {
      detected_tmus = hw.SSTs[0].sstBoard.Voodoo2Config.nTexelfx;
    }
    if (detected_tmus < 1) detected_tmus = 1;
    s = gtool_glide2_try_get_string(GR_HARDWARE);
    if (s) {
      snprintf(hw_str, sizeof(hw_str), "%s", s);
    } else if (hw.SSTs[0].type == GR_SSTTYPE_VOODOO) {
      int rev = hw.SSTs[0].sstBoard.VoodooConfig.fbiRev;
      if ((rev & 0x11000) == 0x11000) {
        snprintf(hw_str, sizeof(hw_str), "Voodoo3 / VSA-100 (H3+)");
      } else if ((rev & 0x01000) == 0x01000) {
        snprintf(hw_str, sizeof(hw_str), "Voodoo Banshee (H3)");
      } else {
        snprintf(hw_str, sizeof(hw_str), "Voodoo Graphics (SST-1)");
      }
    } else if (hw.SSTs[0].type == GR_SSTTYPE_SST96) {
      snprintf(hw_str, sizeof(hw_str), "Voodoo Rush (SST-96)");
    } else {
      snprintf(hw_str, sizeof(hw_str), "Voodoo2 (CVG)");
    }
    s = gtool_glide2_try_get_string(GR_RENDERER);
    if (s) snprintf(renderer_str, sizeof(renderer_str), "%s", s);
  }
#else
  {
    const char* s;
    FxI32 tmu_val = 1;
    s = grGetString(GR_VERSION);
    snprintf(glide_ver, sizeof(glide_ver), "%s", s ? s : "3.x");
    s = grGetString(GR_HARDWARE);
    if (s) snprintf(hw_str, sizeof(hw_str), "%s", s);
    s = grGetString(GR_RENDERER);
    if (s) snprintf(renderer_str, sizeof(renderer_str), "%s", s);
    s = grGetString(GR_EXTENSION);
    if (s) snprintf(ctx.extensions, sizeof(ctx.extensions), "%s", s);
    if (grGet(GR_NUM_TMU, 4, &tmu_val) > 0 && tmu_val >= 1) {
      detected_tmus = (int)tmu_val;
    }
    resolve_g3_ext_procs(&ctx);
    grSstSelect(0);
  }
#endif

  ctx.width = width;
  ctx.height = height;
  ctx.num_tmus = detected_tmus;
  ctx.tmu0_min = grTexMinAddress(GR_TMU0);
  ctx.tmu0_max = grTexMaxAddress(GR_TMU0);
  if (ctx.num_tmus >= 2) {
    ctx.tmu1_min = grTexMinAddress(GR_TMU1);
    ctx.tmu1_max = grTexMaxAddress(GR_TMU1);
  }
  ctx.scene_tmu_limit = ctx.tmu0_max;
  ctx.hud_enabled = hud_enabled;

#if GLIDE_VERSION == 3
  g3_ctx = open_g3_context(&ctx, res_enum, requested_aa);
  if (g3_ctx == 0) {
    fprintf(stderr, "ERROR: grSstWinOpen failed for %dx%d.\n", width, height);
    grGlideShutdown();
    gtool_glide_unload();
    free(frame_ms_buf);
    if (do_pause) gtool_wait_keypress();
    return 1;
  }
  active_aa = ctx.aa_samples;
#else
  if (!grSstWinOpen(0, res_enum, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR,
                    GR_ORIGIN_UPPER_LEFT, 2, 1)) {
    fprintf(stderr, "ERROR: grSstWinOpen failed for %dx%d.\n", width, height);
    grGlideShutdown();
    gtool_glide_unload();
    free(frame_ms_buf);
    if (do_pause) gtool_wait_keypress();
    return 1;
  }
#endif

  bench_init_vertex_layout();
  if (ctx.hud_enabled) {
    bench_hud_init(&ctx);
  }

  gtool_console_begin();
  for (i = 0; i < num_selected && !aborted; ++i) {
#if GLIDE_VERSION == 3
    int want_aa = (selected[i]->requires_aa > 0)
                      ? ((requested_aa >= 2) ? requested_aa : selected[i]->requires_aa)
                      : requested_aa;
    if (want_aa != active_aa &&
        (want_aa == 0 || (bench_has_ext(&ctx, "PIXEXT") && ctx.fn_winOpenExt))) {
      grSstWinClose(g3_ctx);
      g3_ctx = open_g3_context(&ctx, res_enum, want_aa);
      if (g3_ctx == 0) {
        aborted = 1;
        break;
      }
      active_aa = ctx.aa_samples;
      bench_init_vertex_layout();
      ctx.scene_tmu_limit = ctx.tmu0_max;
      if (ctx.hud_enabled) {
        bench_hud_init(&ctx);
      }
    }
#endif
    run_single_scene(&ctx, selected[i], i, num_selected, target_seconds,
                     target_frames, loop_mode, frame_ms_buf, &results[i],
                     &aborted);
    num_executed = i + 1;
  }
  gtool_console_end();

#if GLIDE_VERSION == 3
  grSstWinClose(g3_ctx);
#else
  grSstWinClose();
#endif
  grGlideShutdown();
  gtool_glide_unload();
  free(frame_ms_buf);

  print_summary(stdout, runtime_src, glide_ver, hw_str, renderer_str,
                width, height, results, num_executed);

  if (out_path) {
    FILE* fp = fopen(out_path, "w");
    if (fp) {
      print_summary(fp, runtime_src, glide_ver, hw_str, renderer_str,
                    width, height, results, num_executed);
      fclose(fp);
      printf("Summary saved to: %s\n", out_path);
    } else {
      fprintf(stderr, "ERROR: Failed to write report to '%s'\n", out_path);
    }
  }

  if (csv_path) {
    if (write_csv(csv_path, width, height, results, num_executed)) {
      printf("CSV saved to: %s\n", csv_path);
    } else {
      fprintf(stderr, "ERROR: Failed to write CSV to '%s'\n", csv_path);
    }
  }

  if (do_pause) {
    gtool_wait_keypress();
  }
  return 0;
}

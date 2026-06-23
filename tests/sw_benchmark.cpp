#include <glide.h>
#include <iostream>
#include <chrono>
#include <vector>
#include <iomanip>
#include <cstdlib>
#include <cmath>

struct BenchmarkVert {
  float x, y;
  float r, g, b;
};

void SetHeadless() {
  setenv("GLIDE_HEADLESS", "1", 1);
  setenv("GLIDE_WRAPPER_BACKEND", "software", 1);
  setenv("GLIDE_WRAPPER_FORCE_NO_WINDOW", "1", 1);
  setenv("GLIDE_WRAPPER_LOG_LEVEL", "0", 1); // 0 = OFF (fully disable logging overhead)
  setenv("GLIDE_WRAPPER_LOG_TO_CONSOLE", "0", 1);
}

int main() {
  SetHeadless();

  // 1. ClearBuffer Bandwidth Benchmarks
  double cb_640 = 0.0;
  double cb_1024 = 0.0;
  double cb_2048 = 0.0;

  auto run_clear_benchmark = [](GrScreenResolution_t res, int width, int height) -> double {
    grGlideInit();
    GrContext_t ctx = grSstWinOpen(0, res, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    if (!ctx) return 0.0;

    // Warm up
    for (int i = 0; i < 5; ++i) {
      grBufferClear(0xFF112233, 0xFF, 0xFFFF);
    }

    auto start = std::chrono::high_resolution_clock::now();
    const int iterations = 100;
    for (int i = 0; i < iterations; ++i) {
      grBufferClear(0xFF112233, 0xFF, 0xFFFF);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    grSstWinClose(ctx);
    grGlideShutdown();

    double total_bytes = static_cast<double>(width) * height * 8.0 * iterations; // 4 bytes color + 4 bytes depth
    return (total_bytes / 1e9) / diff.count(); // GB/s
  };

  cb_640 = run_clear_benchmark(GR_RESOLUTION_640x480, 640, 480);
  cb_1024 = run_clear_benchmark(GR_RESOLUTION_1024x768, 1024, 768);
  cb_2048 = run_clear_benchmark(GR_RESOLUTION_2048x1536, 2048, 1536);

  // 2. WriteLFB Throughput Benchmarks
  double lfb_unmasked = 0.0;
  double lfb_masked = 0.0;

  {
    grGlideInit();
    GrContext_t ctx = grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    if (ctx) {
      std::vector<uint32_t> srcData(640 * 480, 0xFF112233);

      // Unmasked writes (pixel pipeline disabled)
      for (int i = 0; i < 5; ++i) {
        grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 0, 0, GR_LFB_SRC_FMT_8888, 640, 480, FXFALSE, 640 * 4, srcData.data());
      }
      auto start = std::chrono::high_resolution_clock::now();
      const int iterations = 100;
      for (int i = 0; i < iterations; ++i) {
        grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 0, 0, GR_LFB_SRC_FMT_8888, 640, 480, FXFALSE, 640 * 4, srcData.data());
      }
      auto end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> diff = end - start;
      lfb_unmasked = (640.0 * 480.0 * iterations) / diff.count();

      // Masked writes (pixel pipeline enabled for alpha blending)
      grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
      for (int i = 0; i < 5; ++i) {
        grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 0, 0, GR_LFB_SRC_FMT_8888, 640, 480, FXTRUE, 640 * 4, srcData.data());
      }
      start = std::chrono::high_resolution_clock::now();
      for (int i = 0; i < iterations; ++i) {
        grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 0, 0, GR_LFB_SRC_FMT_8888, 640, 480, FXTRUE, 640 * 4, srcData.data());
      }
      end = std::chrono::high_resolution_clock::now();
      diff = end - start;
      lfb_masked = (640.0 * 480.0 * iterations) / diff.count();

      grSstWinClose(ctx);
      grGlideShutdown();
    }
  }

  // 3. Triangle Rasterization Benchmark
  double raster_triangles_per_sec = 0.0;

  {
    grGlideInit();
    GrContext_t ctx = grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    if (ctx) {
      grVertexLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
      grVertexLayout(GR_PARAM_RGB, 8, GR_PARAM_ENABLE);

      // Create a grid of triangles covering the screen
      // 100x100 grid = 10,000 vertices, forming 99*99*2 = 19,602 triangles
      const int grid_w = 100;
      const int grid_h = 100;
      std::vector<BenchmarkVert> grid(grid_w * grid_h);
      for (int y = 0; y < grid_h; ++y) {
        for (int x = 0; x < grid_w; ++x) {
          float fx = (static_cast<float>(x) / (grid_w - 1)) * 640.0f;
          float fy = (static_cast<float>(y) / (grid_h - 1)) * 480.0f;
          grid[y * grid_w + x] = {fx, fy, 255.0f, 128.0f, 64.0f};
        }
      }

      // Warm up
      for (int frame = 0; frame < 2; ++frame) {
        grBufferClear(0xFF000000, 0, 0xFFFFFFFF);
        for (int y = 0; y < grid_h - 1; ++y) {
          for (int x = 0; x < grid_w - 1; ++x) {
            const auto& v00 = grid[y * grid_w + x];
            const auto& v10 = grid[y * grid_w + (x + 1)];
            const auto& v01 = grid[(y + 1) * grid_w + x];
            const auto& v11 = grid[(y + 1) * grid_w + (x + 1)];
            grDrawTriangle(&v00, &v10, &v01);
            grDrawTriangle(&v10, &v11, &v01);
          }
        }
      }

      const int frames = 10;
      auto start = std::chrono::high_resolution_clock::now();
      for (int frame = 0; frame < frames; ++frame) {
        grBufferClear(0xFF000000, 0, 0xFFFFFFFF);
        for (int y = 0; y < grid_h - 1; ++y) {
          for (int x = 0; x < grid_w - 1; ++x) {
            const auto& v00 = grid[y * grid_w + x];
            const auto& v10 = grid[y * grid_w + (x + 1)];
            const auto& v01 = grid[(y + 1) * grid_w + x];
            const auto& v11 = grid[(y + 1) * grid_w + (x + 1)];
            grDrawTriangle(&v00, &v10, &v01);
            grDrawTriangle(&v10, &v11, &v01);
          }
        }
      }
      grFinish();
      auto end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> diff = end - start;

      double total_triangles = static_cast<double>(grid_w - 1) * (grid_h - 1) * 2.0 * frames;
      raster_triangles_per_sec = total_triangles / diff.count();

      grSstWinClose(ctx);
      grGlideShutdown();
    }
  }

  // Output as a clean, parsable JSON
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "{\n"
            << "  \"clear_buffer_640x480_gbps\": " << cb_640 << ",\n"
            << "  \"clear_buffer_1024x768_gbps\": " << cb_1024 << ",\n"
            << "  \"clear_buffer_2048x1536_gbps\": " << cb_2048 << ",\n"
            << "  \"lfb_write_unmasked_pixels_per_sec\": " << lfb_unmasked << ",\n"
            << "  \"lfb_write_masked_pixels_per_sec\": " << lfb_masked << ",\n"
            << "  \"rasterizer_triangles_per_sec\": " << raster_triangles_per_sec << "\n"
            << "}\n";

  return 0;
}

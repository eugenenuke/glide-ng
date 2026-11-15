#include <glide.h>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdint>

struct ValidationVert {
  float x, y, z;
  float r, g, b, a;
};

bool SaveTGA(const char* filename, int width, int height, const uint32_t* pixels) {
  FILE* f = fopen(filename, "wb");
  if (!f) return false;

  uint8_t header[18] = {0};
  header[2] = 2; // Uncompressed true-color image
  header[12] = width & 0xFF;
  header[13] = (width >> 8) & 0xFF;
  header[14] = height & 0xFF;
  header[15] = (height >> 8) & 0xFF;
  header[16] = 32; // 32 bits per pixel
  header[17] = 8;  // Attribute bytes (alpha channel depth = 8)

  fwrite(header, 1, 18, f);
  fwrite(pixels, 4, width * height, f);
  fclose(f);
  return true;
}

void SetHeadless() {
  setenv("GLIDE_NG_HEADLESS", "1", 1);
  setenv("GLIDE_NG_BACKEND", "software", 1);
  setenv("GLIDE_NG_LOG_LEVEL", "2", 1); // 2 = WARN (keep basic warning output)
  setenv("GLIDE_NG_LOG_CONSOLE", "1", 1);
}

int main(int argc, char** argv) {
  SetHeadless();

  int width = 640;
  int height = 480;
  int ditherMode = 0;
  bool blendEnabled = true;
  bool stippleEnabled = false;
  bool chromaEnabled = false;
  std::string screenshotFile = "validation_output.tga";

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
      width = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
      height = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--dither") == 0 && i + 1 < argc) {
      ditherMode = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--blend") == 0 && i + 1 < argc) {
      blendEnabled = (std::atoi(argv[++i]) != 0);
    } else if (std::strcmp(argv[i], "--stipple") == 0 && i + 1 < argc) {
      stippleEnabled = (std::atoi(argv[++i]) != 0);
    } else if (std::strcmp(argv[i], "--chroma") == 0 && i + 1 < argc) {
      chromaEnabled = (std::atoi(argv[++i]) != 0);
    } else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
      screenshotFile = argv[++i];
    }
  }

  // Enforce a minimum resolution matching our supported Glide enums
  GrScreenResolution_t resEnum = GR_RESOLUTION_640x480;
  if (width == 320 && height == 200) resEnum = GR_RESOLUTION_320x200;
  else if (width == 320 && height == 240) resEnum = GR_RESOLUTION_320x240;
  else if (width == 800 && height == 600) resEnum = GR_RESOLUTION_800x600;
  else if (width == 1024 && height == 768) resEnum = GR_RESOLUTION_1024x768;
  else if (width == 1600 && height == 1200) resEnum = GR_RESOLUTION_1600x1200;

  grGlideInit();
  GrContext_t ctx = grSstWinOpen(0, resEnum, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  if (!ctx) {
    std::cerr << "Failed to initialize Glide context headlessly!\n";
    return 1;
  }

  // Configure vertex layout with correct Glide 3 enums and offsets
  grVertexLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_Z, 8, GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_RGB, 12, GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_A, 24, GR_PARAM_ENABLE);

  // Configure Pipeline states
  grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
  grDepthBufferFunction(GR_CMP_LESS);
  grDepthMask(FXTRUE);

  if (blendEnabled) {
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
  } else {
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
  }

  if (stippleEnabled) {
    grStippleMode(GR_STIPPLE_PATTERN);
    grStipplePattern(0x55555555);
  } else {
    grStippleMode(GR_STIPPLE_DISABLE);
  }

  grDitherMode(static_cast<GrDitherMode_t>(ditherMode));

  if (chromaEnabled) {
    grChromakeyMode(GR_CHROMAKEY_ENABLE);
    grChromakeyValue(0xFF00FF00); // Green chromakey
  } else {
    grChromakeyMode(GR_CHROMAKEY_DISABLE);
  }

  // Clear buffers
  grBufferClear(0xFF111111, 0xFF, 0xFFFF); // Clear to dark grey

  // 1. Draw solid foreground triangle (Red, Green, Blue) at Z=0.5
  ValidationVert v0 = { 100.0f, 100.0f, 0.5f, 255.0f, 0.0f, 0.0f, 255.0f };
  ValidationVert v1 = { 300.0f, 100.0f, 0.5f, 0.0f, 255.0f, 0.0f, 255.0f };
  ValidationVert v2 = { 200.0f, 300.0f, 0.5f, 0.0f, 0.0f, 255.0f, 255.0f };
  grDrawTriangle(&v0, &v1, &v2);

  // 2. Draw background triangle (Cyan, Magenta, Yellow) at Z=0.7
  ValidationVert v3 = { 150.0f, 150.0f, 0.7f, 0.0f, 255.0f, 255.0f, 255.0f };
  ValidationVert v4 = { 350.0f, 150.0f, 0.7f, 255.0f, 0.0f, 255.0f, 255.0f };
  ValidationVert v5 = { 250.0f, 350.0f, 0.7f, 255.0f, 255.0f, 0.0f, 255.0f };
  grDrawTriangle(&v3, &v4, &v5);

  // 3. Draw transparent overlay quad (White with 50% Alpha) at Z=0.3
  ValidationVert q0 = { 50.0f, 200.0f, 0.3f, 255.0f, 255.0f, 255.0f, 128.0f };
  ValidationVert q1 = { 250.0f, 200.0f, 0.3f, 255.0f, 255.0f, 255.0f, 128.0f };
  ValidationVert q2 = { 250.0f, 400.0f, 0.3f, 255.0f, 255.0f, 255.0f, 128.0f };
  ValidationVert q3 = { 50.0f, 400.0f, 0.3f, 255.0f, 255.0f, 255.0f, 128.0f };
  grDrawTriangle(&q0, &q1, &q2);
  grDrawTriangle(&q0, &q2, &q3);

  // Read back the backbuffer region
  std::vector<uint32_t> fbPixels(width * height);
  grLfbReadRegion(GR_BUFFER_BACKBUFFER, 0, 0, width, height, width * 4, fbPixels.data());

  // Save to TGA
  if (SaveTGA(screenshotFile.c_str(), width, height, fbPixels.data())) {
    std::cout << "Successfully rendered scene and saved validation screenshot to " << screenshotFile << " (" << width << "x" << height << ")\n";
  } else {
    std::cerr << "Failed to save validation screenshot to " << screenshotFile << "\n";
  }

  grSstWinClose(ctx);
  grGlideShutdown();

  return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <cstdint>
#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>

#include <glide.h>
#include <sst1vid.h>
#include <tlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Fallback for Glide 3 which does not define depth constants in headers
#ifndef GR_WDEPTHVALUE_FARTHEST
#define GR_WDEPTHVALUE_FARTHEST 0xFFFF
#endif

#include <DiagnosticInfo.h>



// ======================================================================
// Lightweight CPU 3D Math Structures
// ======================================================================
struct Vector3D {
    float x, y, z;
};

struct TextureVertex {
    Vector3D pos;
    float s, t;
};

struct CubeFace {
    int v0, v1, v2, v3;
    Vector3D normal;
};

Vector3D RotateX(Vector3D v, float angle) {
    float s = sinf(angle);
    float c = cosf(angle);
    return {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
}

Vector3D RotateY(Vector3D v, float angle) {
    float s = sinf(angle);
    float c = cosf(angle);
    return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}

Vector3D RotateZ(Vector3D v, float angle) {
    float s = sinf(angle);
    float c = cosf(angle);
    return {v.x * c - v.y * s, v.x * s + v.y * c, v.z};
}

Vector3D GetNormal(Vector3D p1, Vector3D p2, Vector3D p3) {
    Vector3D u = {p2.x - p1.x, p2.y - p1.y, p2.z - p1.z};
    Vector3D v = {p3.x - p1.x, p3.y - p1.y, p3.z - p1.z};
    Vector3D normal = {
        u.y * v.z - u.z * v.y,
        u.z * v.x - u.x * v.z,
        u.x * v.y - u.y * v.x
    };
    float len = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    if (len > 0.0001f) {
        normal.x /= len;
        normal.y /= len;
        normal.z /= len;
    }
    return normal;
}

// ======================================================================
// Main Application & Window Loop
// ======================================================================
int main(int argc, char* argv[]) {
    // Initialize Glide, parse CLI, print header, and resolve resolution
    auto runConfig = Tools::InitializeAndParse(argc, argv, "Glide 3D Cube", {
        "[ESC]          Exit Tool",
        "[ T ]          Toggle Cube Texture (Neon Grid / glide-ng Chrome & Amber Emblem)",
        "[ F ]          Toggle Bilinear Filtering Mode",
        "[ R ]          Toggle Continuous Rotation"
    });

    // Open Window
    grSstSelect(0);
#if GLIDE_VERSION == 3
    GrContext_t context = grSstWinOpen(0, runConfig.resEnum, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_LOWER_LEFT, 2, 1);
    if (!context) {
        std::cerr << "CRITICAL ERROR: Failed to open Glide 3 window context!" << std::endl;
        grGlideShutdown();
        return EXIT_FAILURE;
    }
#else
    FxBool win_opened = grSstWinOpen(0, runConfig.resEnum, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_LOWER_LEFT, 2, 1);
    if (!win_opened) {
        std::cerr << "CRITICAL ERROR: Failed to open Glide 2 window!" << std::endl;
        grGlideShutdown();
        return EXIT_FAILURE;
    }
#endif

    // Configure the scissor clip window to the full logical resolution size
    grClipWindow(0, 0, runConfig.width, runConfig.height);

    // Setup rendering states
    grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
    grDepthMask(FXTRUE);
    grDepthBufferFunction(GR_CMP_LESS);
    grCullMode(GR_CULL_NEGATIVE);

    // Dynamic Texture Generation
    // 1. Neon Grid Texture (128x128 pixels, 16-bit RGB_565)
    std::vector<unsigned short> neon_grid_buf(128 * 128);
    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {
            // High-contrast Purple and Cyan Checkerboard
            bool cx = (x / 16) % 2 == 0;
            bool cy = (y / 16) % 2 == 0;
            bool grid = (x == 64 || y == 64 || x == 0 || y == 0 || x == 127 || y == 127);
            
            if (grid) {
                neon_grid_buf[y * 128 + x] = 0xFFFF; // Bright white lines
            } else if (cx ^ cy) {
                neon_grid_buf[y * 128 + x] = 0xF81F; // Neon Purple (RGB 31, 0, 31)
            } else {
                neon_grid_buf[y * 128 + x] = 0x07FF; // Electric Cyan (RGB 0, 63, 31)
            }
        }
    }

    // 2. Procedural glide-ng Chrome & Amber Shield Banner Texture (256x128 RGB_565)
    auto pack565 = [](uint8_t r, uint8_t g, uint8_t b) -> unsigned short {
        return static_cast<unsigned short>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
    };
    std::vector<unsigned short> banner_tex_buf(256 * 128, pack565(12, 16, 24));
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 256; ++x) {
            if (((x % 16) == 0) || ((y % 16) == 0)) {
                banner_tex_buf[y * 256 + x] = pack565(36, 26, 16);
            }
        }
    }
    // Slanted brushed-steel badge + amber NG pill centered inside 256x128
    for (int y = 26; y <= 102; ++y) {
        float ty = static_cast<float>(y - 26) / 76.0f;
        int xl = static_cast<int>(lroundf(34.0f - 14.0f * ty));
        int xr = static_cast<int>(lroundf(236.0f - 14.0f * ty));
        bool tb = (y <= 29 || y >= 99);
        for (int x = xl; x <= xr; ++x) {
            bool lr = (x <= xl + 3 || x >= xr - 3);
            banner_tex_buf[y * 256 + x] = (tb || lr)
                ? pack565(212, 224, 236)
                : pack565(static_cast<uint8_t>(42 - 18 * ty),
                          static_cast<uint8_t>(52 - 22 * ty),
                          static_cast<uint8_t>(68 - 28 * ty));
        }
    }
    for (int y = 34; y <= 94; ++y) {
        float ty = static_cast<float>(y - 34) / 60.0f;
        int xl = static_cast<int>(lroundf(172.0f - 11.0f * ty));
        int xr = static_cast<int>(lroundf(226.0f - 11.0f * ty));
        for (int x = xl; x <= xr; ++x) {
            banner_tex_buf[y * 256 + x] = pack565(255, static_cast<uint8_t>(160 - 44 * ty), 28);
        }
    }
    for (int y = 84; y <= 87; ++y) {
        for (int x = 40; x < 152; ++x) {
            banner_tex_buf[y * 256 + x] = pack565(255, 209, 102);
        }
    }
    static const struct { char ch; uint8_t rows[7]; } kCubeGlyphs[] = {
        {'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}},
        {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
        {'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
        {'D', {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}},
        {'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
        {'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    };
    auto drawCubeText = [&](const char* txt, int sx, int sy, int sc, unsigned short col) {
        int cx = sx;
        for (const char* p = txt; *p; ++p) {
            for (const auto& g : kCubeGlyphs) {
                if (g.ch != *p) continue;
                for (int r = 0; r < 7; ++r) {
                    int slant = static_cast<int>(lroundf((6 - r) * 0.35f * sc));
                    for (int c = 0; c < 5; ++c) {
                        if (g.rows[r] & (1u << (4 - c))) {
                            for (int dy = 0; dy < sc; ++dy)
                                for (int dx = 0; dx < sc; ++dx)
                                    banner_tex_buf[(sy + r * sc + dy) * 256 + (cx + c * sc + slant + dx)] = col;
                        }
                    }
                }
            }
            cx += 6 * sc;
        }
    };
    drawCubeText("GLIDE", 42, 46, 4, pack565(248, 250, 252));
    drawCubeText("NG", 172, 46, 4, pack565(17, 24, 39));

    // Define Glide texture info structures
    GrTexInfo neon_tex_info;
#if GLIDE_VERSION == 3
    neon_tex_info.smallLodLog2 = GR_LOD_LOG2_128;
    neon_tex_info.largeLodLog2 = GR_LOD_LOG2_128;
    neon_tex_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
#else
    neon_tex_info.smallLod = GR_LOD_128;
    neon_tex_info.largeLod = GR_LOD_128;
    neon_tex_info.aspectRatio = GR_ASPECT_1x1;
#endif
    neon_tex_info.format = GR_TEXFMT_RGB_565;
    neon_tex_info.data = neon_grid_buf.data();

    GrTexInfo banner_tex_info;
#if GLIDE_VERSION == 3
    banner_tex_info.smallLodLog2 = GR_LOD_LOG2_256;
    banner_tex_info.largeLodLog2 = GR_LOD_LOG2_256;
    banner_tex_info.aspectRatioLog2 = GR_ASPECT_LOG2_2x1;
#else
    banner_tex_info.smallLod = GR_LOD_256;
    banner_tex_info.largeLod = GR_LOD_256;
    banner_tex_info.aspectRatio = GR_ASPECT_2x1;
#endif
    banner_tex_info.format = GR_TEXFMT_RGB_565;
    banner_tex_info.data = banner_tex_buf.data();

    // Download all textures into TMU0 memory
    FxU32 tmu_start_addr = grTexMinAddress(GR_TMU0);
    FxU32 neon_tex_addr = tmu_start_addr;
    // Offset next texture to avoid collision. A 128x128 16-bit texture is 32KB.
    FxU32 banner_tex_addr = neon_tex_addr + 32 * 1024; 

    grTexDownloadMipMap(GR_TMU0, neon_tex_addr, GR_MIPMAPLEVELMASK_BOTH, &neon_tex_info);
    grTexDownloadMipMap(GR_TMU0, banner_tex_addr, GR_MIPMAPLEVELMASK_BOTH, &banner_tex_info);

    // Initial state: Neon Grid texture active
    int active_texture = 0; // 0 = Neon Grid, 1 = 3dfx Banner
    grTexSource(GR_TMU0, neon_tex_addr, GR_MIPMAPLEVELMASK_BOTH, &neon_tex_info);

    // Setup texture combine unit on TMU0
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);

    // Setup color combine: Multiply texture color (OTHER_TEXTURE) by CPU flat-shaded vertex color (LOCAL_ITERATED)
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    // Setup filtering
    bool bilinear = true;
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);

    // 3D Cube Geometry: 8 vertices with positions and UV texture coordinates
    TextureVertex cube_vertices[24] = {
        // Front Face (z = 1.0)
        {{-1.0f, -1.0f,  1.0f},   0.0f, 128.0f},
        {{ 1.0f, -1.0f,  1.0f}, 128.0f, 128.0f},
        {{ 1.0f,  1.0f,  1.0f}, 128.0f,   0.0f},
        {{-1.0f,  1.0f,  1.0f},   0.0f,   0.0f},

        // Back Face (z = -1.0)
        {{ 1.0f, -1.0f, -1.0f},   0.0f, 128.0f},
        {{-1.0f, -1.0f, -1.0f}, 128.0f, 128.0f},
        {{-1.0f,  1.0f, -1.0f}, 128.0f,   0.0f},
        {{ 1.0f,  1.0f, -1.0f},   0.0f,   0.0f},

        // Top Face (y = 1.0)
        {{-1.0f,  1.0f,  1.0f},   0.0f, 128.0f},
        {{ 1.0f,  1.0f,  1.0f}, 128.0f, 128.0f},
        {{ 1.0f,  1.0f, -1.0f}, 128.0f,   0.0f},
        {{-1.0f,  1.0f, -1.0f},   0.0f,   0.0f},

        // Bottom Face (y = -1.0)
        {{-1.0f, -1.0f, -1.0f},   0.0f, 128.0f},
        {{ 1.0f, -1.0f, -1.0f}, 128.0f, 128.0f},
        {{ 1.0f, -1.0f,  1.0f}, 128.0f,   0.0f},
        {{-1.0f, -1.0f,  1.0f},   0.0f,   0.0f},

        // Right Face (x = 1.0)
        {{ 1.0f, -1.0f,  1.0f},   0.0f, 128.0f},
        {{ 1.0f, -1.0f, -1.0f}, 128.0f, 128.0f},
        {{ 1.0f,  1.0f, -1.0f}, 128.0f,   0.0f},
        {{ 1.0f,  1.0f,  1.0f},   0.0f,   0.0f},

        // Left Face (x = -1.0)
        {{-1.0f, -1.0f, -1.0f},   0.0f, 128.0f},
        {{-1.0f, -1.0f,  1.0f}, 128.0f, 128.0f},
        {{-1.0f,  1.0f,  1.0f}, 128.0f,   0.0f},
        {{-1.0f,  1.0f, -1.0f},   0.0f,   0.0f}
    };

    CubeFace cube_faces[6] = {
        {0, 1, 2, 3,     {0.0f, 0.0f, 1.0f}},    // Front
        {4, 5, 6, 7,     {0.0f, 0.0f, -1.0f}},   // Back
        {8, 9, 10, 11,   {0.0f, 1.0f, 0.0f}},    // Top
        {12, 13, 14, 15, {0.0f, -1.0f, 0.0f}},   // Bottom
        {16, 17, 18, 19, {1.0f, 0.0f, 0.0f}},    // Right
        {20, 21, 22, 23, {-1.0f, 0.0f, 0.0f}}    // Left
    };

    // Frame pacing and loop variables
    auto last_fps_report = std::chrono::steady_clock::now();
    int frame_count = 0;
    bool spin = true;

    float angle_x = 30.0f * (float)M_PI / 180.0f;
    float angle_y = 45.0f * (float)M_PI / 180.0f;
    float angle_z = 0.0f;

    int screen_w = runConfig.width;
    int screen_h = runConfig.height;

    // Interactive Loop
    while (tlOkToRender()) {
        while (tlKbHit()) {
            char key = tlGetCH();
            if (key == 0x1b) { // ESC key
                goto exit_loop;
            } else if (key == 'r' || key == 'R') {
                spin = !spin;
            } else if (key == 'f' || key == 'F') {
                // Toggle Bilinear filtering
                bilinear = !bilinear;
                GrTextureFilterMode_t filter = bilinear ? GR_TEXTUREFILTER_BILINEAR : GR_TEXTUREFILTER_POINT_SAMPLED;
                grTexFilterMode(GR_TMU0, filter, filter);
                std::cout << "Bilinear Filtering: " << (bilinear ? "ENABLED\r\n" : "DISABLED\r\n");
            } else if (key == 't' || key == 'T') {
                // Toggle active texture mapping
                active_texture = (active_texture + 1) % 2;
                if (active_texture == 0) {
                    // Load Neon Grid (128x128, 1:1)
                    grTexSource(GR_TMU0, neon_tex_addr, GR_MIPMAPLEVELMASK_BOTH, &neon_tex_info);
                    // Update texture UV scaling in vertex database (0..128)
                    for (int i = 0; i < 24; i++) {
                        cube_vertices[i].s = (cube_vertices[i].s > 0.0f) ? 128.0f : 0.0f;
                        cube_vertices[i].t = (cube_vertices[i].t > 0.0f) ? 128.0f : 0.0f;
                    }
                    std::cout << "Swapped active texture to: Diagnostic Neon Grid\r\n";
                } else {
                    // Load 3dfx Banner (256x128, 2:1)
                    grTexSource(GR_TMU0, banner_tex_addr, GR_MIPMAPLEVELMASK_BOTH, &banner_tex_info);
                    // Update texture UV scaling in vertex database (0..256 for S, 0..128 for T)
                    for (int i = 0; i < 24; i++) {
                        cube_vertices[i].s = (cube_vertices[i].s > 0.0f) ? 256.0f : 0.0f;
                        cube_vertices[i].t = (cube_vertices[i].t > 0.0f) ? 128.0f : 0.0f;
                    }
                    std::cout << "Swapped active texture to: glide-ng Chrome & Amber Emblem\r\n";
                }
            }
        }

        // Rotate the cube
        if (spin) {
            angle_x += 0.01f;
            angle_y += 0.015f;
            angle_z += 0.005f;
        }

        // Clear color and depth buffers
        grBufferClear(0x00000000, 0, GR_WDEPTHVALUE_FARTHEST);

        // Simple static lighting directional vector
        Vector3D light_dir = {0.577f, 0.577f, -0.577f};

        float camera_dist = 5.0f;
        float proj_scale = 650.0f * (screen_h / 480.0f);

        // Render each face of the cube
        for (const auto& face : cube_faces) {
            // 1. Rotate the face normal to check backface culling (CPU side)
            Vector3D rot_normal = RotateZ(RotateY(RotateX(face.normal, angle_x), angle_y), angle_z);
            
            if (rot_normal.z > 0.0f) {
                continue; // Backface culled!
            }

            // Calculate directional flat shading intensity
            float diffuse = rot_normal.x * light_dir.x + rot_normal.y * light_dir.y + rot_normal.z * light_dir.z;
            if (diffuse < 0.0f) diffuse = 0.0f;
            float intensity = 0.3f + 0.7f * diffuse;

            // Shaded color factor (255.0 * intensity)
            float shade = intensity * 255.0f;

            // 2. Transform and project the 4 vertices of this face
            GrVertex face_vertices[4];
            bool clipped = false;

            for (int i = 0; i < 4; i++) {
                int vertex_idx = 0;
                switch(i) {
                    case 0: vertex_idx = face.v0; break;
                    case 1: vertex_idx = face.v1; break;
                    case 2: vertex_idx = face.v2; break;
                    case 3: vertex_idx = face.v3; break;
                }

                TextureVertex raw_v = cube_vertices[vertex_idx];

                // Rotate 3D position
                Vector3D rot_v = RotateZ(RotateY(RotateX(raw_v.pos, angle_x), angle_y), angle_z);
                
                // Camera offset
                float d = rot_v.z + camera_dist;
                if (d <= 0.1f) {
                    clipped = true;
                    break;
                }

                float oow = 1.0f / d;

                // Screen coordinates
                face_vertices[i].x = (screen_w / 2.0f) + (rot_v.x * proj_scale) / d;
                face_vertices[i].y = (screen_h / 2.0f) - (rot_v.y * proj_scale) / d;
                
                // Color (for lighting modulation)
                face_vertices[i].r = shade;
                face_vertices[i].g = shade;
                face_vertices[i].b = shade;
                face_vertices[i].a = 255.0f;
                
                // Perspective-correct texture mapping coordinates (divided by depth!)
                face_vertices[i].oow = oow;
                face_vertices[i].tmuvtx[0].sow = raw_v.s * oow;
                face_vertices[i].tmuvtx[0].tow = raw_v.t * oow;
            }

            if (clipped) continue;

            // 3. Draw the quad as two triangles
            grDrawTriangle(&face_vertices[0], &face_vertices[1], &face_vertices[2]);
            grDrawTriangle(&face_vertices[0], &face_vertices[2], &face_vertices[3]);
        }

        // Present to screen
        grBufferSwap(1);
        frame_count++;

        // Report performance FPS every 5.0 seconds
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - last_fps_report;
        if (elapsed.count() >= 5.0) {
            double fps = frame_count / elapsed.count();
            printf("%d frames in %.3f seconds = %.3f FPS\r\n", frame_count, elapsed.count(), fps);
            fflush(stdout);
            
            frame_count = 0;
            last_fps_report = now;
        }
    }

exit_loop:
    // Clean shutdown
#if GLIDE_VERSION == 3
    grSstWinClose(context);
#else
    grSstWinClose();
#endif

    grGlideShutdown();
    std::cout << "Glide Cube exited cleanly.\r\n";
    return EXIT_SUCCESS;
}

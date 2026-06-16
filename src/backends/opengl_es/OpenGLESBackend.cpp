#include "OpenGLESBackend.h"
#include "core/Logger.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>
#include <SDL2/SDL.h>
#include <GLES3/gl32.h>

// Universal fallback definition for BGRA extension format if header is missing
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

namespace GlideWrapper {

static void checkGLError(const char* label) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        GLIDE_LOG(CRITICAL, "GLES", "GL Error at " << label << ": 0x" << std::hex << err);
    }
}

const char* vsSource = R"(#version 300 es
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inTex;
out vec2 fragTex;
void main() {
    fragTex = inTex;
    gl_Position = vec4(inPos, 0.0, 1.0);
}
)";

const char* fsSource = R"(#version 300 es
precision mediump float;
in vec2 fragTex;
out vec4 outColor;
uniform sampler2D uTex;
void main() {
    outColor = texture(uTex, fragTex);
}
)";

bool OpenGLESBackend::Initialize(const WrapperConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return true;

    GLIDE_LOG(INFO, "GLES", "Initializing OpenGL ES 3.2 staging context...");
    m_config = config;
    m_initialized = true;
    return true;
}

void OpenGLESBackend::Shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized) return;

    GLIDE_LOG(INFO, "GLES", "Shutting down OpenGL ES 3.2 staging context.");
    DetachWindow();
    m_initialized = false;
}

bool OpenGLESBackend::AttachWindow(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized) return false;
    if (m_windowAttached) DetachWindow();

    GLIDE_LOG(INFO, "GLES", "Attaching OpenGL ES window surface (" << width << "x" << height << "), Windowed=" << (windowed ? "Yes" : "No"));

    m_headlessWidth = width;
    m_headlessHeight = height;
    m_headlessMode = m_config.forceNoWindow;

    if (!CreateGLContext(nativeWindowHandle, width, height, windowed)) {
        GLIDE_LOG(CRITICAL, "GLES", "Failed to create OpenGL ES 3.2 context!");
        return false;
    }

    // Dynamic queries of the real hardware renderer!
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));

    std::ostringstream logStream;
    logStream << "\n--- Active OpenGL ES Execution Adapter ---\n"
              << "  Adapter     : " << (renderer ? renderer : "Generic GLES Device") << " (" << (vendor ? vendor : "Unknown") << ")\n"
              << "    Driver    : " << (version ? version : "OpenGL ES 3.2") << "\n"
              << "    Pixel Fmt : RGBA8888 D24S0 nAux 1\n"
              << "------------------------------------------";
    GLIDE_LOG(INFO, "GLES", logStream.str());

    // Milestone 1: Setup Offscreen Framebuffer Object (FBO) for GPU-native rendering
    glGenFramebuffers(1, &m_headlessFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_headlessFBO);

    // Attach our color presentation texture as GL_COLOR_ATTACHMENT0
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_glTexture, 0);

    // Allocate and attach depth renderbuffer for hardware Z-testing support
    glGenRenderbuffers(1, &m_headlessDepthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, m_headlessDepthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_headlessDepthRenderbuffer);

    // Verify FBO completeness
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        GLIDE_LOG(CRITICAL, "GLES", "Headless FBO is incomplete!");
        DestroyGLContext();
        return false;
    }

    // Restore default framebuffer binding
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GLIDE_LOG(INFO, "GLES", "Offscreen Headless FBO & Depth Renderbuffer initialized successfully.");

    // Disable dithering to ensure 100% flat and mathematically consistent color outputs,
    // preventing 1-LSB noise from failing precision regression tests.
    glDisable(GL_DITHER);

    // Milestone 3: Compile shaders and allocate dynamic VBO/VAO
    if (!CreateGeomShaders()) {
        GLIDE_LOG(CRITICAL, "GLES", "Failed to compile GLES geometry shaders!");
        DestroyGLContext();
        return false;
    }
    if (!CreateGeomBuffers()) {
        GLIDE_LOG(CRITICAL, "GLES", "Failed to allocate GLES geometry buffers!");
        DestroyGLContext();
        return false;
    }

    // Allocate software staging memory
    m_headlessPixelMap = std::malloc(width * height * 4);
    if (!m_headlessPixelMap) {
        GLIDE_LOG(CRITICAL, "GLES", "Failed to allocate software pixel map buffer!");
        DestroyGLContext();
        return false;
    }
    std::memset(m_headlessPixelMap, 0xFF, width * height * 4);

    m_headlessDepthBuffer.resize(width * height, 0.0f);
    SetClipWindow(0, 0, width, height);

    m_windowAttached = true;
    return true;
}

void OpenGLESBackend::DetachWindow() {
    if (!m_windowAttached) return;
    GLIDE_LOG(INFO, "GLES", "Detaching OpenGL ES window surface and freeing buffers.");

    DestroyGLContext();

    if (m_headlessPixelMap) {
        std::free(m_headlessPixelMap);
        m_headlessPixelMap = nullptr;
    }

    m_windowAttached = false;
}

bool OpenGLESBackend::SwapBuffers() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return false;

    // Enforce retro game frame pacing
    if (m_config.maxFps > 0.0f) {
        auto delay = std::chrono::microseconds(static_cast<int>(1000000.0f / m_config.maxFps));
        std::this_thread::sleep_for(delay);
    }

    if (!m_headlessMode && m_sdlWindow && m_glContext && m_headlessPixelMap) {
        // 1. Flush GPU drawing and composite with CPU text in host memory
        ReadLFB(0, 0, m_headlessWidth, m_headlessHeight, m_headlessWidth * 4, m_headlessPixelMap);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Upload our software pixel map to GLES texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_glTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_headlessWidth, m_headlessHeight, GL_BGRA_EXT, GL_UNSIGNED_BYTE, m_headlessPixelMap);
        checkGLError("SwapBuffers glTexSubImage2D");

        glUseProgram(m_glProgram);
        glBindVertexArray(m_glVAO);
        
        // Disable depth testing/writing so the full-screen quad draws cleanly over the window
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE); // Guarantee presentation quad is never culled by active game culling state!
        glDisable(GL_SCISSOR_TEST); // Guarantee presentation quad is never clipped by active game scissor state!
        
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        checkGLError("SwapBuffers glDrawArrays");

        SDL_GL_SwapWindow(reinterpret_cast<SDL_Window*>(m_sdlWindow));
    }

    return true;
}

void OpenGLESBackend::SstIdle() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized && m_windowAttached) {
        // Blocks the host CPU until all GLES commands and drawing dispatches 
        // have been fully processed by the hardware GPU!
        glFinish();
    }
}

void OpenGLESBackend::ClearBuffer(uint32_t color, uint32_t alpha, float z, uint32_t clearMask) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return;

    // 1. Clear CPU-side framebuffer for progressive hybrid compatibility
    SoftwareBackendBase::ClearBuffer(color, alpha, z, clearMask);

    // 1.5. Force CPU framebuffer alpha to 0 so background is not cached as hybrid geometry/text!
    if (clearMask & 1) {
        auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
        if (pixels) {
            for (uint32_t i = 0; i < m_headlessWidth * m_headlessHeight; ++i) {
                pixels[i] &= 0x00FFFFFF;
            }
        }
    }

    // Bind offscreen FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_headlessFBO);

    GLbitfield mask = 0;
    GLboolean oldDepthMask = GL_TRUE;

    if (clearMask & 0x1) { // Color Clear
        float a = ((alpha > 0) ? alpha : 255) / 255.0f;
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8)  & 0xFF) / 255.0f;
        float b = (color         & 0xFF) / 255.0f;

        if (m_pixelFormatOverride == 1) { // ABGR format override
            std::swap(r, b);
        }

        // Clear with native RGB color (driver BGRA format handles layout mapping)
        glClearColor(r, g, b, a);
        mask |= GL_COLOR_BUFFER_BIT;
    }

    if (clearMask & 0x2) { // Depth Clear
        glGetBooleanv(GL_DEPTH_WRITEMASK, &oldDepthMask);
        glDepthMask(GL_TRUE); // Force depth write enabled so glClear actually clears the depth buffer!
        glClearDepthf(z);
        mask |= GL_DEPTH_BUFFER_BIT;
    }

    if (mask != 0) {
        // In OpenGL, glClear is affected by the active scissor box.
        // But Glide grBufferClear always clears the entire screen.
        // We must temporarily disable the scissor test to force a full-screen clear!
        GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
        if (scissorEnabled) {
            glDisable(GL_SCISSOR_TEST);
        }

        glClear(mask);

        if (scissorEnabled) {
            glEnable(GL_SCISSOR_TEST);
        }
    }

    if (clearMask & 0x2) {
        glDepthMask(oldDepthMask); // Restore original depth write mask
    }

    // Restore default framebuffer binding
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool OpenGLESBackend::ReadLFB(uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride, void* dstData) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return false;

    // --- STEP 1: Cache CPU-rendered text/geometry pixels ---
    std::vector<uint32_t> cpuRenderingCache(m_headlessWidth * m_headlessHeight, 0);
    const auto* cpuPixels = reinterpret_cast<const uint32_t*>(m_headlessPixelMap);
    bool hasCpuRendering = false;

    if (cpuPixels) {
        for (size_t i = 0; i < cpuRenderingCache.size(); ++i) {
            // If the pixel is not transparent background (alpha != 0)
            if ((cpuPixels[i] >> 24) != 0) {
                cpuRenderingCache[i] = cpuPixels[i];
                hasCpuRendering = true;
            }
        }
    }

    // --- STEP 2: GPU Readback Flush ---
    // Bind our offscreen FBO and read pixels directly into m_headlessPixelMap
    glBindFramebuffer(GL_FRAMEBUFFER, m_headlessFBO);
    
    // Read pixels as GL_RGBA (which contains our R/B swapped BGRA-ordered pixels)
    // GLES glReadPixels is synchronous and guarantees the GPU finishes all rendering before returning!
    glReadPixels(0, 0, m_headlessWidth, m_headlessHeight, GL_RGBA, GL_UNSIGNED_BYTE, m_headlessPixelMap);
    checkGLError("ReadLFB glReadPixels");

    // Convert RGBA from GPU glReadPixels to BGRA expected by host staging buffer
    if (m_headlessPixelMap) {
        auto* dest = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
        for (uint32_t i = 0; i < m_headlessWidth * m_headlessHeight; ++i) {
            uint32_t rgba = dest[i];
            uint32_t r = rgba & 0xFF;
            uint32_t g = (rgba >> 8) & 0xFF;
            uint32_t b = (rgba >> 16) & 0xFF;
            uint32_t a = (rgba >> 24) & 0xFF;
            dest[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    // --- STEP 3: Overlay CPU-rendered text/geometry back ---
    if (hasCpuRendering && m_headlessPixelMap) {
        auto* dest = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
        for (size_t i = 0; i < cpuRenderingCache.size(); ++i) {
            if (cpuRenderingCache[i] != 0) {
                dest[i] = cpuRenderingCache[i];
            }
        }
    }

    // Restore default framebuffer binding
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- STEP 4: Convert and copy to client ---
    bool result = SoftwareBackendBase::ReadLFB(srcX, srcY, srcWidth, srcHeight, dstStride, dstData);

    // --- STEP 5: Force alpha to 0 in staging buffer ---
    // This prevents GPU-rendered pixels (which have alpha=255 from GPU clear/draw)
    // from being incorrectly detected as CPU-rendered text/geometry in the next ReadLFB frame!
    if (m_headlessPixelMap) {
        auto* pixels = reinterpret_cast<uint32_t*>(m_headlessPixelMap);
        for (uint32_t i = 0; i < m_headlessWidth * m_headlessHeight; ++i) {
            pixels[i] &= 0x00FFFFFF;
        }
    }

    return result;
}



// Context Creation & Shaders Compiler Helpers
bool OpenGLESBackend::CreateGLContext(void* nativeWindowHandle, uint32_t width, uint32_t height, bool windowed) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
        GLIDE_LOG(CRITICAL, "GLES", "SDL_InitSubSystem failed: " << SDL_GetError());
        return false;
    }

    // Configure OpenGL ES 3.2 attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    uint32_t windowFlags = SDL_WINDOW_OPENGL;
    if (m_headlessMode) {
        windowFlags |= SDL_WINDOW_HIDDEN;
    } else {
        windowFlags |= SDL_WINDOW_SHOWN;
    }

    auto* win = SDL_CreateWindow(
        "3dfx glide-ng Presentation Console (OpenGL ES 3.2)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, windowFlags
    );

    if (!win) {
        GLIDE_LOG(CRITICAL, "GLES", "SDL_CreateWindow failed: " << SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    auto* ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        GLIDE_LOG(CRITICAL, "GLES", "SDL_GL_CreateContext failed: " << SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    SDL_GL_MakeCurrent(win, ctx);

    // Setup full-screen quad presentation shaders
    auto compileShader = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint status;
        glGetShaderiv(s, GL_COMPILE_STATUS, &status);
        if (!status) {
            char log[512];
            glGetShaderInfoLog(s, 512, nullptr, log);
            GLIDE_LOG(CRITICAL, "GLES", "Shader compilation failure: " << log);
        }
        return s;
    };

    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSource);

    m_glProgram = glCreateProgram();
    glAttachShader(m_glProgram, vs);
    glAttachShader(m_glProgram, fs);
    glLinkProgram(m_glProgram);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linkStatus;
    glGetProgramiv(m_glProgram, GL_LINK_STATUS, &linkStatus);
    if (!linkStatus) {
        char log[512];
        glGetProgramInfoLog(m_glProgram, 512, nullptr, log);
        GLIDE_LOG(CRITICAL, "GLES", "Program linking failure: " << log);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    // Set up presentation VBO/VAO quad
    // Quad vertices (Positions: X,Y and TexCoords: U,V)
    float quadVertices[] = {
        -1.0f,  1.0f,  0.0f, 0.0f, // Top-Left
        -1.0f, -1.0f,  0.0f, 1.0f, // Bottom-Left
         1.0f, -1.0f,  1.0f, 1.0f, // Bottom-Right
         1.0f,  1.0f,  1.0f, 0.0f  // Top-Right
    };

    glGenVertexArrays(1, &m_glVAO);
    glGenBuffers(1, &m_glVBO);

    glBindVertexArray(m_glVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_glVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0); // Pos
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1); // Tex
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    // Create the streaming texture
    glGenTextures(1, &m_glTexture);
    glBindTexture(GL_TEXTURE_2D, m_glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    m_sdlWindow = win;
    m_glContext = ctx;

    return true;
}

void OpenGLESBackend::DestroyGLContext() {
    if (m_glTexture) { glDeleteTextures(1, &m_glTexture); m_glTexture = 0; }
    if (m_glVBO)     { glDeleteBuffers(1, &m_glVBO); m_glVBO = 0; }
    if (m_glVAO)     { glDeleteVertexArrays(1, &m_glVAO); m_glVAO = 0; }
    if (m_glProgram) { glDeleteProgram(m_glProgram); m_glProgram = 0; }
    if (m_headlessDepthRenderbuffer) { glDeleteRenderbuffers(1, &m_headlessDepthRenderbuffer); m_headlessDepthRenderbuffer = 0; }
    if (m_headlessFBO)               { glDeleteFramebuffers(1, &m_headlessFBO); m_headlessFBO = 0; }
    if (m_geomVBO)     { glDeleteBuffers(1, &m_geomVBO); m_geomVBO = 0; }
    if (m_geomVAO)     { glDeleteVertexArrays(1, &m_geomVAO); m_geomVAO = 0; }
    if (m_geomProgram) { glDeleteProgram(m_geomProgram); m_geomProgram = 0; }

    if (m_glContext) {
        SDL_GL_DeleteContext(reinterpret_cast<SDL_GLContext>(m_glContext));
        m_glContext = nullptr;
    }
    if (m_sdlWindow) {
        SDL_DestroyWindow(reinterpret_cast<SDL_Window*>(m_sdlWindow));
        m_sdlWindow = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static const char* geomVsSource = R"(#version 300 es
layout(location = 0) in vec4 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec4 inTex;

out vec4 fragColor;

uniform vec2 uViewportSize;
uniform int uDepthClipOverride;
uniform int uDepthBufferMode; // 0=None, 1=Z, 2=W
uniform float uDepthBias;

void main() {
    // Convert Glide window-space coordinates to GLES NDC (Y-up)
    float ndcX = (inPos.x / (uViewportSize.x / 2.0)) - 1.0;
    float ndcY = (inPos.y / (uViewportSize.y / 2.0)) - 1.0;
    
    float rawDepth = inPos.z;
    if (uDepthBufferMode == 2) {
        rawDepth = 1.0 - inPos.w; // W-buffer mapping: 1.0 - oow
    }

    rawDepth += uDepthBias;

    float ndcZ;
    if (uDepthClipOverride == 1) {
        ndcZ = 0.0; // Force to middle of NDC clip volume if depth testing is disabled (prevents stack garbage clipping)
    } else {
        ndcZ = rawDepth * 2.0 - 1.0;
    }

    gl_Position = vec4(ndcX, ndcY, ndcZ, 1.0);
    gl_PointSize = 1.0; // Canonical Glide point size (1px)
    fragColor = inColor;
}
)";

static const char* geomFsSource = R"(#version 300 es
precision mediump float;

in vec4 fragColor;
out vec4 outColor;

uniform vec4 uConstantColor;
uniform int uUseConstantColor;
uniform int uUseConstantAlpha;
uniform int uAlphaTestOp;
uniform float uAlphaTestRef;

void main() {
    vec4 col;
    col.rgb = (uUseConstantColor == 1) ? uConstantColor.rgb : fragColor.rgb;
    col.a   = (uUseConstantAlpha == 1) ? uConstantColor.a   : fragColor.a;

    // Emulate legacy 3dfx Glide alpha testing (7 = GR_CMP_ALWAYS / disabled)
    if (uAlphaTestOp != 7) {
        float alpha = col.a;
        float refVal = uAlphaTestRef;
        bool passed = true;

        if (uAlphaTestOp == 0) passed = false; // GR_CMP_NEVER
        else if (uAlphaTestOp == 1) passed = (alpha < refVal); // GR_CMP_LESS
        else if (uAlphaTestOp == 2) passed = (alpha == refVal); // GR_CMP_EQUAL
        else if (uAlphaTestOp == 3) passed = (alpha <= refVal); // GR_CMP_LEQUAL
        else if (uAlphaTestOp == 4) passed = (alpha > refVal); // GR_CMP_GREATER
        else if (uAlphaTestOp == 5) passed = (alpha != refVal); // GR_CMP_NOTEQUAL
        else if (uAlphaTestOp == 6) passed = (alpha >= refVal); // GR_CMP_GEQUAL
        
        if (!passed) {
            discard;
        }
    }

    // Output native RGB color (driver BGRA format handles layout mapping)
    outColor = col;
}
)";

bool OpenGLESBackend::CreateGeomShaders() {
    auto compileShader = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint status;
        glGetShaderiv(s, GL_COMPILE_STATUS, &status);
        if (!status) {
            char log[512];
            glGetShaderInfoLog(s, 512, nullptr, log);
            GLIDE_LOG(CRITICAL, "GLES", "Geometry Shader compilation failure: " << log);
        }
        return s;
    };

    GLuint vs = compileShader(GL_VERTEX_SHADER, geomVsSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, geomFsSource);

    m_geomProgram = glCreateProgram();
    glAttachShader(m_geomProgram, vs);
    glAttachShader(m_geomProgram, fs);
    glLinkProgram(m_geomProgram);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linkStatus;
    glGetProgramiv(m_geomProgram, GL_LINK_STATUS, &linkStatus);
    if (!linkStatus) {
        char log[512];
        glGetProgramInfoLog(m_geomProgram, 512, nullptr, log);
        GLIDE_LOG(CRITICAL, "GLES", "Geometry Program linking failure: " << log);
        return false;
    }

    checkGLError("CreateGeomShaders");
    return true;
}

bool OpenGLESBackend::CreateGeomBuffers() {
    glGenVertexArrays(1, &m_geomVAO);
    glGenBuffers(1, &m_geomVBO);

    glBindVertexArray(m_geomVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_geomVBO);
    
    // Allocate 1MB dynamic ring buffer
    glBufferData(GL_ARRAY_BUFFER, m_geomVBOSize, nullptr, GL_DYNAMIC_DRAW);

    // Map ModernVertex attributes:
    // Location 0: inPos (pos) -> vec4 (16 bytes)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(ModernVertex), (void*)offsetof(ModernVertex, pos));

    // Location 1: inColor (color) -> vec4 (16 bytes)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ModernVertex), (void*)offsetof(ModernVertex, color));

    // Location 2: inTex (tex) -> vec4 (16 bytes)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(ModernVertex), (void*)offsetof(ModernVertex, tex));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    checkGLError("CreateGeomBuffers");
    return true;
}

void OpenGLESBackend::DrawPoint(const ModernVertex& pt) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return;

    GLIDE_LOG(DEBUG, "GLES", "DrawPoint GPU draw. Pos: (" << pt.pos[0] << ", " << pt.pos[1] << ", " << pt.pos[2] << ")");

    // 1. Upload vertex to dynamic VBO ring buffer
    ModernVertex pt_final = pt;
    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinate
        pt_final.pos[1] = (float)m_headlessHeight - pt.pos[1];
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_geomVBO);
    glBufferSubData(GL_ARRAY_BUFFER, m_geomVBOOffset, sizeof(ModernVertex), &pt_final);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // 2. Bind offscreen FBO and set state
    glBindFramebuffer(GL_FRAMEBUFFER, m_headlessFBO);
    glViewport(0, 0, m_headlessWidth, m_headlessHeight);
    SyncDepthState();
    ApplyBlendingGLES();
    ApplyScissorGLES();

    // 3. Bind shader program and upload uniforms
    glUseProgram(m_geomProgram);

    // Viewport uniform
    glUniform2f(glGetUniformLocation(m_geomProgram, "uViewportSize"), (float)m_headlessWidth, (float)m_headlessHeight);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uDepthClipOverride"), (m_depthMode == 0) ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uDepthBufferMode"), m_depthMode);

    // Combiner color state uniforms
    bool useConstColor = ((m_colorCombinerFactor & 0xFFFF) == 1) || (m_colorCombinerFactor == 1);
    bool useConstAlpha = ((m_alphaCombinerFactor & 0xFFFF) == 1) || (m_alphaCombinerFactor == 1);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uUseConstantColor"), useConstColor ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uUseConstantAlpha"), useConstAlpha ? 1 : 0);

    float cr = ((m_constantColor >> 16) & 0xFF) / 255.0f;
    float cg = ((m_constantColor >> 8)  & 0xFF) / 255.0f;
    float cb = (m_constantColor         & 0xFF) / 255.0f;
    float ca = ((m_constantColor >> 24) & 0xFF) / 255.0f;
    if (m_pixelFormatOverride == 1) {
        std::swap(cr, cb);
    }
    if (ca < 0.5f) ca = 1.0f; // Force opaque if uninitialized
    glUniform4f(glGetUniformLocation(m_geomProgram, "uConstantColor"), cr, cg, cb, ca);

    // Alpha testing uniforms
    glUniform1i(glGetUniformLocation(m_geomProgram, "uAlphaTestOp"), (int)m_alphaTestOp);
    glUniform1f(glGetUniformLocation(m_geomProgram, "uAlphaTestRef"), (float)m_alphaTestRefVal / 255.0f);
    glUniform1f(glGetUniformLocation(m_geomProgram, "uDepthBias"), static_cast<float>(m_depthBiasLevel) / 65535.0f);

    // 4. Bind VAO and issue GLES point draw call
    glBindVertexArray(m_geomVAO);
    uint32_t firstVertex = m_geomVBOOffset / sizeof(ModernVertex);
    glDrawArrays(GL_POINTS, firstVertex, 1);

    // 5. Cleanup state bindings
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 6. Advance ring buffer offset
    m_geomVBOOffset += sizeof(ModernVertex);
    if (m_geomVBOOffset + sizeof(ModernVertex) > m_geomVBOSize) {
        GLIDE_LOG(WARN, "GLES", "Dynamic Geometry VBO ring buffer wrap-around!");
        m_geomVBOOffset = 0;
    }
    checkGLError("DrawPoint");
}

void OpenGLESBackend::DrawLine(const ModernVertex& v0, const ModernVertex& v1) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return;

    GLIDE_LOG(DEBUG, "GLES", "DrawLine GPU draw. V0: (" << v0.pos[0] << ", " << v0.pos[1] << "), V1: (" << v1.pos[0] << ", " << v1.pos[1] << ")");
    GLIDE_LOG(DEBUG, "GLES", "DrawLine Colors. V0: (" << v0.color[0] << "," << v0.color[1] << "," << v0.color[2] << "," << v0.color[3] << "), V1: (" << v1.color[0] << "," << v1.color[1] << "," << v1.color[2] << "," << v1.color[3] << ")");

    // 1. Upload both vertices to dynamic VBO ring buffer
    ModernVertex v0_final = v0;
    ModernVertex v1_final = v1;
    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
        v0_final.pos[1] = (float)m_headlessHeight - v0.pos[1];
        v1_final.pos[1] = (float)m_headlessHeight - v1.pos[1];
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_geomVBO);
    glBufferSubData(GL_ARRAY_BUFFER, m_geomVBOOffset, sizeof(ModernVertex), &v0_final);
    glBufferSubData(GL_ARRAY_BUFFER, m_geomVBOOffset + sizeof(ModernVertex), sizeof(ModernVertex), &v1_final);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // 2. Bind offscreen FBO and set state
    glBindFramebuffer(GL_FRAMEBUFFER, m_headlessFBO);
    glViewport(0, 0, m_headlessWidth, m_headlessHeight);
    SyncDepthState();
    ApplyBlendingGLES();
    ApplyScissorGLES();

    // 3. Bind shader program and upload uniforms
    glUseProgram(m_geomProgram);

    // Viewport uniform
    glUniform2f(glGetUniformLocation(m_geomProgram, "uViewportSize"), (float)m_headlessWidth, (float)m_headlessHeight);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uDepthClipOverride"), (m_depthMode == 0) ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uDepthBufferMode"), m_depthMode);

    // Combiner color state uniforms
    bool useConstColor = ((m_colorCombinerFactor & 0xFFFF) == 1) || (m_colorCombinerFactor == 1);
    bool useConstAlpha = ((m_alphaCombinerFactor & 0xFFFF) == 1) || (m_alphaCombinerFactor == 1);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uUseConstantColor"), useConstColor ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uUseConstantAlpha"), useConstAlpha ? 1 : 0);

    float cr = ((m_constantColor >> 16) & 0xFF) / 255.0f;
    float cg = ((m_constantColor >> 8)  & 0xFF) / 255.0f;
    float cb = (m_constantColor         & 0xFF) / 255.0f;
    float ca = ((m_constantColor >> 24) & 0xFF) / 255.0f;
    if (m_pixelFormatOverride == 1) {
        std::swap(cr, cb);
    }
    if (ca < 0.5f) ca = 1.0f; // Force opaque if uninitialized
    glUniform4f(glGetUniformLocation(m_geomProgram, "uConstantColor"), cr, cg, cb, ca);

    // Alpha testing uniforms
    glUniform1i(glGetUniformLocation(m_geomProgram, "uAlphaTestOp"), (int)m_alphaTestOp);
    glUniform1f(glGetUniformLocation(m_geomProgram, "uAlphaTestRef"), (float)m_alphaTestRefVal / 255.0f);
    glUniform1f(glGetUniformLocation(m_geomProgram, "uDepthBias"), static_cast<float>(m_depthBiasLevel) / 65535.0f);

    // 4. Bind VAO and issue GLES line draw call
    glBindVertexArray(m_geomVAO);
    uint32_t firstVertex = m_geomVBOOffset / sizeof(ModernVertex);
    glDrawArrays(GL_LINES, firstVertex, 2);

    // 5. Cleanup state bindings
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 6. Advance ring buffer offset
    m_geomVBOOffset += 2 * sizeof(ModernVertex);
    if (m_geomVBOOffset + 2 * sizeof(ModernVertex) > m_geomVBOSize) {
        GLIDE_LOG(WARN, "GLES", "Dynamic Geometry VBO ring buffer wrap-around!");
        m_geomVBOOffset = 0;
    }

    checkGLError("DrawLine");
}

void OpenGLESBackend::DrawTriangle(const ModernVertex& a, const ModernVertex& b, const ModernVertex& c) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return;

    // TODO(Phase4): Remove this temporary CPU software rasterizer fallback once
    // GPU-native texture mapping is fully implemented in Phase 4.
    // This fallback ensures that textured console text characters (drawn as textured quads)
    // are correctly rasterized on the CPU and composited, while flat/Gouraud shaded
    // triangles (like test04) continue to run 100% natively on the GPU.
    if (m_boundTexAddress[0] != 0) {
        SoftwareBackendBase::DrawTriangle(a, b, c);
        return;
    }

    GLIDE_LOG(DEBUG, "GLES", "DrawTriangle GPU draw. A: (" << a.pos[0] << ", " << a.pos[1] << "), B: (" << b.pos[0] << ", " << b.pos[1] << "), C: (" << c.pos[0] << ", " << c.pos[1] << ")");
    GLIDE_LOG(DEBUG, "GLES", "DrawTriangle Colors. A: (" << a.color[0] << "," << a.color[1] << "," << a.color[2] << "," << a.color[3] << "), B: (" << b.color[0] << "," << b.color[1] << "," << b.color[2] << "," << b.color[3] << "), C: (" << c.color[0] << "," << c.color[1] << "," << c.color[2] << "," << c.color[3] << ")");

    // 1. Upload all three vertices directly to VBO
    ModernVertex vertices[3] = { a, b, c };
    if (m_sstOrigin == 1) { // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
        vertices[0].pos[1] = (float)m_headlessHeight - a.pos[1];
        vertices[1].pos[1] = (float)m_headlessHeight - b.pos[1];
        vertices[2].pos[1] = (float)m_headlessHeight - c.pos[1];
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_geomVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // 2. Bind offscreen FBO and set state
    glBindFramebuffer(GL_FRAMEBUFFER, m_headlessFBO);
    glViewport(0, 0, m_headlessWidth, m_headlessHeight);
    SyncDepthState();
    ApplyBlendingGLES();
    ApplyScissorGLES();
    SyncCullStateGLES();

    // 3. Bind shader program and upload uniforms
    glUseProgram(m_geomProgram);

    // Viewport uniform
    glUniform2f(glGetUniformLocation(m_geomProgram, "uViewportSize"), (float)m_headlessWidth, (float)m_headlessHeight);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uDepthClipOverride"), (m_depthMode == 0) ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uDepthBufferMode"), m_depthMode);

    // Combiner color state uniforms
    bool useConstColor = ((m_colorCombinerFactor & 0xFFFF) == 1) || (m_colorCombinerFactor == 1);
    bool useConstAlpha = ((m_alphaCombinerFactor & 0xFFFF) == 1) || (m_alphaCombinerFactor == 1);
    GLIDE_LOG(DEBUG, "GLES", "Combiner State. useConstColor=" << useConstColor << ", useConstAlpha=" << useConstAlpha << ", constColor=0x" << std::hex << m_constantColor << std::dec);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uUseConstantColor"), useConstColor ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uUseConstantAlpha"), useConstAlpha ? 1 : 0);

    float cr = ((m_constantColor >> 16) & 0xFF) / 255.0f;
    float cg = ((m_constantColor >> 8)  & 0xFF) / 255.0f;
    float cb = (m_constantColor         & 0xFF) / 255.0f;
    float ca = ((m_constantColor >> 24) & 0xFF) / 255.0f;
    if (m_pixelFormatOverride == 1) {
        std::swap(cr, cb);
    }
    if (ca < 0.5f) ca = 1.0f; // Force opaque if uninitialized
    glUniform4f(glGetUniformLocation(m_geomProgram, "uConstantColor"), cr, cg, cb, ca);

    // Alpha testing uniforms
    GLint locOp = glGetUniformLocation(m_geomProgram, "uAlphaTestOp");
    GLint locRef = glGetUniformLocation(m_geomProgram, "uAlphaTestRef");
    GLIDE_LOG(DEBUG, "GLES", "AlphaTest Uniforms. LocOp=" << locOp << " (val=" << (int)m_alphaTestOp << "), LocRef=" << locRef << " (val=" << (float)m_alphaTestRefVal / 255.0f << ")");
    glUniform1i(locOp, (int)m_alphaTestOp);
    glUniform1f(locRef, (float)m_alphaTestRefVal / 255.0f);
    glUniform1f(glGetUniformLocation(m_geomProgram, "uDepthBias"), static_cast<float>(m_depthBiasLevel) / 65535.0f);

    // 4. Bind VAO and issue GLES triangle draw call
    glBindVertexArray(m_geomVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3); // Always draw from index 0!

    // 5. Cleanup state bindings
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    checkGLError("DrawTriangle");
}

void OpenGLESBackend::SyncDepthState() {
    if (m_depthMode != 0) { // GR_DEPTHBUFFER_DISABLE is 0
        glEnable(GL_DEPTH_TEST);
        
        GLenum glDepthFuncVal = GL_LESS;
        switch (m_depthCompareOp) {
            case 0: glDepthFuncVal = GL_NEVER; break;
            case 1: glDepthFuncVal = GL_LESS; break;
            case 2: glDepthFuncVal = GL_EQUAL; break;
            case 3: glDepthFuncVal = GL_LEQUAL; break;
            case 4: glDepthFuncVal = GL_GREATER; break;
            case 5: glDepthFuncVal = GL_NOTEQUAL; break;
            case 6: glDepthFuncVal = GL_GEQUAL; break;
            case 7: glDepthFuncVal = GL_ALWAYS; break;
            default: break;
        }
        glDepthFunc(glDepthFuncVal);
        glDepthMask(m_depthMask ? GL_TRUE : GL_FALSE);
    } else {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE); // Explicitly disable depth writes when depth test is disabled!
    }
}

static GLenum MapBlendFactorGLES(uint32_t factor) {
    switch (factor) {
        case 0: return GL_ZERO;                  // GR_BLEND_ZERO
        case 1: return GL_SRC_ALPHA;             // GR_BLEND_SRC_ALPHA
        case 2: return GL_SRC_COLOR;             // GR_BLEND_SRC_COLOR
        case 3: return GL_DST_ALPHA;             // GR_BLEND_DST_ALPHA
        case 4: return GL_ONE;                   // GR_BLEND_ONE
        case 5: return GL_ONE_MINUS_SRC_ALPHA;   // GR_BLEND_ONE_MINUS_SRC_ALPHA
        case 6: return GL_ONE_MINUS_SRC_COLOR;   // GR_BLEND_ONE_MINUS_SRC_COLOR
        case 7: return GL_ONE_MINUS_DST_ALPHA;   // GR_BLEND_ONE_MINUS_DST_ALPHA
        case 15: return GL_SRC_ALPHA_SATURATE;  // GR_BLEND_ALPHA_SATURATE
        default: return GL_ONE;
    }
}

void OpenGLESBackend::SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor, uint32_t alphaSrcFactor, uint32_t alphaDstFactor) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    SoftwareBackendBase::SetBlendState(rgbSrcFactor, rgbDstFactor, alphaSrcFactor, alphaDstFactor);
}

void OpenGLESBackend::ApplyBlendingGLES() {
    if (m_rgbSrcBlend == 4 && m_rgbDstBlend == 0 && m_alphaSrcBlend == 4 && m_alphaDstBlend == 0) { // GR_BLEND_ONE & GR_BLEND_ZERO (disabled)
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(
            MapBlendFactorGLES(m_rgbSrcBlend),
            MapBlendFactorGLES(m_rgbDstBlend),
            MapBlendFactorGLES(m_alphaSrcBlend),
            MapBlendFactorGLES(m_alphaDstBlend)
        );
    }
}

void OpenGLESBackend::ApplyScissorGLES() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    glEnable(GL_SCISSOR_TEST);
    
    // Since the vertex shader does not flip Y, the GPU rendering is already vertically
    // inverted (Glide top maps to OpenGL bottom). To match this inverted rendering, the
    // scissor box must also be kept in its raw Glide coordinates (no Y-flipping).
    uint32_t glScissorY = m_clipMinY;
    uint32_t glScissorW = (m_clipMaxX > m_clipMinX) ? (m_clipMaxX - m_clipMinX) : 0;
    uint32_t glScissorH = (m_clipMaxY > m_clipMinY) ? (m_clipMaxY - m_clipMinY) : 0;
    
    glScissor(m_clipMinX, glScissorY, glScissorW, glScissorH);
}

void OpenGLESBackend::SyncCullStateGLES() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_cullMode == 0) { // GR_CULL_DISABLE
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        if (m_sstOrigin == 1) {
            glFrontFace(GL_CW);  // Two Y-inversions (C++ flip + shader projection) cancel out; winding is native CW!
        } else {
            glFrontFace(GL_CCW); // Single Y-inversion from GLES Y-up shader projection; winding is inverted to CCW!
        }
        if (m_cullMode == 1) { // GR_CULL_NEGATIVE (cull negative winding / CCW in Glide)
            glCullFace(GL_BACK); // Culls CCW under GL_CW, or CW under GL_CCW. Maps perfectly!
        } else if (m_cullMode == 2) { // GR_CULL_POSITIVE (cull positive winding / CW in Glide)
            glCullFace(GL_FRONT); // Culls CW under GL_CW, or CCW under GL_CCW. Maps perfectly!
        }
    }
}

} // namespace GlideWrapper

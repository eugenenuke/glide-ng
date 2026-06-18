#include "OpenGLESBackend.h"
#include "core/Logger.h"
#include "core/TextureManager.h"
#include "core/Telemetry.h"
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
    outColor = texture(uTex, fragTex).bgra;
}
)";

bool OpenGLESBackend::Initialize(const WrapperConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return true;

    GLIDE_LOG(INFO, "GLES", "Initializing OpenGL ES 3.2 staging context...");
    m_config = config;
    m_msaaSamples = config.msaaSamples;
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
    GLIDE_LOG(DEBUG, "GLES", logStream.str());

    // Milestone 1: Setup Offscreen Framebuffer Objects (FBOs) for GPU-native rendering (Double-Buffered)
    glGenFramebuffers(2, m_headlessFBOs);
    glGenRenderbuffers(2, m_headlessDepthRenderbuffers);

    for (int i = 0; i < 2; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_headlessFBOs[i]);

        // Attach our color offscreen GPU rendering texture as GL_COLOR_ATTACHMENT0
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fboTextures[i], 0);

        // Allocate and attach depth renderbuffer for hardware Z-testing support
        glBindRenderbuffer(GL_RENDERBUFFER, m_headlessDepthRenderbuffers[i]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_headlessDepthRenderbuffers[i]);

        // Verify FBO completeness
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            GLIDE_LOG(CRITICAL, "GLES", "Headless FBO " << i << " is incomplete!");
            DestroyGLContext();
            return false;
        }
    }

    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    // Restore default framebuffer binding
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GLIDE_LOG(INFO, "GLES", "Double-buffered offscreen FBOs & Depth Renderbuffers initialized successfully.");

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
    AllocateCpuBuffers(width, height);
    m_swizzleBuffer.resize(width * height);

    m_headlessDepthBuffer.resize(width * height, 0.0f);
    SetClipWindow(0, 0, width, height);

    m_windowAttached = true;
    return true;
}

void OpenGLESBackend::DetachWindow() {
    if (!m_windowAttached) return;
    GLIDE_LOG(INFO, "GLES", "Detaching OpenGL ES window surface and freeing buffers.");

    DestroyGLContext();

    FreeCpuBuffers();

    m_windowAttached = false;
}

bool OpenGLESBackend::SwapBuffers() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return false;

    // Proactively flush any pending CPU LFB writes to the GPU FBO before presenting/swapping!
    FlushLFBToGPU();

    // Enforce frame pacing and track frame timing using unified FrameTracker
    auto& tracker = TelemetryManager::GetInstance().GetFrameTracker();
    tracker.MarkFrameEnd(m_config.maxFps);
    tracker.MarkFrameStart();

    GLIDE_PROFILE_SCOPE("GLES::SwapBuffers");

    if (!m_headlessMode && m_sdlWindow && m_glContext) {
        // --- 100% GPU-NATIVE PRESENTATION (glBlitFramebuffer) ---
        // Bind our offscreen FBO as READ and the default window framebuffer 0 as DRAW
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_headlessFBOs[0]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        // Reset crucial OpenGL states to safe presentation defaults,
        // preventing active game states (like disabled color masks or custom scissors)
        // from masking or clipping our blit!
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        // Blit the color buffer from our offscreen FBO directly to the window surface.
        // glBlitFramebuffer performs a high-speed GPU-to-GPU copy.
        glBlitFramebuffer(0, 0, m_headlessWidth, m_headlessHeight,
                          0, 0, m_headlessWidth, m_headlessHeight,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        checkGLError("SwapBuffers glBlitFramebuffer");

        // Restore framebuffer bindings
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Swap front and back window buffers to display the rendered frame on the screen!
        SDL_GL_SwapWindow(static_cast<SDL_Window*>(m_sdlWindow));
    }

    // Swap the front and back CPU buffer indices for double-buffering
    if (m_headlessPixelMap) {
        std::swap(m_frontBufferIdx, m_backBufferIdx);
        std::swap(m_lfbBufferDirty[0], m_lfbBufferDirty[1]); // Swap dirty flags alongside buffer indices!
        if (m_activeRenderBuffer == 0) { // FRONTBUFFER
            m_headlessPixelMap = m_cpuBuffers[m_frontBufferIdx].data();
        } else { // BACKBUFFER
            m_headlessPixelMap = m_cpuBuffers[m_backBufferIdx].data();
        }
    }

    // Swap the GPU color targets for physical double-buffering!
    std::swap(m_headlessFBOs[0], m_headlessFBOs[1]);
    std::swap(m_fboTextures[0], m_fboTextures[1]);
    std::swap(m_headlessDepthRenderbuffers[0], m_headlessDepthRenderbuffers[1]);

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

    // Bind offscreen FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_headlessFBOs[0]);

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

void OpenGLESBackend::FlushLFBToGPU() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_lfbDirty) return;

    GLint prevTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);

    // Helper lambda to swizzle and upload a CPU buffer to a specific GPU texture
    auto flushBuffer = [this](const uint8_t* cpuBufferData, uint32_t gpuTexture) {
        const auto* src = reinterpret_cast<const uint32_t*>(cpuBufferData);
        if (!src || m_swizzleBuffer.empty()) return;
        for (uint32_t y = 0; y < m_headlessHeight; ++y) {
            uint32_t srcY = y;
            uint32_t dstY = m_headlessHeight - 1 - y; // Flip to match bottom-up FBO layout
            const uint32_t* srcRow = src + srcY * m_headlessWidth;
            uint32_t* dstRow = m_swizzleBuffer.data() + dstY * m_headlessWidth;

            for (uint32_t x = 0; x < m_headlessWidth; ++x) {
                uint32_t bgra = srcRow[x];
                uint32_t b = bgra & 0xFF;
                uint32_t g = (bgra >> 8) & 0xFF;
                uint32_t r = (bgra >> 16) & 0xFF;
                uint32_t a = (bgra >> 24) & 0xFF;
                dstRow[x] = (a << 24) | (b << 16) | (g << 8) | r; // RGBA
            }
        }
        glBindTexture(GL_TEXTURE_2D, gpuTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_headlessWidth, m_headlessHeight, GL_RGBA, GL_UNSIGNED_BYTE, m_swizzleBuffer.data());
    };

    // Determine which FBO texture corresponds to FRONTBUFFER and BACKBUFFER.
    // - If m_activeRenderBuffer == 0 (FRONTBUFFER):
    //   m_fboTextures[0] is the GPU FRONTBUFFER, m_fboTextures[1] is the GPU BACKBUFFER.
    // - If m_activeRenderBuffer == 1 (BACKBUFFER):
    //   m_fboTextures[0] is the GPU BACKBUFFER, m_fboTextures[1] is the GPU FRONTBUFFER.
    uint32_t gpuFrontTexture = (m_activeRenderBuffer == 0) ? m_fboTextures[0] : m_fboTextures[1];
    uint32_t gpuBackTexture  = (m_activeRenderBuffer == 0) ? m_fboTextures[1] : m_fboTextures[0];

    // 1. Flush FRONTBUFFER if dirty
    if (m_lfbBufferDirty[0]) {
        flushBuffer(m_cpuBuffers[m_frontBufferIdx].data(), gpuFrontTexture);
        checkGLError("FlushLFBToGPU FrontBuffer glTexSubImage2D");
        m_lfbBufferDirty[0] = false;
    }

    // 2. Flush BACKBUFFER if dirty
    if (m_lfbBufferDirty[1]) {
        flushBuffer(m_cpuBuffers[m_backBufferIdx].data(), gpuBackTexture);
        checkGLError("FlushLFBToGPU BackBuffer glTexSubImage2D");
        m_lfbBufferDirty[1] = false;
    }

    glBindTexture(GL_TEXTURE_2D, prevTexture);

    m_lfbDirty = false;
}

bool OpenGLESBackend::ReadLFB(uint32_t buffer, uint32_t srcX, uint32_t srcY, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstStride, void* dstData) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return false;

    // Flush any pending CPU LFB writes to the GPU first, ensuring they are
    // not obliterated by the subsequent glReadPixels readback!
    FlushLFBToGPU();

    // --- STEP 1: GPU Readback Flush ---
    // Query the currently bound FBO to restore it at the end, preventing state leakage!
    GLint previousFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);

    // Bind the correct offscreen FBO based on the requested buffer (0 = FRONT, 1 = BACK)
    GLuint fboToRead = (buffer == 0) ? m_headlessFBOs[1] : m_headlessFBOs[0];
    glBindFramebuffer(GL_FRAMEBUFFER, fboToRead);
    
    // Resolve the destination CPU buffer based on the requested buffer
    void* readDst = (buffer == 0) ? m_cpuBuffers[m_frontBufferIdx].data() : m_cpuBuffers[m_backBufferIdx].data();

    if (readDst) {
        // Read pixels as GL_RGBA (which contains our R/B swapped BGRA-ordered pixels)
        // GLES glReadPixels is synchronous and guarantees the GPU finishes all rendering before returning!
        glReadPixels(0, 0, m_headlessWidth, m_headlessHeight, GL_RGBA, GL_UNSIGNED_BYTE, readDst);
        checkGLError("ReadLFB glReadPixels");

        // Convert RGBA from GPU glReadPixels to BGRA and vertically flip the image in-place
        auto* dest = reinterpret_cast<uint32_t*>(readDst);
        // 1. Swizzle RGBA to BGRA
        for (uint32_t i = 0; i < m_headlessWidth * m_headlessHeight; ++i) {
            uint32_t rgba = dest[i];
            uint32_t r = rgba & 0xFF;
            uint32_t g = (rgba >> 8) & 0xFF;
            uint32_t b = (rgba >> 16) & 0xFF;
            uint32_t a = (rgba >> 24) & 0xFF;
            dest[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
        // 2. Vertically flip rows to convert OpenGL's bottom-up layout to host's top-down layout
        std::vector<uint32_t> rowBuffer(m_headlessWidth);
        for (uint32_t y = 0; y < m_headlessHeight / 2; ++y) {
            uint32_t topY = y;
            uint32_t bottomY = m_headlessHeight - 1 - y;
            uint32_t* topRow = dest + topY * m_headlessWidth;
            uint32_t* bottomRow = dest + bottomY * m_headlessWidth;
            std::memcpy(rowBuffer.data(), topRow, m_headlessWidth * 4);
            std::memcpy(topRow, bottomRow, m_headlessWidth * 4);
            std::memcpy(bottomRow, rowBuffer.data(), m_headlessWidth * 4);
        }
    }

    // Restore the previously bound framebuffer to prevent state leakage!
    glBindFramebuffer(GL_FRAMEBUFFER, previousFBO);

    // --- STEP 2: Convert and copy to client ---
    bool result = SoftwareBackendBase::ReadLFB(buffer, srcX, srcY, srcWidth, srcHeight, dstStride, dstData);

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

    if (m_msaaSamples > 1) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, m_msaaSamples);
    } else {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    }

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
    if (!ctx && m_msaaSamples > 1) {
        GLIDE_LOG(WARN, "GLES", "Failed to create multi-sampled OpenGL ES context. Falling back to single-sampling.");
        m_msaaSamples = 1;
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);

        SDL_DestroyWindow(win);
        win = SDL_CreateWindow(
            "3dfx glide-ng Presentation Console (OpenGL ES 3.2)",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            width, height, windowFlags
        );
        if (win) {
            ctx = SDL_GL_CreateContext(win);
        }
    }

    if (!ctx) {
        GLIDE_LOG(CRITICAL, "GLES", "SDL_GL_CreateContext failed: " << SDL_GetError());
        if (win) {
            SDL_DestroyWindow(win);
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    SDL_GL_MakeCurrent(win, ctx);

    // Query actual EGL samples allocated by host driver
    GLint actualSamples = 0;
    glGetIntegerv(GL_SAMPLES, &actualSamples);
    m_msaaSamples = (actualSamples > 1) ? static_cast<uint32_t>(actualSamples) : 1;

    // Output dynamic startup telemetry info
    const char* glVendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));

    std::cout << "Info: InitialiseOpenGLWindow(wnd=" << nativeWindowHandle << ", res=" << width << "x" << height << ")\r\n";
    std::cout << "Info: Host OpenGL Adapter: " << (glVendor ? glVendor : "Unknown") << " - " 
              << (glRenderer ? glRenderer : "Unknown") << " (GL Ver " << (glVersion ? glVersion : "Unknown") << ")\r\n";
    
    uint32_t samplesVal = (m_msaaSamples > 1) ? m_msaaSamples : 0;
    std::cout << "Info: Pixel Format RGBA8888 D24S8 nAux 0 nSamples " << samplesVal << " " << samplesVal << "\r\n";
    std::cout << "Info: Drawable Size: " << width << "x" << height << "\r\n" << std::flush;

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

    // Explicitly bind the presentation shader's texture uniform to texture unit 0
    GLint uTexLoc = glGetUniformLocation(m_glProgram, "uTex");
    if (uTexLoc != -1) {
        glUseProgram(m_glProgram);
        glUniform1i(uTexLoc, 0);
        glUseProgram(0);
    } else {
        GLIDE_LOG(WARN, "GLES", "Could not find uniform location for 'uTex' in presentation shader!");
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

    // Create the streaming presentation texture
    glGenTextures(1, &m_glTexture);
    glBindTexture(GL_TEXTURE_2D, m_glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Create the offscreen FBO GPU rendering textures (Double-Buffered)
    glGenTextures(2, m_fboTextures);
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, m_fboTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    m_sdlWindow = win;
    m_glContext = ctx;

    return true;
}

void OpenGLESBackend::DestroyGLContext() {
    if (m_glTexture)  { glDeleteTextures(1, &m_glTexture); m_glTexture = 0; }
    if (m_fboTextures[0]) { glDeleteTextures(2, m_fboTextures); m_fboTextures[0] = m_fboTextures[1] = 0; }
    if (m_glVBO)     { glDeleteBuffers(1, &m_glVBO); m_glVBO = 0; }
    if (m_glVAO)     { glDeleteVertexArrays(1, &m_glVAO); m_glVAO = 0; }
    if (m_glProgram) { glDeleteProgram(m_glProgram); m_glProgram = 0; }
    if (m_headlessDepthRenderbuffers[0]) { glDeleteRenderbuffers(2, m_headlessDepthRenderbuffers); m_headlessDepthRenderbuffers[0] = m_headlessDepthRenderbuffers[1] = 0; }
    if (m_headlessFBOs[0])               { glDeleteFramebuffers(2, m_headlessFBOs); m_headlessFBOs[0] = m_headlessFBOs[1] = 0; }
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
layout(location = 3) in vec2 inTmuOow;

out vec4 fragColor;
out float fragOOW;
out vec2 fragTexCoord;
out vec2 fragTmuOow;

uniform int uSstOrigin; // 0 = UPPER_LEFT, 1 = LOWER_LEFT
uniform vec2 uViewportSize;
uniform int uDepthClipOverride;
uniform highp int uDepthBufferMode; // 0=None, 1=Z, 2=W
uniform float uDepthBias;

void main() {
    // Convert Glide window-space coordinates to GLES NDC (Y-up)
    float ndcX = (inPos.x / (uViewportSize.x / 2.0)) - 1.0;
    float ndcY;
    if (uSstOrigin == 0) { // GR_ORIGIN_UPPER_LEFT
        ndcY = 1.0 - (inPos.y / (uViewportSize.y / 2.0));
    } else {              // GR_ORIGIN_LOWER_LEFT
        ndcY = (inPos.y / (uViewportSize.y / 2.0)) - 1.0;
    }
    
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
    fragOOW = inPos.w;
    fragTexCoord = inTex.xy;
    fragTmuOow = inTmuOow;
}
)";

static const char* geomFsSource = R"(#version 300 es
precision highp float;

in vec4 fragColor;
in float fragOOW;
in vec2 fragTexCoord;
in vec2 fragTmuOow;
out vec4 outColor;

uniform vec4 uConstantColor;

// Combiner Uniforms
uniform int uColorFunc;
uniform int uColorFactor;
uniform int uColorLocal;
uniform int uColorOther;
uniform int uColorInvert;

uniform int uAlphaFunc;
uniform int uAlphaFactor;
uniform int uAlphaLocal;
uniform int uAlphaOther;
uniform int uAlphaInvert;

uniform int uTextureEnabled;
uniform sampler2D uTexture0;
uniform int uUseTmuOow;

uniform int uChromakeyEnabled;
uniform vec4 uChromakeyValue;

uniform int uAlphaTestOp;
uniform float uAlphaTestRef;

// Milestone 8: Fogging Uniforms
uniform int uFogMode;
uniform vec4 uFogColor;
uniform float uFogTable[64];

// Depth Buffer Mode Uniform for Clipping
uniform highp int uDepthBufferMode;

const float s_tableIndexToW[64] = float[](
    1.000000,      1.142857,      1.333333,      1.600000, 
    2.000000,      2.285714,      2.666667,      3.200000, 
    4.000000,      4.571429,      5.333333,      6.400000, 
    8.000000,      9.142858,     10.666667,     12.800000, 
    16.000000,     18.285715,     21.333334,     25.600000, 
    32.000000,     36.571430,     42.666668,     51.200001, 
    64.000000,     73.142860,     85.333336,    102.400002, 
    128.000000,    146.285721,    170.666672,    204.800003, 
    256.000000,    292.571442,    341.333344,    409.600006, 
    512.000000,    585.142883,    682.666687,    819.200012, 
    1024.000000,   1170.285767,   1365.333374,   1638.400024, 
    2048.000000,   2340.571533,   2730.666748,   3276.800049, 
    4096.000000,   4681.143066,   5461.333496,   6553.600098, 
    8192.000000,   9362.286133,  10922.666992,  13107.200195, 
    16384.000000,  18724.572266,  21845.333984,  26214.400391, 
    32768.000000,  37449.144531,  43690.667969,  52428.800781
);

void main() {
    // W-based coordinate near-plane and camera-space clipping
    if (uDepthBufferMode == 2) {
        if (fragOOW > 1.0) discard;
    } else if (uTextureEnabled == 1) {
        if (fragOOW <= 0.0) discard;
    }

    // 1. Sample texture if enabled
    vec4 texColor = vec4(1.0);
    if (uTextureEnabled == 1) {
        ivec2 texSize = textureSize(uTexture0, 0);
        float tw = float(texSize.x);
        float th = float(texSize.y);
        float maxDim = max(tw, th);
        float divW = (uUseTmuOow == 1) ? fragTmuOow.x : fragOOW;
        vec2 uv = (fragTexCoord / divW) / 256.0;
        uv.x *= (maxDim / tw);
        uv.y *= (maxDim / th);
        texColor = texture(uTexture0, uv);
    }

    // 2. Route Alpha Combiner inputs
    float aLocal = 1.0;
    if (uAlphaLocal == 0) aLocal = fragColor.a;
    else if (uAlphaLocal == 1) aLocal = uConstantColor.a;

    float aOther = 1.0;
    if (uAlphaOther == 0) aOther = fragColor.a;
    else if (uAlphaOther == 1) aOther = texColor.a;
    else if (uAlphaOther == 2) aOther = uConstantColor.a;

    // 3. Evaluate Alpha Combiner Factor
    float factA = 0.0;
    if (uAlphaFactor == 1 || uAlphaFactor == 3) factA = aLocal;
    else if (uAlphaFactor == 2) factA = aOther;
    else if (uAlphaFactor == 4) factA = texColor.a;
    else if (uAlphaFactor == 8) factA = 1.0;
    else if (uAlphaFactor == 9 || uAlphaFactor == 11) factA = 1.0 - aLocal;
    else if (uAlphaFactor == 10) factA = 1.0 - aOther;
    else if (uAlphaFactor == 12) factA = 1.0 - texColor.a;

    // 4. Evaluate Alpha Combiner Function
    float finalA = 0.0;
    if (uAlphaFunc == 1) finalA = aLocal;
    else if (uAlphaFunc == 3) finalA = aOther * factA;
    else if (uAlphaFunc == 4 || uAlphaFunc == 5) finalA = aOther * factA + aLocal;
    else if (uAlphaFunc == 6) finalA = (aOther - aLocal) * factA;
    else if (uAlphaFunc == 7 || uAlphaFunc == 8) finalA = (aOther - aLocal) * factA + aLocal;
    
    if (uAlphaInvert == 1) finalA = 1.0 - finalA;

    // 5. Route Color Combiner inputs
    vec3 cLocal = vec3(0.0);
    if (uColorLocal == 0) cLocal = fragColor.rgb;
    else if (uColorLocal == 1) cLocal = uConstantColor.rgb;

    vec3 cOther = vec3(0.0);
    if (uColorOther == 0) cOther = fragColor.rgb;
    else if (uColorOther == 1) cOther = texColor.rgb;
    else if (uColorOther == 2) cOther = uConstantColor.rgb;

    // 6. Evaluate Color Combiner Factor
    vec3 factC = vec3(0.0);
    if (uColorFactor == 1) factC = cLocal;
    else if (uColorFactor == 2) factC = vec3(aOther);
    else if (uColorFactor == 3) factC = vec3(aLocal);
    else if (uColorFactor == 4) factC = vec3(texColor.a);
    else if (uColorFactor == 5) factC = texColor.rgb;
    else if (uColorFactor == 8) factC = vec3(1.0);
    else if (uColorFactor == 9) factC = vec3(1.0) - cLocal;
    else if (uColorFactor == 10) factC = vec3(1.0 - aOther);
    else if (uColorFactor == 11) factC = vec3(1.0 - aLocal);
    else if (uColorFactor == 12) factC = vec3(1.0 - texColor.a);

    // 7. Evaluate Color Combiner Function
    vec3 finalC = vec3(0.0);
    if (uColorFunc == 1) finalC = cLocal;
    else if (uColorFunc == 3) finalC = cOther * factC;
    else if (uColorFunc == 4) finalC = cOther * factC + cLocal;
    else if (uColorFunc == 5) finalC = cOther * factC + vec3(aLocal);
    else if (uColorFunc == 6) finalC = (cOther - cLocal) * factC;
    else if (uColorFunc == 7) finalC = (cOther - cLocal) * factC + cLocal;
    else if (uColorFunc == 8) finalC = (cOther - cLocal) * factC + vec3(aLocal);

    if (uColorInvert == 1) finalC = vec3(1.0) - finalC;

    vec4 col = vec4(finalC, finalA);

    // Milestone 10: Chromakey Emulation
    if (uChromakeyEnabled == 1) {
        if (distance(col.rgb, uChromakeyValue.rgb) < 0.01) {
            discard;
        }
    }

    // Milestone 8: Fogging Emulation
    if (uFogMode > 0) {
        float eyeW = 1.0 / fragOOW;
        if (eyeW < 1.0) eyeW = 1.0;

        int idx = 0;
        for (int i = 0; i < 63; ++i) {
            if (s_tableIndexToW[i] < eyeW) {
                idx = i;
            } else {
                break;
            }
        }

        float w0 = s_tableIndexToW[idx];
        float w1 = s_tableIndexToW[idx + 1];
        float f0 = uFogTable[idx];
        float f1 = uFogTable[idx + 1];

        float t = 0.0;
        if (w1 > w0) {
            t = (eyeW - w0) / (w1 - w0);
        }
        t = clamp(t, 0.0, 1.0);

        float f = mix(f0, f1, t);
        col.rgb = col.rgb * (1.0 - f) + uFogColor.rgb * f;
    }

    // Emulate legacy 3dfx Glide alpha testing
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

    // Location 3: inTmuOow (tmu_oow) -> vec2 (8 bytes)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(ModernVertex), (void*)offsetof(ModernVertex, tmu_oow));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    checkGLError("CreateGeomBuffers");
    return true;
}

void OpenGLESBackend::DrawPoint(const ModernVertex& pt) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized || !m_windowAttached) return;

    FlushLFBToGPU();

    GLIDE_LOG(DEBUG, "GLES", "DrawPoint GPU draw. Pos: (" << pt.pos[0] << ", " << pt.pos[1] << ", " << pt.pos[2] << ")");

    // 1. Upload vertex to dynamic VBO ring buffer
    glBindBuffer(GL_ARRAY_BUFFER, m_geomVBO);
    glBufferSubData(GL_ARRAY_BUFFER, m_geomVBOOffset, sizeof(ModernVertex), &pt);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // 2. Bind offscreen FBO and set state
    glBindFramebuffer(GL_FRAMEBUFFER, m_headlessFBOs[0]);
    glViewport(0, 0, m_headlessWidth, m_headlessHeight);
    SyncDepthState();
    ApplyBlendingGLES();
    ApplyScissorGLES();
    ApplyColorMaskGLES();

    // 3. Bind shader program and upload uniforms
    glUseProgram(m_geomProgram);
    PopulateUniforms();

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

    FlushLFBToGPU();

    GLIDE_LOG(DEBUG, "GLES", "DrawLine GPU draw. V0: (" << v0.pos[0] << ", " << v0.pos[1] << "), V1: (" << v1.pos[0] << ", " << v1.pos[1] << ")");
    GLIDE_LOG(DEBUG, "GLES", "DrawLine Colors. V0: (" << v0.color[0] << "," << v0.color[1] << "," << v0.color[2] << "," << v0.color[3] << "), V1: (" << v1.color[0] << "," << v1.color[1] << "," << v1.color[2] << "," << v1.color[3] << ")");

    // 1. Upload both vertices to dynamic VBO ring buffer
    glBindBuffer(GL_ARRAY_BUFFER, m_geomVBO);
    glBufferSubData(GL_ARRAY_BUFFER, m_geomVBOOffset, sizeof(ModernVertex), &v0);
    glBufferSubData(GL_ARRAY_BUFFER, m_geomVBOOffset + sizeof(ModernVertex), sizeof(ModernVertex), &v1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // 2. Bind offscreen FBO and set state
    glBindFramebuffer(GL_FRAMEBUFFER, m_headlessFBOs[0]);
    glViewport(0, 0, m_headlessWidth, m_headlessHeight);
    SyncDepthState();
    ApplyBlendingGLES();
    ApplyScissorGLES();
    ApplyColorMaskGLES();

    // 3. Bind shader program and upload uniforms
    glUseProgram(m_geomProgram);
    PopulateUniforms();

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

    FlushLFBToGPU();

    GLIDE_LOG(DEBUG, "GLES", "DrawTriangle GPU draw. A: (" << a.pos[0] << ", " << a.pos[1] << "), B: (" << b.pos[0] << ", " << b.pos[1] << "), C: (" << c.pos[0] << ", " << c.pos[1] << ")");

    // 1. Upload all three vertices directly to dynamic VBO ring buffer
    ModernVertex vertices[3] = { a, b, c };
    glBindBuffer(GL_ARRAY_BUFFER, m_geomVBO);
    glBufferSubData(GL_ARRAY_BUFFER, m_geomVBOOffset, sizeof(ModernVertex), &vertices[0]);
    glBufferSubData(GL_ARRAY_BUFFER, m_geomVBOOffset + sizeof(ModernVertex), sizeof(ModernVertex), &vertices[1]);
    glBufferSubData(GL_ARRAY_BUFFER, m_geomVBOOffset + 2 * sizeof(ModernVertex), sizeof(ModernVertex), &vertices[2]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // 2. Bind offscreen FBO and set state
    glBindFramebuffer(GL_FRAMEBUFFER, m_headlessFBOs[0]);
    glViewport(0, 0, m_headlessWidth, m_headlessHeight);
    SyncDepthState();
    ApplyBlendingGLES();
    ApplyScissorGLES();
    SyncCullStateGLES();
    ApplyColorMaskGLES();

    // 3. Bind shader program and upload uniforms
    glUseProgram(m_geomProgram);
    PopulateUniforms();

    // Bind texture if bound
    uint32_t addr0 = m_boundTexAddress[0];
    if (addr0 != 0xFFFFFFFF) {
        auto it = m_glesTextures.find(addr0);
        if (it != m_glesTextures.end()) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, it->second);

            // Set sampler wrap modes
            GLint wrapS = (m_texClampS[0] == 1) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
            GLint wrapT = (m_texClampT[0] == 1) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);

            // Set sampler filter modes
            GLint minFilter = (m_texMinFilter[0] == 1) ? GL_LINEAR : GL_NEAREST;
            GLint magFilter = (m_texMagFilter[0] == 1) ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
        }
    }

    // 4. Bind VAO and issue GLES triangle draw call
    glBindVertexArray(m_geomVAO);
    uint32_t firstVertex = m_geomVBOOffset / sizeof(ModernVertex);
    glDrawArrays(GL_TRIANGLES, firstVertex, 3);

    // Increment global statistics of triangles actually drawn to the screen
    TelemetryManager::GetInstance().IncrementTrianglesDrawn(1);

    // 5. Cleanup state bindings
    glBindVertexArray(0);
    glUseProgram(0);
    if (addr0 != 0xFFFFFFFF) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 6. Advance ring buffer offset
    m_geomVBOOffset += 3 * sizeof(ModernVertex);
    if (m_geomVBOOffset + 3 * sizeof(ModernVertex) > m_geomVBOSize) {
        GLIDE_LOG(WARN, "GLES", "Dynamic Geometry VBO ring buffer wrap-around!");
        m_geomVBOOffset = 0;
    }

    checkGLError("DrawTriangle");
}

void OpenGLESBackend::UploadTexture(uint32_t tmu, uint32_t startAddress, const struct VirtualTexture& tex) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_windowAttached && !m_headlessMode) {
        GLIDE_LOG(WARN, "GLES", "UploadTexture called without active GL context! Postponing or ignoring.");
        return;
    }

    GLIDE_PROFILE_SCOPE("GLES::UploadTexture");

    GLuint texId = 0;
    auto it = m_glesTextures.find(startAddress);
    if (it == m_glesTextures.end()) {
        glGenTextures(1, &texId);
        m_glesTextures[startAddress] = texId;
        GLIDE_LOG(DEBUG, "GLES", "Generated new GLES Texture ID " << texId << " for Address 0x" << std::hex << startAddress << std::dec);
    } else {
        texId = it->second;
        GLIDE_LOG(DEBUG, "GLES", "Re-uploading to existing GLES Texture ID " << texId << " for Address 0x" << std::hex << startAddress << std::dec);
    }

    glBindTexture(GL_TEXTURE_2D, texId);

    uint32_t baseW = tex.baseWidth;
    uint32_t baseH = tex.baseHeight;

    for (size_t i = 0; i < tex.swizzledMipLevels.size(); ++i) {
        uint32_t w = std::max(1u, baseW >> i);
        uint32_t h = std::max(1u, baseH >> i);
        const uint32_t* pixels = tex.swizzledMipLevels[i].data();

        // Swizzle BGRA from TextureManager to RGBA on the CPU before uploading to GLES
        std::vector<uint32_t> rgbaPixels(w * h);
        for (uint32_t j = 0; j < w * h; ++j) {
            uint32_t bgra = pixels[j];
            uint32_t b = bgra & 0xFF;
            uint32_t g = (bgra >> 8) & 0xFF;
            uint32_t r = (bgra >> 16) & 0xFF;
            uint32_t a = (bgra >> 24) & 0xFF;
            rgbaPixels[j] = (a << 24) | (b << 16) | (g << 8) | r;
        }

        glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), GL_RGBA, 
                     static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, 
                     GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels.data());
        
        GLIDE_LOG(DEBUG, "GLES", "Uploaded Mip Level " << i << " (" << w << "x" << h << ") to GLES Texture ID " << texId);
    }

    // Set default wrap/filter parameters so texture is complete
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, tex.swizzledMipLevels.size() > 1 ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBindTexture(GL_TEXTURE_2D, 0);
    checkGLError("UploadTexture");
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

static GLenum MapBlendFactorGLES(uint32_t factor, bool isDest) {
    switch (factor) {
        case 0: return GL_ZERO;                  // GR_BLEND_ZERO
        case 1: return GL_SRC_ALPHA;             // GR_BLEND_SRC_ALPHA
        case 2: return isDest ? GL_SRC_COLOR : GL_DST_COLOR; // 2 = GR_BLEND_SRC_COLOR (dest) / GR_BLEND_DST_COLOR (src)
        case 3: return GL_DST_ALPHA;             // GR_BLEND_DST_ALPHA
        case 4: return GL_ONE;                   // GR_BLEND_ONE
        case 5: return GL_ONE_MINUS_SRC_ALPHA;   // GR_BLEND_ONE_MINUS_SRC_ALPHA
        case 6: return isDest ? GL_ONE_MINUS_SRC_COLOR : GL_ONE_MINUS_DST_COLOR; // 6 = ONE_MINUS_SRC_COLOR (dest) / ONE_MINUS_DST_COLOR (src)
        case 7: return GL_ONE_MINUS_DST_ALPHA;   // GR_BLEND_ONE_MINUS_DST_ALPHA
        case 15: return GL_SRC_ALPHA_SATURATE;  // GR_BLEND_ALPHA_SATURATE
        default: return GL_ONE;
    }
}

void OpenGLESBackend::SetBlendState(uint32_t rgbSrcFactor, uint32_t rgbDstFactor, uint32_t alphaSrcFactor, uint32_t alphaDstFactor) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    SoftwareBackendBase::SetBlendState(rgbSrcFactor, rgbDstFactor, alphaSrcFactor, alphaDstFactor);
}

void OpenGLESBackend::SetColorMask(bool rgb, bool alpha) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    SoftwareBackendBase::SetColorMask(rgb, alpha);
}

void OpenGLESBackend::ApplyBlendingGLES() {
    if (m_rgbSrcBlend == 4 && m_rgbDstBlend == 0 && m_alphaSrcBlend == 4 && m_alphaDstBlend == 0) { // GR_BLEND_ONE & GR_BLEND_ZERO (disabled)
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(
            MapBlendFactorGLES(m_rgbSrcBlend, false),
            MapBlendFactorGLES(m_rgbDstBlend, true),
            MapBlendFactorGLES(m_alphaSrcBlend, false),
            MapBlendFactorGLES(m_alphaDstBlend, true)
        );
    }
}

void OpenGLESBackend::ApplyColorMaskGLES() {
    glColorMask(
        m_colorMaskRgb ? GL_TRUE : GL_FALSE,
        m_colorMaskRgb ? GL_TRUE : GL_FALSE,
        m_colorMaskRgb ? GL_TRUE : GL_FALSE,
        m_colorMaskAlpha ? GL_TRUE : GL_FALSE
    );
}

void OpenGLESBackend::ApplyScissorGLES() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    glEnable(GL_SCISSOR_TEST);
    
    // OpenGL scissor box expects bottom-up coordinates (0 = bottom).
    // If origin is UPPER_LEFT (0, top-down), we must flip Y.
    // If origin is LOWER_LEFT (1, bottom-up), we use raw coordinates.
    uint32_t glScissorY = m_clipMinY;
    if (m_sstOrigin == 0) { // GR_ORIGIN_UPPER_LEFT
        glScissorY = m_headlessHeight - m_clipMaxY;
    }
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
            glFrontFace(GL_CCW); // LOWER_LEFT is native Y-up; Glide front-face (positive area) is CCW!
        } else {
            glFrontFace(GL_CW);  // UPPER_LEFT is Y-down, shader Y-flip (reflection) maps Glide front-face (positive area) to CW!
        }
        if (m_cullMode == 1) { // GR_CULL_NEGATIVE (cull negative winding / CCW in Glide)
            glCullFace(GL_BACK); // Culls CCW under GL_CW, or CW under GL_CCW. Maps perfectly!
        } else if (m_cullMode == 2) { // GR_CULL_POSITIVE (cull positive winding / CW in Glide)
            glCullFace(GL_FRONT); // Culls CW under GL_CW, or CCW under GL_CCW. Maps perfectly!
        }
    }
}

void OpenGLESBackend::PopulateUniforms() {
    glUniform1i(glGetUniformLocation(m_geomProgram, "uSstOrigin"), (int)m_sstOrigin);
    glUniform2f(glGetUniformLocation(m_geomProgram, "uViewportSize"), (float)m_headlessWidth, (float)m_headlessHeight);
    bool depthClipOverride = (m_depthMode == 0) || (m_depthCompareOp == 7); // 7 = GR_CMP_ALWAYS (prevents uninitialized ooz stack garbage clipping in text overlays)
    glUniform1i(glGetUniformLocation(m_geomProgram, "uDepthClipOverride"), depthClipOverride ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uDepthBufferMode"), m_depthMode);

    float cr = ((m_constantColor >> 16) & 0xFF) / 255.0f;
    float cg = ((m_constantColor >> 8)  & 0xFF) / 255.0f;
    float cb = (m_constantColor         & 0xFF) / 255.0f;
    float ca = ((m_constantColor >> 24) & 0xFF) / 255.0f;
    if (m_pixelFormatOverride == 1) {
        std::swap(cr, cb);
    }
    if (ca < 0.5f) ca = 1.0f;
    glUniform4f(glGetUniformLocation(m_geomProgram, "uConstantColor"), cr, cg, cb, ca);

    // Upload Chromakey State Uniforms
    glUniform1i(glGetUniformLocation(m_geomProgram, "uChromakeyEnabled"), (int)m_chromakeyMode);
    float chR = ((m_chromakeyValue >> 16) & 0xFF) / 255.0f;
    float chG = ((m_chromakeyValue >> 8)  & 0xFF) / 255.0f;
    float chB = (m_chromakeyValue         & 0xFF) / 255.0f;
    float chA = ((m_chromakeyValue >> 24) & 0xFF) / 255.0f;
    if (m_pixelFormatOverride == 1) {
        std::swap(chR, chB);
    }
    glUniform4f(glGetUniformLocation(m_geomProgram, "uChromakeyValue"), chR, chG, chB, chA);

    // Upload Combiner State Uniforms
    glUniform1i(glGetUniformLocation(m_geomProgram, "uColorFunc"), (int)m_colorCombinerFunc);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uColorFactor"), (int)m_colorCombinerFactor);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uColorLocal"), (int)m_colorCombinerLocal);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uColorOther"), (int)m_colorCombinerOther);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uColorInvert"), m_colorCombinerInvert ? 1 : 0);

    glUniform1i(glGetUniformLocation(m_geomProgram, "uAlphaFunc"), (int)m_alphaCombinerFunc);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uAlphaFactor"), (int)m_alphaCombinerFactor);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uAlphaLocal"), (int)m_alphaCombinerLocal);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uAlphaOther"), (int)m_alphaCombinerOther);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uAlphaInvert"), m_alphaCombinerInvert ? 1 : 0);

    // Upload Texture Binding State
    bool texEnabled = (m_boundTexAddress[0] != 0xFFFFFFFF);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uTextureEnabled"), texEnabled ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_geomProgram, "uTexture0"), 0); // Always texture unit 0 for TMU0
    glUniform1i(glGetUniformLocation(m_geomProgram, "uUseTmuOow"), (m_stwHintMask & 0x2) ? 1 : 0);

    glUniform1i(glGetUniformLocation(m_geomProgram, "uAlphaTestOp"), (int)m_alphaTestOp);
    glUniform1f(glGetUniformLocation(m_geomProgram, "uAlphaTestRef"), (float)m_alphaTestRefVal / 255.0f);
    glUniform1f(glGetUniformLocation(m_geomProgram, "uDepthBias"), static_cast<float>(m_depthBiasLevel) / 65535.0f);

    // Milestone 8: Fogging State
    glUniform1i(glGetUniformLocation(m_geomProgram, "uFogMode"), (int)m_fogMode);
    
    float fogR = ((m_fogColor >> 16) & 0xFF) / 255.0f;
    float fogG = ((m_fogColor >> 8)  & 0xFF) / 255.0f;
    float fogB = (m_fogColor & 0xFF)        / 255.0f;
    if (m_pixelFormatOverride == 1) {
        std::swap(fogR, fogB);
    }
    glUniform4f(glGetUniformLocation(m_geomProgram, "uFogColor"), fogR, fogG, fogB, 1.0f);

    float glesFogTable[64];
    for (int i = 0; i < 64; ++i) {
        glesFogTable[i] = m_fogTable[i] / 255.0f;
    }
    glUniform1fv(glGetUniformLocation(m_geomProgram, "uFogTable"), 64, glesFogTable);
}

} // namespace GlideWrapper

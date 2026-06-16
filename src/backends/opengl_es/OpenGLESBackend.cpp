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

    std::cout << "Info: InitialiseOpenGLWindow(wnd=" << nativeWindowHandle << ", res=" << width << "x" << height << ")\r\n";
    std::cout << "Info: " << (renderer ? renderer : "Generic GLES Device") << " (" << (vendor ? vendor : "Unknown") << ") " 
              << (version ? version : "OpenGL ES 3.2") << "\r\n";
    std::cout << "Info: Pixel Format RGBA8888 D24S0 nAux 0 nSamples 0 0\r\n";
    std::cout << "Info: Drawable Size: " << width << "x" << height << "\r\n" << std::flush;

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
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Upload our software pixel map to GLES texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_glTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_headlessWidth, m_headlessHeight, GL_BGRA_EXT, GL_UNSIGNED_BYTE, m_headlessPixelMap);

        glUseProgram(m_glProgram);
        glBindVertexArray(m_glVAO);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        SDL_GL_SwapWindow(reinterpret_cast<SDL_Window*>(m_sdlWindow));
    }

    return true;
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

} // namespace GlideWrapper

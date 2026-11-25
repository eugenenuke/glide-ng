#include "BackendManager.h"
#include <algorithm>
#include <cctype>
#include "backends/vulkan/VulkanBackend.h"
#include "backends/opengl_es/OpenGLESBackend.h"
#include "backends/software/SoftwareBackend.h"
#include "Logger.h"
#include "TextureManager.h"
#include "Telemetry.h"
#include "ISplashAnimator.h"

namespace GlideWrapper {

BackendManager::BackendManager() {
    // Force construction of deinitialization dependencies first to guarantee they are destroyed AFTER BackendManager
    TextureManager::GetInstance();
    TelemetryManager::GetInstance();
}

BackendManager& BackendManager::GetInstance() {
    static BackendManager instance;
    return instance;
}

BackendManager::~BackendManager() {
    ShutdownBackend();
}

bool BackendManager::EstablishBackend(const WrapperConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    GLIDE_LOG(INFO, "Backend", "EstablishBackend entered. config.backend='" << config.backend << "', m_backend=" << (m_backend ? "NON-NULL" : "NULL"));

    // Resolve requested backend name from config (already resolved/validated by IniConfigLoader)
    std::string requested = config.backend;

    // Normalize to lowercase for case-insensitive matching
    std::transform(requested.begin(), requested.end(), requested.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    if (requested == "gles" || requested == "goes" || requested == "opengl_es") requested = "opengl_es";
    else if (requested == "sw" || requested == "software") requested = "software";
    else if (requested == "vk" || requested == "vulkan") requested = "vulkan";

    if (m_backend) {
        if (m_activeBackendName == requested) {
            GLIDE_LOG(INFO, "Backend", "Re-initializing active " << m_activeBackendName << " backend to reset state.");
            if (m_backend->Initialize(config)) {
                return true;
            }
            if (requested == "software") {
                return false;
            }
            GLIDE_LOG(WARN, "Backend",
                      "Active backend '" << requested << "' (gpu='"
                          << FormatGpuPreferenceFull(config.gpuPreference,
                                                     config.gpuNameMatch)
                          << "') failed to re-initialize; falling back safely to SoftwareBackend.");
            auto savedSplash = std::move(m_splashAnimator);
            ShutdownBackend_Locked();
            m_backend = std::make_unique<SoftwareBackend>();
            m_activeBackendName = "software";
            m_splashAnimator = std::move(savedSplash);
            return m_backend->Initialize(config);
        } else {
            GLIDE_LOG(INFO, "Backend", "Switching backend: shutting down active " << m_activeBackendName << " to establish " << requested << ".");
            ShutdownBackend_Locked();
        }
    }

    if (!m_backend) {
        if (requested == "opengl_es") {
            GLIDE_LOG(INFO, "Backend", "Instantiating OpenGLESBackend.");
            m_backend = std::make_unique<OpenGLESBackend>();
            m_activeBackendName = "opengl_es";
        } else if (requested == "software") {
            GLIDE_LOG(INFO, "Backend", "Instantiating SoftwareBackend.");
            m_backend = std::make_unique<SoftwareBackend>();
            m_activeBackendName = "software";
        } else if (requested == "vulkan") {
            GLIDE_LOG(INFO, "Backend", "Instantiating VulkanBackend.");
            m_backend = std::make_unique<VulkanBackend>();
            m_activeBackendName = "vulkan";
        } else {
            GLIDE_LOG(CRITICAL, "Backend", "Unrecognized rendering backend specified: '" << config.backend 
                      << "'. Supported backends are: vulkan (alias: vk), opengl_es (aliases: gles, goes), software (alias: sw).");
            return false;
        }
    }
    if (m_backend->Initialize(config)) {
        return true;
    }
    if (m_activeBackendName != "software") {
        GLIDE_LOG(WARN, "Backend",
                  "Requested backend '" << m_activeBackendName << "' (gpu='"
                      << FormatGpuPreferenceFull(config.gpuPreference,
                                                 config.gpuNameMatch)
                      << "') failed to initialize; falling back safely to SoftwareBackend.");
        auto savedSplash = std::move(m_splashAnimator);
        ShutdownBackend_Locked();
        m_backend = std::make_unique<SoftwareBackend>();
        m_activeBackendName = "software";
        m_splashAnimator = std::move(savedSplash);
        return m_backend->Initialize(config);
    }
    return false;
}

bool BackendManager::AttachWindowWithFallback(
    const WrapperConfig& config, void* nativeWindowHandle, uint32_t width,
    uint32_t height, bool windowed, uint32_t requestedMsaaSamples,
    uint32_t stwHintMask) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_backend) {
        return false;
    }
    m_backend->SetSTWHintState(stwHintMask);
    m_backend->SetRequestedMsaaSamples(requestedMsaaSamples);
    if (m_backend->AttachWindow(nativeWindowHandle, width, height, windowed)) {
        return true;
    }
    if (m_activeBackendName == "software") {
        return false;
    }

    GLIDE_LOG(WARN, "Backend",
              "Active backend '" << m_activeBackendName << "' (gpu='"
                  << FormatGpuPreferenceFull(config.gpuPreference,
                                             config.gpuNameMatch)
                  << "') failed to attach window; falling back safely to SoftwareBackend.");
    auto savedSplash = std::move(m_splashAnimator);
    ShutdownBackend_Locked();
    m_backend = std::make_unique<SoftwareBackend>();
    m_activeBackendName = "software";
    m_splashAnimator = std::move(savedSplash);
    if (!m_backend->Initialize(config)) {
        GLIDE_LOG(CRITICAL, "Backend",
                  "Fallback SoftwareBackend initialization failed.");
        ShutdownBackend_Locked();
        return false;
    }
    m_backend->SetSTWHintState(stwHintMask);
    m_backend->SetRequestedMsaaSamples(requestedMsaaSamples);
    return m_backend->AttachWindow(nativeWindowHandle, width, height, windowed);
}

void BackendManager::ShutdownBackend_Locked() {
    if (m_backend) {
        m_backend->Shutdown();
        m_backend.reset();
    }
    m_splashAnimator.reset();
    m_activeBackendName.clear();
    // Purge the core texture manager cache to reset state for the next context
    TextureManager::GetInstance().Reset();
    // Purge and reset all telemetry statistics
    TelemetryManager::GetInstance().Reset();
}

void BackendManager::ShutdownBackend() {
    std::lock_guard<std::mutex> lock(m_mutex);
    ShutdownBackend_Locked();
}

ISplashAnimator* BackendManager::GetSplashAnimator() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_splashAnimator.get();
}

void BackendManager::SetSplashAnimator(std::unique_ptr<ISplashAnimator> animator) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_splashAnimator = std::move(animator);
}

} // namespace GlideWrapper

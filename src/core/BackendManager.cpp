#include "BackendManager.h"
#include "backends/vulkan/VulkanBackend.h"
#include "backends/opengl_es/OpenGLESBackend.h"
#include "backends/software/SoftwareBackend.h"
#include "Logger.h"
#include "TextureManager.h"
#include "Telemetry.h"

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

IGraphicsBackend* BackendManager::GetBackend() {
    return m_backend.get();
}

bool BackendManager::EstablishBackend(const WrapperConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_backend) {
        if (config.backend == "opengl_es" || config.backend == "gles") {
            m_backend = std::make_unique<OpenGLESBackend>();
        } else if (config.backend == "software") {
            m_backend = std::make_unique<SoftwareBackend>();
        } else if (config.backend == "vulkan") {
            m_backend = std::make_unique<VulkanBackend>();
        } else {
            GLIDE_LOG(CRITICAL, "Backend", "Unrecognized rendering backend specified: '" << config.backend 
                      << "'. Supported backends are: vulkan, opengl_es (alias: gles), software.");
            return false;
        }
    }
    return m_backend->Initialize(config);
}

void BackendManager::ShutdownBackend() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_backend) {
        m_backend->Shutdown();
        m_backend.reset();
    }
    // Purge the core texture manager cache to reset state for the next context
    TextureManager::GetInstance().Reset();
    // Purge and reset all telemetry statistics
    TelemetryManager::GetInstance().Reset();
}

} // namespace GlideWrapper

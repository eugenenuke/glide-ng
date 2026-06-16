#include "BackendManager.h"
#include "backends/vulkan/VulkanBackend.h"
#include "backends/opengl_es/OpenGLESBackend.h"

namespace GlideWrapper {

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
        if (config.backend == "opengl_es") {
            m_backend = std::make_unique<OpenGLESBackend>();
        } else {
            m_backend = std::make_unique<VulkanBackend>();
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
}

} // namespace GlideWrapper

#include "BackendManager.h"
#include <algorithm>
#include <cctype>
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
    GLIDE_LOG(INFO, "Backend", "EstablishBackend entered. config.backend='" << config.backend << "', m_backend=" << (m_backend ? "NON-NULL" : "NULL"));
    if (m_backend) return true;

    // Resolve requested backend name from config (already resolved/validated by JsonConfigLoader)
    std::string requested = config.backend;

    // Normalize to lowercase for case-insensitive matching
    std::transform(requested.begin(), requested.end(), requested.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    if (requested == "gles" || requested == "goes" || requested == "opengl_es") requested = "opengl_es";
    else if (requested == "sw" || requested == "software") requested = "software";
    else if (requested == "vk" || requested == "vulkan") requested = "vulkan";

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
    return m_backend->Initialize(config);
}

void BackendManager::ShutdownBackend() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_backend) {
        m_backend->Shutdown();
        m_backend.reset();
    }
    m_activeBackendName.clear();
    // Purge the core texture manager cache to reset state for the next context
    TextureManager::GetInstance().Reset();
    // Purge and reset all telemetry statistics
    TelemetryManager::GetInstance().Reset();
}

} // namespace GlideWrapper

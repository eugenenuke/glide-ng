#pragma once

#include "core/IGraphicsBackend.h"
#include <memory>
#include <mutex>
#include <string>

namespace GlideWrapper {

class ISplashAnimator;

/**
 * @brief Authoritative Backend Manager singleton maintaining our low-level graphics execution pipeline.
 * Perfectly firewalled to ensure legacy C headers do not collide with complex C++20 Vulkan templates.
 */
class BackendManager {
public:
    static BackendManager& GetInstance();

    IGraphicsBackend* GetBackend() const { return m_backend.get(); }
    const std::string& GetActiveBackendName() const {
        return m_activeBackendName;
    }
    bool EstablishBackend(const WrapperConfig& config);
    bool AttachWindowWithFallback(const WrapperConfig& config,
                                  void* nativeWindowHandle, uint32_t width,
                                  uint32_t height, bool windowed,
                                  uint32_t requestedMsaaSamples = 0u,
                                  uint32_t stwHintMask = 0u);
    void ShutdownBackend();

    // Context-bound Splash Animator lifecycle
    ISplashAnimator* GetSplashAnimator();
    void SetSplashAnimator(std::unique_ptr<ISplashAnimator> animator);

private:
    BackendManager();
    ~BackendManager();
    void ShutdownBackend_Locked();

    std::mutex m_mutex;
    std::unique_ptr<IGraphicsBackend> m_backend;
    std::string m_activeBackendName;
    std::unique_ptr<ISplashAnimator> m_splashAnimator;
};

} // namespace GlideWrapper

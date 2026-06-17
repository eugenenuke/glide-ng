#pragma once

#include "core/IGraphicsBackend.h"
#include <memory>
#include <mutex>

namespace GlideWrapper {

/**
 * @brief Authoritative Backend Manager singleton maintaining our low-level graphics execution pipeline.
 * Perfectly firewalled to ensure legacy C headers do not collide with complex C++20 Vulkan templates.
 */
class BackendManager {
public:
    static BackendManager& GetInstance();

    IGraphicsBackend* GetBackend();
    bool EstablishBackend(const WrapperConfig& config);
    void ShutdownBackend();

private:
    BackendManager();
    ~BackendManager();

    std::recursive_mutex m_mutex;
    std::unique_ptr<IGraphicsBackend> m_backend;
};

} // namespace GlideWrapper

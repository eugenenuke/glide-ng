#pragma once

#include <3dfx.h>

namespace GlideWrapper {

class ISplashAnimator {
public:
    virtual ~ISplashAnimator() = default;
    virtual void Render(float x, float y, float w, float h, FxU32 frame, void (*callback)(int frame) = nullptr) = 0;
};

} // namespace GlideWrapper


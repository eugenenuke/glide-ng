/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * glide-ng clean-room procedural 3D startup splash ("Chrome & Amber Shield")
 * and 180x90 corner watermark renderer (GR_SHAMELESS_PLUG).
 */
#pragma once

#include <cstdint>
#include <glide.h>
#include "core/ISplashAnimator.h"

namespace GlideWrapper {

class GlideSplashAnimator : public ISplashAnimator {
public:
    static constexpr uint32_t kTotalFrames  = 75;
    static constexpr uint32_t kBannerWidth  = 180;
    static constexpr uint32_t kBannerHeight = 90;

    GlideSplashAnimator() = default;
    ~GlideSplashAnimator() override = default;

    void Render(float x, float y, float w, float h, FxU32 frame,
                void (*callback)(int frame) = nullptr) override;
    void RenderBanner(uint32_t screenWidth, uint32_t screenHeight) override;

    /// Rasterizes the 180x90 top-down RGB565 "GLIDE NG" Chrome & Amber badge
    /// onto a pure black (0x0000) chromakey background.
    static void BuildBannerRgb565(uint16_t* dst180x90);

private:
    void RenderSingleFrame(float vpX, float vpY, float vpW, float vpH,
                           int frame, bool drawFloorGrid) const;
};

} // namespace GlideWrapper

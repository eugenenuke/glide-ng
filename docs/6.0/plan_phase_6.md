# Implementation Plan - Phase 6: Advanced Fog Atmospheric Effects & Direct LFB Memory Access

This document outlines the architectural implementation and mathematical specification for introducing classic 3dfx **Atmospheric Fog Combiners** and complete **Direct Linear Framebuffer (LFB)** CPU memory manipulation.

---

## 1. Executive Summary & Foundational Architecture

Phase 6 targets the foundational realism and direct video memory hacking capabilities of 1990s retro PC gaming. Classic Glide games utilize hardware Fog tables to blend rendered pixels against global atmospheric colors, and lock the Linear Framebuffer to draw custom user interfaces, FMV videos, or subpixel effects directly into VRAM.

Our implementation establishes mathematical parity with authentic 3dfx Voodoo2 silicon across both linear/exponential fog tables and 16-bit / 32-bit LFB write regions.

---

## 2. Mathematical Specification

### Atmospheric Fog Blending
When Fog is enabled (`GR_FOG_WITH_TABLE` or `GR_FOG_WITH_ITERATED_Z`), the final combined color $C_{final}$ is evaluated by blending the incoming combiner color $C_{in}$ against the constant global fog color $C_{fog}$ using a 64-entry lookup table:

$$C_{final} = \alpha_{fog} \cdot C_{in} + (1 - \alpha_{fog}) \cdot C_{fog}$$

Where $\alpha_{fog}$ is an 8-bit blending weight mapped from the fragment's $W$ or $Z$ depth coordinate.
- **`GR_FOG_MULT2` Linear Mode**: $\alpha_{fog}$ scales linearly across minimum and maximum depth boundaries.
- **`GR_FOG_EXP` / `GR_FOG_EXP2` Exponential Mode**: $\alpha_{fog} = e^{-d \cdot z}$ or $e^{-(d \cdot z)^2}$.

### Direct LFB Pixel Swizzling
Classic applications write 16-bit RGB 565 or 15-bit RGB 555 pixels directly to the staging pointer returned by `grLfbGetWritePtr`. Our backend must automatically convert and unpack these words into our internal 32-bit ARGB Linear buffer:

$$\text{Red} = \left(\frac{\text{Pixel}_{16} \gg 11}{31}\right) \cdot 255, \quad \text{Green} = \left(\frac{(\text{Pixel}_{16} \gg 5) \ \& \ 0x3F}{63}\right) \cdot 255, \quad \text{Blue} = \left(\frac{\text{Pixel}_{16} \ \& \ 0x1F}{31}\right) \cdot 255$$

---

## 3. Systematic Implementation Workflow

### Stage 1: Documentation & State Trackers
- [ ] Initialize operational task tracker (`docs/6.0/task_phase_6.md`).
- [ ] In `VulkanBackend.h`, declare internal Fog state trackers:
  - `bool m_fogEnabled{false};`
  - `uint32_t m_fogMode{0};`
  - `uint32_t m_fogColor{0};`
  - `uint8_t m_fogTable[64];`

### Stage 2: Foundational Fog API Singletons
- [ ] **Frontend Exports**: Mapped in `glide2_entry.cpp` and `glide3_entry.cpp`:
  - `grFogMode(GrFogMode_t mode)`
  - `grFogColorValue(GrColor_t color)`
  - `grFogTable(const GrFog_t* fogtable)`
- [ ] **Backend Forwarders**: Implement pure virtual methods in `IGraphicsBackend` and concrete overrides in `VulkanBackend`:
  - `SetFogState(bool enabled, uint32_t mode, uint32_t color, const uint8_t* table)`

### Stage 3: Fog Table Evaluation Engine
- [ ] In `VulkanBackend::DrawTriangle`, integrate our Fog blending math into the C++ Software Rasterizer loop.
- [ ] For each rasterized fragment, if `m_fogEnabled` is active, compute the fragment depth, look up the interpolated $\alpha_{fog}$ weight from `m_fogTable`, and apply the atmospheric blending equation.

### Stage 4: Comprehensive LFB Readback & Swizzling
- [ ] **Frontend Exports**: Implement complete canonical dispatches for `grLfbLock`, `grLfbUnlock`, `grLfbWriteRegion`, `grLfbReadRegion`.
- [ ] **Format Singletons**: Implement explicit swizzling handlers for `GR_LFBWRITEMODE_565`, `GR_LFBWRITEMODE_555`, `GR_LFBWRITEMODE_888`, and `GR_LFBWRITEMODE_ARGB_8888`.

### Stage 5: Test Suite Regression & Verification
- [ ] **Catch2 Integration Tests**: Author `tests/test_fog_lfb.cpp` verifying perfect mathematical blending parity across pre-computed Fog tables and multi-format LFB pixel blocks.
- [ ] **Automated CI Execution**: Run `GLIDE_FORCE_NO_WINDOW=1 ./build/bin/GlideWrapperTests` and assert a 100% passing success score!

---

## 4. Verification Matrix

| Verification Target | Execution Mode | Expected Architectural Result |
| :--- | :--- | :--- |
| `FogTableBlending` | Catch2 CI Unit Test | Evaluates interpolated 64-entry Fog table weights to exact bitwise parity. |
| `LfbPixelSwizzle` | Catch2 CI Unit Test | Converts 16-bit RGB 565 and 15-bit 555 words into crisp 32-bit ARGB VRAM. |

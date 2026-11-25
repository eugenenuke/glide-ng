#include "VulkanBackend.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "core/Logger.h"
#include "core/Telemetry.h"

namespace GlideWrapper {

static uint32_t PackRGBA(float r, float g, float b, float a) {
  uint8_t ur = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
  uint8_t ug = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
  uint8_t ub = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
  uint8_t ua = static_cast<uint8_t>(std::clamp(a * 255.0f, 0.0f, 255.0f));
  return (ur << 0) | (ug << 8) | (ub << 16) | (ua << 24);
}

void VulkanBackend::ClearBuffer(uint32_t color, uint32_t alpha, float z,
                                uint32_t clearMask) {
  ClearBufferExt(color, alpha, z, 0u, clearMask & ~0x4u);
}

void VulkanBackend::ClearBufferExt(uint32_t color, uint32_t alpha, float z,
                                   uint8_t stencil, uint32_t clearMask) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return;

  const vk::Rect2D clipRect = GetScissorRectVulkan_Locked();
  if (clipRect.extent.width == 0 || clipRect.extent.height == 0) {
    return;
  }
  const bool fullClip = (clipRect.offset.x == 0 && clipRect.offset.y == 0 &&
                         clipRect.extent.width == m_headlessWidth &&
                         clipRect.extent.height == m_headlessHeight);

  GLIDE_LOG(DEBUG, "Vulkan",
            "ClearBufferExt: color=" << std::hex << color
                                     << ", clearMask=" << clearMask
                                     << ", stencil=" << static_cast<int>(stencil)
                                     << std::dec << ", rect=(" << clipRect.offset.x
                                     << "," << clipRect.offset.y << " "
                                     << clipRect.extent.width << "x"
                                     << clipRect.extent.height << ")");

  if (clearMask & 1) {
    const uint32_t logicalSlot = (m_activeRenderBuffer == 0) ? 0u : 1u;
    m_lfbReadCacheValid[logicalSlot] = false;
  }
  if (clearMask & 2) {
    m_auxDepthShadowValid = false;
  }

  // 1. Prepare color, depth, and stencil clear values respecting active masks.
  std::array<vk::ClearAttachment, 2> clears;
  uint32_t clearCount = 0;
  vk::ClearColorValue colorClearVal;
  float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
  bool doColorClear = false;
  bool doDepthClear = false;
  bool doStencilClear = false;

  if ((clearMask & 1) && (GetColorWriteMask4() != 0)) {
    float dummyA = 1.0f;
    UnpackGrColorNorm(color, m_pixelFormatOverride, r, g, b, dummyA);
    a = ((alpha > 0) ? alpha : 255) / 255.0f;

    colorClearVal = vk::ClearColorValue(std::array<float, 4>{r, g, b, a});
    clears[clearCount++] =
        vk::ClearAttachment(vk::ImageAspectFlagBits::eColor, 0, colorClearVal);
    doColorClear = true;
  }

  if ((clearMask & 2) && m_depthMask) {  // Depth clear (obeys grDepthMask)
    doDepthClear = true;
  }

  if ((clearMask & 4) && m_stencilEnabled && m_stencilWriteMask != 0u &&
      HasStencilAspect()) {
    doStencilClear = true;
  }

  if (doDepthClear || doStencilClear) {
    vk::ImageAspectFlags dsAspect{};
    if (doDepthClear) dsAspect |= vk::ImageAspectFlagBits::eDepth;
    if (doStencilClear) dsAspect |= vk::ImageAspectFlagBits::eStencil;
    clears[clearCount++] = vk::ClearAttachment(
        dsAspect, 0,
        vk::ClearValue(vk::ClearDepthStencilValue(z, stencil)));
  }

  if (clearCount == 0) return;

  // Check if VSA-100 grTBufferWriteMaskExt restricts this clear to a subset of
  // MSAA samples, OR if grColorMaskExt masks any color channel, OR if
  // grStencilMaskExt masks stencil bits. vkCmdClearAttachments ignores all three,
  // so masked clears must execute as a masked fullscreen quad.
  const uint32_t fullSampleMask =
      (m_msaaSamples >= 32) ? 0xFFFFFFFFu : ((1u << m_msaaSamples) - 1u);
  const uint32_t effSampleMask = GetEffectiveSampleMask_Locked();
  const bool isSampleMaskedClear =
      (m_msaaSamples > 1 && effSampleMask != fullSampleMask);
  const bool isChannelMaskedClear = (doColorClear && !IsFullColorMask());
  const bool isStencilMaskedClear =
      (doStencilClear && m_stencilWriteMask != 0xFFu);

  if (isSampleMaskedClear || isChannelMaskedClear || isStencilMaskedClear) {
    if (isSampleMaskedClear && effSampleMask == 0u) return;
    if (doColorClear && m_lfbStaging.IsOverlayDirty(m_activeRenderBuffer)) {
      FlushLFBToGPU_Locked();
    }
    if (doColorClear && fullClip && !isSampleMaskedClear) {
      m_lfbStaging.ScheduleLazyClearMask(m_activeRenderBuffer, color, alpha,
                                         m_pixelFormatOverride,
                                         GetColorWriteMask4());
    }
    if (m_batchedVertexCount > 0) {
      FlushBatch();
    }
    const bool useSingle =
        isSampleMaskedClear ? false : ShouldUseSingleSamplePass_Locked();
    const uint32_t passSampleMask =
        (m_msaaSamples > 1 && !useSingle) ? effSampleMask : 1u;
    DrawMaskedClearQuad_Locked(r, g, b, a, z, stencil, doColorClear,
                               doDepthClear, doStencilClear, passSampleMask,
                               clipRect, useSingle);
    if (m_msaaSamples > 1 && doDepthClear) {
      if (fullClip && !isSampleMaskedClear) {
        const uint32_t activeIdx = GetActiveGpuBufferIdx();
        m_clearDepthValue[activeIdx] = z;
        if (m_activePassIsSingleSample) {
          m_clearDepthPending[activeIdx] = true;
          m_msaaDepthDirty = false;
        } else {
          m_clearDepthPendingSingle[activeIdx] = true;
          m_msaaDepthDirty = true;
        }
      } else if (!useSingle) {
        m_msaaDepthDirty = true;
      }
    }
    if (m_msaaSamples > 1 && fullClip && !isSampleMaskedClear) {
      const uint32_t activeIdx = GetActiveGpuBufferIdx();
      if (doStencilClear && !isStencilMaskedClear) {
        m_clearStencilValue[activeIdx] = stencil;
        if (m_activePassIsSingleSample) {
          m_clearStencilPending[activeIdx] = true;
        } else {
          m_clearStencilPendingSingle[activeIdx] = true;
        }
      }
    }
    if (doColorClear && m_activeRenderBuffer == 0u) {
      NoteFrontBufferMutation_Locked();
    }
    return;
  }

  // 2. Handle any pending sparse LFB overlay before clearing, and defer
  // CPU-side full framebuffer clear lazily so pure 3D frames allocate 0 bytes.
  if (doColorClear) {
    if (fullClip && IsFullColorMask()) {
      m_lfbStaging.ClearOverlay(m_activeRenderBuffer);
    } else if (m_lfbStaging.IsOverlayDirty(m_activeRenderBuffer)) {
      FlushLFBToGPU_Locked();
    }
    if (fullClip) {
      m_lfbStaging.ScheduleLazyClearMask(m_activeRenderBuffer, color, alpha,
                                         m_pixelFormatOverride,
                                         GetColorWriteMask4());
    }
  }

  const uint32_t activeIdx = GetActiveGpuBufferIdx();

  // 3. Full-screen unmasked clears outside an active render pass stay deferred
  // so frame-start clears coalesce cleanly before LFB overlays or geometry.
  // Clipped sub-rect clears (e.g., split-screen panes / divider stripes) or
  // clears inside an active render pass execute immediately via
  // vkCmdClearAttachments so exact clip bounds and call ordering are preserved.
  if (fullClip && !m_inRenderPass) {
    if (doColorClear) {
      m_clearColorValue[activeIdx] = colorClearVal;
      m_clearColorPending[activeIdx] = true;
      if (IsFullColorMask()) {
        m_msaaColorDirty[activeIdx] = false;
        m_singleSampleColorDirty[activeIdx] = false;
      }
    }
    if (doDepthClear) {
      m_clearDepthValue[activeIdx] = z;
      m_clearDepthPending[activeIdx] = true;
      if (m_msaaSamples > 1) {
        m_clearDepthPendingSingle[activeIdx] = true;
        m_msaaDepthDirty = false;
      }
    }
    if (doStencilClear) {
      m_clearStencilValue[activeIdx] = stencil;
      m_clearStencilPending[activeIdx] = true;
      if (m_msaaSamples > 1) {
        m_clearStencilPendingSingle[activeIdx] = true;
      }
    }
    if (doColorClear && m_activeRenderBuffer == 0u) {
      NoteFrontBufferMutation_Locked();
    }
    return;
  }

  if (m_batchedVertexCount > 0) {
    FlushBatch();
  }
  EnsureRenderPassActive(ShouldUseSingleSamplePass_Locked());

  vk::ClearRect clearRect(clipRect, 0, 1);
  m_commandBuffer->clearAttachments(clearCount, clears.data(), 1, &clearRect);

  if (m_msaaSamples > 1) {
    if (doColorClear) {
      if (m_activePassIsSingleSample) {
        m_singleSampleColorDirty[activeIdx] = true;
        m_msaaColorDirty[activeIdx] = false;
      } else {
        m_msaaColorDirty[activeIdx] = true;
        m_singleSampleColorDirty[activeIdx] = false;
      }
    }
    if (doDepthClear) {
      if (fullClip) {
        m_clearDepthValue[activeIdx] = z;
        if (m_activePassIsSingleSample) {
          m_clearDepthPending[activeIdx] = true;
          m_msaaDepthDirty = false;
        } else {
          m_clearDepthPendingSingle[activeIdx] = true;
          m_msaaDepthDirty = true;
        }
      } else {
        m_msaaDepthDirty = !m_activePassIsSingleSample;
      }
    }
    if (fullClip && doStencilClear) {
      m_clearStencilValue[activeIdx] = stencil;
      if (m_activePassIsSingleSample) {
        m_clearStencilPending[activeIdx] = true;
      } else {
        m_clearStencilPendingSingle[activeIdx] = true;
      }
    }
  }
  if (doColorClear && m_activeRenderBuffer == 0u) {
    NoteFrontBufferMutation_Locked();
  }
}

void VulkanBackend::DrawMaskedClearQuad_Locked(float r, float g, float b,
                                               float a, float z,
                                               uint8_t stencil,
                                               bool doColorClear,
                                               bool doDepthClear,
                                               bool doStencilClear,
                                               uint32_t sampleMask,
                                               const vk::Rect2D& clipRect,
                                               bool useSingleSample) {
  if (!m_vertexBufferMap) return;

  const uint32_t slotBase = m_currentFrameSlot * kVertexSlotBytes;
  const uint32_t slotEnd = slotBase + kVertexSlotBytes;
  if (m_vertexBufferOffset + 6 * sizeof(ModernVertex) > slotEnd) {
    SubmitMidFrameAndWaitIdle_Locked(true);
    m_vertexBufferOffset = slotBase;
  }

  EnsureRenderPassActive(useSingleSample);

  const float w = static_cast<float>(m_guestWidth);
  const float h = static_cast<float>(m_guestHeight);
  const float clampedZ = std::clamp(z, 0.0f, 1.0f);

  auto makeVert = [&](float x, float y) -> ModernVertex {
    ModernVertex v{};
    v.pos[0] = x;
    v.pos[1] = y;
    v.pos[2] = clampedZ;
    v.pos[3] = 1.0f;
    v.color[0] = r;
    v.color[1] = g;
    v.color[2] = b;
    v.color[3] = a;
    return v;
  };

  const uint32_t firstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
  auto* dest = reinterpret_cast<ModernVertex*>(
      reinterpret_cast<uint8_t*>(m_vertexBufferMap) + m_vertexBufferOffset);
  dest[0] = makeVert(0.0f, 0.0f);
  dest[1] = makeVert(w, 0.0f);
  dest[2] = makeVert(w, h);
  dest[3] = makeVert(0.0f, 0.0f);
  dest[4] = makeVert(w, h);
  dest[5] = makeVert(0.0f, h);
  m_vertexBufferOffset += 6 * sizeof(ModernVertex);

  PipelineStateKey clearKey{};
  clearKey.cullMode = 0;  // GR_CULL_DISABLE
  clearKey.sstOrigin = 0;
  clearKey.depthMode = doDepthClear ? 1u : 0u;
  clearKey.depthMask = doDepthClear;
  clearKey.depthCompareOp = doDepthClear ? 7u : 0u;  // GR_CMP_ALWAYS
  clearKey.rgbSrcBlend = 4u;                         // GR_BLEND_ONE
  clearKey.rgbDstBlend = 0u;                         // GR_BLEND_ZERO
  clearKey.alphaSrcBlend = 4u;                       // GR_BLEND_ONE
  clearKey.alphaDstBlend = 0u;                       // GR_BLEND_ZERO
  clearKey.colorWriteMask = doColorClear ? GetColorWriteMask4() : 0u;
  clearKey.singleSample = (m_msaaSamples > 1) && useSingleSample;
  clearKey.sampleMask = sampleMask;
  clearKey.stencilEnabled = doStencilClear && HasStencilAspect();
  if (clearKey.stencilEnabled) {
    clearKey.stencilCompareOp = 7u;  // GR_CMP_ALWAYS
    clearKey.stencilFailOp = 2u;     // GR_STENCILOP_REPLACE
    clearKey.stencilZFailOp = 2u;
    clearKey.stencilZPassOp = 2u;
  }

  vk::Pipeline clearPipeline =
      GetOrCreatePipeline(clearKey, vk::PrimitiveTopology::eTriangleList);
  if (clearPipeline != m_boundPipelineInPass) {
    m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics,
                                  clearPipeline);
    m_boundPipelineInPass = clearPipeline;
  }
  if (clearKey.stencilEnabled) {
    m_commandBuffer->setStencilCompareMask(
        vk::StencilFaceFlagBits::eFrontAndBack, 0xFFu);
    m_commandBuffer->setStencilWriteMask(
        vk::StencilFaceFlagBits::eFrontAndBack, m_stencilWriteMask);
    m_commandBuffer->setStencilReference(
        vk::StencilFaceFlagBits::eFrontAndBack, stencil);
  }
  if (m_dummyDescriptorSet != m_boundDescSetInPass) {
    m_commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                        m_pipelineLayout.get(), 0, 1,
                                        &m_dummyDescriptorSet, 0, nullptr);
    m_boundDescSetInPass = m_dummyDescriptorSet;
  }

  PushConstants pcs{};
  pcs.viewportWidth = w;
  pcs.viewportHeight = h;
  pcs.depthBufferMode = doDepthClear ? 1u : 0u;
  pcs.alphaTestOp = 7u;  // GR_CMP_ALWAYS
  pcs.colorFunc = 1u;    // LOCAL
  pcs.colorLocal = 0u;   // ITERATED (fragColor)
  pcs.alphaFunc = 1u;    // LOCAL
  pcs.alphaLocal = 0u;   // ITERATED (fragColor.a)
  pcs.depthNear = 0.0f;
  pcs.depthFar = 1.0f;
  m_commandBuffer->pushConstants<PushConstants>(
      m_pipelineLayout.get(),
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
      pcs);

  m_commandBuffer->setScissor(0, 1, &clipRect);
  m_commandBuffer->draw(6, 1, firstVertex, 0);

  if (doColorClear && m_msaaSamples > 1) {
    const uint32_t activeIdx = GetActiveGpuBufferIdx();
    if (m_activePassIsSingleSample) {
      m_singleSampleColorDirty[activeIdx] = true;
      m_msaaColorDirty[activeIdx] = false;
    } else {
      m_msaaColorDirty[activeIdx] = true;
      m_singleSampleColorDirty[activeIdx] = false;
    }
  }
}

void VulkanBackend::ResolveDrawState_Locked(vk::PrimitiveTopology topology,
                                            vk::Pipeline& outPipeline,
                                            vk::DescriptorSet& outDescSet) {
  if (m_pipelineStateDirty || !m_cachedPipeline ||
      topology != m_cachedTopology) {
    m_cachedPipeline = GetOrCreatePipeline(GetCurrentStateKey(), topology);
    m_cachedTopology = topology;
    m_pipelineStateDirty = false;
  }

  if (m_descriptorStateDirty || !m_cachedDescSet) {
    vk::ImageView view0 = m_dummyTexture.imageView.get();
    vk::Sampler sampler0 = m_dummySampler.get();
    if (m_boundTexAddress[0] != 0xFFFFFFFF) {
      auto it = m_vulkanTextures[0].find(m_boundTexAddress[0]);
      if (it != m_vulkanTextures[0].end() && it->second) {
        view0 = it->second->imageView.get();
        sampler0 = GetOrCreateSampler(m_texClampS[0], m_texClampT[0],
                                      m_texMinFilter[0], m_texMagFilter[0],
                                      m_texLodBias[0], m_texMipMapMode[0],
                                      m_texLodBlend[0]);
      }
    }

    vk::ImageView view1 = m_dummyTexture.imageView.get();
    vk::Sampler sampler1 = m_dummySampler.get();
    if (m_boundTexAddress[1] != 0xFFFFFFFF) {
      auto it = m_vulkanTextures[1].find(m_boundTexAddress[1]);
      if (it != m_vulkanTextures[1].end() && it->second) {
        view1 = it->second->imageView.get();
        sampler1 = GetOrCreateSampler(m_texClampS[1], m_texClampT[1],
                                      m_texMinFilter[1], m_texMagFilter[1],
                                      m_texLodBias[1], m_texMipMapMode[1],
                                      m_texLodBlend[1]);
      }
    }

    m_cachedDescSet =
        GetOrCreateDescriptorSet(view0, sampler0, view1, sampler1);
    m_descriptorStateDirty = false;
  }

  outPipeline = m_cachedPipeline;
  outDescSet = m_cachedDescSet;
}

void VulkanBackend::DrawPoint(const ModernVertex& pt) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return;

  if (m_lfbStaging.IsDirty()) {
    FlushLFBToGPU_Locked();
  }

  GLIDE_LOG(DEBUG, "Vulkan",
            "DrawPoint GPU draw. Pos: (" << pt.pos[0] << ", " << pt.pos[1]
                                         << ", " << pt.pos[2] << ")");

  // 1. Resolve cached active pipeline & descriptor set
  vk::Pipeline reqPipeline;
  vk::DescriptorSet reqDescSet;
  ResolveDrawState_Locked(vk::PrimitiveTopology::ePointList, reqPipeline,
                          reqDescSet);

  // 2. Check if we need to flush the active batch
  const uint32_t slotBase = m_currentFrameSlot * kVertexSlotBytes;
  const uint32_t slotEnd = slotBase + kVertexSlotBytes;
  bool stateChanged =
      (reqPipeline != m_batchedPipeline || reqDescSet != m_batchedDescSet);
  bool batchEmpty = (m_batchedVertexCount == 0);
  bool bufferFull = (m_vertexBufferOffset + sizeof(ModernVertex) > slotEnd);

  if (stateChanged || batchEmpty || bufferFull) {
    if (stateChanged || bufferFull) {
      FlushBatch(stateChanged);
    }
    if (bufferFull) {
      GLIDE_LOG(WARN, "Vulkan",
                "Dynamic Vertex Buffer slot overflow during DrawPoint! "
                "Stalling GPU to prevent geometry corruption.");
      SubmitMidFrameAndWaitIdle_Locked(true);
      m_vertexBufferOffset = slotBase;
    }

    m_batchedPipeline = reqPipeline;
    m_batchedDescSet = reqDescSet;
    m_batchedTopology = vk::PrimitiveTopology::ePointList;
    m_batchedFirstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
  }

  // 3. Append vertex to mapped buffer
  if (!m_vertexBufferMap) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "Vertex buffer map is null during DrawPoint!");
    return;
  }
  auto* dest = reinterpret_cast<ModernVertex*>(
      reinterpret_cast<uint8_t*>(m_vertexBufferMap) + m_vertexBufferOffset);
  if (m_sstOrigin == 1) {  // GR_ORIGIN_LOWER_LEFT: flip Y coordinate
    *dest = pt;
    dest->pos[1] = (float)m_headlessHeight - pt.pos[1];
  } else {
    *dest = pt;
  }

  // 4. Update batch & offset tracking
  m_batchedVertexCount += 1;
  m_vertexBufferOffset += sizeof(ModernVertex);
  if (__builtin_expect(m_activeRenderBuffer == 0u, 0)) {
    NoteFrontBufferMutation_Locked();
  }
}

void VulkanBackend::DrawLine(const ModernVertex& v1, const ModernVertex& v2) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return;

  if (m_lfbStaging.IsDirty()) {
    FlushLFBToGPU_Locked();
  }

  GLIDE_LOG(DEBUG, "Vulkan",
            "DrawLine GPU draw. V1: (" << v1.pos[0] << ", " << v1.pos[1]
                                       << "), V2: (" << v2.pos[0] << ", "
                                       << v2.pos[1] << ")");

  // 1. Resolve cached active pipeline & descriptor set
  vk::Pipeline reqPipeline;
  vk::DescriptorSet reqDescSet;
  ResolveDrawState_Locked(vk::PrimitiveTopology::eLineList, reqPipeline,
                          reqDescSet);

  // 2. Check if we need to flush the active batch
  const uint32_t slotBase = m_currentFrameSlot * kVertexSlotBytes;
  const uint32_t slotEnd = slotBase + kVertexSlotBytes;
  bool stateChanged =
      (reqPipeline != m_batchedPipeline || reqDescSet != m_batchedDescSet);
  bool batchEmpty = (m_batchedVertexCount == 0);
  bool bufferFull = (m_vertexBufferOffset + 2 * sizeof(ModernVertex) > slotEnd);

  if (stateChanged || batchEmpty || bufferFull) {
    if (stateChanged || bufferFull) {
      FlushBatch(stateChanged);
    }
    if (bufferFull) {
      GLIDE_LOG(WARN, "Vulkan",
                "Dynamic Vertex Buffer slot overflow during DrawLine! Stalling "
                "GPU to prevent geometry corruption.");
      SubmitMidFrameAndWaitIdle_Locked(true);
      m_vertexBufferOffset = slotBase;
    }

    m_batchedPipeline = reqPipeline;
    m_batchedDescSet = reqDescSet;
    m_batchedTopology = vk::PrimitiveTopology::eLineList;
    m_batchedFirstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
  }

  // 3. Append vertices to mapped buffer
  if (!m_vertexBufferMap) {
    GLIDE_LOG(CRITICAL, "Vulkan", "Vertex buffer map is null during DrawLine!");
    return;
  }
  auto* dest = reinterpret_cast<ModernVertex*>(
      reinterpret_cast<uint8_t*>(m_vertexBufferMap) + m_vertexBufferOffset);
  if (m_sstOrigin == 1) {  // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
    dest[0] = v1;
    dest[0].pos[1] = (float)m_headlessHeight - v1.pos[1];
    dest[1] = v2;
    dest[1].pos[1] = (float)m_headlessHeight - v2.pos[1];
  } else {
    dest[0] = v1;
    dest[1] = v2;
  }

  // 4. Update batch & offset tracking
  m_batchedVertexCount += 2;
  m_vertexBufferOffset += 2 * sizeof(ModernVertex);
  if (__builtin_expect(m_activeRenderBuffer == 0u, 0)) {
    NoteFrontBufferMutation_Locked();
  }
}

void VulkanBackend::DrawTriangle(const ModernVertex& a, const ModernVertex& b,
                                 const ModernVertex& c) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (__builtin_expect(!m_initialized || !m_windowAttached, 0)) return;

  if (__builtin_expect(m_lfbStaging.IsDirty(), 0)) {
    FlushLFBToGPU_Locked();
  }

  GLIDE_LOG(TRACE, "Vulkan",
            "DrawTriangle GPU draw. A pos: ("
                << a.pos[0] << ", " << a.pos[1] << ", " << a.pos[2] << ", "
                << a.pos[3] << ") color: (" << a.color[0] << ", " << a.color[1]
                << ", " << a.color[2] << ", " << a.color[3] << ") tex: ("
                << a.tex[0] << ", " << a.tex[1] << ")");

  const uint32_t slotBase = m_currentFrameSlot * kVertexSlotBytes;
  const uint32_t slotEnd = slotBase + kVertexSlotBytes;

  // Fast path: pipeline & descriptor set already resolved for active triangle
  // batch and vertex slot has room — bypass ResolveDrawState_Locked entirely.
  if (__builtin_expect(
          !m_pipelineStateDirty && !m_descriptorStateDirty &&
              m_batchedVertexCount > 0 &&
              m_cachedTopology == vk::PrimitiveTopology::eTriangleList &&
              m_batchedPipeline == m_cachedPipeline &&
              m_batchedDescSet == m_cachedDescSet &&
              m_vertexBufferOffset + 3 * sizeof(ModernVertex) <= slotEnd &&
              m_vertexBufferMap != nullptr,
          1)) {
    auto* dest = reinterpret_cast<ModernVertex*>(
        reinterpret_cast<uint8_t*>(m_vertexBufferMap) + m_vertexBufferOffset);
    if (__builtin_expect(m_sstOrigin == 0, 1)) {
      dest[0] = a;
      dest[1] = b;
      dest[2] = c;
    } else {
      const float h = static_cast<float>(m_headlessHeight);
      dest[0] = a;
      dest[0].pos[1] = h - a.pos[1];
      dest[1] = b;
      dest[1].pos[1] = h - b.pos[1];
      dest[2] = c;
      dest[2].pos[1] = h - c.pos[1];
    }
    m_batchedVertexCount += 3;
    m_vertexBufferOffset += 3 * sizeof(ModernVertex);
    if (__builtin_expect(m_activeRenderBuffer == 0u, 0)) {
      NoteFrontBufferMutation_Locked();
    }
    return;
  }

  // 1. Resolve cached active pipeline & descriptor set
  vk::Pipeline reqPipeline;
  vk::DescriptorSet reqDescSet;
  ResolveDrawState_Locked(vk::PrimitiveTopology::eTriangleList, reqPipeline,
                          reqDescSet);

  // 2. Check if we need to flush the active batch
  bool stateChanged =
      (reqPipeline != m_batchedPipeline || reqDescSet != m_batchedDescSet);
  bool batchEmpty = (m_batchedVertexCount == 0);
  bool bufferFull = (m_vertexBufferOffset + 3 * sizeof(ModernVertex) > slotEnd);

  if (stateChanged || batchEmpty || bufferFull) {
    if (stateChanged || bufferFull) {
      FlushBatch(stateChanged);
    }
    if (bufferFull) {
      GLIDE_LOG(WARN, "Vulkan",
                "Dynamic Vertex Buffer slot overflow during DrawTriangle! "
                "Stalling GPU to prevent geometry corruption.");
      SubmitMidFrameAndWaitIdle_Locked(true);
      m_vertexBufferOffset = slotBase;
    }

    m_batchedPipeline = reqPipeline;
    m_batchedDescSet = reqDescSet;
    m_batchedTopology = vk::PrimitiveTopology::eTriangleList;
    m_batchedFirstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
  }

  // 3. Append vertices to mapped buffer
  if (!m_vertexBufferMap) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "Vertex buffer map is null during DrawTriangle!");
    return;
  }
  auto* dest = reinterpret_cast<ModernVertex*>(
      reinterpret_cast<uint8_t*>(m_vertexBufferMap) + m_vertexBufferOffset);
  if (m_sstOrigin == 1) {  // GR_ORIGIN_LOWER_LEFT: flip Y coordinates
    const float h = static_cast<float>(m_headlessHeight);
    dest[0] = a;
    dest[0].pos[1] = h - a.pos[1];
    dest[1] = b;
    dest[1].pos[1] = h - b.pos[1];
    dest[2] = c;
    dest[2].pos[1] = h - c.pos[1];
  } else {
    dest[0] = a;
    dest[1] = b;
    dest[2] = c;
  }

  // 4. Update batch & offset tracking
  m_batchedVertexCount += 3;
  m_vertexBufferOffset += 3 * sizeof(ModernVertex);
  if (__builtin_expect(m_activeRenderBuffer == 0u, 0)) {
    NoteFrontBufferMutation_Locked();
  }
}

void VulkanBackend::DrawTriangleFan(const ModernVertex* verts,
                                    uint32_t count) {
  if (!verts || count < 3u) return;
  std::lock_guard<std::mutex> lock(m_mutex);
  if (__builtin_expect(!m_initialized || !m_windowAttached, 0)) return;

  if (__builtin_expect(m_lfbStaging.IsDirty(), 0)) {
    FlushLFBToGPU_Locked();
  }

  vk::Pipeline reqPipeline;
  vk::DescriptorSet reqDescSet;
  if (__builtin_expect(
          !m_pipelineStateDirty && !m_descriptorStateDirty &&
              m_batchedVertexCount > 0 &&
              m_cachedTopology == vk::PrimitiveTopology::eTriangleList &&
              m_batchedPipeline == m_cachedPipeline &&
              m_batchedDescSet == m_cachedDescSet,
          1)) {
    reqPipeline = m_cachedPipeline;
    reqDescSet = m_cachedDescSet;
  } else {
    ResolveDrawState_Locked(vk::PrimitiveTopology::eTriangleList, reqPipeline,
                            reqDescSet);
    const bool stateChanged =
        (reqPipeline != m_batchedPipeline || reqDescSet != m_batchedDescSet);
    const bool batchEmpty = (m_batchedVertexCount == 0);
    if (stateChanged) {
      FlushBatch(true);
    }
    if (stateChanged || batchEmpty) {
      m_batchedPipeline = reqPipeline;
      m_batchedDescSet = reqDescSet;
      m_batchedTopology = vk::PrimitiveTopology::eTriangleList;
      m_batchedFirstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
    }
  }

  if (!m_vertexBufferMap) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "Vertex buffer map is null during DrawTriangleFan!");
    return;
  }

  const uint32_t slotBase = m_currentFrameSlot * kVertexSlotBytes;
  const uint32_t slotEnd = slotBase + kVertexSlotBytes;
  const bool flipY = (m_sstOrigin == 1);
  const float h = static_cast<float>(m_headlessHeight);
  ModernVertex v0 = verts[0];
  if (flipY) {
    v0.pos[1] = h - v0.pos[1];
  }

  uint32_t i = 1u;
  while (i + 1u < count) {
    if (m_vertexBufferOffset + 3u * sizeof(ModernVertex) > slotEnd) {
      FlushBatch(false);
      GLIDE_LOG(WARN, "Vulkan",
                "Dynamic Vertex Buffer slot overflow during DrawTriangleFan! "
                "Stalling GPU to prevent geometry corruption.");
      SubmitMidFrameAndWaitIdle_Locked(true);
      m_vertexBufferOffset = slotBase;
      m_batchedPipeline = reqPipeline;
      m_batchedDescSet = reqDescSet;
      m_batchedTopology = vk::PrimitiveTopology::eTriangleList;
      m_batchedFirstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
    }

    const uint32_t availTris =
        (slotEnd - m_vertexBufferOffset) / (3u * sizeof(ModernVertex));
    const uint32_t remTris = (count - 1u) - i;
    const uint32_t chunkTris = std::min(availTris, remTris);
    auto* dest = reinterpret_cast<ModernVertex*>(
        reinterpret_cast<uint8_t*>(m_vertexBufferMap) + m_vertexBufferOffset);

    if (!flipY) {
      for (uint32_t k = 0; k < chunkTris; ++k, ++i) {
        dest[0] = v0;
        dest[1] = verts[i];
        dest[2] = verts[i + 1u];
        dest += 3;
      }
    } else {
      for (uint32_t k = 0; k < chunkTris; ++k, ++i) {
        ModernVertex v1 = verts[i];
        v1.pos[1] = h - v1.pos[1];
        ModernVertex v2 = verts[i + 1u];
        v2.pos[1] = h - v2.pos[1];
        dest[0] = v0;
        dest[1] = v1;
        dest[2] = v2;
        dest += 3;
      }
    }

    const uint32_t addedVerts = chunkTris * 3u;
    m_batchedVertexCount += addedVerts;
    m_vertexBufferOffset += addedVerts * sizeof(ModernVertex);
  }

  if (__builtin_expect(m_activeRenderBuffer == 0u, 0)) {
    NoteFrontBufferMutation_Locked();
  }
}

void VulkanBackend::DrawTriangleList(const ModernVertex* verts,
                                     uint32_t count) {
  if (!verts || count < 3u) return;
  const uint32_t numTris = count / 3u;
  if (numTris == 0u) return;

  std::lock_guard<std::mutex> lock(m_mutex);
  if (__builtin_expect(!m_initialized || !m_windowAttached, 0)) return;

  if (__builtin_expect(m_lfbStaging.IsDirty(), 0)) {
    FlushLFBToGPU_Locked();
  }

  vk::Pipeline reqPipeline;
  vk::DescriptorSet reqDescSet;
  if (__builtin_expect(
          !m_pipelineStateDirty && !m_descriptorStateDirty &&
              m_batchedVertexCount > 0 &&
              m_cachedTopology == vk::PrimitiveTopology::eTriangleList &&
              m_batchedPipeline == m_cachedPipeline &&
              m_batchedDescSet == m_cachedDescSet,
          1)) {
    reqPipeline = m_cachedPipeline;
    reqDescSet = m_cachedDescSet;
  } else {
    ResolveDrawState_Locked(vk::PrimitiveTopology::eTriangleList, reqPipeline,
                            reqDescSet);
    const bool stateChanged =
        (reqPipeline != m_batchedPipeline || reqDescSet != m_batchedDescSet);
    const bool batchEmpty = (m_batchedVertexCount == 0);
    if (stateChanged) {
      FlushBatch(true);
    }
    if (stateChanged || batchEmpty) {
      m_batchedPipeline = reqPipeline;
      m_batchedDescSet = reqDescSet;
      m_batchedTopology = vk::PrimitiveTopology::eTriangleList;
      m_batchedFirstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
    }
  }

  if (!m_vertexBufferMap) {
    GLIDE_LOG(CRITICAL, "Vulkan",
              "Vertex buffer map is null during DrawTriangleList!");
    return;
  }

  const uint32_t slotBase = m_currentFrameSlot * kVertexSlotBytes;
  const uint32_t slotEnd = slotBase + kVertexSlotBytes;
  const bool flipY = (m_sstOrigin == 1);
  const float h = static_cast<float>(m_headlessHeight);

  uint32_t triIdx = 0u;
  while (triIdx < numTris) {
    if (m_vertexBufferOffset + 3u * sizeof(ModernVertex) > slotEnd) {
      FlushBatch(false);
      GLIDE_LOG(WARN, "Vulkan",
                "Dynamic Vertex Buffer slot overflow during DrawTriangleList! "
                "Stalling GPU to prevent geometry corruption.");
      SubmitMidFrameAndWaitIdle_Locked(true);
      m_vertexBufferOffset = slotBase;
      m_batchedPipeline = reqPipeline;
      m_batchedDescSet = reqDescSet;
      m_batchedTopology = vk::PrimitiveTopology::eTriangleList;
      m_batchedFirstVertex = m_vertexBufferOffset / sizeof(ModernVertex);
    }

    const uint32_t availTris =
        (slotEnd - m_vertexBufferOffset) / (3u * sizeof(ModernVertex));
    const uint32_t chunkTris = std::min(availTris, numTris - triIdx);
    const uint32_t addedVerts = chunkTris * 3u;
    auto* dest = reinterpret_cast<ModernVertex*>(
        reinterpret_cast<uint8_t*>(m_vertexBufferMap) + m_vertexBufferOffset);
    const ModernVertex* src = verts + triIdx * 3u;

    if (!flipY) {
      std::memcpy(dest, src, addedVerts * sizeof(ModernVertex));
    } else {
      for (uint32_t v = 0; v < addedVerts; ++v) {
        ModernVertex mv = src[v];
        mv.pos[1] = h - mv.pos[1];
        dest[v] = mv;
      }
    }

    triIdx += chunkTris;
    m_batchedVertexCount += addedVerts;
    m_vertexBufferOffset += addedVerts * sizeof(ModernVertex);
  }

  if (__builtin_expect(m_activeRenderBuffer == 0u, 0)) {
    NoteFrontBufferMutation_Locked();
  }
}

void VulkanBackend::FlushBatch(bool fromStateFlush) {
  if (m_batchedVertexCount == 0) return;

  GLIDE_CPU_SCOPE(CpuBucket::Draw);
  GLIDE_PROFILE_SCOPE("Vulkan::FlushBatch");
  TelemetryManager::GetInstance().RecordDrawCall(fromStateFlush);
  GLIDE_INCREMENT_TRIANGLES_DRAWN(m_batchedVertexCount / 3);

  GLIDE_LOG(TRACE, "Vulkan",
            "FlushBatch: drawing "
                << m_batchedVertexCount
                << " vertices. FirstVertex=" << m_batchedFirstVertex
                << (fromStateFlush ? " [state-flush]" : ""));

  EnsureRenderPassActive(ShouldUseSingleSamplePass_Locked());

  // Invalidate cached CPU LFB readback for the target buffer whenever 3D
  // geometry executes on it (zero overhead inside normal rendering loops).
  m_lfbReadCacheValid[(m_activeRenderBuffer == 0) ? 0u : 1u] = false;
  if (m_depthMode != 0 && m_depthMask) {
    m_auxDepthShadowValid = false;
    if (m_msaaSamples > 1) {
      m_msaaDepthDirty = !m_activePassIsSingleSample;
    }
  }

  if (m_batchedDescSet != m_boundDescSetInPass) {
    m_commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                        m_pipelineLayout.get(), 0, 1,
                                        &m_batchedDescSet, 0, nullptr);
    m_boundDescSetInPass = m_batchedDescSet;
  }

  const bool isCompareToBias =
      (m_depthMode == 3u || m_depthMode == 4u) && HasStencilAspect();
  if (isCompareToBias) {
    // Authentic 3dfx GR_DEPTHBUFFER_{Z,W}BUFFER_COMPARE_TO_BIAS two-pass:
    //   Pass 1 probes DecodeAuxDepth16ToNorm(bias) against live depth buffer,
    //   marking stencil bit 0x80 where the comparison passes (zero color/depth
    //   writes). Pass 2 writes color + real incoming Z/W depth only where bit
    //   0x80 is set, clearing bit 0x80 as it passes.
    if (!m_stencilEnabled) {
      vk::ClearAttachment stClear(vk::ImageAspectFlagBits::eStencil, 0,
                                  vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0u)));
      vk::Rect2D clearSc = GetScissorRectVulkan_Locked();
      if (clearSc.extent.width > 0 && clearSc.extent.height > 0) {
        vk::ClearRect cr(clearSc, 0, 1);
        m_commandBuffer->clearAttachments(1, &stClear, 1, &cr);
      }
    }

    PipelineStateKey baseKey = GetCurrentStateKey();
    PipelineStateKey pass1Key = baseKey;
    pass1Key.compareToBiasPass = 1u;
    vk::Pipeline pass1Pipe = GetOrCreatePipeline(pass1Key, m_batchedTopology);

    PipelineStateKey pass2Key = baseKey;
    pass2Key.compareToBiasPass = 2u;
    vk::Pipeline pass2Pipe = GetOrCreatePipeline(pass2Key, m_batchedTopology);

    PushConstants pcs{};
    PopulatePushConstants(pcs);
    const float biasNorm = DecodeAuxDepth16ToNorm(
        static_cast<uint16_t>(m_depthBiasLevel), m_depthMode, m_depthNear,
        m_depthFar);

    // Pass 1: depth compare against biasNorm -> stamp stencil bit 0x80
    PushConstants pcs1 = pcs;
    pcs1.depthBias = biasNorm;
    pcs1.flags |= (1u << 13u);
    m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, pass1Pipe);
    m_commandBuffer->pushConstants<PushConstants>(
        m_pipelineLayout.get(),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, pcs1);
    m_commandBuffer->setStencilCompareMask(
        vk::StencilFaceFlagBits::eFrontAndBack, 0x80u);
    m_commandBuffer->setStencilWriteMask(
        vk::StencilFaceFlagBits::eFrontAndBack, 0x80u);
    m_commandBuffer->setStencilReference(
        vk::StencilFaceFlagBits::eFrontAndBack, 0x80u);
    m_commandBuffer->draw(m_batchedVertexCount, 1, m_batchedFirstVertex, 0);

    // Pass 2: stencil-gated color + true Z/W depth write, clearing bit 0x80
    PushConstants pcs2 = pcs;
    pcs2.flags &= ~(1u << 13u);
    m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, pass2Pipe);
    m_commandBuffer->pushConstants<PushConstants>(
        m_pipelineLayout.get(),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, pcs2);
    m_commandBuffer->draw(m_batchedVertexCount, 1, m_batchedFirstVertex, 0);

    m_boundPipelineInPass = pass2Pipe;
    if (m_stencilEnabled) {
      m_commandBuffer->setStencilCompareMask(
          vk::StencilFaceFlagBits::eFrontAndBack, m_stencilReadMask);
      m_commandBuffer->setStencilWriteMask(
          vk::StencilFaceFlagBits::eFrontAndBack, m_stencilWriteMask);
      m_commandBuffer->setStencilReference(
          vk::StencilFaceFlagBits::eFrontAndBack, m_stencilRef);
    }
  } else {
    if (m_batchedPipeline != m_boundPipelineInPass) {
      m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics,
                                    m_batchedPipeline);
      m_boundPipelineInPass = m_batchedPipeline;
    }

    PushConstants pcs{};
    PopulatePushConstants(pcs);
    m_commandBuffer->pushConstants<PushConstants>(
        m_pipelineLayout.get(),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, pcs);

    if (m_stencilEnabled && HasStencilAspect()) {
      m_commandBuffer->setStencilCompareMask(
          vk::StencilFaceFlagBits::eFrontAndBack, m_stencilReadMask);
      m_commandBuffer->setStencilWriteMask(
          vk::StencilFaceFlagBits::eFrontAndBack, m_stencilWriteMask);
      m_commandBuffer->setStencilReference(
          vk::StencilFaceFlagBits::eFrontAndBack, m_stencilRef);
    }

    m_commandBuffer->draw(m_batchedVertexCount, 1, m_batchedFirstVertex, 0);
  }

  if (m_msaaSamples > 1 && GetColorWriteMask4() != 0) {
    const uint32_t activeIdx = GetActiveGpuBufferIdx();
    if (m_activePassIsSingleSample) {
      m_singleSampleColorDirty[activeIdx] = true;
      m_msaaColorDirty[activeIdx] = false;
    } else {
      m_msaaColorDirty[activeIdx] = true;
      m_singleSampleColorDirty[activeIdx] = false;
    }
  }

  m_batchedVertexCount = 0;
}

vk::Rect2D VulkanBackend::GetScissorRectVulkan_Locked() {
  uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (!ComputeClipRectTopDown_Locked(x0, y0, x1, y1)) {
    return vk::Rect2D({0, 0}, {0, 0});
  }
  return vk::Rect2D(
      vk::Offset2D(static_cast<int32_t>(x0), static_cast<int32_t>(y0)),
      vk::Extent2D(x1 - x0, y1 - y0));
}

void VulkanBackend::EnsureRenderPassActive(bool useSingleSample) {
  const uint32_t activeIdx = GetActiveGpuBufferIdx();
  const bool actualSingle =
      (m_msaaSamples > 1 && useSingleSample && m_singleSampleRenderPass &&
       m_singleSampleFramebuffers[activeIdx]);

  if (m_inRenderPass) {
    if (m_activePassIsSingleSample == actualSingle &&
        m_activePassGpuIdx == activeIdx) {
      return;
    }
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
  }

  // Synchronize MSAA <-> 1x color targets when switching render pass types
  // mid-frame, unless a deferred full-screen clear is about to overwrite all
  // pixels on the target.
  if (m_msaaSamples > 1 && !m_clearColorPending[activeIdx]) {
    if (actualSingle && m_msaaColorDirty[activeIdx]) {
      ResolveMsaaToSingle_Locked(activeIdx);
    } else if (!actualSingle && m_singleSampleColorDirty[activeIdx]) {
      CopySingleToMsaa_Locked(activeIdx);
    }
  }

  vk::RenderPass targetRp =
      actualSingle ? m_singleSampleRenderPass.get() : m_renderPass.get();
  vk::Framebuffer targetFb =
      actualSingle ? m_singleSampleFramebuffers[activeIdx].get()
                   : m_headlessFramebuffers[activeIdx].get();

  // Begin render pass with eLoad (no automatic clear)
  vk::RenderPassBeginInfo renderPassInfo(
      targetRp, targetFb,
      vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0,
      nullptr);  // 0 clear values since we use eLoad!
  m_commandBuffer->beginRenderPass(renderPassInfo,
                                   vk::SubpassContents::eInline);

  // Configure Dynamic Viewport & Scissor
  vk::Viewport viewport(0.0f, 0.0f, (float)m_headlessWidth,
                        (float)m_headlessHeight, 0.0f, 1.0f);
  m_commandBuffer->setViewport(0, 1, &viewport);

  vk::Rect2D scissor = GetScissorRectVulkan_Locked();
  m_commandBuffer->setScissor(0, 1, &scissor);
  m_commandBuffer->setStencilCompareMask(vk::StencilFaceFlagBits::eFrontAndBack,
                                         m_stencilReadMask);
  m_commandBuffer->setStencilWriteMask(vk::StencilFaceFlagBits::eFrontAndBack,
                                       m_stencilWriteMask);
  m_commandBuffer->setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack,
                                       m_stencilRef);

  // Bind vertex buffer
  vk::DeviceSize zeroOffset = 0;
  m_commandBuffer->bindVertexBuffers(0, 1, &m_vertexBuffer.get(), &zeroOffset);

  m_boundPipelineInPass = vk::Pipeline{};
  m_boundDescSetInPass = vk::DescriptorSet{};
  m_inRenderPass = true;
  m_activePassIsSingleSample = actualSingle;
  m_activePassGpuIdx = activeIdx;

  // Perform explicit clears using vkCmdClearAttachments if pending on the
  // active FBO!
  std::array<vk::ClearAttachment, 2> clears;
  uint32_t clearCount = 0;
  if (m_clearColorPending[activeIdx]) {
    clears[clearCount++] = vk::ClearAttachment(vk::ImageAspectFlagBits::eColor,
                                               0, m_clearColorValue[activeIdx]);
    m_clearColorPending[activeIdx] = false;
    if (m_msaaSamples > 1) {
      if (actualSingle) {
        m_singleSampleColorDirty[activeIdx] = true;
        m_msaaColorDirty[activeIdx] = false;
      } else {
        m_msaaColorDirty[activeIdx] = true;
        m_singleSampleColorDirty[activeIdx] = false;
      }
    }
  }
  bool& depthPending = actualSingle ? m_clearDepthPendingSingle[activeIdx]
                                    : m_clearDepthPending[activeIdx];
  bool& stencilPending = actualSingle ? m_clearStencilPendingSingle[activeIdx]
                                      : m_clearStencilPending[activeIdx];
  if (depthPending || (stencilPending && HasStencilAspect())) {
    vk::ImageAspectFlags dsAspect{};
    if (depthPending) dsAspect |= vk::ImageAspectFlagBits::eDepth;
    if (stencilPending && HasStencilAspect()) {
      dsAspect |= vk::ImageAspectFlagBits::eStencil;
    }
    clears[clearCount++] = vk::ClearAttachment(
        dsAspect, 0,
        vk::ClearValue(vk::ClearDepthStencilValue(
            m_clearDepthValue[activeIdx], m_clearStencilValue[activeIdx])));
    depthPending = false;
    stencilPending = false;
  }

  if (clearCount > 0) {
    vk::ClearRect clearRect(
        vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0, 1);
    m_commandBuffer->clearAttachments(clearCount, clears.data(), 1, &clearRect);
    GLIDE_LOG(DEBUG, "Vulkan",
              "Explicitly cleared " << clearCount
                                    << " attachments inside render pass.");
  }
}

void VulkanBackend::PopulatePushConstants(PushConstants& pcs) {
  pcs.viewportWidth = (float)m_guestWidth;
  pcs.viewportHeight = (float)m_guestHeight;
  pcs.depthBufferMode = m_depthMode;

  // Extract and normalize constant color components, then pack
  float constR = 0.0f, constG = 0.0f, constB = 0.0f, constA = 1.0f;
  UnpackGrColorNorm(m_constantColor, m_pixelFormatOverride, constR, constG,
                    constB, constA);
  pcs.constantColor = PackRGBA(constR, constG, constB, constA);

  pcs.alphaTestOp = m_alphaTestOp;
  pcs.alphaTestRef = (float)m_alphaTestRefVal / 255.0f;
  pcs.depthBias = static_cast<float>(m_depthBiasLevel) / 65535.0f;

  // Milestone 8: Fogging State (packed 128-byte layout)
  pcs.fogMode = m_fogMode;

  // color.frag unpacks fogColor as canonical 0xAARRGGBB (R=16..23, G=8..15, B=0..7)
  pcs.fogColor = GrColorToCanonicalArgb(m_fogColor, m_pixelFormatOverride);

  // Pack 64-entry 8-bit fog table into 16 uint32_t values
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
  static_assert(sizeof(pcs.fogTable) == sizeof(m_fogTable),
                "PushConstants::fogTable size mismatch");
  std::memcpy(pcs.fogTable, m_fogTable, sizeof(m_fogTable));
#else
  for (int i = 0; i < 16; ++i) {
    uint32_t word = 0;
    word |= static_cast<uint32_t>(m_fogTable[i * 4 + 0]) << 0;
    word |= static_cast<uint32_t>(m_fogTable[i * 4 + 1]) << 8;
    word |= static_cast<uint32_t>(m_fogTable[i * 4 + 2]) << 16;
    word |= static_cast<uint32_t>(m_fogTable[i * 4 + 3]) << 24;
    pcs.fogTable[i] = word;
  }
#endif

  // Milestone 9: Combiner & Texture State
  pcs.colorFunc = m_colorCombinerFunc;
  pcs.colorFactor = m_colorCombinerFactor;
  pcs.colorLocal = m_colorCombinerLocal;
  pcs.colorOther = m_colorCombinerOther;

  pcs.alphaFunc = m_alphaCombinerFunc;
  pcs.alphaFactor = m_alphaCombinerFactor;
  pcs.alphaLocal = m_alphaCombinerLocal;
  pcs.alphaOther = m_alphaCombinerOther;

  uint32_t textureEnabled = 0;
  const bool combinerReadsTex = DoesCombinerReadTexture_Locked();
  if (combinerReadsTex && m_boundTexAddress[0] != 0xFFFFFFFF) {
    textureEnabled |= (1u << 0);
  }
  if (combinerReadsTex && m_boundTexAddress[1] != 0xFFFFFFFF) {
    textureEnabled |= (1u << 1);
  }
  if (m_texCombinerRgbInvert[0]) textureEnabled |= (1u << 2);
  if (m_texCombinerAlphaInvert[0]) textureEnabled |= (1u << 3);
  if (m_texCombinerRgbInvert[1]) textureEnabled |= (1u << 4);
  if (m_texCombinerAlphaInvert[1]) textureEnabled |= (1u << 5);
  pcs.textureEnabled = textureEnabled;

  // Pack combiner modes (functions and factors) for both TMUs into separate
  // uint32_t fields (8-bit precision)
  uint32_t tmu0Comb = 0;
  tmu0Comb |= (m_texCombinerRgbFunc[0] & 0xFF) << 0;
  tmu0Comb |= (m_texCombinerRgbFactor[0] & 0xFF) << 8;
  tmu0Comb |= (m_texCombinerAlphaFunc[0] & 0xFF) << 16;
  tmu0Comb |= (m_texCombinerAlphaFactor[0] & 0xFF) << 24;
  pcs.tmuCombinerModes0 = tmu0Comb;

  uint32_t tmu1Comb = 0;
  tmu1Comb |= (m_texCombinerRgbFunc[1] & 0xFF) << 0;
  tmu1Comb |= (m_texCombinerRgbFactor[1] & 0xFF) << 8;
  tmu1Comb |= (m_texCombinerAlphaFunc[1] & 0xFF) << 16;
  tmu1Comb |= (m_texCombinerAlphaFactor[1] & 0xFF) << 24;
  pcs.tmuCombinerModes1 = tmu1Comb;

  // Milestone 10: Chromakey State
  pcs.chromakeyRangeMode = m_chromakeyRangeMode;

  float cValR = 0.0f, cValG = 0.0f, cValB = 0.0f, cValA = 0.0f;
  float cMinR = 0.0f, cMinG = 0.0f, cMinB = 0.0f, cMinA = 0.0f;
  float cMaxR = 0.0f, cMaxG = 0.0f, cMaxB = 0.0f, cMaxA = 0.0f;
  UnpackGrColorNorm(m_chromakeyValue, m_pixelFormatOverride, cValR, cValG,
                    cValB, cValA);
  UnpackGrColorNorm(m_chromakeyRangeMin, m_pixelFormatOverride, cMinR, cMinG,
                    cMinB, cMinA);
  UnpackGrColorNorm(m_chromakeyRangeMax, m_pixelFormatOverride, cMaxR, cMaxG,
                    cMaxB, cMaxA);

  pcs.chromakeyValue = PackRGBA(cValR, cValG, cValB, cValA);
  pcs.chromakeyRangeMin = PackRGBA(cMinR, cMinG, cMinB, cMinA);
  pcs.chromakeyRangeMax = PackRGBA(cMaxR, cMaxG, cMaxB, cMaxA);

  // Populate texture-specific chromakeying states for TMU0 and TMU1 (compact
  // RGBA uint32 layout so PushConstants stays <= 256 bytes with VSA-100 COMBINE)
  uint32_t packedTexChromaMin[2]{0, 0};
  uint32_t packedTexChromaMax[2]{0, 0};
  uint32_t packedTmuConst[2]{0, 0};
  for (int i = 0; i < 2; ++i) {
    float minR = 0.0f, minG = 0.0f, minB = 0.0f, minA = 0.0f;
    float maxR = 0.0f, maxG = 0.0f, maxB = 0.0f, maxA = 0.0f;
    float tcR = 0.0f, tcG = 0.0f, tcB = 0.0f, tcA = 0.0f;
    UnpackGrColorNorm(m_texChromaMin[i], m_pixelFormatOverride, minR, minG,
                      minB, minA);
    UnpackGrColorNorm(m_texChromaMax[i], m_pixelFormatOverride, maxR, maxG,
                      maxB, maxA);
    UnpackGrColorNorm(m_tmuConstantColor[i], m_pixelFormatOverride, tcR, tcG,
                      tcB, tcA);

    packedTexChromaMin[i] = PackRGBA(minR, minG, minB, minA);
    packedTexChromaMax[i] = PackRGBA(maxR, maxG, maxB, maxA);
    packedTmuConst[i] = PackRGBA(tcR, tcG, tcB, tcA);
  }
  pcs.texChromaMin0 = packedTexChromaMin[0];
  pcs.texChromaMin1 = packedTexChromaMin[1];
  pcs.texChromaMax0 = packedTexChromaMax[0];
  pcs.texChromaMax1 = packedTexChromaMax[1];

  // Phase 4 additions: depth range, stipple pattern, and packed flags
  pcs.depthNear = m_depthNear;
  pcs.depthFar = m_depthFar;
  pcs.stipplePattern = m_stipplePattern;

  uint32_t flags = 0;
  if (m_colorCombinerInvert) flags |= (1u << 0);
  if (m_alphaCombinerInvert) flags |= (1u << 1);
  if (m_chromakeyMode != 0) flags |= (1u << 2);

  uint32_t useTmuOow = 0;
  if (m_stwHintMask & 0x2) useTmuOow |= (1u << 0);
  if (m_stwHintMask & 0x8) useTmuOow |= (1u << 1);
  flags |= (useTmuOow & 3u) << 3;

  uint32_t texChromaEnabled = 0;
  if (m_texChromaMode[0] != 0) texChromaEnabled |= (1u << 0);
  if (m_texChromaMode[1] != 0) texChromaEnabled |= (1u << 1);
  flags |= (texChromaEnabled & 3u) << 5;

  uint32_t texChromaRangeMode = 0;
  if (m_texChromaRangeMode[0] != 0) texChromaRangeMode |= (1u << 0);
  if (m_texChromaRangeMode[1] != 0) texChromaRangeMode |= (1u << 1);
  flags |= (texChromaRangeMode & 3u) << 7;

  flags |= (m_ditherMode & 3u) << 9;
  flags |= (m_stippleMode & 3u) << 11;

  pcs.flags = flags;
  pcs.texDetail0 = PackTexDetail_Locked(0);
  pcs.texDetail1 = PackTexDetail_Locked(1);
  pcs.texLodBias0 = m_texLodBias[0];
  pcs.texLodBias1 = m_texLodBias[1];
  pcs.extColorCombine = m_extColorCombine;
  pcs.extAlphaCombine = m_extAlphaCombine;
  pcs.extTexColorCombine0 = m_extTexColorCombine[0];
  pcs.extTexAlphaCombine0 = m_extTexAlphaCombine[0];
  pcs.extTexColorCombine1 = m_extTexColorCombine[1];
  pcs.extTexAlphaCombine1 = m_extTexAlphaCombine[1];
  pcs.tmuConstantColor0 = packedTmuConst[0];
  pcs.tmuConstantColor1 = packedTmuConst[1];
}

}  // namespace GlideWrapper

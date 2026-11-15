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
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return;

  // Terminate active render pass if any (so next draw starts a new render pass
  // that triggers LoadOp Clear!)
  if (m_inRenderPass) {
    FlushBatch();
    m_commandBuffer->endRenderPass();
    m_inRenderPass = false;
  }

  GLIDE_LOG(DEBUG, "Vulkan",
            "ClearBuffer: color=" << std::hex << color
                                  << ", clearMask=" << clearMask);

  // 1. Defer CPU-side framebuffer clear lazily so pure 3D frames allocate 0
  // bytes and avoid CPU fills; materialized on demand before any LFB access.
  if (clearMask & 1) {
    m_lfbStaging.ScheduleLazyClear(m_activeRenderBuffer, color, alpha,
                                   m_pixelFormatOverride, m_colorMaskRgb,
                                   m_colorMaskAlpha);
  }

  // 2. Save clear state and mark pending for GPU clears when Render Pass
  // starts!
  uint32_t activeIdx = GetActiveGpuBufferIdx();
  if (clearMask & 1) {  // Color clear
    // Only schedule a GPU color clear if color or alpha writes are actually
    // enabled!
    if (m_colorMaskRgb || m_colorMaskAlpha) {
      float a = ((alpha > 0) ? alpha : 255) / 255.0f;
      float r = ((color >> 16) & 0xFF) / 255.0f;
      float g = ((color >> 8) & 0xFF) / 255.0f;
      float b = (color & 0xFF) / 255.0f;

      // Respect the active pixel format from GlideStateTracker!
      if (m_pixelFormatOverride == 1) {  // ABGR format override
        std::swap(r, b);
      }

      m_clearColorValue[activeIdx] =
          vk::ClearColorValue(std::array<float, 4>{r, g, b, a});
      m_clearColorPending[activeIdx] = true;
    }
  }

  if (clearMask & 2) {  // Depth clear
    m_clearDepthValue[activeIdx] = z;
    m_clearDepthPending[activeIdx] = true;
  }
}

void VulkanBackend::DrawPoint(const ModernVertex& pt) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return;

  FlushLFBToGPU();

  GLIDE_LOG(DEBUG, "Vulkan",
            "DrawPoint GPU draw. Pos: (" << pt.pos[0] << ", " << pt.pos[1]
                                         << ", " << pt.pos[2] << ")");

  // 1. Determine active pipeline & descriptor set
  vk::Pipeline reqPipeline = GetOrCreatePipeline(
      GetCurrentStateKey(), vk::PrimitiveTopology::ePointList);

  vk::ImageView view0 = m_dummyTexture.imageView.get();
  vk::Sampler sampler0 = m_dummySampler.get();
  if (m_boundTexAddress[0] != 0xFFFFFFFF) {
    auto it = m_vulkanTextures.find(m_boundTexAddress[0]);
    if (it != m_vulkanTextures.end()) {
      auto& tex = it->second;
      view0 = tex.imageView.get();
      sampler0 = GetOrCreateSampler(tex.clampS, tex.clampT, tex.minFilter,
                                    tex.magFilter, m_texLodBias[0],
                                    m_texMipMapMode[0], m_texLodBlend[0]);
    }
  }

  vk::ImageView view1 = m_dummyTexture.imageView.get();
  vk::Sampler sampler1 = m_dummySampler.get();
  if (m_boundTexAddress[1] != 0xFFFFFFFF) {
    auto it = m_vulkanTextures.find(m_boundTexAddress[1]);
    if (it != m_vulkanTextures.end()) {
      auto& tex = it->second;
      view1 = tex.imageView.get();
      sampler1 = GetOrCreateSampler(tex.clampS, tex.clampT, tex.minFilter,
                                    tex.magFilter, m_texLodBias[1],
                                    m_texMipMapMode[1], m_texLodBlend[1]);
    }
  }

  vk::DescriptorSet reqDescSet =
      GetOrCreateDescriptorSet(view0, sampler0, view1, sampler1);

  // 2. Check if we need to flush the active batch
  bool stateChanged =
      (reqPipeline != m_batchedPipeline || reqDescSet != m_batchedDescSet);
  bool batchEmpty = (m_batchedVertexCount == 0);
  bool bufferFull =
      (m_vertexBufferOffset + sizeof(ModernVertex) > m_vertexBufferSize);

  if (stateChanged || batchEmpty || bufferFull) {
    if (stateChanged || bufferFull) {
      FlushBatch();
    }
    if (bufferFull) {
      GLIDE_LOG(WARN, "Vulkan",
                "Dynamic Vertex Buffer wrap-around during DrawPoint! Stalling "
                "GPU to prevent geometry corruption.");
      FlushBatch();
      if (m_inRenderPass) {
        m_commandBuffer->endRenderPass();
        m_inRenderPass = false;
      }
      m_commandBuffer->end();

      vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, m_commandBuffer);
      m_graphicsQueue.submit(submitInfo, {});
      m_device->waitIdle();

      m_commandBuffer->reset({});
      m_commandBuffer->begin(vk::CommandBufferBeginInfo(
          vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

      m_vertexBufferOffset = 0;
    }

    m_batchedPipeline = reqPipeline;
    m_batchedDescSet = reqDescSet;
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
}

void VulkanBackend::DrawLine(const ModernVertex& v1, const ModernVertex& v2) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return;

  FlushLFBToGPU();

  GLIDE_LOG(DEBUG, "Vulkan",
            "DrawLine GPU draw. V1: (" << v1.pos[0] << ", " << v1.pos[1]
                                       << "), V2: (" << v2.pos[0] << ", "
                                       << v2.pos[1] << ")");

  // 1. Determine active pipeline & descriptor set
  vk::Pipeline reqPipeline = GetOrCreatePipeline(
      GetCurrentStateKey(), vk::PrimitiveTopology::eLineList);

  vk::ImageView view0 = m_dummyTexture.imageView.get();
  vk::Sampler sampler0 = m_dummySampler.get();
  if (m_boundTexAddress[0] != 0xFFFFFFFF) {
    auto it = m_vulkanTextures.find(m_boundTexAddress[0]);
    if (it != m_vulkanTextures.end()) {
      auto& tex = it->second;
      view0 = tex.imageView.get();
      sampler0 = GetOrCreateSampler(tex.clampS, tex.clampT, tex.minFilter,
                                    tex.magFilter, m_texLodBias[0],
                                    m_texMipMapMode[0], m_texLodBlend[0]);
    }
  }

  vk::ImageView view1 = m_dummyTexture.imageView.get();
  vk::Sampler sampler1 = m_dummySampler.get();
  if (m_boundTexAddress[1] != 0xFFFFFFFF) {
    auto it = m_vulkanTextures.find(m_boundTexAddress[1]);
    if (it != m_vulkanTextures.end()) {
      auto& tex = it->second;
      view1 = tex.imageView.get();
      sampler1 = GetOrCreateSampler(tex.clampS, tex.clampT, tex.minFilter,
                                    tex.magFilter, m_texLodBias[1],
                                    m_texMipMapMode[1], m_texLodBlend[1]);
    }
  }

  vk::DescriptorSet reqDescSet =
      GetOrCreateDescriptorSet(view0, sampler0, view1, sampler1);

  // 2. Check if we need to flush the active batch
  bool stateChanged =
      (reqPipeline != m_batchedPipeline || reqDescSet != m_batchedDescSet);
  bool batchEmpty = (m_batchedVertexCount == 0);
  bool bufferFull =
      (m_vertexBufferOffset + 2 * sizeof(ModernVertex) > m_vertexBufferSize);

  if (stateChanged || batchEmpty || bufferFull) {
    if (stateChanged || bufferFull) {
      FlushBatch();
    }
    if (bufferFull) {
      GLIDE_LOG(WARN, "Vulkan",
                "Dynamic Vertex Buffer wrap-around during DrawLine! Stalling "
                "GPU to prevent geometry corruption.");
      FlushBatch();
      if (m_inRenderPass) {
        m_commandBuffer->endRenderPass();
        m_inRenderPass = false;
      }
      m_commandBuffer->end();

      vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, m_commandBuffer);
      m_graphicsQueue.submit(submitInfo, {});
      m_device->waitIdle();

      m_commandBuffer->reset({});
      m_commandBuffer->begin(vk::CommandBufferBeginInfo(
          vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

      m_vertexBufferOffset = 0;
    }

    m_batchedPipeline = reqPipeline;
    m_batchedDescSet = reqDescSet;
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
}

void VulkanBackend::DrawTriangle(const ModernVertex& a, const ModernVertex& b,
                                 const ModernVertex& c) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_initialized || !m_windowAttached) return;

  FlushLFBToGPU();

  GLIDE_LOG(DEBUG, "Vulkan",
            "DrawTriangle GPU draw. A pos: ("
                << a.pos[0] << ", " << a.pos[1] << ", " << a.pos[2] << ", "
                << a.pos[3] << ") color: (" << a.color[0] << ", " << a.color[1]
                << ", " << a.color[2] << ", " << a.color[3] << ") tex: ("
                << a.tex[0] << ", " << a.tex[1] << ")");

  // 1. Determine active pipeline & descriptor set
  vk::Pipeline reqPipeline = GetOrCreatePipeline(
      GetCurrentStateKey(), vk::PrimitiveTopology::eTriangleList);

  vk::ImageView view0 = m_dummyTexture.imageView.get();
  vk::Sampler sampler0 = m_dummySampler.get();
  if (m_boundTexAddress[0] != 0xFFFFFFFF) {
    auto it = m_vulkanTextures.find(m_boundTexAddress[0]);
    if (it != m_vulkanTextures.end()) {
      auto& tex = it->second;
      view0 = tex.imageView.get();
      sampler0 = GetOrCreateSampler(tex.clampS, tex.clampT, tex.minFilter,
                                    tex.magFilter, m_texLodBias[0],
                                    m_texMipMapMode[0], m_texLodBlend[0]);
    }
  }

  vk::ImageView view1 = m_dummyTexture.imageView.get();
  vk::Sampler sampler1 = m_dummySampler.get();
  if (m_boundTexAddress[1] != 0xFFFFFFFF) {
    auto it = m_vulkanTextures.find(m_boundTexAddress[1]);
    if (it != m_vulkanTextures.end()) {
      auto& tex = it->second;
      view1 = tex.imageView.get();
      sampler1 = GetOrCreateSampler(tex.clampS, tex.clampT, tex.minFilter,
                                    tex.magFilter, m_texLodBias[1],
                                    m_texMipMapMode[1], m_texLodBlend[1]);
    }
  }

  vk::DescriptorSet reqDescSet =
      GetOrCreateDescriptorSet(view0, sampler0, view1, sampler1);

  // 2. Check if we need to flush the active batch
  bool stateChanged =
      (reqPipeline != m_batchedPipeline || reqDescSet != m_batchedDescSet);
  bool batchEmpty = (m_batchedVertexCount == 0);
  bool bufferFull =
      (m_vertexBufferOffset + 3 * sizeof(ModernVertex) > m_vertexBufferSize);

  if (stateChanged || batchEmpty || bufferFull) {
    if (stateChanged || bufferFull) {
      FlushBatch();
    }
    if (bufferFull) {
      GLIDE_LOG(WARN, "Vulkan",
                "Dynamic Vertex Buffer wrap-around during DrawTriangle! "
                "Stalling GPU to prevent geometry corruption.");
      FlushBatch();
      if (m_inRenderPass) {
        m_commandBuffer->endRenderPass();
        m_inRenderPass = false;
      }
      m_commandBuffer->end();

      vk::SubmitInfo submitInfo(0, nullptr, nullptr, 1, m_commandBuffer);
      m_graphicsQueue.submit(submitInfo, {});
      m_device->waitIdle();

      m_commandBuffer->reset({});
      m_commandBuffer->begin(vk::CommandBufferBeginInfo(
          vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

      m_vertexBufferOffset = 0;
    }

    m_batchedPipeline = reqPipeline;
    m_batchedDescSet = reqDescSet;
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
    dest[0] = a;
    dest[0].pos[1] = (float)m_headlessHeight - a.pos[1];
    dest[1] = b;
    dest[1].pos[1] = (float)m_headlessHeight - b.pos[1];
    dest[2] = c;
    dest[2].pos[1] = (float)m_headlessHeight - c.pos[1];
  } else {
    dest[0] = a;
    dest[1] = b;
    dest[2] = c;
  }

  // 4. Update batch & offset tracking
  m_batchedVertexCount += 3;
  m_vertexBufferOffset += 3 * sizeof(ModernVertex);
}

void VulkanBackend::FlushBatch() {
  if (m_batchedVertexCount == 0) return;

  GLIDE_PROFILE_SCOPE("Vulkan::FlushBatch");
  GLIDE_INCREMENT_TRIANGLES_DRAWN(m_batchedVertexCount / 3);

  GLIDE_LOG(DEBUG, "Vulkan",
            "FlushBatch: drawing "
                << m_batchedVertexCount
                << " vertices. FirstVertex=" << m_batchedFirstVertex);

  EnsureRenderPassActive();

  m_commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics,
                                m_batchedPipeline);
  m_commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                      m_pipelineLayout.get(), 0, 1,
                                      &m_batchedDescSet, 0, nullptr);

  PushConstants pcs{};
  PopulatePushConstants(pcs);
  m_commandBuffer->pushConstants<PushConstants>(
      m_pipelineLayout.get(),
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
      pcs);

  m_commandBuffer->draw(m_batchedVertexCount, 1, m_batchedFirstVertex, 0);

  m_batchedVertexCount = 0;
}

vk::Rect2D VulkanBackend::GetScissorRectVulkan() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  uint32_t vkScissorY = m_clipMinY;
  if (m_sstOrigin == 1) {  // GR_ORIGIN_LOWER_LEFT is 1 (bottom-left)
    // Flip Y to top-left coordinate system of Vulkan
    vkScissorY = m_headlessHeight - m_clipMaxY;
  }
  uint32_t vkScissorW =
      (m_clipMaxX > m_clipMinX) ? (m_clipMaxX - m_clipMinX) : 0;
  uint32_t vkScissorH =
      (m_clipMaxY > m_clipMinY) ? (m_clipMaxY - m_clipMinY) : 0;

  vk::Offset2D offset(static_cast<int32_t>(m_clipMinX),
                      static_cast<int32_t>(vkScissorY));
  vk::Extent2D extent(vkScissorW, vkScissorH);
  return vk::Rect2D(offset, extent);
}

void VulkanBackend::EnsureRenderPassActive() {
  if (m_inRenderPass) return;

  // Begin render pass with eLoad (no automatic clear)
  vk::RenderPassBeginInfo renderPassInfo(
      m_renderPass.get(), m_headlessFramebuffers[GetActiveGpuBufferIdx()].get(),
      vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0,
      nullptr);  // 0 clear values since we use eLoad!
  m_commandBuffer->beginRenderPass(renderPassInfo,
                                   vk::SubpassContents::eInline);

  // Configure Dynamic Viewport & Scissor
  vk::Viewport viewport(0.0f, 0.0f, (float)m_headlessWidth,
                        (float)m_headlessHeight, 0.0f, 1.0f);
  m_commandBuffer->setViewport(0, 1, &viewport);

  vk::Rect2D scissor = GetScissorRectVulkan();
  m_commandBuffer->setScissor(0, 1, &scissor);

  // Bind vertex buffer
  vk::DeviceSize zeroOffset = 0;
  m_commandBuffer->bindVertexBuffers(0, 1, &m_vertexBuffer.get(), &zeroOffset);

  m_inRenderPass = true;

  // Perform explicit clears using vkCmdClearAttachments if pending on the
  // active FBO!
  std::vector<vk::ClearAttachment> clears;
  uint32_t activeIdx = GetActiveGpuBufferIdx();
  if (m_clearColorPending[activeIdx]) {
    clears.push_back(vk::ClearAttachment(vk::ImageAspectFlagBits::eColor, 0,
                                         m_clearColorValue[activeIdx]));
    m_clearColorPending[activeIdx] = false;
  }
  if (m_clearDepthPending[activeIdx]) {
    clears.push_back(
        vk::ClearAttachment(vk::ImageAspectFlagBits::eDepth, 0,
                            vk::ClearValue(vk::ClearDepthStencilValue(
                                m_clearDepthValue[activeIdx], 0))));
    m_clearDepthPending[activeIdx] = false;
  }

  if (!clears.empty()) {
    vk::ClearRect clearRect(
        vk::Rect2D({0, 0}, {m_headlessWidth, m_headlessHeight}), 0, 1);
    m_commandBuffer->clearAttachments(static_cast<uint32_t>(clears.size()),
                                      clears.data(), 1, &clearRect);
    GLIDE_LOG(DEBUG, "Vulkan",
              "Explicitly cleared " << clears.size()
                                    << " attachments inside render pass.");
  }
}

void VulkanBackend::PopulatePushConstants(PushConstants& pcs) {
  pcs.viewportWidth = (float)m_guestWidth;
  pcs.viewportHeight = (float)m_guestHeight;
  pcs.depthBufferMode = m_depthMode;

  // Extract and normalize constant color components, then pack
  float constR = ((m_constantColor >> 16) & 0xFF) / 255.0f;
  float constG = ((m_constantColor >> 8) & 0xFF) / 255.0f;
  float constB = (m_constantColor & 0xFF) / 255.0f;
  float constA = ((m_constantColor >> 24) & 0xFF) / 255.0f;
  if (m_pixelFormatOverride == 1) {
    std::swap(constR, constB);
  }
  pcs.constantColor = PackRGBA(constR, constG, constB, constA);

  pcs.alphaTestOp = m_alphaTestOp;
  pcs.alphaTestRef = (float)m_alphaTestRefVal / 255.0f;
  pcs.depthBias = static_cast<float>(m_depthBiasLevel) / 65535.0f;

  // Milestone 8: Fogging State (packed 128-byte layout)
  pcs.fogMode = m_fogMode;

  // Swizzle fog color if format override is active
  uint32_t packedFogColor = m_fogColor;
  if (m_pixelFormatOverride == 1) {
    uint32_t r = (m_fogColor >> 16) & 0xFF;
    uint32_t g = (m_fogColor >> 8) & 0xFF;
    uint32_t b = m_fogColor & 0xFF;
    uint32_t a = (m_fogColor >> 24) & 0xFF;
    packedFogColor = (a << 24) | (b << 16) | (g << 8) | r;
  }
  pcs.fogColor = packedFogColor;

  // Pack 64-entry 8-bit fog table into 16 uint32_t values
  for (int i = 0; i < 16; ++i) {
    uint32_t word = 0;
    word |= static_cast<uint32_t>(m_fogTable[i * 4 + 0]) << 0;
    word |= static_cast<uint32_t>(m_fogTable[i * 4 + 1]) << 8;
    word |= static_cast<uint32_t>(m_fogTable[i * 4 + 2]) << 16;
    word |= static_cast<uint32_t>(m_fogTable[i * 4 + 3]) << 24;
    pcs.fogTable[i] = word;
  }

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
  if (m_boundTexAddress[0] != 0xFFFFFFFF) textureEnabled |= (1u << 0);
  if (m_boundTexAddress[1] != 0xFFFFFFFF) textureEnabled |= (1u << 1);
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

  float cValR = ((m_chromakeyValue >> 16) & 0xFF) / 255.0f;
  float cValG = ((m_chromakeyValue >> 8) & 0xFF) / 255.0f;
  float cValB = (m_chromakeyValue & 0xFF) / 255.0f;
  float cValA = ((m_chromakeyValue >> 24) & 0xFF) / 255.0f;

  float cMinR = ((m_chromakeyRangeMin >> 16) & 0xFF) / 255.0f;
  float cMinG = ((m_chromakeyRangeMin >> 8) & 0xFF) / 255.0f;
  float cMinB = (m_chromakeyRangeMin & 0xFF) / 255.0f;
  float cMinA = ((m_chromakeyRangeMin >> 24) & 0xFF) / 255.0f;

  float cMaxR = ((m_chromakeyRangeMax >> 16) & 0xFF) / 255.0f;
  float cMaxG = ((m_chromakeyRangeMax >> 8) & 0xFF) / 255.0f;
  float cMaxB = (m_chromakeyRangeMax & 0xFF) / 255.0f;
  float cMaxA = ((m_chromakeyRangeMax >> 24) & 0xFF) / 255.0f;

  if (m_pixelFormatOverride == 1) {
    std::swap(cValR, cValB);
    std::swap(cMinR, cMinB);
    std::swap(cMaxR, cMaxB);
  }

  pcs.chromakeyValue = PackRGBA(cValR, cValG, cValB, cValA);
  pcs.chromakeyRangeMin = PackRGBA(cMinR, cMinG, cMinB, cMinA);
  pcs.chromakeyRangeMax = PackRGBA(cMaxR, cMaxG, cMaxB, cMaxA);

  // Populate texture-specific chromakeying states for TMU0 and TMU1
  for (int i = 0; i < 2; ++i) {
    float minR = ((m_texChromaMin[i] >> 16) & 0xFF) / 255.0f;
    float minG = ((m_texChromaMin[i] >> 8) & 0xFF) / 255.0f;
    float minB = (m_texChromaMin[i] & 0xFF) / 255.0f;
    float minA = ((m_texChromaMin[i] >> 24) & 0xFF) / 255.0f;

    float maxR = ((m_texChromaMax[i] >> 16) & 0xFF) / 255.0f;
    float maxG = ((m_texChromaMax[i] >> 8) & 0xFF) / 255.0f;
    float maxB = (m_texChromaMax[i] & 0xFF) / 255.0f;
    float maxA = ((m_texChromaMax[i] >> 24) & 0xFF) / 255.0f;

    if (m_pixelFormatOverride == 1) {
      std::swap(minR, minB);
      std::swap(maxR, maxB);
    }

    pcs.texChromaMin[i][0] = minR;
    pcs.texChromaMin[i][1] = minG;
    pcs.texChromaMin[i][2] = minB;
    pcs.texChromaMin[i][3] = minA;

    pcs.texChromaMax[i][0] = maxR;
    pcs.texChromaMax[i][1] = maxG;
    pcs.texChromaMax[i][2] = maxB;
    pcs.texChromaMax[i][3] = maxA;
  }

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
}

}  // namespace GlideWrapper

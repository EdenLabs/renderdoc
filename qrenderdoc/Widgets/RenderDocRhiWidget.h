/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#pragma once

#if defined(RENDERDOC_WAYLAND_UI)

#include <QRhiWidget>
#include <vulkan/vulkan.h>
#include <rhi/qrhi.h>

// Displays a dmabuf-backed VkImage exported by RenderDoc's replay driver.
//
// Replaces CustomPaintWidgetInternal + WA_PaintOnScreen on Wayland to avoid
// creating wl_subsurfaces. The widget renders a fullscreen textured quad
// sampling from the imported dmabuf image.
class RenderDocRhiWidget : public QRhiWidget
{
  Q_OBJECT

public:
  explicit RenderDocRhiWidget(QWidget *parent = nullptr);
  ~RenderDocRhiWidget();

  // Update the dmabuf source. Called after IReplayOutput::Display() completes.
  // The fd is NOT owned by this widget — do not close it.
  void setDmabuf(int fd, uint32_t width, uint32_t height, uint32_t stride);

protected:
  void initialize(QRhiCommandBuffer *cb) override;
  void render(QRhiCommandBuffer *cb) override;

private:
  void importDmabuf();
  void cleanupImport();

  // Dmabuf source parameters.
  int m_fd          = -1;
  uint32_t m_srcWidth  = 0;
  uint32_t m_srcHeight = 0;
  uint32_t m_srcStride = 0;

  // Tracks whether the source changed since last import.
  int m_importedFd  = -1;
  uint32_t m_importedWidth  = 0;
  uint32_t m_importedHeight = 0;

  // Vulkan handles for the imported image (on Qt's device).
  VkDevice m_vkDevice                         = VK_NULL_HANDLE;
  PFN_vkGetDeviceProcAddr m_vkGetDeviceProcAddr = nullptr;
  VkImage m_importedImage                     = VK_NULL_HANDLE;
  VkDeviceMemory m_importedMem                 = VK_NULL_HANDLE;

  // QRhi resources for the fullscreen quad.
  QRhiTexture *m_texture           = nullptr;
  QRhiSampler *m_sampler           = nullptr;
  QRhiBuffer *m_vertexBuf          = nullptr;
  QRhiShaderResourceBindings *m_srb = nullptr;
  QRhiGraphicsPipeline *m_pipeline  = nullptr;

  bool m_pipelineInitialized = false;
};

#endif    // RENDERDOC_WAYLAND_UI

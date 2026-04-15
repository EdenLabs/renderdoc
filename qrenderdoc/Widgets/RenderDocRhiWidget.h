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

#include <vector>
#include <QRhiWidget>
#include <vulkan/vulkan.h>
#include <rhi/qrhi.h>

class CustomPaintWidget;

// Displays a dmabuf-backed VkImage exported by RenderDoc's replay driver.
//
// Replaces CustomPaintWidgetInternal + WA_PaintOnScreen on Wayland to avoid
// creating wl_subsurfaces. The widget renders a fullscreen textured quad
// sampling from the imported dmabuf image. Input events are forwarded to the
// owning CustomPaintWidget's signals so callers see the same signal flow as
// the non-Wayland CustomPaintWidgetInternal path.
class RenderDocRhiWidget : public QRhiWidget
{
  Q_OBJECT

public:
  explicit RenderDocRhiWidget(CustomPaintWidget *custom);
  ~RenderDocRhiWidget();

  // Update the dmabuf source. Called after IReplayOutput::Display() completes.
  // The fd is NOT owned by this widget — do not close it.
  void setDmabuf(int fd, uint32_t width, uint32_t height, uint32_t stride);

protected:
  void initialize(QRhiCommandBuffer *cb) override;
  void render(QRhiCommandBuffer *cb) override;

  void mousePressEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void mouseDoubleClickEvent(QMouseEvent *e) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void wheelEvent(QWheelEvent *e) override;
  void resizeEvent(QResizeEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  void keyReleaseEvent(QKeyEvent *e) override;

private:
  void importDmabuf();
  void cleanupImport();

  CustomPaintWidget *m_Custom;

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

  // Deferred destruction of old imports. Qt's command buffers may still
  // reference the previous VkImage/Memory after we've moved on to a new fd
  // (e.g. after a resize); destroying immediately produces GPUVM faults, and
  // vkDeviceWaitIdle stalls the desktop compositor (Qt shares the device).
  // Hold each retired pair for a few frames before destroying.
  struct RetiredImport
  {
    VkImage img;
    VkDeviceMemory mem;
    int framesAlive;
  };
  std::vector<RetiredImport> m_RetiredImports;

  // QRhi resources for the fullscreen quad.
  QRhiTexture *m_texture           = nullptr;
  QRhiSampler *m_sampler           = nullptr;
  QRhiBuffer *m_vertexBuf          = nullptr;
  QRhiShaderResourceBindings *m_srb = nullptr;
  QRhiGraphicsPipeline *m_pipeline  = nullptr;

  bool m_pipelineInitialized = false;
};

#endif    // RENDERDOC_WAYLAND_UI

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

#if defined(RENDERDOC_WAYLAND_UI)

#include "RenderDocRhiWidget.h"
#include <unistd.h>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <rhi/qshaderbaker.h>
#include "CustomPaintWidget.h"

// Fullscreen triangle vertex shader. Generates a fullscreen triangle from
// gl_VertexIndex without a vertex buffer. Baked at runtime via QShaderBaker so
// QRhi gets the reflection metadata it needs to build descriptor set layouts.
static const char *s_vertGlsl =
    "#version 440\n"
    "layout(location = 0) out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);\n"
    "    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

// Fullscreen texture sample fragment shader. Samples the sRGB-format source
// (sampler does sRGB->linear), then re-encodes to sRGB before output. Qt's
// QRhiWidget render target is UNORM and Qt treats the resulting bytes as
// sRGB-encoded for compositing — without manual encoding we'd display linear
// values as if they were sRGB, producing the characteristic darkening.
static const char *s_fragGlsl =
    "#version 440\n"
    "layout(location = 0) in vec2 v_uv;\n"
    "layout(location = 0) out vec4 fragColor;\n"
    "layout(binding = 0) uniform sampler2D tex;\n"
    "vec3 linearToSrgb(vec3 c) {\n"
    "    bvec3 cutoff = lessThan(c, vec3(0.0031308));\n"
    "    vec3 hi = 1.055 * pow(c, vec3(1.0/2.4)) - 0.055;\n"
    "    vec3 lo = c * 12.92;\n"
    "    return mix(hi, lo, vec3(cutoff));\n"
    "}\n"
    "void main() {\n"
    "    vec4 s = texture(tex, v_uv);\n"
    "    fragColor = vec4(linearToSrgb(s.rgb), s.a);\n"
    "}\n";

static QShader bakeShader(const char *glslSource, QShader::Stage stage)
{
  QShaderBaker baker;
  baker.setSourceString(QByteArray(glslSource), stage);
  baker.setGeneratedShaderVariants({QShader::StandardShader});
  baker.setGeneratedShaders({{QShader::SpirvShader, QShaderVersion(100)}});
  return baker.bake();
}

RenderDocRhiWidget::RenderDocRhiWidget(CustomPaintWidget *custom)
    : QRhiWidget(custom), m_Custom(custom)
{
  setApi(QRhiWidget::Api::Vulkan);
  // StrongFocus so click+tab can focus this widget — the mesh viewer's flycam
  // (WASD) requires keyPressEvent delivery, and Qt only delivers keys to the
  // currently focused widget.
  setFocusPolicy(Qt::StrongFocus);
}

RenderDocRhiWidget::~RenderDocRhiWidget()
{
  cleanupImport();
}

void RenderDocRhiWidget::setDmabuf(int fd, uint32_t width, uint32_t height, uint32_t stride)
{
  m_fd = fd;
  m_srcWidth = width;
  m_srcHeight = height;
  m_srcStride = stride;

  QRhiWidget::update();
}

void RenderDocRhiWidget::cleanupImport()
{
  // Qt may still have in-flight GPU work referencing m_importedImage. Defer
  // the actual destruction by parking the resources on a retire list, then
  // sweep the list in render() once enough frames have passed.
  if(m_importedImage != VK_NULL_HANDLE)
    m_RetiredImports.push_back({m_importedImage, m_importedMem, 0});

  m_importedImage = VK_NULL_HANDLE;
  m_importedMem = VK_NULL_HANDLE;
  m_importedFd = -1;
  m_importedWidth = 0;
  m_importedHeight = 0;

  // Pipeline references the old texture, must be rebuilt.
  delete m_pipeline;
  m_pipeline = nullptr;

  delete m_srb;
  m_srb = nullptr;

  delete m_texture;
  m_texture = nullptr;
}

void RenderDocRhiWidget::importDmabuf()
{
  if(m_fd < 0 || m_srcWidth == 0 || m_srcHeight == 0)
    return;

  cleanupImport();

  QRhi *r = rhi();
  if(!r)
    return;

  // Get Qt's Vulkan device handles.
  const QRhiVulkanNativeHandles *vkHandles =
      static_cast<const QRhiVulkanNativeHandles *>(r->nativeHandles());
  if(!vkHandles || !vkHandles->dev || !vkHandles->inst)
    return;

  m_vkDevice = vkHandles->dev;
  VkPhysicalDevice physDev = vkHandles->physDev;

  m_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)vkHandles->inst->getInstanceProcAddr(
      "vkGetDeviceProcAddr");
  if(!m_vkGetDeviceProcAddr)
    return;

  auto pfnCreateImage =
      (PFN_vkCreateImage)m_vkGetDeviceProcAddr(m_vkDevice, "vkCreateImage");
  auto pfnAllocateMemory =
      (PFN_vkAllocateMemory)m_vkGetDeviceProcAddr(m_vkDevice, "vkAllocateMemory");
  auto pfnBindImageMemory =
      (PFN_vkBindImageMemory)m_vkGetDeviceProcAddr(m_vkDevice, "vkBindImageMemory");
  auto pfnGetImageMemoryRequirements =
      (PFN_vkGetImageMemoryRequirements)m_vkGetDeviceProcAddr(m_vkDevice, "vkGetImageMemoryRequirements");
  auto pfnGetPhysicalDeviceMemoryProperties =
      (PFN_vkGetPhysicalDeviceMemoryProperties)vkHandles->inst->getInstanceProcAddr(
          "vkGetPhysicalDeviceMemoryProperties");

  if(!pfnCreateImage || !pfnAllocateMemory || !pfnBindImageMemory ||
     !pfnGetImageMemoryRequirements || !pfnGetPhysicalDeviceMemoryProperties)
    return;

  // Create image with external memory.
  VkExternalMemoryImageCreateInfo extImgInfo = {
      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      nullptr,
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
  };

  VkImageCreateInfo imInfo = {
      VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      &extImgInfo,
      0,
      VK_IMAGE_TYPE_2D,
      VK_FORMAT_R8G8B8A8_SRGB,
      {m_srcWidth, m_srcHeight, 1},
      1,
      1,
      VK_SAMPLE_COUNT_1_BIT,
      VK_IMAGE_TILING_LINEAR,
      VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_SHARING_MODE_EXCLUSIVE,
      0,
      nullptr,
      VK_IMAGE_LAYOUT_UNDEFINED,
  };

  VkResult vkr = pfnCreateImage(m_vkDevice, &imInfo, nullptr, &m_importedImage);
  if(vkr != VK_SUCCESS)
  {
    m_importedImage = VK_NULL_HANDLE;
    return;
  }

  // Find a suitable memory type.
  VkMemoryRequirements memReq = {};
  pfnGetImageMemoryRequirements(m_vkDevice, m_importedImage, &memReq);

  VkPhysicalDeviceMemoryProperties memProps = {};
  pfnGetPhysicalDeviceMemoryProperties(physDev, &memProps);

  uint32_t memTypeIndex = UINT32_MAX;
  for(uint32_t i = 0; i < memProps.memoryTypeCount; i++)
  {
    if(memReq.memoryTypeBits & (1u << i))
    {
      memTypeIndex = i;
      break;
    }
  }

  if(memTypeIndex == UINT32_MAX)
  {
    auto pfnDestroyImage =
        (PFN_vkDestroyImage)m_vkGetDeviceProcAddr(m_vkDevice, "vkDestroyImage");
    pfnDestroyImage(m_vkDevice, m_importedImage, nullptr);
    m_importedImage = VK_NULL_HANDLE;
    return;
  }

  // Import the dmabuf. dup() because Vulkan takes ownership of the fd.
  VkImportMemoryFdInfoKHR importInfo = {
      VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      nullptr,
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      ::dup(m_fd),
  };

  VkMemoryAllocateInfo allocInfo = {
      VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      &importInfo,
      memReq.size,
      memTypeIndex,
  };

  vkr = pfnAllocateMemory(m_vkDevice, &allocInfo, nullptr, &m_importedMem);
  if(vkr != VK_SUCCESS)
  {
    ::close(importInfo.fd);
    auto pfnDestroyImage =
        (PFN_vkDestroyImage)m_vkGetDeviceProcAddr(m_vkDevice, "vkDestroyImage");
    pfnDestroyImage(m_vkDevice, m_importedImage, nullptr);
    m_importedImage = VK_NULL_HANDLE;
    return;
  }

  vkr = pfnBindImageMemory(m_vkDevice, m_importedImage, m_importedMem, 0);
  if(vkr != VK_SUCCESS)
  {
    auto pfnDestroyImage =
        (PFN_vkDestroyImage)m_vkGetDeviceProcAddr(m_vkDevice, "vkDestroyImage");
    auto pfnFreeMemory =
        (PFN_vkFreeMemory)m_vkGetDeviceProcAddr(m_vkDevice, "vkFreeMemory");
    pfnFreeMemory(m_vkDevice, m_importedMem, nullptr);
    pfnDestroyImage(m_vkDevice, m_importedImage, nullptr);
    m_importedImage = VK_NULL_HANDLE;
    m_importedMem = VK_NULL_HANDLE;
    return;
  }

  // Wrap the imported VkImage in a QRhiTexture. The exporter creates the image
  // as VK_FORMAT_R8G8B8A8_SRGB so QRhi must use a matching view format —
  // sampling an SRGB image through a UNORM view (or vice versa) without
  // VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT is invalid and reads zero on radv.
  // The exporter leaves the image in VK_IMAGE_LAYOUT_GENERAL — the only layout
  // valid for cross-device sampling without explicit ownership transfer.
  m_texture = r->newTexture(QRhiTexture::RGBA8, QSize(m_srcWidth, m_srcHeight), 1,
                            QRhiTexture::sRGB);
  if(!m_texture->createFrom({(quint64)m_importedImage, VK_IMAGE_LAYOUT_GENERAL}))
  {
    delete m_texture;
    m_texture = nullptr;
    return;
  }

  m_importedFd = m_fd;
  m_importedWidth = m_srcWidth;
  m_importedHeight = m_srcHeight;
}

void RenderDocRhiWidget::initialize(QRhiCommandBuffer *cb)
{
  if(m_pipelineInitialized)
    return;

  QRhi *r = rhi();

  m_sampler = r->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                            QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
  m_sampler->create();

  m_pipelineInitialized = true;
}

void RenderDocRhiWidget::render(QRhiCommandBuffer *cb)
{
  // Sweep the retire list — destroy entries that have been retired long enough
  // for any pending Qt frame referencing them to have completed. Bound is
  // intentionally generous; memory cost per retired entry is one VkImage's
  // worth of dmabuf-backed memory, well under a few MB.
  static constexpr int kRetireFrames = 60;
  if(!m_RetiredImports.empty() && m_vkDevice != VK_NULL_HANDLE)
  {
    PFN_vkDestroyImage pfnDestroyImage =
        (PFN_vkDestroyImage)m_vkGetDeviceProcAddr(m_vkDevice, "vkDestroyImage");
    PFN_vkFreeMemory pfnFreeMemory =
        (PFN_vkFreeMemory)m_vkGetDeviceProcAddr(m_vkDevice, "vkFreeMemory");

    for(auto it = m_RetiredImports.begin(); it != m_RetiredImports.end();)
    {
      it->framesAlive++;
      if(it->framesAlive > kRetireFrames)
      {
        if(pfnDestroyImage)
          pfnDestroyImage(m_vkDevice, it->img, nullptr);
        if(pfnFreeMemory)
          pfnFreeMemory(m_vkDevice, it->mem, nullptr);
        it = m_RetiredImports.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }

  if(m_fd < 0)
    return;

  // Reimport if source changed.
  if(m_fd != m_importedFd || m_srcWidth != m_importedWidth || m_srcHeight != m_importedHeight)
    importDmabuf();

  if(!m_texture)
    return;

  QRhi *r = rhi();
  const QSize outputSize = renderTarget()->pixelSize();

  // Rebuild SRB and pipeline if needed.
  if(!m_pipeline)
  {
    m_srb = r->newShaderResourceBindings();
    m_srb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(
            0, QRhiShaderResourceBinding::FragmentStage, m_texture, m_sampler),
    });
    m_srb->create();

    m_pipeline = r->newGraphicsPipeline();
    m_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);

    QShader vs = bakeShader(s_vertGlsl, QShader::VertexStage);
    QShader fs = bakeShader(s_fragGlsl, QShader::FragmentStage);

    m_pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vs},
        {QRhiShaderStage::Fragment, fs},
    });

    m_pipeline->setShaderResourceBindings(m_srb);
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_pipeline->create();
  }

  // The exporting device left the image in GENERAL after its flush.
  m_texture->setNativeLayout(VK_IMAGE_LAYOUT_GENERAL);

  cb->beginPass(renderTarget(), QColor::fromRgbF(0.0f, 0.0f, 0.0f, 1.0f), {1.0f, 0});
  cb->setGraphicsPipeline(m_pipeline);
  cb->setViewport({0, 0, (float)outputSize.width(), (float)outputSize.height()});
  cb->setShaderResources(m_srb);
  cb->draw(3);
  cb->endPass();
}

// Mirror CustomPaintWidgetInternal's signal forwarding so the TextureViewer's
// connections on CustomPaintWidget see input regardless of which inner widget
// is in use.
void RenderDocRhiWidget::mousePressEvent(QMouseEvent *e)
{
  emit m_Custom->clicked(e);
}

void RenderDocRhiWidget::mouseReleaseEvent(QMouseEvent *e)
{
  emit m_Custom->unclicked(e);
}

void RenderDocRhiWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
  emit m_Custom->doubleClicked(e);
}

void RenderDocRhiWidget::mouseMoveEvent(QMouseEvent *e)
{
  emit m_Custom->mouseMove(e);
}

void RenderDocRhiWidget::wheelEvent(QWheelEvent *e)
{
  emit m_Custom->mouseWheel(e);
}

void RenderDocRhiWidget::resizeEvent(QResizeEvent *e)
{
  QRhiWidget::resizeEvent(e);
  emit m_Custom->resize(e);
}

void RenderDocRhiWidget::keyPressEvent(QKeyEvent *e)
{
  emit m_Custom->keyPress(e);
}

void RenderDocRhiWidget::keyReleaseEvent(QKeyEvent *e)
{
  emit m_Custom->keyRelease(e);
}

#endif    // RENDERDOC_WAYLAND_UI

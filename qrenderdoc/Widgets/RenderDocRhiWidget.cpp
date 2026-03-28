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
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

// Fullscreen triangle vertex shader (SPIR-V).
// Generates a fullscreen triangle from gl_VertexIndex without a vertex buffer.
//
// #version 450
// layout(location = 0) out vec2 v_uv;
// void main() {
//     v_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
//     gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
//     v_uv.y = 1.0 - v_uv.y;
// }
static const uint32_t s_vertSpirv[] = {
    0x07230203, 0x00010000, 0x000d000a, 0x00000025, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0008000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000c, 0x00000019,
    0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d, 0x00000000, 0x00040005,
    0x00000009, 0x76755f76, 0x00000000, 0x00060005, 0x0000000c, 0x565f6c67, 0x65747265, 0x646e4978,
    0x00007865, 0x00060005, 0x00000017, 0x505f6c67, 0x65567265, 0x78657472, 0x00000000, 0x00060006,
    0x00000017, 0x00000000, 0x505f6c67, 0x7469736f, 0x006e6f69, 0x00070006, 0x00000017, 0x00000001,
    0x505f6c67, 0x746e696f, 0x657a6953, 0x00000000, 0x00070006, 0x00000017, 0x00000002, 0x435f6c67,
    0x4470696c, 0x61747369, 0x0065636e, 0x00070006, 0x00000017, 0x00000003, 0x435f6c67, 0x446c6c75,
    0x61747369, 0x0065636e, 0x00030005, 0x00000019, 0x00000000, 0x00040047, 0x00000009, 0x0000001e,
    0x00000000, 0x00040047, 0x0000000c, 0x0000000b, 0x0000002a, 0x00050048, 0x00000017, 0x00000000,
    0x0000000b, 0x00000000, 0x00050048, 0x00000017, 0x00000001, 0x0000000b, 0x00000001, 0x00050048,
    0x00000017, 0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x00000017, 0x00000003, 0x0000000b,
    0x00000004, 0x00030047, 0x00000017, 0x00000002, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
    0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000002,
    0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003,
    0x00040015, 0x0000000a, 0x00000020, 0x00000001, 0x00040020, 0x0000000b, 0x00000001, 0x0000000a,
    0x0004003b, 0x0000000b, 0x0000000c, 0x00000001, 0x00040015, 0x0000000d, 0x00000020, 0x00000000,
    0x0004002b, 0x0000000a, 0x0000000e, 0x00000001, 0x0004002b, 0x0000000d, 0x00000010, 0x00000002,
    0x00040017, 0x00000015, 0x00000006, 0x00000004, 0x0004002b, 0x0000000d, 0x00000016, 0x00000001,
    0x0006001e, 0x00000017, 0x00000015, 0x00000006, 0x00000016, 0x00000016, 0x00040020, 0x00000018,
    0x00000003, 0x00000017, 0x0004003b, 0x00000018, 0x00000019, 0x00000003, 0x0004002b, 0x0000000a,
    0x0000001a, 0x00000000, 0x0004002b, 0x00000006, 0x0000001c, 0x40000000, 0x0004002b, 0x00000006,
    0x0000001e, 0x3f800000, 0x0004002b, 0x00000006, 0x00000020, 0x00000000, 0x00040020, 0x00000023,
    0x00000003, 0x00000015, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8,
    0x00000005, 0x0004003d, 0x0000000a, 0x000000a0, 0x0000000c, 0x000500c4, 0x0000000a, 0x000000a1,
    0x000000a0, 0x0000000e, 0x000500c7, 0x0000000a, 0x000000a2, 0x000000a1, 0x0000000e, 0x00040070,
    0x00000006, 0x000000a3, 0x000000a2, 0x000500c7, 0x0000000a, 0x000000a4, 0x000000a0, 0x0000000e,
    0x00040070, 0x00000006, 0x000000a5, 0x000000a4, 0x00050050, 0x00000007, 0x000000a6, 0x000000a3,
    0x000000a5, 0x0003003e, 0x00000009, 0x000000a6, 0x0004003d, 0x00000007, 0x000000a7, 0x00000009,
    0x0005008e, 0x00000007, 0x000000a8, 0x000000a7, 0x0000001c, 0x00050050, 0x00000007, 0x000000a9,
    0x0000001e, 0x0000001e, 0x00050083, 0x00000007, 0x000000aa, 0x000000a8, 0x000000a9, 0x00050051,
    0x00000006, 0x000000ab, 0x000000aa, 0x00000000, 0x00050051, 0x00000006, 0x000000ac, 0x000000aa,
    0x00000001, 0x00070050, 0x00000015, 0x000000ad, 0x000000ab, 0x000000ac, 0x00000020, 0x0000001e,
    0x00050041, 0x00000023, 0x000000ae, 0x00000019, 0x0000001a, 0x0003003e, 0x000000ae, 0x000000ad,
    0x0004003d, 0x00000007, 0x000000af, 0x00000009, 0x00050051, 0x00000006, 0x000000b0, 0x000000af,
    0x00000001, 0x00050083, 0x00000006, 0x000000b1, 0x0000001e, 0x000000b0, 0x00050051, 0x00000006,
    0x000000b2, 0x000000af, 0x00000000, 0x00050050, 0x00000007, 0x000000b3, 0x000000b2, 0x000000b1,
    0x0003003e, 0x00000009, 0x000000b3, 0x000100fd, 0x00010038,
};

// Fullscreen texture sample fragment shader (SPIR-V).
//
// #version 450
// layout(location = 0) in vec2 v_uv;
// layout(location = 0) out vec4 fragColor;
// layout(binding = 0) uniform sampler2D tex;
// void main() {
//     fragColor = texture(tex, v_uv);
// }
static const uint32_t s_fragSpirv[] = {
    0x07230203, 0x00010000, 0x000d000a, 0x00000013, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0007000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x0000000d, 0x00030010,
    0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d,
    0x00000000, 0x00050005, 0x00000009, 0x67617266, 0x6f6c6f43, 0x00000072, 0x00030005, 0x0000000b,
    0x00786574, 0x00040005, 0x0000000d, 0x76755f76, 0x00000000, 0x00040047, 0x00000009, 0x0000001e,
    0x00000000, 0x00040047, 0x0000000b, 0x00000022, 0x00000000, 0x00040047, 0x0000000b, 0x00000021,
    0x00000000, 0x00040047, 0x0000000d, 0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021,
    0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006,
    0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007, 0x0004003b, 0x00000008, 0x00000009,
    0x00000003, 0x00090019, 0x0000000a, 0x00000006, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
    0x00000001, 0x00000000, 0x0003001b, 0x0000000f, 0x0000000a, 0x00040020, 0x00000010, 0x00000000,
    0x0000000f, 0x0004003b, 0x00000010, 0x0000000b, 0x00000000, 0x00040017, 0x0000000c, 0x00000006,
    0x00000002, 0x00040020, 0x00000011, 0x00000001, 0x0000000c, 0x0004003b, 0x00000011, 0x0000000d,
    0x00000001, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
    0x0004003d, 0x0000000f, 0x00000012, 0x0000000b, 0x0004003d, 0x0000000c, 0x000000e0, 0x0000000d,
    0x00050057, 0x00000007, 0x000000e1, 0x00000012, 0x000000e0, 0x0003003e, 0x00000009, 0x000000e1,
    0x000100fd, 0x00010038,
};

RenderDocRhiWidget::RenderDocRhiWidget(QWidget *parent) : QRhiWidget(parent)
{
  setApi(QRhiWidget::Api::Vulkan);
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
  if(m_importedImage != VK_NULL_HANDLE && m_vkDevice != VK_NULL_HANDLE)
  {
    PFN_vkDestroyImage pfnDestroyImage =
        (PFN_vkDestroyImage)m_vkGetDeviceProcAddr(m_vkDevice, "vkDestroyImage");
    PFN_vkFreeMemory pfnFreeMemory =
        (PFN_vkFreeMemory)m_vkGetDeviceProcAddr(m_vkDevice, "vkFreeMemory");

    if(pfnDestroyImage)
      pfnDestroyImage(m_vkDevice, m_importedImage, nullptr);
    if(pfnFreeMemory)
      pfnFreeMemory(m_vkDevice, m_importedMem, nullptr);
  }

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

  // Wrap the imported VkImage in a QRhiTexture.
  m_texture = r->newTexture(QRhiTexture::RGBA8, QSize(m_srcWidth, m_srcHeight), 1,
                            QRhiTexture::UsedWithLoadStore);
  if(!m_texture->createFrom({(quint64)m_importedImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}))
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

    // Build QShader wrappers for embedded SPIR-V.
    QShader vs;
    vs.setStage(QShader::VertexStage);
    vs.setShader({QShader::SpirvShader, QShaderVersion(100)},
                 QShaderCode(QByteArray((const char *)s_vertSpirv, sizeof(s_vertSpirv))));

    QShader fs;
    fs.setStage(QShader::FragmentStage);
    fs.setShader({QShader::SpirvShader, QShaderVersion(100)},
                 QShaderCode(QByteArray((const char *)s_fragSpirv, sizeof(s_fragSpirv))));

    m_pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vs},
        {QRhiShaderStage::Fragment, fs},
    });

    m_pipeline->setShaderResourceBindings(m_srb);
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_pipeline->create();
  }

  // Assume the image is already in shader-read layout from the exporting device's flush.
  m_texture->setNativeLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  cb->beginPass(renderTarget(), QColor::fromRgbF(0.0f, 0.0f, 0.0f, 1.0f), {1.0f, 0});
  cb->setGraphicsPipeline(m_pipeline);
  cb->setViewport({0, 0, (float)outputSize.width(), (float)outputSize.height()});
  cb->setShaderResources(m_srb);
  cb->draw(3);
  cb->endPass();
}

#endif    // RENDERDOC_WAYLAND_UI

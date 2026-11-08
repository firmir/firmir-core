// Vulkan offscreen renderer. No window-system integration is required: the
// existing GUI backends present pixels exposed through CpuRenderTarget.
#include <firmir/render/backend.hpp>
#include <firmir/log.hpp>

#include <vulkan/vulkan.h>

#include "builtin_vert.hpp"
#include "builtin_frag.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

using namespace Firmir::Render;

namespace {

const Firmir::LoggingCategory& lcVulkan() {
    static const Firmir::LoggingCategory category("render.vulkan");
    return category;
}

bool ok(VkResult result, const char* operation) {
    if (result == VK_SUCCESS)
        return true;
    cLogWarning(lcVulkan()) << operation << "failed with VkResult" << result;
    return false;
}

VkFormat vkFormat(TextureFormat format) {
    return format == TextureFormat::Bgra8 ? VK_FORMAT_B8G8R8A8_UNORM
                                          : VK_FORMAT_R8G8B8A8_UNORM;
}

class Device;

class Buffer final : public RenderBuffer {
public:
    Buffer(Device* device, const BufferDesc& desc);
    ~Buffer() override;
    BufferUsage usage() const override { return m_desc.usage; }
    std::uint64_t size() const override { return m_desc.size; }
    bool write(const void* data, std::uint64_t bytes, std::uint64_t offset) override;
    bool valid() const { return m_buffer != VK_NULL_HANDLE; }
    VkBuffer handle() const { return m_buffer; }
private:
    Device* m_device;
    BufferDesc m_desc;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    void* m_mapped = nullptr;
};

class Texture final : public RenderTexture {
public:
    Texture(Device* device, const TextureDesc& desc);
    /// Imports a dma-buf video frame (zero-copy): external memory + a YCbCr
    /// conversion sampler for YUV formats. valid() reports the outcome.
    Texture(Device* device, const DmaBufTextureDesc& desc);
    ~Texture() override;
    const TextureDesc& desc() const override { return m_desc; }
    bool write(const void* data, std::uint64_t bytes) override;
    bool isExternal() const override { return m_imported; }
    bool valid() const { return m_image != VK_NULL_HANDLE; }
    bool imported() const { return m_imported; }
    VkImageView view() const { return m_view; }
    VkSampler sampler() const { return m_sampler; }
private:
    bool importDmaBuf(const DmaBufTextureDesc& desc);
    Device* m_device;
    TextureDesc m_desc;
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkSamplerYcbcrConversion m_ycbcr = VK_NULL_HANDLE;
    bool m_imported = false;
};

class Shader final : public RenderShader {
public:
    Shader(Device* device, const ShaderDesc& desc);
    ~Shader() override;
    ShaderStage stage() const override { return m_desc.stage; }
    const std::string& entryPoint() const override { return m_desc.entryPoint; }
    VkShaderModule module() const { return m_module; }
    bool builtin() const { return m_builtin; }
private:
    Device* m_device;
    ShaderDesc m_desc;
    VkShaderModule m_module = VK_NULL_HANDLE;
    bool m_builtin = false;
};

class Pipeline final : public RenderPipeline {
public:
    Pipeline(Device* device, const PipelineDesc& desc, Shader* vertex, Shader* fragment);
    ~Pipeline() override;
    const PipelineDesc& desc() const override { return m_desc; }
    VkPipeline handle(VkRenderPass pass, TextureFormat format, bool depth);
    bool valid() const { return m_vertex && m_fragment; }
private:
    Device* m_device;
    PipelineDesc m_desc;
    Shader* m_vertex;
    Shader* m_fragment;
    std::map<std::pair<int, bool>, VkPipeline> m_pipelines;
};

class Target final : public CpuRenderTarget {
public:
    Target(Device* device, const RenderTargetDesc& desc);
    ~Target() override;
    Size size() const override { return {m_desc.width, m_desc.height}; }
    TextureFormat colorFormat() const override { return m_desc.colorFormat; }
    bool hasDepth() const override { return m_desc.depth; }
    const std::uint32_t* pixels() const override;
    bool valid() const { return m_framebuffer != VK_NULL_HANDLE; }
    VkRenderPass renderPass() const;
    VkFramebuffer framebuffer() const { return m_framebuffer; }
    VkImage image() const { return m_image; }
    bool readback() const;
private:
    Device* m_device;
    RenderTargetDesc m_desc;
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkBuffer m_readback = VK_NULL_HANDLE;
    VkDeviceMemory m_readbackMemory = VK_NULL_HANDLE;
    void* m_readbackMapped = nullptr;
    mutable std::vector<std::uint32_t> m_argb;
};

class Device final : public RenderDevice {
public:
    explicit Device(const DeviceDesc& desc);
    ~Device() override;
    bool valid() const { return m_ready; }
    std::string backendName() const override { return "vulkan"; }
    RenderBuffer* createBuffer(const BufferDesc& desc) override;
    RenderTexture* createTexture(const TextureDesc& desc) override;
    RenderShader* createShader(const ShaderDesc& desc) override;
    RenderPipeline* createPipeline(const PipelineDesc& desc, RenderShader* vertex,
                                   RenderShader* fragment) override;
    RenderTarget* createRenderTarget(const RenderTargetDesc& desc) override;
    RenderSwapchain* createSwapchain(const SwapchainDesc&) override { return nullptr; }
    RenderCommandBuffer* createCommandBuffer() override;
    void submit(RenderCommandBuffer* commands) override;
    bool present(RenderSwapchain*) override { return false; }
    void waitIdle() override;

    // Zero-copy video frames (dma-buf import).
    bool supportsDmaBufTextures() const override { return m_hasDmaBuf; }
    RenderTexture* importDmaBufTexture(const DmaBufTextureDesc& desc) override;
    bool hasYcbcrConversion() const { return m_hasYcbcr; }
    bool hasDrmFormatModifier() const { return m_hasDrmModifier; }

    std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags properties) const;
    bool createImage(std::uint32_t width, std::uint32_t height, VkFormat format,
                     VkImageUsageFlags usage, VkImage& image,
                     VkDeviceMemory& memory) const;
    bool oneShot(const std::function<void(VkCommandBuffer)>& record);
    VkRenderPass renderPass(TextureFormat format, bool depth) const {
        return m_renderPasses[format == TextureFormat::Bgra8 ? 1 : 0][depth ? 1 : 0];
    }
    VkQueue queue() const { return m_queue; }
    VkDevice handle() const { return m_device; }
    VkCommandPool commandPool() const { return m_commandPool; }
    VkDescriptorPool descriptorPool() const { return m_descriptorPool; }
    VkDescriptorSetLayout descriptorLayout() const { return m_descriptorLayout; }
    VkPipelineLayout pipelineLayout() const { return m_pipelineLayout; }
    VkFormat depthFormat() const { return m_depthFormat; }
    Texture* whiteTexture() const { return m_white.get(); }
    Buffer* emptyUniform() const { return m_emptyUniform.get(); }
private:
    bool createRenderPasses();
    DeviceDesc m_desc;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    std::uint32_t m_queueFamily = 0;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkRenderPass m_renderPasses[2][2] = {};
    VkFormat m_depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
    std::unique_ptr<Texture> m_white;
    std::unique_ptr<Buffer> m_emptyUniform;
    bool m_ready = false;
    // Probed at device creation: zero-copy dma-buf import capabilities.
    bool m_hasDmaBuf = false;
    bool m_hasYcbcr = false;
    bool m_hasDrmModifier = false;
};

bool createRawBuffer(Device* device, VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties, VkBuffer& buffer,
                     VkDeviceMemory& memory, void** mapped = nullptr) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = std::max<VkDeviceSize>(size, 1);
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!ok(vkCreateBuffer(device->handle(), &info, nullptr, &buffer), "vkCreateBuffer "))
        return false;
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device->handle(), buffer, &requirements);
    const std::uint32_t type = device->memoryType(requirements.memoryTypeBits, properties);
    if (type == UINT32_MAX) {
        vkDestroyBuffer(device->handle(), buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    if (!ok(vkAllocateMemory(device->handle(), &allocation, nullptr, &memory),
            "vkAllocateMemory(buffer) ") ||
        !ok(vkBindBufferMemory(device->handle(), buffer, memory, 0),
            "vkBindBufferMemory ")) {
        vkDestroyBuffer(device->handle(), buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }
    if (mapped && !ok(vkMapMemory(device->handle(), memory, 0, VK_WHOLE_SIZE, 0, mapped),
                      "vkMapMemory ")) {
        vkDestroyBuffer(device->handle(), buffer, nullptr);
        vkFreeMemory(device->handle(), memory, nullptr);
        buffer = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

Buffer::Buffer(Device* device, const BufferDesc& desc) : m_device(device), m_desc(desc) {
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (desc.usage == BufferUsage::Vertex)
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    else if (desc.usage == BufferUsage::Index)
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    else
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    createRawBuffer(device, desc.size, usage,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    m_buffer, m_memory, &m_mapped);
}

Buffer::~Buffer() {
    if (m_mapped)
        vkUnmapMemory(m_device->handle(), m_memory);
    if (m_buffer)
        vkDestroyBuffer(m_device->handle(), m_buffer, nullptr);
    if (m_memory)
        vkFreeMemory(m_device->handle(), m_memory, nullptr);
}

bool Buffer::write(const void* data, std::uint64_t bytes, std::uint64_t offset) {
    if (!valid() || offset > m_desc.size || bytes > m_desc.size - offset || (bytes && !data))
        return false;
    if (bytes)
        std::memcpy(static_cast<std::uint8_t*>(m_mapped) + offset, data,
                    static_cast<std::size_t>(bytes));
    return true;
}

Texture::Texture(Device* device, const TextureDesc& desc) : m_device(device), m_desc(desc) {
    if (!device->createImage(desc.width, desc.height, vkFormat(desc.format),
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                             m_image, m_memory))
        return;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = m_image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = vkFormat(desc.format);
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!ok(vkCreateImageView(device->handle(), &view, nullptr, &m_view),
            "vkCreateImageView(texture) "))
        return;
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.minFilter = desc.sampler.minFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sampler.magFilter = desc.sampler.magFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    const VkSamplerAddressMode u = desc.sampler.addressU == AddressMode::Repeat
        ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    const VkSamplerAddressMode v = desc.sampler.addressV == AddressMode::Repeat
        ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeU = u;
    sampler.addressModeV = v;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 0.0f;
    ok(vkCreateSampler(device->handle(), &sampler, nullptr, &m_sampler),
       "vkCreateSampler ");
}

Texture::~Texture() {
    if (m_sampler) vkDestroySampler(m_device->handle(), m_sampler, nullptr);
    if (m_ycbcr) vkDestroySamplerYcbcrConversion(m_device->handle(), m_ycbcr, nullptr);
    if (m_view) vkDestroyImageView(m_device->handle(), m_view, nullptr);
    if (m_image) vkDestroyImage(m_device->handle(), m_image, nullptr);
    if (m_memory) vkFreeMemory(m_device->handle(), m_memory, nullptr);
}

bool Texture::write(const void* data, std::uint64_t bytes) {
    if (m_imported) return false; // external textures are producer-written
    const std::uint64_t expected = static_cast<std::uint64_t>(m_desc.width) * m_desc.height * 4;
    if (!valid() || !m_view || !m_sampler || !data || bytes != expected)
        return false;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    if (!createRawBuffer(m_device, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         staging, stagingMemory, &mapped))
        return false;
    std::memcpy(mapped, data, static_cast<std::size_t>(bytes));
    const bool uploaded = m_device->oneShot([&](VkCommandBuffer commands) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {static_cast<std::uint32_t>(m_desc.width),
                            static_cast<std::uint32_t>(m_desc.height), 1};
        vkCmdCopyBufferToImage(commands, staging, m_image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &barrier);
    });
    vkUnmapMemory(m_device->handle(), stagingMemory);
    vkDestroyBuffer(m_device->handle(), staging, nullptr);
    vkFreeMemory(m_device->handle(), stagingMemory, nullptr);
    return uploaded;
}

namespace {

VkFormat vkFormatFromFourcc(std::uint32_t fourcc) {
    switch (fourcc) {
    case Fourcc::Abgr8: return VK_FORMAT_R8G8B8A8_UNORM;
    case Fourcc::Nv12:  return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    case Fourcc::P010:
        return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
    case Fourcc::P016:  return VK_FORMAT_G16_B16R16_2PLANE_420_UNORM;
    default:            return VK_FORMAT_UNDEFINED;
    }
}

VkSamplerYcbcrModelConversion vkYcbcrModel(YcbcrModel model) {
    switch (model) {
    case YcbcrModel::Bt601:  return VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
    case YcbcrModel::Bt2020: return VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020;
    default:                 return VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
    }
}

} // namespace

Texture::Texture(Device* device, const DmaBufTextureDesc& desc)
    : m_device(device), m_imported(true) {
    m_desc.width = desc.width;
    m_desc.height = desc.height;
    m_desc.format = TextureFormat::Rgba8;
    if (importDmaBuf(desc))
        return;
    if (m_view) { vkDestroyImageView(device->handle(), m_view, nullptr); m_view = VK_NULL_HANDLE; }
    if (m_image) { vkDestroyImage(device->handle(), m_image, nullptr); m_image = VK_NULL_HANDLE; }
    if (m_memory) { vkFreeMemory(device->handle(), m_memory, nullptr); m_memory = VK_NULL_HANDLE; }
}

bool Texture::importDmaBuf(const DmaBufTextureDesc& desc) {
    const VkFormat format = vkFormatFromFourcc(desc.fourcc);
    const bool yuv = desc.fourcc != Fourcc::Abgr8;
    if (format == VK_FORMAT_UNDEFINED || desc.planeCount <= 0 ||
        desc.planes[0].fd < 0)
        return false;
    if (yuv && !m_device->hasYcbcrConversion())
        return false; // no GPU-side YUV->RGB: the caller falls back to CPU

    // Image over external memory. With an explicit DRM modifier (real
    // drivers) the exact plane layout is passed; otherwise the legacy
    // modifier-less path assumes a linear layout.
    VkImageDrmFormatModifierExplicitCreateInfoEXT modifierInfo{
        VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};
    std::array<VkSubresourceLayout, 4> planeLayouts{};
    VkExternalMemoryImageCreateInfo external{
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.pNext = &external;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = {static_cast<std::uint32_t>(desc.width),
                    static_cast<std::uint32_t>(desc.height), 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (desc.modifier != Fourcc::ModifierInvalid &&
        m_device->hasDrmFormatModifier()) {
        modifierInfo.drmFormatModifier = desc.modifier;
        modifierInfo.drmFormatModifierPlaneCount = desc.planeCount;
        for (int i = 0; i < desc.planeCount; ++i) {
            planeLayouts[i].offset = desc.planes[i].offset;
            planeLayouts[i].rowPitch = desc.planes[i].stride;
        }
        modifierInfo.pPlaneLayouts = planeLayouts.data();
        external.pNext = &modifierInfo;
        image.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    } else {
        image.tiling = VK_IMAGE_TILING_LINEAR;
    }
    if (!ok(vkCreateImage(m_device->handle(), &image, nullptr, &m_image),
            "vkCreateImage(dmabuf) "))
        return false;

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device->handle(), m_image, &requirements);
    // The dma-buf size comes from the fd itself; the allocation must not
    // exceed it (some drivers require an exact match).
    const off_t bufSize = ::lseek(desc.planes[0].fd, 0, SEEK_END);
    VkDeviceSize allocationSize = requirements.size;
    if (bufSize > 0 && allocationSize > static_cast<VkDeviceSize>(bufSize))
        allocationSize = static_cast<VkDeviceSize>(bufSize);
    std::uint32_t type = m_device->memoryType(requirements.memoryTypeBits, 0);
    if (type == UINT32_MAX) {
        // Imported dma-buf memory may only land in host-visible types.
        type = m_device->memoryType(requirements.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    }
    if (type == UINT32_MAX)
        return false;

    const int fd = ::dup(desc.planes[0].fd); // consumed by the allocation
    if (fd < 0)
        return false;
    VkImportMemoryFdInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
    import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    import.fd = fd;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.pNext = &import;
    allocation.allocationSize = allocationSize;
    allocation.memoryTypeIndex = type;
    if (!ok(vkAllocateMemory(m_device->handle(), &allocation, nullptr,
                             &m_memory),
            "vkAllocateMemory(import) ")) {
        ::close(fd);
        return false;
    }
    if (!ok(vkBindImageMemory(m_device->handle(), m_image, m_memory, 0),
            "vkBindImageMemory(import) "))
        return false;

    // YUV formats: GPU-side YCbCr conversion with the stream's matrix/range.
    VkSamplerYcbcrConversionInfo conversionInfo{
        VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    if (yuv) {
        VkSamplerYcbcrConversionCreateInfo conversion{
            VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO};
        conversion.format = format;
        conversion.ycbcrModel = vkYcbcrModel(desc.ycbcrModel);
        conversion.ycbcrRange = desc.ycbcrRange == YcbcrRange::Full
                                    ? VK_SAMPLER_YCBCR_RANGE_ITU_FULL
                                    : VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
        conversion.components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY};
        conversion.xChromaOffset = VK_CHROMA_LOCATION_COSITED_EVEN;
        conversion.yChromaOffset = VK_CHROMA_LOCATION_COSITED_EVEN;
        conversion.chromaFilter = VK_FILTER_LINEAR;
        conversion.forceExplicitReconstruction = VK_FALSE;
        if (!ok(vkCreateSamplerYcbcrConversion(m_device->handle(),
                                               &conversion, nullptr, &m_ycbcr),
                "vkCreateSamplerYcbcrConversion "))
            return false;
        conversionInfo.conversion = m_ycbcr;
    }

    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.pNext = yuv ? &conversionInfo : nullptr;
    view.image = m_image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!ok(vkCreateImageView(m_device->handle(), &view, nullptr, &m_view),
            "vkCreateImageView(dmabuf) "))
        return false;

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.pNext = yuv ? &conversionInfo : nullptr;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 0.0f;
    if (!ok(vkCreateSampler(m_device->handle(), &sampler, nullptr, &m_sampler),
            "vkCreateSampler(dmabuf) "))
        return false;

    // Imported images start UNDEFINED; move to GENERAL for sampling.
    VkImage imageHandle = m_image;
    return m_device->oneShot([imageHandle](VkCommandBuffer commands) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = imageHandle;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
    });
}

RenderTexture* Device::importDmaBufTexture(const DmaBufTextureDesc& desc) {
    if (!valid() || !supportsDmaBufTextures())
        return nullptr;
    auto* texture = new Texture(this, desc);
    if (!texture->valid()) {
        delete texture;
        return nullptr;
    }
    return texture;
}

Shader::Shader(Device* device, const ShaderDesc& desc) : m_device(device), m_desc(desc) {
    std::uint32_t magic = 0;
    if (desc.code.size() >= 4)
        std::memcpy(&magic, desc.code.data(), 4);
    m_builtin = desc.code.size() < 4 || desc.code.size() % 4 != 0 || magic != 0x07230203u;
    const void* code = desc.code.data();
    std::size_t size = desc.code.size();
    if (m_builtin) {
        if (desc.stage == ShaderStage::Vertex) {
            code = firmirVulkanVertSpirv;
            size = sizeof(firmirVulkanVertSpirv);
        } else {
            code = firmirVulkanFragSpirv;
            size = sizeof(firmirVulkanFragSpirv);
        }
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size;
    info.pCode = static_cast<const std::uint32_t*>(code);
    ok(vkCreateShaderModule(device->handle(), &info, nullptr, &m_module),
       "vkCreateShaderModule ");
}

Shader::~Shader() {
    if (m_module)
        vkDestroyShaderModule(m_device->handle(), m_module, nullptr);
}

Pipeline::Pipeline(Device* device, const PipelineDesc& desc, Shader* vertex, Shader* fragment)
    : m_device(device), m_desc(desc), m_vertex(vertex), m_fragment(fragment) {}

Pipeline::~Pipeline() {
    for (const auto& entry : m_pipelines)
        vkDestroyPipeline(m_device->handle(), entry.second, nullptr);
}

VkPrimitiveTopology vkTopology(PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkFormat vkVertexFormat(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float: return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::UByte4Norm: return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_R32G32_SFLOAT;
}

VkBlendFactor vkBlendFactor(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

VkBlendOp vkBlendOp(BlendOp op) {
    if (op == BlendOp::Subtract) return VK_BLEND_OP_SUBTRACT;
    if (op == BlendOp::ReverseSubtract) return VK_BLEND_OP_REVERSE_SUBTRACT;
    return VK_BLEND_OP_ADD;
}

VkCompareOp vkCompareOp(CompareOp op) {
    switch (op) {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS;
}

VkPipeline Pipeline::handle(VkRenderPass pass, TextureFormat format, bool depth) {
    const auto key = std::make_pair(static_cast<int>(format), depth);
    const auto existing = m_pipelines.find(key);
    if (existing != m_pipelines.end())
        return existing->second;
    if (!m_vertex->module() || !m_fragment->module())
        return VK_NULL_HANDLE;
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_VERTEX_BIT, m_vertex->module(), m_vertex->entryPoint().c_str()},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, m_fragment->module(), m_fragment->entryPoint().c_str()}
    }};
    VkVertexInputBindingDescription binding{0, m_desc.vertexLayout.stride, VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const VertexAttribute& attribute : m_desc.vertexLayout.attributes)
        attributes.push_back({attribute.location, 0, vkVertexFormat(attribute.format), attribute.offset});
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    input.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = vkTopology(m_desc.topology);
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = m_desc.rasterizer.cullMode == CullMode::Back ? VK_CULL_MODE_BACK_BIT
        : m_desc.rasterizer.cullMode == CullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE;
    raster.frontFace = m_desc.rasterizer.frontFaceCounterClockwise
        ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthState.depthTestEnable = m_desc.depth.testEnabled ? VK_TRUE : VK_FALSE;
    depthState.depthWriteEnable = m_desc.depth.writeEnabled ? VK_TRUE : VK_FALSE;
    depthState.depthCompareOp = vkCompareOp(m_desc.depth.compare);
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.blendEnable = m_desc.blend.enabled ? VK_TRUE : VK_FALSE;
    attachment.srcColorBlendFactor = vkBlendFactor(m_desc.blend.srcColor);
    attachment.dstColorBlendFactor = vkBlendFactor(m_desc.blend.dstColor);
    attachment.colorBlendOp = vkBlendOp(m_desc.blend.colorOp);
    attachment.srcAlphaBlendFactor = vkBlendFactor(m_desc.blend.srcAlpha);
    attachment.dstAlphaBlendFactor = vkBlendFactor(m_desc.blend.dstAlpha);
    attachment.alphaBlendOp = vkBlendOp(m_desc.blend.alphaOp);
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const std::array<VkDynamicState, 2> dynamic{{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamic.size());
    dynamicState.pDynamicStates = dynamic.data();
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = static_cast<std::uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = depth ? &depthState : nullptr;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamicState;
    info.layout = m_device->pipelineLayout();
    info.renderPass = pass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (!ok(vkCreateGraphicsPipelines(m_device->handle(), VK_NULL_HANDLE, 1, &info,
                                      nullptr, &pipeline), "vkCreateGraphicsPipelines "))
        return VK_NULL_HANDLE;
    m_pipelines.emplace(key, pipeline);
    return pipeline;
}

Target::Target(Device* device, const RenderTargetDesc& desc) : m_device(device), m_desc(desc) {
    if (!device->createImage(desc.width, desc.height, vkFormat(desc.colorFormat),
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                             m_image, m_memory))
        return;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = m_image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = vkFormat(desc.colorFormat);
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!ok(vkCreateImageView(device->handle(), &view, nullptr, &m_view),
            "vkCreateImageView(target) "))
        return;
    if (desc.depth) {
        if (!device->createImage(desc.width, desc.height, device->depthFormat(),
                                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                 m_depthImage, m_depthMemory))
            return;
        view.image = m_depthImage;
        view.format = device->depthFormat();
        view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
        if (!ok(vkCreateImageView(device->handle(), &view, nullptr, &m_depthView),
                "vkCreateImageView(depth) "))
            return;
    }
    const std::array<VkImageView, 2> attachments{{m_view, m_depthView}};
    VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer.renderPass = renderPass();
    framebuffer.attachmentCount = desc.depth ? 2u : 1u;
    framebuffer.pAttachments = attachments.data();
    framebuffer.width = desc.width;
    framebuffer.height = desc.height;
    framebuffer.layers = 1;
    if (!ok(vkCreateFramebuffer(device->handle(), &framebuffer, nullptr, &m_framebuffer),
            "vkCreateFramebuffer "))
        return;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(desc.width) * desc.height * 4;
    if (!createRawBuffer(device, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         m_readback, m_readbackMemory, &m_readbackMapped))
        return;
    m_argb.resize(static_cast<std::size_t>(desc.width) * desc.height);
}

Target::~Target() {
    if (m_readbackMapped) vkUnmapMemory(m_device->handle(), m_readbackMemory);
    if (m_readback) vkDestroyBuffer(m_device->handle(), m_readback, nullptr);
    if (m_readbackMemory) vkFreeMemory(m_device->handle(), m_readbackMemory, nullptr);
    if (m_framebuffer) vkDestroyFramebuffer(m_device->handle(), m_framebuffer, nullptr);
    if (m_depthView) vkDestroyImageView(m_device->handle(), m_depthView, nullptr);
    if (m_depthImage) vkDestroyImage(m_device->handle(), m_depthImage, nullptr);
    if (m_depthMemory) vkFreeMemory(m_device->handle(), m_depthMemory, nullptr);
    if (m_view) vkDestroyImageView(m_device->handle(), m_view, nullptr);
    if (m_image) vkDestroyImage(m_device->handle(), m_image, nullptr);
    if (m_memory) vkFreeMemory(m_device->handle(), m_memory, nullptr);
}

VkRenderPass Target::renderPass() const {
    return m_device->renderPass(m_desc.colorFormat, m_desc.depth);
}

bool Target::readback() const {
    if (!valid() || !m_readbackMapped)
        return false;
    return m_device->oneShot([&](VkCommandBuffer commands) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {static_cast<std::uint32_t>(m_desc.width),
                            static_cast<std::uint32_t>(m_desc.height), 1};
        vkCmdCopyImageToBuffer(commands, m_image, VK_IMAGE_LAYOUT_GENERAL, m_readback, 1, &copy);
    });
}

const std::uint32_t* Target::pixels() const {
    if (!readback())
        return nullptr;
    const auto* source = static_cast<const std::uint8_t*>(m_readbackMapped);
    for (std::size_t i = 0; i < m_argb.size(); ++i) {
        const std::uint8_t* p = source + i * 4;
        const std::uint8_t r = m_desc.colorFormat == TextureFormat::Bgra8 ? p[2] : p[0];
        const std::uint8_t b = m_desc.colorFormat == TextureFormat::Bgra8 ? p[0] : p[2];
        m_argb[i] = (static_cast<std::uint32_t>(p[3]) << 24) |
                    (static_cast<std::uint32_t>(r) << 16) |
                    (static_cast<std::uint32_t>(p[1]) << 8) | b;
    }
    return m_argb.data();
}

struct Command {
    enum Kind { BeginPass, EndPass, Viewport, Scissor, PipelineBind, VertexBind,
                IndexBind, UniformBind, TextureBind, Draw, DrawIndexed } kind;
    explicit Command(Kind value) : kind(value) {}
    Target* target = nullptr;
    ClearValue clear{};
    Firmir::Render::Viewport viewport{};
    Rect rect{};
    Pipeline* pipeline = nullptr;
    Buffer* buffer = nullptr;
    Texture* texture = nullptr;
    std::uint64_t offset = 0;
    std::uint64_t bytes = 0;
    IndexFormat indexFormat = IndexFormat::UInt16;
    std::uint32_t count = 0;
    std::uint32_t first = 0;
    std::int32_t vertexOffset = 0;
};

class CommandBuffer final : public RenderCommandBuffer {
public:
    explicit CommandBuffer(Device* device) : m_device(device) {}
    void begin() override { m_commands.clear(); m_recording = true; }
    void end() override { m_recording = false; }
    void beginRenderPass(RenderTarget* target, const ClearValue& clear) override {
        if (!m_recording) return;
        Command command(Command::BeginPass);
        command.target = static_cast<Target*>(target);
        command.clear = clear;
        m_commands.push_back(command);
    }
    void endRenderPass() override { if (m_recording) m_commands.emplace_back(Command::EndPass); }
    void setViewport(const Viewport& viewport) override {
        Command command(Command::Viewport); command.viewport = viewport; m_commands.push_back(command);
    }
    void setScissor(const Rect& rect) override {
        Command command(Command::Scissor); command.rect = rect; m_commands.push_back(command);
    }
    void bindPipeline(RenderPipeline* pipeline) override {
        Command command(Command::PipelineBind); command.pipeline = static_cast<Pipeline*>(pipeline);
        m_commands.push_back(command);
    }
    void bindVertexBuffer(std::uint32_t slot, RenderBuffer* buffer, std::uint64_t offset) override {
        if (slot != 0) return;
        Command command(Command::VertexBind); command.buffer = static_cast<Buffer*>(buffer);
        command.offset = offset; m_commands.push_back(command);
    }
    void bindIndexBuffer(RenderBuffer* buffer, IndexFormat format, std::uint64_t offset) override {
        Command command(Command::IndexBind); command.buffer = static_cast<Buffer*>(buffer);
        command.indexFormat = format; command.offset = offset; m_commands.push_back(command);
    }
    void bindUniformBuffer(std::uint32_t slot, RenderBuffer* buffer,
                           std::uint64_t offset, std::uint64_t bytes) override {
        if (slot != 0) return;
        Command command(Command::UniformBind); command.buffer = static_cast<Buffer*>(buffer);
        command.offset = offset; command.bytes = bytes; m_commands.push_back(command);
    }
    void bindTexture(std::uint32_t slot, RenderTexture* texture) override {
        if (slot != 0) return;
        Command command(Command::TextureBind); command.texture = static_cast<Texture*>(texture);
        m_commands.push_back(command);
    }
    void draw(std::uint32_t count, std::uint32_t first) override {
        Command command(Command::Draw); command.count = count; command.first = first;
        m_commands.push_back(command);
    }
    void drawIndexed(std::uint32_t count, std::uint32_t first, std::int32_t vertexOffset) override {
        Command command(Command::DrawIndexed); command.count = count; command.first = first;
        command.vertexOffset = vertexOffset; m_commands.push_back(command);
    }
    void execute();
private:
    Device* m_device;
    std::vector<Command> m_commands;
    bool m_recording = false;
};

struct ExecState {
    Target* target = nullptr;
    Pipeline* pipeline = nullptr;
    Buffer* vertices = nullptr;
    Buffer* indices = nullptr;
    Buffer* uniform = nullptr;
    Texture* texture = nullptr;
    std::uint64_t vertexOffset = 0;
    std::uint64_t indexOffset = 0;
    std::uint64_t uniformOffset = 0;
    std::uint64_t uniformBytes = 0;
    IndexFormat indexFormat = IndexFormat::UInt16;
    Viewport viewport{};
    Rect scissor{};
};

void applyScissor(VkCommandBuffer commands, const ExecState& state) {
    if (!state.target) return;
    const Size size = state.target->size();
    const int x = std::clamp(state.scissor.x, 0, size.width);
    const int y = std::clamp(state.scissor.y, 0, size.height);
    const int right = std::clamp(state.scissor.x + state.scissor.width, 0, size.width);
    const int bottom = std::clamp(state.scissor.y + state.scissor.height, 0, size.height);
    const VkRect2D scissor{{x, y}, {static_cast<std::uint32_t>(std::max(0, right - x)),
                                   static_cast<std::uint32_t>(std::max(0, bottom - y))}};
    vkCmdSetScissor(commands, 0, 1, &scissor);
}

bool bindDrawState(Device* device, VkCommandBuffer commands, const ExecState& state) {
    if (!state.target || !state.pipeline || !state.vertices)
        return false;
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = device->descriptorPool();
    allocation.descriptorSetCount = 1;
    const VkDescriptorSetLayout layout = device->descriptorLayout();
    allocation.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!ok(vkAllocateDescriptorSets(device->handle(), &allocation, &set),
            "vkAllocateDescriptorSets "))
        return false;
    Buffer* uniform = state.uniform ? state.uniform : device->emptyUniform();
    Texture* texture = state.texture ? state.texture : device->whiteTexture();
    if (!uniform || !texture)
        return false;
    VkDescriptorBufferInfo bufferInfo{uniform->handle(),
        state.uniform ? state.uniformOffset : 0,
        state.uniform && state.uniformBytes ? state.uniformBytes : VK_WHOLE_SIZE};
    VkDescriptorImageInfo imageInfo{texture->sampler(), texture->view(), VK_IMAGE_LAYOUT_GENERAL};
    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &bufferInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device->handle(), static_cast<std::uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            device->pipelineLayout(), 0, 1, &set, 0, nullptr);
    struct Push { float width, height; std::int32_t textured, padding; } push{
        static_cast<float>(state.target->size().width),
        static_cast<float>(state.target->size().height), state.texture ? 1 : 0, 0};
    vkCmdPushConstants(commands, device->pipelineLayout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    const VkBuffer vertex = state.vertices->handle();
    const VkDeviceSize offset = state.vertexOffset;
    vkCmdBindVertexBuffers(commands, 0, 1, &vertex, &offset);
    return true;
}

void CommandBuffer::execute() {
    m_device->waitIdle();
    vkResetDescriptorPool(m_device->handle(), m_device->descriptorPool(), 0);
    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = m_device->commandPool();
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    if (!ok(vkAllocateCommandBuffers(m_device->handle(), &allocation, &commands),
            "vkAllocateCommandBuffers "))
        return;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!ok(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer ")) {
        vkFreeCommandBuffers(m_device->handle(), m_device->commandPool(), 1, &commands);
        return;
    }
    ExecState state;
    for (const Command& command : m_commands) {
        switch (command.kind) {
        case Command::BeginPass: {
            state.target = command.target;
            if (!state.target || !state.target->valid()) break;
            const Size size = state.target->size();
            state.viewport = {0, 0, static_cast<float>(size.width), static_cast<float>(size.height), 0, 1};
            state.scissor = {0, 0, size.width, size.height};
            std::array<VkClearValue, 2> clears{};
            clears[0].color = {{command.clear.r, command.clear.g, command.clear.b, command.clear.a}};
            clears[1].depthStencil = {command.clear.depth, command.clear.stencil};
            VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            pass.renderPass = state.target->renderPass();
            pass.framebuffer = state.target->framebuffer();
            pass.renderArea = {{0, 0}, {static_cast<std::uint32_t>(size.width),
                                        static_cast<std::uint32_t>(size.height)}};
            pass.clearValueCount = state.target->hasDepth() ? 2u : 1u;
            pass.pClearValues = clears.data();
            vkCmdBeginRenderPass(commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
            const VkViewport viewport{state.viewport.x, state.viewport.y, state.viewport.width,
                                      state.viewport.height, 0, 1};
            vkCmdSetViewport(commands, 0, 1, &viewport);
            applyScissor(commands, state);
            break;
        }
        case Command::EndPass:
            if (state.target) vkCmdEndRenderPass(commands);
            state.target = nullptr;
            break;
        case Command::Viewport: {
            state.viewport = command.viewport;
            const VkViewport viewport{command.viewport.x, command.viewport.y,
                command.viewport.width, command.viewport.height,
                command.viewport.minDepth, command.viewport.maxDepth};
            vkCmdSetViewport(commands, 0, 1, &viewport);
            break;
        }
        case Command::Scissor:
            state.scissor = command.rect;
            applyScissor(commands, state);
            break;
        case Command::PipelineBind:
            state.pipeline = command.pipeline;
            if (state.target && state.pipeline) {
                const VkPipeline pipeline = state.pipeline->handle(state.target->renderPass(),
                    state.target->colorFormat(), state.target->hasDepth());
                if (pipeline) vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            }
            break;
        case Command::VertexBind: state.vertices = command.buffer; state.vertexOffset = command.offset; break;
        case Command::IndexBind:
            state.indices = command.buffer; state.indexOffset = command.offset;
            state.indexFormat = command.indexFormat; break;
        case Command::UniformBind:
            state.uniform = command.buffer; state.uniformOffset = command.offset;
            state.uniformBytes = command.bytes; break;
        case Command::TextureBind: state.texture = command.texture; break;
        case Command::Draw:
            if (bindDrawState(m_device, commands, state))
                vkCmdDraw(commands, command.count, 1, command.first, 0);
            break;
        case Command::DrawIndexed:
            if (state.indices && bindDrawState(m_device, commands, state)) {
                vkCmdBindIndexBuffer(commands, state.indices->handle(), state.indexOffset,
                    state.indexFormat == IndexFormat::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(commands, command.count, 1, command.first, command.vertexOffset, 0);
            }
            break;
        }
    }
    if (state.target)
        vkCmdEndRenderPass(commands);
    if (ok(vkEndCommandBuffer(commands), "vkEndCommandBuffer ")) {
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commands;
        ok(vkQueueSubmit(m_device->queue(), 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit ");
        vkQueueWaitIdle(m_device->queue());
    }
    vkFreeCommandBuffers(m_device->handle(), m_device->commandPool(), 1, &commands);
}

Device::Device(const DeviceDesc& desc) : m_desc(desc) {
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "Firmir";
    application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance.pApplicationInfo = &application;
    const char* validation = "VK_LAYER_KHRONOS_validation";
    if (desc.validation) {
        std::uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> layers(count);
        vkEnumerateInstanceLayerProperties(&count, layers.data());
        const auto found = std::find_if(layers.begin(), layers.end(), [](const VkLayerProperties& layer) {
            return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        });
        if (found != layers.end()) {
            instance.enabledLayerCount = 1;
            instance.ppEnabledLayerNames = &validation;
        }
    }
    if (!ok(vkCreateInstance(&instance, nullptr, &m_instance), "vkCreateInstance "))
        return;
    std::uint32_t physicalCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &physicalCount, nullptr);
    std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
    vkEnumeratePhysicalDevices(m_instance, &physicalCount, physicalDevices.data());
    for (VkPhysicalDevice physical : physicalDevices) {
        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
        for (std::uint32_t i = 0; i < familyCount; ++i) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                m_physical = physical;
                m_queueFamily = i;
                break;
            }
        }
        if (m_physical) break;
    }
    if (!m_physical)
        return;

    // Probe zero-copy capabilities: dma-buf external memory (import), YCbCr
    // sampler conversion (GPU-side YUV->RGB) and explicit DRM modifiers.
    std::vector<const char*> extensions;
    {
        std::uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &count,
                                             nullptr);
        std::vector<VkExtensionProperties> available(count);
        vkEnumerateDeviceExtensionProperties(m_physical, nullptr, &count,
                                             available.data());
        const auto has = [&](const char* name) {
            for (const auto& ext : available)
                if (!std::strcmp(ext.extensionName, name))
                    return true;
            return false;
        };
        const bool fd = has(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        const bool dmaBuf = has(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
        m_hasDmaBuf = fd && dmaBuf;
        if (fd)
            extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        if (dmaBuf)
            extensions.push_back(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
        m_hasDrmModifier = has(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
        if (m_hasDrmModifier)
            extensions.push_back(
                VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
        VkPhysicalDeviceVulkan11Features features11{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        VkPhysicalDeviceFeatures2 features2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features2.pNext = &features11;
        vkGetPhysicalDeviceFeatures2(m_physical, &features2);
        m_hasYcbcr = has(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME) &&
                     features11.samplerYcbcrConversion == VK_TRUE;
        if (m_hasYcbcr)
            extensions.push_back(
                VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = m_queueFamily;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan11Features features11{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    features11.samplerYcbcrConversion = m_hasYcbcr ? VK_TRUE : VK_FALSE;
    VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device.queueCreateInfoCount = 1;
    device.pQueueCreateInfos = &queue;
    device.enabledExtensionCount =
        static_cast<std::uint32_t>(extensions.size());
    device.ppEnabledExtensionNames = extensions.data();
    device.pNext = &features11;
    if (!ok(vkCreateDevice(m_physical, &device, nullptr, &m_device), "vkCreateDevice "))
        return;
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = m_queueFamily;
    if (!ok(vkCreateCommandPool(m_device, &pool, nullptr, &m_commandPool),
            "vkCreateCommandPool "))
        return;
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layout.pBindings = bindings.data();
    if (!ok(vkCreateDescriptorSetLayout(m_device, &layout, nullptr, &m_descriptorLayout),
            "vkCreateDescriptorSetLayout "))
        return;
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayout.setLayoutCount = 1;
    pipelineLayout.pSetLayouts = &m_descriptorLayout;
    pipelineLayout.pushConstantRangeCount = 1;
    pipelineLayout.pPushConstantRanges = &push;
    if (!ok(vkCreatePipelineLayout(m_device, &pipelineLayout, nullptr, &m_pipelineLayout),
            "vkCreatePipelineLayout "))
        return;
    const std::array<VkDescriptorPoolSize, 2> sizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192}
    }};
    VkDescriptorPoolCreateInfo descriptorPool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptorPool.maxSets = 8192;
    descriptorPool.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    descriptorPool.pPoolSizes = sizes.data();
    if (!ok(vkCreateDescriptorPool(m_device, &descriptorPool, nullptr, &m_descriptorPool),
            "vkCreateDescriptorPool ") || !createRenderPasses())
        return;
    TextureDesc whiteDesc;
    m_white = std::make_unique<Texture>(this, whiteDesc);
    const std::uint8_t white[4] = {255, 255, 255, 255};
    if (!m_white->write(white, sizeof(white)))
        return;
    m_emptyUniform = std::make_unique<Buffer>(this, BufferDesc{BufferUsage::Uniform, 16});
    const std::uint8_t zeros[16] = {};
    if (!m_emptyUniform->write(zeros, sizeof(zeros), 0))
        return;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physical, &properties);
    cLogInfo(lcVulkan()) << "Vulkan renderer:" << properties.deviceName;
    m_ready = true;
}

Device::~Device() {
    if (m_device)
        vkDeviceWaitIdle(m_device);
    m_white.reset();
    m_emptyUniform.reset();
    if (m_device) {
        for (auto& formats : m_renderPasses)
            for (VkRenderPass pass : formats)
                if (pass) vkDestroyRenderPass(m_device, pass, nullptr);
        if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        if (m_descriptorLayout) vkDestroyDescriptorSetLayout(m_device, m_descriptorLayout, nullptr);
        if (m_commandPool) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_instance)
        vkDestroyInstance(m_instance, nullptr);
}

bool Device::createRenderPasses() {
    for (int format = 0; format < 2; ++format) {
        for (int depth = 0; depth < 2; ++depth) {
            std::array<VkAttachmentDescription, 2> attachments{};
            attachments[0].format = format ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
            attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
            attachments[1].format = m_depthFormat;
            attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            VkAttachmentReference depthReference{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &color;
            subpass.pDepthStencilAttachment = depth ? &depthReference : nullptr;
            VkSubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.dstStageMask = dependency.srcStageMask;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            info.attachmentCount = depth ? 2u : 1u;
            info.pAttachments = attachments.data();
            info.subpassCount = 1;
            info.pSubpasses = &subpass;
            info.dependencyCount = 1;
            info.pDependencies = &dependency;
            if (!ok(vkCreateRenderPass(m_device, &info, nullptr, &m_renderPasses[format][depth]),
                    "vkCreateRenderPass "))
                return false;
        }
    }
    return true;
}

std::uint32_t Device::memoryType(std::uint32_t bits, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(m_physical, &memory);
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((bits & (1u << i)) &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    return UINT32_MAX;
}

bool Device::createImage(std::uint32_t width, std::uint32_t height, VkFormat format,
                         VkImageUsageFlags usage, VkImage& image,
                         VkDeviceMemory& memory) const {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!ok(vkCreateImage(m_device, &info, nullptr, &image), "vkCreateImage "))
        return false;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, image, &requirements);
    const std::uint32_t type = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) {
        vkDestroyImage(m_device, image, nullptr);
        image = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    if (!ok(vkAllocateMemory(m_device, &allocation, nullptr, &memory),
            "vkAllocateMemory(image) ") ||
        !ok(vkBindImageMemory(m_device, image, memory, 0), "vkBindImageMemory ")) {
        vkDestroyImage(m_device, image, nullptr);
        image = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool Device::oneShot(const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = m_commandPool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    if (!ok(vkAllocateCommandBuffers(m_device, &allocation, &commands),
            "vkAllocateCommandBuffers(oneShot) "))
        return false;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    bool success = ok(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer(oneShot) ");
    if (success) {
        record(commands);
        success = ok(vkEndCommandBuffer(commands), "vkEndCommandBuffer(oneShot) ");
    }
    if (success) {
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commands;
        success = ok(vkQueueSubmit(m_queue, 1, &submit, VK_NULL_HANDLE),
                     "vkQueueSubmit(oneShot) ");
        if (success)
            success = ok(vkQueueWaitIdle(m_queue), "vkQueueWaitIdle(oneShot) ");
    }
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commands);
    return success;
}

RenderBuffer* Device::createBuffer(const BufferDesc& desc) {
    if (!valid() || desc.size == 0) return nullptr;
    auto* buffer = new Buffer(this, desc);
    if (!buffer->valid()) { delete buffer; return nullptr; }
    return buffer;
}
RenderTexture* Device::createTexture(const TextureDesc& desc) {
    if (!valid() || desc.width <= 0 || desc.height <= 0 || desc.format == TextureFormat::Depth24Stencil8)
        return nullptr;
    auto* texture = new Texture(this, desc);
    if (!texture->valid() || !texture->view() || !texture->sampler()) { delete texture; return nullptr; }
    return texture;
}
RenderShader* Device::createShader(const ShaderDesc& desc) {
    if (!valid()) return nullptr;
    auto* shader = new Shader(this, desc);
    if (!shader->module()) { delete shader; return nullptr; }
    return shader;
}
RenderPipeline* Device::createPipeline(const PipelineDesc& desc, RenderShader* vertex,
                                       RenderShader* fragment) {
    if (!valid() || !vertex || !fragment || desc.vertexLayout.stride == 0 ||
        vertex->stage() != ShaderStage::Vertex || fragment->stage() != ShaderStage::Fragment)
        return nullptr;
    return new Pipeline(this, desc, static_cast<Shader*>(vertex), static_cast<Shader*>(fragment));
}
RenderTarget* Device::createRenderTarget(const RenderTargetDesc& desc) {
    if (!valid() || desc.width <= 0 || desc.height <= 0 || desc.colorFormat == TextureFormat::Depth24Stencil8)
        return nullptr;
    auto* target = new Target(this, desc);
    if (!target->valid()) { delete target; return nullptr; }
    return target;
}
RenderCommandBuffer* Device::createCommandBuffer() {
    return valid() ? new CommandBuffer(this) : nullptr;
}
void Device::submit(RenderCommandBuffer* commands) {
    if (valid() && commands) static_cast<CommandBuffer*>(commands)->execute();
}
void Device::waitIdle() {
    if (m_device) vkDeviceWaitIdle(m_device);
}

class VulkanBackend final : public RenderBackend {
public:
    std::string name() const override { return "vulkan"; }
    RenderDevice* createDevice(const DeviceDesc& desc) override {
        auto* device = new Device(desc);
        if (!device->valid()) { delete device; return nullptr; }
        return device;
    }
};

VulkanBackend& backendInstance() {
    static VulkanBackend backend;
    return backend;
}

} // namespace

extern "C" Firmir::Render::RenderBackend* firmir_render_create_backend() {
    return &backendInstance();
}

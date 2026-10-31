// Compiled with Plume's real VulkanCommandList::copyTextureRegion appended.
// The Vulkan boundary prints the produced copy region so scenarios can check
// the aspect, buffer pitch/offset, subresource, image layout and extent that
// the production method passes to vkCmdCopyImageToBuffer.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using VkCommandBuffer = int;
using VkImage = int;
using VkBuffer = int;
using VkDeviceSize = uint64_t;
using VkImageAspectFlags = uint32_t;
using VkImageLayout = int;
constexpr VkImage VK_NULL_HANDLE = 0;
constexpr VkImageLayout VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL = 6;
constexpr VkImageLayout VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL = 7;
constexpr VkImageAspectFlags VK_IMAGE_ASPECT_COLOR_BIT = 1;
constexpr VkImageAspectFlags VK_IMAGE_ASPECT_DEPTH_BIT = 2;
constexpr VkImageAspectFlags VK_IMAGE_ASPECT_STENCIL_BIT = 4;

struct VkOffset3D { int32_t x, y, z; };
struct VkExtent3D { uint32_t width, height, depth; };
struct VkImageSubresourceLayers { VkImageAspectFlags aspectMask; uint32_t mipLevel, baseArrayLayer, layerCount; };
struct VkImageCopy {
    VkImageSubresourceLayers srcSubresource{};
    VkOffset3D srcOffset{};
    VkImageSubresourceLayers dstSubresource{};
    VkOffset3D dstOffset{};
    VkExtent3D extent{};
};

struct VkBufferImageCopy {
    VkDeviceSize bufferOffset = 0;
    uint32_t bufferRowLength = 0;
    uint32_t bufferImageHeight = 0;
    VkImageSubresourceLayers imageSubresource{};
    VkOffset3D imageOffset{};
    VkExtent3D imageExtent{};
};

enum class RenderFormat { UNKNOWN, R32_FLOAT, R8G8B8A8_UNORM, BC1_UNORM };
enum class RenderTextureLayout {
    UNKNOWN, GENERAL, SHADER_READ, COLOR_WRITE, DEPTH_WRITE, DEPTH_READ,
    COPY_SOURCE, COPY_DEST, RESOLVE_SOURCE, RESOLVE_DEST, PRESENT
};
namespace RenderTextureFlag {
    enum Bits : uint32_t { NONE = 0U, RENDER_TARGET = 1U << 0, DEPTH_TARGET = 1U << 1 };
}
using RenderTextureFlags = uint32_t;

constexpr uint32_t RenderFormatSize(RenderFormat format) {
    switch (format) {
    case RenderFormat::R32_FLOAT:
    case RenderFormat::R8G8B8A8_UNORM:
        return 4;
    case RenderFormat::BC1_UNORM:
        return 8;
    default:
        return 1;
    }
}

constexpr uint32_t RenderFormatBlockWidth(RenderFormat format) {
    return format == RenderFormat::BC1_UNORM ? 4 : 1;
}

constexpr bool RenderFormatIsStencil(RenderFormat) { return false; }

struct RenderTextureDesc {
    uint32_t width = 0, height = 0, depth = 1;
    uint32_t mipLevels = 1, arraySize = 1;
    RenderFormat format = RenderFormat::UNKNOWN;
    RenderTextureFlags flags = RenderTextureFlag::NONE;
};

struct RenderTexture { virtual ~RenderTexture() = default; };
struct RenderBuffer { virtual ~RenderBuffer() = default; };

struct VulkanTexture : RenderTexture {
    VkImage vk = VK_NULL_HANDLE;
    RenderTextureDesc desc;
    RenderTextureLayout textureLayout = RenderTextureLayout::UNKNOWN;
};

struct VulkanBuffer : RenderBuffer {
    VkBuffer vk = VK_NULL_HANDLE;
};

enum class RenderTextureCopyType { UNKNOWN, SUBRESOURCE, PLACED_FOOTPRINT };

struct RenderTextureCopyLocation {
    const RenderTexture *texture = nullptr;
    const RenderBuffer *buffer = nullptr;
    RenderTextureCopyType type = RenderTextureCopyType::UNKNOWN;
    union {
        struct {
            RenderFormat format;
            uint32_t width, height, depth, rowWidth;
            uint64_t offset;
        } placedFootprint;
        struct { uint32_t mipLevel, arrayIndex; } subresource;
    };

    static RenderTextureCopyLocation PlacedFootprint(const RenderBuffer *buffer, RenderFormat format,
            uint32_t width, uint32_t height, uint32_t depth, uint32_t rowWidth, uint64_t offset = 0) {
        RenderTextureCopyLocation location;
        location.buffer = buffer;
        location.type = RenderTextureCopyType::PLACED_FOOTPRINT;
        location.placedFootprint = {format, width, height, depth, rowWidth, offset};
        return location;
    }

    static RenderTextureCopyLocation Subresource(const RenderTexture *texture,
            uint32_t mipLevel = 0, uint32_t arrayIndex = 0) {
        RenderTextureCopyLocation location;
        location.texture = texture;
        location.type = RenderTextureCopyType::SUBRESOURCE;
        location.subresource = {mipLevel, arrayIndex};
        return location;
    }
};

struct RenderBox {
    int32_t left = 0, top = 0, front = 0, right = 0, bottom = 0, back = 0;
};

VkImageLayout toImageLayout(RenderTextureLayout layout) {
    switch (layout) {
    case RenderTextureLayout::COPY_SOURCE:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case RenderTextureLayout::COPY_DEST:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    default:
        return 0;
    }
}

VkImageAspectFlags toAspectFlags(RenderFormat format, RenderTextureFlags flags) {
    VkImageAspectFlags aspect = (flags & RenderTextureFlag::DEPTH_TARGET) != 0
        ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    if ((flags & RenderTextureFlag::DEPTH_TARGET) != 0 && RenderFormatIsStencil(format)) {
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return aspect;
}

struct VulkanCommandList {
    VkCommandBuffer vk = 1;
    void endActiveRenderPass() {}
    void copyTextureRegion(const RenderTextureCopyLocation &dstLocation,
            const RenderTextureCopyLocation &srcLocation, uint32_t dstX = 0,
            uint32_t dstY = 0, uint32_t dstZ = 0, const RenderBox *srcBox = nullptr);
};

void vkCmdCopyImageToBuffer(VkCommandBuffer, VkImage src, VkImageLayout layout, VkBuffer dst,
        uint32_t regionCount, const VkBufferImageCopy *regions) {
    assert(regionCount == 1);
    const auto &copy = regions[0];
    std::printf("image-to-buffer src=%d layout=%d dst=%d offset=%llu row=%u height=%u "
                "aspect=%u mip=%u layer=%u image=%d,%d,%d extent=%u,%u,%u\n",
        src, layout, dst, (unsigned long long)copy.bufferOffset, copy.bufferRowLength,
        copy.bufferImageHeight, copy.imageSubresource.aspectMask, copy.imageSubresource.mipLevel,
        copy.imageSubresource.baseArrayLayer, copy.imageOffset.x, copy.imageOffset.y,
        copy.imageOffset.z, copy.imageExtent.width, copy.imageExtent.height, copy.imageExtent.depth);
}

void vkCmdCopyBufferToImage(VkCommandBuffer, VkBuffer src, VkImage dst, VkImageLayout layout,
        uint32_t regionCount, const VkBufferImageCopy *regions) {
    assert(regionCount == 1);
    const auto &copy = regions[0];
    std::printf("buffer-to-image src=%d layout=%d dst=%d offset=%llu row=%u height=%u "
                "aspect=%u mip=%u layer=%u image=%d,%d,%d extent=%u,%u,%u\n",
        src, layout, dst, (unsigned long long)copy.bufferOffset, copy.bufferRowLength,
        copy.bufferImageHeight, copy.imageSubresource.aspectMask, copy.imageSubresource.mipLevel,
        copy.imageSubresource.baseArrayLayer, copy.imageOffset.x, copy.imageOffset.y,
        copy.imageOffset.z, copy.imageExtent.width, copy.imageExtent.height, copy.imageExtent.depth);
}

void vkCmdCopyImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout,
        uint32_t, const void *) {
    std::puts("image-to-image");
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string scenario = argv[1];
    VulkanCommandList list;
    VulkanBuffer buffer;
    buffer.vk = 71;
    VulkanTexture texture;
    texture.vk = 41;

    if (scenario == "padded-offset") {
        // A 5x3 read into a 5x5 footprint with an 8-texel row pitch at byte
        // offset 64: the copy starts one texel into the second row.
        texture.desc = {5, 3, 1, 1, 1, RenderFormat::R8G8B8A8_UNORM, RenderTextureFlag::NONE};
        texture.textureLayout = RenderTextureLayout::COPY_SOURCE;
        list.copyTextureRegion(
            RenderTextureCopyLocation::PlacedFootprint(&buffer, RenderFormat::R8G8B8A8_UNORM,
                5, 5, 1, 8, 64),
            RenderTextureCopyLocation::Subresource(&texture), 1, 1, 0, nullptr);
    }
    else if (scenario == "subresource-srcbox") {
        // Depth aspect, mip 2, array layer 5 and a partial source box.
        texture.vk = 42;
        texture.desc = {32, 32, 1, 4, 8, RenderFormat::R32_FLOAT, RenderTextureFlag::DEPTH_TARGET};
        texture.textureLayout = RenderTextureLayout::COPY_SOURCE;
        const RenderBox box{4, 2, 0, 10, 6, 1};
        list.copyTextureRegion(
            RenderTextureCopyLocation::PlacedFootprint(&buffer, RenderFormat::R32_FLOAT,
                16, 6, 1, 16, 128),
            RenderTextureCopyLocation::Subresource(&texture, 2, 5), 2, 3, 0, &box);
    }
    else if (scenario == "compressed") {
        // BC1 uses 4x4 blocks of 8 bytes, so dstX/dstY are block-aligned and
        // the row pitch and image height round up to whole blocks.
        texture.vk = 43;
        texture.desc = {16, 8, 1, 1, 1, RenderFormat::BC1_UNORM, RenderTextureFlag::NONE};
        texture.textureLayout = RenderTextureLayout::COPY_SOURCE;
        list.copyTextureRegion(
            RenderTextureCopyLocation::PlacedFootprint(&buffer, RenderFormat::BC1_UNORM,
                16, 8, 1, 16, 256),
            RenderTextureCopyLocation::Subresource(&texture), 4, 4, 0, nullptr);
    }
    else if (scenario == "upload") {
        // The existing buffer-to-image branch stays unchanged.
        texture.vk = 44;
        texture.desc = {5, 3, 1, 1, 1, RenderFormat::R8G8B8A8_UNORM, RenderTextureFlag::NONE};
        texture.textureLayout = RenderTextureLayout::COPY_DEST;
        list.copyTextureRegion(
            RenderTextureCopyLocation::Subresource(&texture),
            RenderTextureCopyLocation::PlacedFootprint(&buffer, RenderFormat::R8G8B8A8_UNORM,
                5, 3, 1, 8, 512));
    }
    else if (scenario == "image-to-image") {
        VulkanTexture destination;
        destination.vk = 45;
        destination.desc = texture.desc = {5, 3, 1, 1, 1, RenderFormat::R8G8B8A8_UNORM,
            RenderTextureFlag::NONE};
        destination.textureLayout = RenderTextureLayout::COPY_DEST;
        texture.textureLayout = RenderTextureLayout::COPY_SOURCE;
        list.copyTextureRegion(RenderTextureCopyLocation::Subresource(&destination),
            RenderTextureCopyLocation::Subresource(&texture));
    }
    else {
        return 2;
    }
    return 0;
}

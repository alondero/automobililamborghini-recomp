// Compiled with Plume's real clear-rectangle builder and clear commands appended.
// The Vulkan boundary prints each vkCmdClearAttachments rectangle so scenarios
// can compare it with the active render area.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using VkCommandBuffer = int;
constexpr uint32_t VK_IMAGE_ASPECT_COLOR_BIT = 1, VK_IMAGE_ASPECT_DEPTH_BIT = 2,
                   VK_IMAGE_ASPECT_STENCIL_BIT = 4;
struct VkOffset2D { int32_t x, y; };
struct VkExtent2D { uint32_t width, height; };
struct VkRect2D { VkOffset2D offset; VkExtent2D extent; };
struct VkClearRect { VkRect2D rect; uint32_t baseArrayLayer, layerCount; };
union VkClearColorValue { float float32[4]; };
struct VkClearDepthStencilValue { float depth; uint32_t stencil; };
union VkClearValue { VkClearColorValue color; VkClearDepthStencilValue depthStencil; };
struct VkClearAttachment { uint32_t aspectMask, colorAttachment; VkClearValue clearValue; };
struct RenderRect { int32_t left, top, right, bottom; };
struct RenderColor { float r, g, b, a; };
struct VulkanFramebuffer {
    uint32_t width, height;
    std::vector<int> colorAttachments{ 1 };
    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }
};
struct VulkanCommandList {
    VkCommandBuffer vk = 1;
    const VulkanFramebuffer *targetFramebuffer = nullptr;
    void checkActiveRenderPass() {}
    void clearColor(uint32_t, RenderColor, const RenderRect *, uint32_t);
    void clearDepthStencil(bool, bool, float, uint32_t, const RenderRect *, uint32_t);
};

void vkCmdClearAttachments(VkCommandBuffer, uint32_t, const VkClearAttachment *,
                           uint32_t count, const VkClearRect *rects) {
    std::printf("clear");
    for (uint32_t i = 0; i < count; ++i) {
        const VkRect2D &r = rects[i].rect;
        std::printf(" %d,%d,%u,%u", r.offset.x, r.offset.y, r.extent.width, r.extent.height);
    }
    std::printf("\n");
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string scenario = argv[1];
    VulkanFramebuffer framebuffer{ 320, 241 };
    VulkanCommandList list;
    list.targetFramebuffer = &framebuffer;
    const RenderColor black{ 0, 0, 0, 1 };
    std::vector<RenderRect> rects;
    // The attract race's inclusive N64 fill rectangle at native and 2x scale.
    if (scenario == "native-fill") rects = { { 0, 0, 321, 241 } };
    else if (scenario == "scaled-depth-fill") {
        framebuffer = { 640, 482 };
        const RenderRect rect{ 0, 0, 642, 482 };
        list.clearDepthStencil(true, false, 1.0f, 0, &rect, 1);
        return 0;
    }
    else if (scenario == "negative-origin") rects = { { -4, -2, 10, 10 } };
    else if (scenario == "inside") rects = { { 10, 20, 30, 40 } };
    else if (scenario == "outside") rects = { { 320, 0, 330, 10 } };
    else if (scenario == "mixed") rects = { { 400, 0, 410, 10 }, { 300, 230, 330, 250 } };
    else if (scenario != "whole-target") return 2;
    list.clearColor(0, black, rects.data(), uint32_t(rects.size()));
    return 0;
}

// ROM-free offscreen upload/draw/readback through the patched Plume backend.
#include "plume_vulkan.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

using namespace plume;

static void require(bool condition, const char *message) {
    if (!condition) { fprintf(stderr, "%s\n", message); std::exit(1); }
}

static std::atomic<unsigned> validationErrors{0};
static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT *data, void *) {
    fprintf(stderr, "%s\n", data->pMessage);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++validationErrors;
    return VK_FALSE;
}

static std::vector<char> readFile(const char *path) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "cannot read SPIR-V shader");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

static void reportMemory(VulkanDevice &device, const VulkanBuffer &buffer, const char *label) {
    VkMemoryPropertyFlags flags;
    vmaGetAllocationMemoryProperties(device.allocator, buffer.allocation, &flags);
    printf("%s: heap=%u type=%u flags=0x%X visible=%u coherent=%u cached=%u\n", label,
        unsigned(buffer.desc.heapType), buffer.allocationInfo.memoryType, flags,
        !!(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), !!(flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        !!(flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT));
}

static std::unique_ptr<VulkanBuffer> makeBuffer(VulkanDevice &device, const RenderBufferDesc &desc,
        VkBufferUsageFlags usage, bool forceNoncoherent, const char *label) {
    auto buffer = std::make_unique<VulkanBuffer>(&device, nullptr, desc);
    require(buffer->vk != VK_NULL_HANDLE, "buffer allocation failed");
    if (forceNoncoherent) {
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device.vk, buffer->vk, &requirements);
        VkPhysicalDeviceMemoryProperties properties;
        vkGetPhysicalDeviceMemoryProperties(device.physicalDevice, &properties);
        uint32_t bits = 0;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            const auto flags = properties.memoryTypes[i].propertyFlags;
            if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && !(flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                bits |= requirements.memoryTypeBits & (1U << i);
        }
        if (bits) {
            // Force a compatible type only in the probe. Production still lets VMA choose.
            vmaDestroyBuffer(device.allocator, buffer->vk, buffer->allocation);
            buffer->vk = VK_NULL_HANDLE;
            buffer->allocation = VK_NULL_HANDLE;
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = desc.size;
            info.usage = usage;
            VmaAllocationCreateInfo allocation{};
            allocation.usage = VMA_MEMORY_USAGE_AUTO;
            allocation.flags = desc.heapType == RenderHeapType::READBACK ?
                VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            allocation.memoryTypeBits = bits;
            require(vmaCreateBuffer(device.allocator, &info, &allocation, &buffer->vk, &buffer->allocation,
                &buffer->allocationInfo) == VK_SUCCESS, "forced noncoherent allocation failed");
        } else {
            printf("%s: no compatible host-visible noncoherent memory type\n", label);
        }
    }
    reportMemory(device, *buffer, label);
    return buffer;
}

static void draw(VulkanDevice &device, const std::vector<char> &vs, const std::vector<char> &ps,
        bool partial, bool forceNoncoherent) {
    auto queue = device.createCommandQueue(RenderCommandListType::DIRECT);
    auto list = queue->createCommandList();
    auto fence = device.createCommandFence();
    auto upload = makeBuffer(device, RenderBufferDesc::UploadBuffer(256),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, forceNoncoherent, "upload");
    auto geometry = device.createBuffer(RenderBufferDesc::VertexBuffer(256, RenderHeapType::DEFAULT));
    auto readback = makeBuffer(device, RenderBufferDesc::ReadbackBuffer(256),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT, forceNoncoherent, "readback");

    // A full-screen triangle with uploaded positions and red vertex colors.
    const float vertices[] = {-1,-1, 1,0,0,1, -1,3, 1,0,0,1, 3,-1, 1,0,0,1};
    const RenderRange noRead{0, 0}, written{16, 16 + sizeof(vertices)}, read{16, 80};
    auto *bytes = static_cast<uint8_t *>(upload->map(0, &noRead));
    memcpy(bytes + written.begin, vertices, sizeof(vertices));
    upload->unmap(0, partial ? &written : nullptr);

    RenderPipelineLayoutDesc layoutDesc;
    layoutDesc.allowInputLayout = true;
    auto layout = device.createPipelineLayout(layoutDesc);
    auto vertex = device.createShader(vs.data(), vs.size(), "VSMain", RenderShaderFormat::SPIRV);
    auto pixel = device.createShader(ps.data(), ps.size(), "PSMain", RenderShaderFormat::SPIRV);
    RenderInputSlot slot{0, 6 * sizeof(float)};
    const RenderInputElement elements[] = {
        {"POSITION", 0, 0, RenderFormat::R32G32_FLOAT, 0, 0},
        {"COLOR", 0, 1, RenderFormat::R32G32B32A32_FLOAT, 0, 2 * sizeof(float)}
    };
    RenderGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.pipelineLayout = layout.get();
    pipelineDesc.vertexShader = vertex.get();
    pipelineDesc.pixelShader = pixel.get();
    pipelineDesc.inputSlots = &slot;
    pipelineDesc.inputSlotsCount = 1;
    pipelineDesc.inputElements = elements;
    pipelineDesc.inputElementsCount = 2;
    pipelineDesc.renderTargetCount = 1;
    pipelineDesc.renderTargetFormat[0] = RenderFormat::R8G8B8A8_UNORM;
    pipelineDesc.renderTargetBlend[0] = RenderBlendDesc::Copy();
    auto pipeline = device.createGraphicsPipeline(pipelineDesc);
    require(static_cast<VulkanGraphicsPipeline *>(pipeline.get())->vk != VK_NULL_HANDLE, "pipeline failed");
    auto target = device.createTexture(RenderTextureDesc::Texture2D(4, 4, 1,
        RenderFormat::R8G8B8A8_UNORM, RenderTextureFlag::RENDER_TARGET));
    const RenderTexture *attachment = target.get();
    auto framebuffer = device.createFramebuffer(RenderFramebufferDesc(&attachment, 1));

    list->begin();
    list->copyBufferRegion(geometry->at(written.begin), upload->at(written.begin), sizeof(vertices));
    list->barriers(RenderBarrierStage::GRAPHICS, RenderBufferBarrier(geometry.get(), RenderBufferAccess::READ));
    list->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(target.get(), RenderTextureLayout::COLOR_WRITE));
    list->setFramebuffer(framebuffer.get());
    list->clearColor(0, RenderColor(0, 0, 0, 1));
    list->setPipeline(pipeline.get());
    list->setGraphicsPipelineLayout(layout.get());
    list->setViewports(RenderViewport(0, 0, 4, 4));
    list->setScissors(RenderRect(0, 0, 4, 4));
    const RenderVertexBufferView view{geometry->at(written.begin), sizeof(vertices)};
    list->setVertexBuffers(0, &view, 1, &slot);
    list->drawInstanced(3, 1, 0, 0);
    list->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(target.get(), RenderTextureLayout::COPY_SOURCE));
    // Plume's pinned image-to-buffer copy dereferences a null dstTexture.
    // Use native Vulkan here to isolate mapped-memory visibility from that bug.
    const auto command = static_cast<VulkanCommandList *>(list.get())->vk;
    VkBufferImageCopy copy{};
    copy.bufferOffset = read.begin;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {4, 4, 1};
    vkCmdCopyImageToBuffer(command, static_cast<VulkanTexture *>(target.get())->vk,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback->vk, 1, &copy);
    VkBufferMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hostBarrier.srcQueueFamilyIndex = hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.buffer = readback->vk;
    hostBarrier.offset = read.begin;
    hostBarrier.size = read.end - read.begin;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &hostBarrier, 0, nullptr);
    list->end();
    queue->executeCommandLists(list.get(), fence.get());
    const auto nativeFence = static_cast<VulkanCommandFence *>(fence.get())->vk;
    require(vkWaitForFences(device.vk, 1, &nativeFence, VK_TRUE, 30000000000ULL) == VK_SUCCESS,
        "GPU completion fence failed");
    printf("graphics family=%u, GPU completion fence waited\n", device.queueFamilyIndices[0]);
    bytes = static_cast<uint8_t *>(readback->map(0, partial ? &read : nullptr));
    for (unsigned i = 16; i < 80; i += 4) {
        require(bytes[i] == 255 && bytes[i+1] == 0 && bytes[i+2] == 0 && bytes[i+3] == 255,
            "upload/draw/readback pixel mismatch");
    }
    readback->unmap(0, nullptr);
    puts("PASS: 16 red pixels read back");
}

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    require(argc == 3, "expected vertex and pixel SPIR-V paths");
    VulkanInterface renderInterface;
    require(renderInterface.isValid(), "Vulkan instance unavailable");
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    bool validationAvailable = false;
    for (const auto &layer : layers) {
        if (!strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation")) validationAvailable = true;
    }
    printf("Khronos validation: %s\n", validationAvailable ? "enabled by debug Plume" : "UNAVAILABLE");
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    if (vkCreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback = validation;
        require(vkCreateDebugUtilsMessengerEXT(renderInterface.instance, &info, nullptr, &messenger) == VK_SUCCESS,
            "validation messenger failed");
    }
    const auto vs = readFile(argv[1]), ps = readFile(argv[2]);
    require(!renderInterface.getDeviceNames().empty(), "UNAVAILABLE: no Vulkan device; no experiment ran");
    for (const auto &name : renderInterface.getDeviceNames()) {
        auto base = renderInterface.createDevice(name);
        require(bool(base), "Vulkan device unavailable");
        auto &device = *static_cast<VulkanDevice *>(base.get());
        printf("device=%s vendor=0x%X driver=0x%X atom=%llu families=%u,%u,%u\n", name.c_str(),
            device.physicalDeviceProperties.vendorID, device.physicalDeviceProperties.driverVersion,
            (unsigned long long)device.physicalDeviceProperties.limits.nonCoherentAtomSize,
            device.queueFamilyIndices[0], device.queueFamilyIndices[1], device.queueFamilyIndices[2]);
        VkPhysicalDeviceMemoryProperties memory;
        vkGetPhysicalDeviceMemoryProperties(device.physicalDevice, &memory);
        bool noncoherent = false;
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            const auto flags = memory.memoryTypes[i].propertyFlags;
            printf("memory type=%u heap=%u flags=0x%X\n", i, memory.memoryTypes[i].heapIndex, flags);
            noncoherent |= (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && !(flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        }
        auto defaultBuffer = makeBuffer(device, RenderBufferDesc::DefaultBuffer(256),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, "default");
        if (device.capabilities.gpuUploadHeap) {
            auto desc = RenderBufferDesc::UploadBuffer(256);
            desc.heapType = RenderHeapType::GPU_UPLOAD;
            auto gpuUpload = makeBuffer(device, desc, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                false, "gpu-upload");
        }
        for (bool partial : {false, true}) {
            printf("ranges=%s\n", partial ? "partial" : "omitted");
            draw(device, vs, ps, partial, false);
            if (noncoherent) draw(device, vs, ps, partial, true);
        }
        if (!noncoherent) puts("UNVERIFIED: physical noncoherent memory; this device offers no host-visible noncoherent type");
    }
    if (messenger) vkDestroyDebugUtilsMessengerEXT(renderInterface.instance, messenger, nullptr);
    require(validationErrors == 0, "Vulkan validation errors observed");
}

// Compiled with Plume's real VulkanSwapChain declaration and implementation
// appended by tests/test_vulkan_swapchain.py. Only the window-system surface
// block and getWindowSize are replaced; everything else is production code.
//
// The Vulkan boundary enforces the VkSwapchainCreateInfoKHR contract against
// the reported surface capabilities and models the Android loader's surface
// ownership: one active chain per surface, oldSwapchain is retired even when
// creation fails, and a retired chain cannot be replaced again or acquired from.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

using VkFlags = uint32_t;
using VkBool32 = uint32_t;
using VkInstance = uintptr_t;
using VkPhysicalDevice = uintptr_t;
using VkDevice = uintptr_t;
using VkQueue = uintptr_t;
using VkSurfaceKHR = uintptr_t;
using VkSwapchainKHR = uintptr_t;
using VkImage = uintptr_t;
using VkImageView = uintptr_t;
using VkSemaphore = uintptr_t;
using VkFence = uintptr_t;
using VkAllocationCallbacks = void;
constexpr uintptr_t VK_NULL_HANDLE = 0;
constexpr VkBool32 VK_TRUE = 1;

enum VkResult {
    VK_SUCCESS = 0,
    VK_INCOMPLETE = 5,
    VK_ERROR_OUT_OF_DEVICE_MEMORY = -2,
    VK_ERROR_SURFACE_LOST_KHR = -1000000000,
    VK_ERROR_NATIVE_WINDOW_IN_USE_KHR = -1000000001,
    VK_SUBOPTIMAL_KHR = 1000001003,
    VK_ERROR_OUT_OF_DATE_KHR = -1000001004,
};
enum VkStructureType {
    VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR = 1000001000,
    VK_STRUCTURE_TYPE_PRESENT_INFO_KHR = 1000001001,
    VK_STRUCTURE_TYPE_PRESENT_ID_KHR = 1000294000,
};
enum VkFormat { VK_FORMAT_UNDEFINED = 0, VK_FORMAT_R8G8B8A8_UNORM = 37, VK_FORMAT_B8G8R8A8_UNORM = 44 };
enum VkColorSpaceKHR { VK_COLOR_SPACE_SRGB_NONLINEAR_KHR = 0, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT = 1000104002 };
enum VkPresentModeKHR {
    VK_PRESENT_MODE_IMMEDIATE_KHR = 0,
    VK_PRESENT_MODE_MAILBOX_KHR = 1,
    VK_PRESENT_MODE_FIFO_KHR = 2,
};
enum VkSharingMode { VK_SHARING_MODE_EXCLUSIVE = 0 };
enum VkCompositeAlphaFlagBitsKHR {
    VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR = 0x1,
    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR = 0x2,
    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR = 0x4,
    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR = 0x8,
};
enum VkSurfaceTransformFlagBitsKHR {
    VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR = 0x1,
    VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR = 0x2,
};
enum VkImageUsageFlagBits {
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT = 0x1,
    VK_IMAGE_USAGE_TRANSFER_DST_BIT = 0x2,
    VK_IMAGE_USAGE_SAMPLED_BIT = 0x4,
    VK_IMAGE_USAGE_STORAGE_BIT = 0x8,
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 0x10,
    VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT = 0x80,
};
using VkCompositeAlphaFlagsKHR = VkFlags;
using VkSurfaceTransformFlagsKHR = VkFlags;
using VkImageUsageFlags = VkFlags;

struct VkExtent2D { uint32_t width; uint32_t height; };
struct VkSurfaceCapabilitiesKHR {
    uint32_t minImageCount;
    uint32_t maxImageCount;
    VkExtent2D currentExtent;
    VkExtent2D minImageExtent;
    VkExtent2D maxImageExtent;
    uint32_t maxImageArrayLayers;
    VkSurfaceTransformFlagsKHR supportedTransforms;
    VkSurfaceTransformFlagBitsKHR currentTransform;
    VkCompositeAlphaFlagsKHR supportedCompositeAlpha;
    VkImageUsageFlags supportedUsageFlags;
};
struct VkSurfaceFormatKHR { VkFormat format; VkColorSpaceKHR colorSpace; };
struct VkSwapchainCreateInfoKHR {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    VkSurfaceKHR surface;
    uint32_t minImageCount;
    VkFormat imageFormat;
    VkColorSpaceKHR imageColorSpace;
    VkExtent2D imageExtent;
    uint32_t imageArrayLayers;
    VkImageUsageFlags imageUsage;
    VkSharingMode imageSharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
    VkSurfaceTransformFlagBitsKHR preTransform;
    VkCompositeAlphaFlagBitsKHR compositeAlpha;
    VkPresentModeKHR presentMode;
    VkBool32 clipped;
    VkSwapchainKHR oldSwapchain;
};
struct VkPresentInfoKHR {
    VkStructureType sType;
    const void *pNext;
    uint32_t waitSemaphoreCount;
    const VkSemaphore *pWaitSemaphores;
    uint32_t swapchainCount;
    const VkSwapchainKHR *pSwapchains;
    const uint32_t *pImageIndices;
    VkResult *pResults;
};
struct VkPresentIdKHR {
    VkStructureType sType;
    const void *pNext;
    uint32_t swapchainCount;
    const uint64_t *pPresentIds;
};
struct VkRefreshCycleDurationGOOGLE { uint64_t refreshDuration; };

// Recording driver state.
namespace driver {
    enum class State { Live, Retired, Destroyed };
    struct Chain {
        State state = State::Live;
        std::vector<VkImage> images;
    };

    VkSurfaceCapabilitiesKHR caps;
    VkResult capsResult = VK_SUCCESS;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
    VkResult createResult = VK_SUCCESS;
    uint32_t extraImages = 0;
    uint32_t missingImages = 0;
    uint32_t windowWidth = 0, windowHeight = 0;

    std::map<VkSwapchainKHR, Chain> chains;
    std::vector<VkSwapchainCreateInfoKHR> attempts;
    std::vector<VkSwapchainCreateInfoKHR> created;
    std::unordered_set<VkImageView> liveViews;
    VkSwapchainKHR activeChain = VK_NULL_HANDLE;
    bool surfaceAlive = false;
    uintptr_t nextHandle = 100;
    int violations = 0;

    void violation(const char *format, ...) {
        va_list args;
        va_start(args, format);
        printf("VIOLATION: ");
        vprintf(format, args);
        printf("\n");
        va_end(args);
        ++violations;
    }

    bool singleBit(VkFlags value) { return (value != 0) && ((value & (value - 1)) == 0); }

    Chain *find(VkSwapchainKHR handle) {
        auto it = chains.find(handle);
        return (it == chains.end()) ? nullptr : &it->second;
    }

    void reset() {
        caps = {};
        caps.minImageCount = 2;
        caps.maxImageCount = 8;
        caps.currentExtent = {1280, 720};
        caps.minImageExtent = {1, 1};
        caps.maxImageExtent = {4096, 4096};
        caps.maxImageArrayLayers = 1;
        caps.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
        caps.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        // The Android loader reports inherit only and this usage set.
        caps.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        caps.supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
        formats = {{VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
                   {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
        presentModes = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR};
        windowWidth = 1280;
        windowHeight = 720;
    }
}

VkResult vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32 *supported) {
    *supported = VK_TRUE;
    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *caps) {
    if (!driver::surfaceAlive || (surface == VK_NULL_HANDLE)) driver::violation("capability query on a dead surface");
    if (driver::capsResult != VK_SUCCESS) {
        // The output is undefined on failure; make reliance on it visible.
        memset(caps, 0, sizeof(*caps));
        return driver::capsResult;
    }
    *caps = driver::caps;
    return VK_SUCCESS;
}

template<typename T>
static VkResult enumerate(const std::vector<T> &values, uint32_t *count, T *out) {
    if (out == nullptr) {
        *count = uint32_t(values.size());
        return VK_SUCCESS;
    }
    const uint32_t written = std::min(*count, uint32_t(values.size()));
    std::copy(values.begin(), values.begin() + written, out);
    *count = written;
    return (written < values.size()) ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t *count, VkSurfaceFormatKHR *formats) {
    return enumerate(driver::formats, count, formats);
}

VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t *count, VkPresentModeKHR *modes) {
    return enumerate(driver::presentModes, count, modes);
}

VkResult vkCreateSwapchainKHR(VkDevice, const VkSwapchainCreateInfoKHR *info, const VkAllocationCallbacks *, VkSwapchainKHR *out) {
    using namespace driver;
    attempts.push_back(*info);
    // Android: CreateSwapchainKHR refuses an oldSwapchain other than the surface's active chain.
    if (info->oldSwapchain != activeChain) {
        violation("oldSwapchain %llu is not the surface's active chain %llu",
            (unsigned long long)info->oldSwapchain, (unsigned long long)activeChain);
        return VK_ERROR_NATIVE_WINDOW_IN_USE_KHR;
    }

    // oldSwapchain is retired even if creation fails.
    if (info->oldSwapchain != VK_NULL_HANDLE) {
        find(info->oldSwapchain)->state = State::Retired;
        activeChain = VK_NULL_HANDLE;
    }

    const VkExtent2D extent = info->imageExtent;
    if ((extent.width < caps.minImageExtent.width) || (extent.width > caps.maxImageExtent.width) ||
        (extent.height < caps.minImageExtent.height) || (extent.height > caps.maxImageExtent.height)) {
        violation("imageExtent %ux%u outside %ux%u..%ux%u", extent.width, extent.height,
            caps.minImageExtent.width, caps.minImageExtent.height, caps.maxImageExtent.width, caps.maxImageExtent.height);
    }
    if ((extent.width == 0) || (extent.height == 0)) violation("zero imageExtent");
    if (info->imageUsage & ~caps.supportedUsageFlags) {
        violation("imageUsage 0x%X not in supported 0x%X", info->imageUsage, caps.supportedUsageFlags);
    }
    if (!singleBit(info->preTransform) || !(info->preTransform & caps.supportedTransforms)) {
        violation("preTransform 0x%X not in supported 0x%X", info->preTransform, caps.supportedTransforms);
    }
    if (!singleBit(info->compositeAlpha) || !(info->compositeAlpha & caps.supportedCompositeAlpha)) {
        violation("compositeAlpha 0x%X not in supported 0x%X", info->compositeAlpha, caps.supportedCompositeAlpha);
    }
    if ((info->minImageCount < caps.minImageCount) || ((caps.maxImageCount != 0) && (info->minImageCount > caps.maxImageCount))) {
        violation("minImageCount %u outside %u..%u", info->minImageCount, caps.minImageCount, caps.maxImageCount);
    }
    const bool formatSupported = std::any_of(formats.begin(), formats.end(), [&](const VkSurfaceFormatKHR &f) {
        return (f.format == info->imageFormat) && (f.colorSpace == info->imageColorSpace);
    });
    if (!formatSupported) violation("format %d/%d not supported", info->imageFormat, info->imageColorSpace);
    if (std::find(presentModes.begin(), presentModes.end(), info->presentMode) == presentModes.end()) {
        violation("present mode %d not supported", info->presentMode);
    }

    if (createResult != VK_SUCCESS) return createResult;

    const VkSwapchainKHR handle = nextHandle++;
    Chain &chain = chains[handle];
    const uint32_t imageCount = info->minImageCount + extraImages - missingImages;
    for (uint32_t i = 0; i < imageCount; i++) chain.images.push_back(nextHandle++);
    activeChain = handle;
    created.push_back(*info);
    *out = handle;
    return VK_SUCCESS;
}

void vkDestroySwapchainKHR(VkDevice, VkSwapchainKHR handle, const VkAllocationCallbacks *) {
    using namespace driver;
    if (handle == VK_NULL_HANDLE) return;
    Chain *chain = find(handle);
    if ((chain == nullptr) || (chain->state == State::Destroyed)) {
        violation("destroying unknown or destroyed chain %llu", (unsigned long long)handle);
        return;
    }
    chain->state = State::Destroyed;
    if (activeChain == handle) activeChain = VK_NULL_HANDLE;
}

VkResult vkGetSwapchainImagesKHR(VkDevice, VkSwapchainKHR handle, uint32_t *count, VkImage *images) {
    driver::Chain *chain = driver::find(handle);
    if ((chain == nullptr) || (chain->state == driver::State::Destroyed)) {
        driver::violation("image query on unknown or destroyed chain");
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    return enumerate(chain->images, count, images);
}

VkResult vkAcquireNextImageKHR(VkDevice, VkSwapchainKHR handle, uint64_t, VkSemaphore, VkFence, uint32_t *index) {
    driver::Chain *chain = driver::find(handle);
    if ((chain == nullptr) || (chain->state != driver::State::Live)) {
        driver::violation("acquire from %s chain %llu", (chain == nullptr) ? "unknown" :
            ((chain->state == driver::State::Retired) ? "retired" : "destroyed"), (unsigned long long)handle);
        return VK_ERROR_OUT_OF_DATE_KHR;
    }
    *index = 0;
    return VK_SUCCESS;
}

VkResult vkQueuePresentKHR(VkQueue, const VkPresentInfoKHR *info) {
    driver::Chain *chain = driver::find(info->pSwapchains[0]);
    if ((chain == nullptr) || (chain->state != driver::State::Live)) {
        driver::violation("present to a chain that is not live");
        return VK_ERROR_OUT_OF_DATE_KHR;
    }
    return VK_SUCCESS;
}

VkResult vkWaitForPresentKHR(VkDevice, VkSwapchainKHR, uint64_t, uint64_t) { return VK_SUCCESS; }

VkResult vkGetRefreshCycleDurationGOOGLE(VkDevice, VkSwapchainKHR, VkRefreshCycleDurationGOOGLE *cycle) {
    cycle->refreshDuration = 16666667;
    return VK_SUCCESS;
}

void vkDestroyImageView(VkDevice, VkImageView view, const VkAllocationCallbacks *) {
    if (driver::liveViews.erase(view) == 0) driver::violation("destroying unknown image view");
}

void vkDestroySurfaceKHR(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks *) {
    for (const auto &[handle, chain] : driver::chains) {
        if (chain.state != driver::State::Destroyed) driver::violation("surface destroyed before chain %llu", (unsigned long long)handle);
    }
    driver::surfaceAlive = false;
}

static VkSurfaceKHR fixtureCreateSurface() {
    driver::surfaceAlive = true;
    return 7;
}

namespace plume {
    typedef int RenderWindow;
    enum class RenderFormat { UNKNOWN, R8G8B8A8_UNORM, B8G8R8A8_UNORM };
    enum class RenderTextureDimension { UNKNOWN, TEXTURE_2D };
    namespace RenderTextureFlag {
        enum Bits : uint32_t { NONE = 0, RENDER_TARGET = 1 };
    }
    struct RenderTextureDesc {
        RenderTextureDimension dimension = RenderTextureDimension::UNKNOWN;
        uint32_t width = 0, height = 0, depth = 0, mipLevels = 0, arraySize = 0;
        RenderFormat format = RenderFormat::UNKNOWN;
        uint32_t flags = 0;
    };
    struct RenderTexture { virtual ~RenderTexture() { } };
    struct RenderCommandSemaphore { virtual ~RenderCommandSemaphore() { } };

    struct RenderSwapChainDesc {
        RenderWindow renderWindow = {};
        RenderFormat format = RenderFormat::UNKNOWN;
        uint32_t textureCount = 0;
        bool enablePresentWait = false;
        uint32_t maxFrameLatency = 0;
    };

    struct RenderSwapChain {
        virtual ~RenderSwapChain() { }
        virtual bool present(uint32_t textureIndex, RenderCommandSemaphore **waitSemaphores, uint32_t waitSemaphoreCount) = 0;
        virtual void wait() = 0;
        virtual bool resize() = 0;
        virtual bool needsResize() const = 0;
        virtual void setVsyncEnabled(bool vsyncEnabled) = 0;
        virtual bool isVsyncEnabled() const = 0;
        virtual uint32_t getWidth() const = 0;
        virtual uint32_t getHeight() const = 0;
        virtual RenderTexture *getTexture(uint32_t textureIndex) = 0;
        virtual uint32_t getTextureCount() const = 0;
        virtual bool acquireTexture(RenderCommandSemaphore *signalSemaphore, uint32_t *textureIndex) = 0;
        virtual RenderWindow getWindow() const = 0;
        virtual bool isEmpty() const = 0;
        virtual uint32_t getRefreshRate() const = 0;
    };

    struct VulkanInterface { VkInstance instance = 1; };
    struct VulkanDevice {
        VkDevice vk = 2;
        VkPhysicalDevice physicalDevice = 3;
        VulkanInterface *renderInterface = nullptr;
        struct { bool presentWait = false; } capabilities;
    };
    struct VulkanQueue {
        VkQueue vk = 4;
        std::unique_ptr<std::mutex> mutex = std::make_unique<std::mutex>();
    };
    struct VulkanSwapChain;
    struct VulkanCommandQueue {
        VulkanQueue *queue = nullptr;
        VulkanDevice *device = nullptr;
        uint32_t familyIndex = 0;
        std::unordered_set<VulkanSwapChain *> swapChains;
    };
    struct VulkanCommandSemaphore : RenderCommandSemaphore { VkSemaphore vk = 5; };

    struct VulkanTexture : RenderTexture {
        VkImage vk = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VulkanDevice *device = nullptr;
        RenderTextureDesc desc;

        VulkanTexture() = default;
        VulkanTexture(VulkanDevice *device, VkImage image) : vk(image), device(device) { }
        ~VulkanTexture() override {
            if (imageView != VK_NULL_HANDLE) vkDestroyImageView(device->vk, imageView, nullptr);
        }
        void fillSubresourceRange() { }
        void createImageView(VkFormat) {
            bool live = false;
            for (const auto &[handle, chain] : driver::chains) {
                if ((chain.state == driver::State::Live) && (std::find(chain.images.begin(), chain.images.end(), vk) != chain.images.end())) live = true;
            }
            if (!live) driver::violation("image view for an image outside the live chain");
            imageView = driver::nextHandle++;
            driver::liveViews.insert(imageView);
        }
    };

    VkFormat toVk(RenderFormat format) {
        switch (format) {
        case RenderFormat::R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case RenderFormat::B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        default: return VK_FORMAT_UNDEFINED;
        }
    }
}

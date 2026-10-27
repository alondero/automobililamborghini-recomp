// Appended after Plume's production swap chain code. Each scenario drives the
// swap chain the way RT64's present thread does and checks the recording driver.
namespace plume {
    void VulkanSwapChain::getWindowSize(uint32_t &dstWidth, uint32_t &dstHeight) const {
        dstWidth = driver::windowWidth;
        dstHeight = driver::windowHeight;
    }
}

using namespace plume;

static int failures = 0;

#define CHECK(condition) do { if (!(condition)) { printf("FAILED: %s (line %d)\n", #condition, __LINE__); ++failures; } } while (0)

struct Harness {
    VulkanInterface renderInterface;
    VulkanDevice device;
    VulkanQueue queue;
    VulkanCommandQueue commandQueue;
    std::unique_ptr<VulkanSwapChain> swapChain;
    bool valid = false;
    uint32_t refreshRate = 0;

    explicit Harness(uint32_t textureCount) {
        device.renderInterface = &renderInterface;
        commandQueue.queue = &queue;
        commandQueue.device = &device;
        RenderSwapChainDesc desc;
        desc.renderWindow = 1;
        desc.format = RenderFormat::R8G8B8A8_UNORM;
        desc.textureCount = textureCount;
        swapChain = std::make_unique<VulkanSwapChain>(&commandQueue, desc);
        valid = !swapChain->needsResize();
    }

    // Mirrors RT64 PresentQueue::threadLoop and threadPresent.
    bool frame() {
        if (swapChain->needsResize() || !valid) {
            valid = swapChain->resize();
        }
        // RT64 reads the refresh rate every frame, even after a failed resize, when the
        // device has display timing (always on Android).
        refreshRate = swapChain->getRefreshRate();
        if (swapChain->isEmpty() || !valid) {
            return false;
        }
        VulkanCommandSemaphore semaphore;
        uint32_t index = 0;
        valid = swapChain->acquireTexture(&semaphore, &index);
        if (valid) {
            RenderCommandSemaphore *wait = &semaphore;
            valid = swapChain->present(index, &wait, 1);
        }
        return valid;
    }

    void frames(int count) {
        for (int i = 0; i < count; i++) frame();
    }
};

static const VkSwapchainCreateInfoKHR &lastCreated() {
    assert(!driver::created.empty());
    return driver::created.back();
}

static void checkNoLiveChain(const VulkanSwapChain &swapChain) {
    CHECK(swapChain.vk == VK_NULL_HANDLE);
    CHECK(swapChain.isEmpty());
    CHECK(swapChain.textures.empty());
    CHECK(driver::liveViews.empty());
    CHECK(driver::activeChain == VK_NULL_HANDLE);
    for (const auto &[handle, chain] : driver::chains) CHECK(chain.state == driver::State::Destroyed);
}

static void run(const std::string &scenario) {
    using namespace driver;
    if (scenario == "android-defaults") {
        Harness h(2);
        CHECK(h.frame());
        h.frames(10);
        CHECK(created.size() == 1);
        const VkSwapchainCreateInfoKHR &info = lastCreated();
        CHECK((info.imageExtent.width == 1280) && (info.imageExtent.height == 720));
        CHECK(info.imageUsage == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        CHECK(info.compositeAlpha == VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR);
        CHECK(info.preTransform == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR);
        CHECK(info.imageFormat == VK_FORMAT_R8G8B8A8_UNORM);
        CHECK(info.presentMode == VK_PRESENT_MODE_FIFO_KHR);
        CHECK(info.minImageCount == 2);
        CHECK(h.swapChain->getTextureCount() == 2);
        CHECK(liveViews.size() == 2);
    }
    else if (scenario == "query-failure-create") {
        // A transient failure while constructing must not leave the swap chain unusable.
        capsResult = VK_ERROR_SURFACE_LOST_KHR;
        Harness h(3);
        h.frames(3);
        CHECK(attempts.empty());
        CHECK(h.swapChain->isEmpty());
        capsResult = VK_SUCCESS;
        CHECK(h.frame());
        h.frames(3);
        CHECK(created.size() == 1);
        CHECK(lastCreated().imageFormat == VK_FORMAT_R8G8B8A8_UNORM);
        CHECK(lastCreated().compositeAlpha == VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR);
        CHECK(lastCreated().minImageCount == 3);
    }
    else if (scenario == "query-failure-resize") {
        Harness h(2);
        CHECK(h.frame());
        const VkSwapchainKHR first = activeChain;
        windowWidth = 1920;
        windowHeight = 1080;
        caps.currentExtent = {1920, 1080};
        capsResult = VK_ERROR_SURFACE_LOST_KHR;
        CHECK(!h.frame());
        h.frames(3);
        CHECK(attempts.size() == 1);
        CHECK(activeChain == first);
        CHECK(find(first)->state == State::Live);
        capsResult = VK_SUCCESS;
        CHECK(h.frame());
        CHECK(created.size() == 2);
        CHECK(lastCreated().oldSwapchain == first);
        CHECK(lastCreated().imageExtent.width == 1920);
    }
    else if (scenario == "fixed-extent") {
        // A defined current extent is the surface's size, even when wider bounds would allow the window size.
        caps.currentExtent = {640, 480};
        windowWidth = 800;
        windowHeight = 600;
        Harness h(2);
        CHECK(h.frame());
        CHECK((lastCreated().imageExtent.width == 640) && (lastCreated().imageExtent.height == 480));
        CHECK((h.swapChain->getWidth() == 640) && (h.swapChain->getHeight() == 480));
        CHECK(!h.swapChain->needsResize());
        h.frames(10);
        CHECK(created.size() == 1);
        windowWidth = 1024;
        windowHeight = 768;
        h.frames(10);
        CHECK(created.size() == 2);
        CHECK((lastCreated().imageExtent.width == 640) && (lastCreated().imageExtent.height == 480));
    }
    else if (scenario == "zero-current-extent") {
        caps.currentExtent = {0, 0};
        caps.minImageExtent = {0, 0};
        Harness h(2);
        h.frames(3);
        CHECK(attempts.empty());
        CHECK(h.swapChain->isEmpty());
    }
    else if (scenario == "fixed-bounds") {
        // Win32 fixes all three extents; the window's client size may differ.
        caps.currentExtent = caps.minImageExtent = caps.maxImageExtent = {1280, 720};
        windowWidth = 1283;
        Harness h(2);
        CHECK(h.frame());
        CHECK((lastCreated().imageExtent.width == 1280) && (lastCreated().imageExtent.height == 720));
        CHECK(h.swapChain->getWidth() == 1280);
        // The window size is stable, so the clamped chain must not be recreated.
        CHECK(!h.swapChain->needsResize());
        h.frames(10);
        CHECK(created.size() == 1);
    }
    else if (scenario == "variable-extent") {
        caps.currentExtent = {0xFFFFFFFFu, 0xFFFFFFFFu};
        caps.minImageExtent = {64, 64};
        caps.maxImageExtent = {1920, 1080};
        windowWidth = 2560;
        windowHeight = 40;
        Harness h(2);
        CHECK(h.frame());
        CHECK((lastCreated().imageExtent.width == 1920) && (lastCreated().imageExtent.height == 64));
        CHECK((h.swapChain->getWidth() == 1920) && (h.swapChain->getHeight() == 64));
        CHECK(h.swapChain->textures[0].desc.width == 1920);
        h.frames(10);
        CHECK(created.size() == 1);
        windowWidth = 800;
        windowHeight = 600;
        h.frames(10);
        CHECK(created.size() == 2);
        CHECK((lastCreated().imageExtent.width == 800) && (lastCreated().imageExtent.height == 600));
    }
    else if (scenario == "zero-window") {
        windowWidth = 0;
        windowHeight = 0;
        Harness h(2);
        h.frames(3);
        CHECK(attempts.empty());
        CHECK(h.swapChain->isEmpty());
        windowWidth = 1280;
        windowHeight = 720;
        CHECK(h.frame());
        h.frames(3);
        CHECK(created.size() == 1);
    }
    else if (scenario == "zero-surface-extent") {
        // Win32 reports a zero extent for a minimized window.
        caps.currentExtent = caps.minImageExtent = caps.maxImageExtent = {0, 0};
        Harness h(2);
        h.frames(3);
        CHECK(attempts.empty());
        CHECK(h.swapChain->isEmpty());
        caps.currentExtent = caps.minImageExtent = caps.maxImageExtent = {1280, 720};
        CHECK(h.frame());
        CHECK(created.size() == 1);
    }
    else if (scenario == "unbounded-count") {
        caps.minImageCount = 2;
        caps.maxImageCount = 0;
        Harness h(3);
        CHECK(h.frame());
        CHECK(lastCreated().minImageCount == 3);
        CHECK(h.swapChain->getTextureCount() == 3);
    }
    else if (scenario == "bounded-count") {
        caps.minImageCount = 2;
        caps.maxImageCount = 3;
        Harness h(5);
        CHECK(h.frame());
        CHECK(lastCreated().minImageCount == 3);
    }
    else if (scenario == "minimum-count") {
        caps.minImageCount = 3;
        caps.maxImageCount = 0;
        Harness h(2);
        CHECK(h.frame());
        CHECK(lastCreated().minImageCount == 3);
    }
    else if (scenario == "count-stable") {
        // Drivers may return more images than requested; recreation must not escalate.
        extraImages = 1;
        Harness h(2);
        CHECK(h.frame());
        CHECK(h.swapChain->getTextureCount() == 3);
        windowWidth = 1024;
        h.frames(2);
        windowWidth = 800;
        h.frames(2);
        CHECK(created.size() == 3);
        for (const VkSwapchainCreateInfoKHR &info : created) CHECK(info.minImageCount == 2);
    }
    else if (scenario == "usage-color-only") {
        caps.supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        Harness h(2);
        CHECK(h.frame());
        CHECK(lastCreated().imageUsage == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    }
    else if (scenario == "alpha-opaque") {
        caps.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR |
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        Harness h(2);
        CHECK(h.frame());
        CHECK(lastCreated().compositeAlpha == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);
    }
    else if (scenario == "alpha-inherit") {
        caps.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR | VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR |
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        Harness h(2);
        CHECK(h.frame());
        CHECK(lastCreated().compositeAlpha == VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR);
    }
    else if ((scenario == "alpha-premultiplied") || (scenario == "alpha-postmultiplied")) {
        // RT64 does not promise alpha 1 in its final pass, so a blending compositor could show through.
        caps.supportedCompositeAlpha = (scenario == "alpha-premultiplied") ?
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR : VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        Harness h(2);
        h.frames(3);
        CHECK(attempts.empty());
        CHECK(h.swapChain->isEmpty());
    }
    else if (scenario == "transform-refusal") {
        caps.supportedTransforms = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
        caps.currentTransform = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
        Harness h(2);
        h.frames(3);
        CHECK(attempts.empty());
        CHECK(h.swapChain->isEmpty());
    }
    else if (scenario == "replacement-failure") {
        Harness h(2);
        CHECK(h.frame());
        const VkSwapchainKHR first = activeChain;
        windowWidth = 1920;
        windowHeight = 1080;
        caps.currentExtent = {1920, 1080};
        createResult = VK_ERROR_SURFACE_LOST_KHR;
        CHECK(!h.frame());
        h.frames(5);
        CHECK(find(first)->state == State::Destroyed);
        checkNoLiveChain(*h.swapChain);
        CHECK(h.swapChain->needsResize());
        // attempts[1] is the failed replacement of the first chain; retries name no old chain.
        CHECK(attempts.size() > 2);
        CHECK(attempts[1].oldSwapchain == first);
        for (size_t i = 2; i < attempts.size(); i++) CHECK(attempts[i].oldSwapchain == VK_NULL_HANDLE);
        createResult = VK_SUCCESS;
        CHECK(h.frame());
        CHECK(lastCreated().oldSwapchain == VK_NULL_HANDLE);
        CHECK(lastCreated().imageExtent.width == 1920);
        CHECK(liveViews.size() == h.swapChain->getTextureCount());
        h.frames(3);
        CHECK(created.size() == 2);
    }
    else if (scenario == "abandoned-surface") {
        // Android Home: the surface's buffer queue is abandoned at the same window size.
        Harness h(2);
        CHECK(h.frame());
        CHECK(h.refreshRate == 60);
        acquireResult = VK_ERROR_OUT_OF_DATE_KHR;
        createResult = VK_ERROR_SURFACE_LOST_KHR;
        CHECK(!h.frame());
        h.frames(5);
        checkNoLiveChain(*h.swapChain);
        CHECK(h.refreshRate == 0);
        acquireResult = VK_SUCCESS;
        createResult = VK_SUCCESS;
        CHECK(h.frame());
        CHECK(h.refreshRate == 60);
        CHECK(lastCreated().oldSwapchain == VK_NULL_HANDLE);
    }
    else if (scenario == "short-image-count") {
        missingImages = 1;
        Harness h(3);
        CHECK(!h.frame());
        checkNoLiveChain(*h.swapChain);
    }
    else if (scenario == "present-mode") {
        Harness h(2);
        CHECK(h.frame());
        h.swapChain->setVsyncEnabled(false);
        CHECK(h.swapChain->needsResize());
        CHECK(h.frame());
        CHECK(lastCreated().presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR);
        h.frames(3);
        CHECK(created.size() == 2);
    }
    else {
        printf("unknown scenario %s\n", scenario.c_str());
        ++failures;
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    driver::reset();
    run(argv[1]);
    // Every scenario's swap chain has been destroyed: nothing may outlive it.
    for (const auto &[handle, chain] : driver::chains) {
        if (chain.state != driver::State::Destroyed) {
            printf("FAILED: chain %llu leaked\n", (unsigned long long)handle);
            ++failures;
        }
    }
    CHECK(driver::liveViews.empty());
    CHECK(!driver::surfaceAlive);
    fflush(stderr);
    return (failures + driver::violations) ? 1 : 0;
}

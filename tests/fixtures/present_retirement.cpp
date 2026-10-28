#include <cassert>
#include <memory>
#include <vector>

bool submitted = false, drained = false, createSucceeds = true, drainSucceeds = true;
int framebuffers = 0, semaphores = 0, semaphoreSerial = 0;
struct Framebuffer {
    Framebuffer() { ++framebuffers; }
    ~Framebuffer() { assert(!submitted && drained); --framebuffers; }
};
struct Semaphore {
    int serial = ++semaphoreSerial;
    Semaphore() { ++semaphores; }
    ~Semaphore() { assert(!submitted && drained); --semaphores; }
};
struct CommandList { void begin() {} void end() {} };
struct Worker {
    std::unique_ptr<CommandList> commandList = std::make_unique<CommandList>();
    void execute() { submitted = true; drained = false; }
    void wait() { submitted = false; }
};
struct SwapChain {
    enum class DrainResult { Complete, DeviceLost, Failed };
    DrainResult drain() { assert(!submitted); drained = drainSucceeds; return drained ? DrainResult::Complete : DrainResult::Failed; }
    bool resize() {
        assert(drained && framebuffers == 0 && semaphores == 0);
        return createSucceeds;
    }
    int getWidth() { return 1280; }
    int getHeight() { return 720; }
    unsigned getTextureCount() { return 3; }
};
using RenderSwapChain = SwapChain;
struct Device {
    std::unique_ptr<Semaphore> createCommandSemaphore() { return std::make_unique<Semaphore>(); }
};
struct Resources {
    void setSwapChainSize(int width, int height) { assert(width == 1280 && height == 720); }
};
using Timestamp = int;
struct PresentQueue {
    struct { Worker *presentGraphicsWorker; SwapChain *swapChain; Device *device; Resources *sharedResources; } ext;
    std::vector<std::unique_ptr<Framebuffer>> swapChainFramebuffers;
    std::vector<std::unique_ptr<Semaphore>> drawSemaphores;
    std::unique_ptr<Semaphore> acquiredSemaphore;
    Timestamp presentTimestamp = 99;
    void threadResize(bool &valid);
};
// PRODUCTION_RESIZE
int main() {
    Worker worker;
    SwapChain chain;
    Device device;
    Resources resources;
    PresentQueue queue{{&worker, &chain, &device, &resources}};
    bool valid = true;
    queue.swapChainFramebuffers.emplace_back(std::make_unique<Framebuffer>());
    queue.acquiredSemaphore = device.createCommandSemaphore();
    queue.drawSemaphores.emplace_back(device.createCommandSemaphore());
    const int retainedSemaphore = queue.acquiredSemaphore->serial;
    drainSucceeds = false;
    queue.threadResize(valid);
    assert(!valid);
    assert(framebuffers == 1 && semaphores == 2);
    assert(queue.acquiredSemaphore->serial == retainedSemaphore);
    assert(queue.drawSemaphores.size() == 1);
    drainSucceeds = true;
    queue.threadResize(valid);
    assert(valid && queue.acquiredSemaphore->serial > retainedSemaphore);
    for (bool success : {false, false, true, true, false, true}) {
        queue.swapChainFramebuffers.emplace_back(std::make_unique<Framebuffer>());
        queue.acquiredSemaphore = device.createCommandSemaphore();
        const int previous = queue.acquiredSemaphore->serial;
        createSucceeds = success;
        queue.threadResize(valid);
        assert(valid == success);
        assert(queue.presentTimestamp == 99); // Resizing does not change desktop pacing.
        assert(queue.acquiredSemaphore->serial > previous);
        assert(queue.drawSemaphores.size() == (success ? 3 : 0));
    }
}

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

bool canDestroy = false;
int resources = 0;
struct GpuResource {
    GpuResource() { ++resources; }
    ~GpuResource() { assert(canDestroy); --resources; }
};
struct RenderSwapChain {
    enum class DrainResult { Complete, DeviceLost, Failed };
    DrainResult result = DrainResult::Failed;
    DrainResult drain() {
        canDestroy = (result != DrainResult::Failed);
        return result;
    }
};
struct PresentQueue {
    struct { RenderSwapChain *swapChain = nullptr; } ext;
    std::mutex cursorMutex;
    std::condition_variable cursorCondition, barrierCondition, presentIdCondition;
    std::atomic<bool> presentThreadRunning = false;
    std::thread *presentThread = nullptr;
    std::vector<std::unique_ptr<GpuResource>> swapChainFramebuffers, drawSemaphores;
    std::unique_ptr<GpuResource> acquiredSemaphore;
    ~PresentQueue();
    void releaseSwapChainResources();
};

// PRODUCTION_TEARDOWN

int main() {
    for (auto result : {RenderSwapChain::DrainResult::Failed,
                        RenderSwapChain::DrainResult::DeviceLost,
                        RenderSwapChain::DrainResult::Complete}) {
        canDestroy = false;
        RenderSwapChain chain;
        chain.result = result;
        std::vector<GpuResource *> retained;
        {
            PresentQueue queue;
            queue.ext.swapChain = &chain;
            queue.swapChainFramebuffers.emplace_back(std::make_unique<GpuResource>());
            queue.drawSemaphores.emplace_back(std::make_unique<GpuResource>());
            queue.acquiredSemaphore = std::make_unique<GpuResource>();
            retained = {queue.swapChainFramebuffers[0].get(), queue.drawSemaphores[0].get(), queue.acquiredSemaphore.get()};
        } // Exercise the real PresentQueue destructor, including automatic member cleanup.
        if (result == RenderSwapChain::DrainResult::Failed) {
            assert(resources == 3);
            // The fixture later establishes completion to clean up its deliberately
            // abandoned wrappers. Production does not reclaim them without that proof.
            canDestroy = true;
            for (auto *resource : retained) delete resource;
        }
        assert(resources == 0);
    }
    PresentQueue neverStarted; // Teardown before setup must not dereference a null swap chain.
}

#include "rhi/rt64_render_hooks.h"

#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
}
struct Observer final : RT64::RenderEvidenceObserver {
    void begin(const RT64::Workload&, float) noexcept override {}
    bool raster(const RT64::RasterEvidenceRange&) noexcept override { return true; }
    void completed(const RT64::Workload&, RT64::RenderWorker*) noexcept override {}
    void presented(const RT64::PresentationEvidence&, RT64::RenderWorker*) noexcept override {}
};
}

int main() {
    try {
        Observer queue, nested;
        require(RT64::GetActiveRenderEvidenceObserver() == nullptr, "default raster path has an observer");
        RT64::SetRenderEvidenceObserver(&queue);
        {
            RT64::ScopedRenderEvidence scope(RT64::GetRenderEvidenceObserver());
            require(RT64::GetActiveRenderEvidenceObserver() == &queue, "queue scope lost its observer");
            bool hle_isolated = false;
            std::thread hle([&] {
                hle_isolated = RT64::GetActiveRenderEvidenceObserver() == nullptr &&
                    RT64::GetRenderEvidenceObserver() == &queue;
            });
            hle.join();
            require(hle_isolated, "HLE entered the queue's mutable raster observer");
            {
                RT64::ScopedRenderEvidence scope(&nested);
                require(RT64::GetActiveRenderEvidenceObserver() == &nested, "nested render scope failed");
            }
            require(RT64::GetActiveRenderEvidenceObserver() == &queue, "nested scope failed to restore queue");
            try {
                RT64::ScopedRenderEvidence scope(nullptr);
                throw std::runtime_error("scope exit");
            } catch (const std::runtime_error&) {}
            require(RT64::GetActiveRenderEvidenceObserver() == &queue, "unwinding leaked the active observer");
        }
        require(RT64::GetActiveRenderEvidenceObserver() == nullptr, "observer survived its workload scope");
        RT64::SetRenderEvidenceObserver(nullptr);
        require(RT64::GetRenderEvidenceObserver() == nullptr, "observer survived unregistration");
        std::puts("RT evidence queue/HLE isolation passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}

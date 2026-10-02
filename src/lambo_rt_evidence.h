#pragma once

#include <memory>
#include "lambo_rt_shadows.h"
#include "rhi/rt64_render_hooks.h"

namespace lambo::rt {
// Port policy and local capture files. The observer holds no live RAM pointer.
// Its owner must join RT64 queues before destruction (docs/rt-provenance.md).
class RenderEvidence final : public RT64::RenderEvidenceObserver {
public:
    RenderEvidence();
    ~RenderEvidence() override;
    bool enabled() const;
    void remember(uint64_t workload_id, const std::optional<TaskSunProbe>& task);
    void processed(uint64_t first, uint64_t last);
    void begin(const RT64::Workload&, float weight) noexcept override;
    bool raster(const RT64::RasterEvidenceRange&) noexcept override;
    void completed(const RT64::Workload&, RT64::RenderWorker*) noexcept override;
    void presented(const RT64::PresentationEvidence&, RT64::RenderWorker*) noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

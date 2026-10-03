#pragma once

#include <memory>
#include <vector>
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
    std::shared_ptr<const RT64::SunShadowWorkload> sunShadow(const RT64::Workload&) noexcept override;
    void begin(const RT64::Workload&, float weight) noexcept override;
    bool raster(const RT64::RasterEvidenceRange&) noexcept override;
    bool ownerBufferEnabled() const noexcept override;
    bool nativeAlphaEvidenceEnabled() const noexcept override;
    void ownerBufferIncomplete(uint32_t color_address, const char* reason,
        uint32_t identity) noexcept override;
    void ownerBufferRendered(uint32_t color_address, uint32_t width, uint32_t height,
        const plume::RenderTexture* owner_texture, bool complete) noexcept override;
    void nativeMaterialBindings(uint32_t color_address, uint32_t width, uint32_t height,
        const RT64::FramebufferRenderer* renderer, uint32_t framebuffer_index) noexcept override;
    void completed(const RT64::Workload&, RT64::RenderWorker*) noexcept override;
    void presented(const RT64::PresentationEvidence&, RT64::RenderWorker*) noexcept override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

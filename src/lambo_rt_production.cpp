#define HLSL_CPU
#include "lambo_rt_production.h"

#include "hle/rt64_workload.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "lambo_log.h"
#include "lambo_rt_presented.h"

namespace lambo::rt {

namespace {
// Workloads are published in order; a few may be queued or re-rendered for
// interpolation. Older identities can no longer match a Workload.
constexpr uint64_t retained_workloads = 16;
constexpr size_t cost_window = 120;

// Developer fault injection for the native-restore proof. "as" adds a caster
// range outside the index buffer, so RT64's real acceleration-structure
// preparation fails and the view must draw its native overlay.
bool inject_as_fault() {
    static const bool enabled = [] {
        const char* value = std::getenv("LAMBO_RT_SHADOW_FAULT");
        return value && std::strcmp(value, "as") == 0;
    }();
    return enabled;
}

// Developer validation sweep: lets an unvalidated player model reach the
// production path so its scene can be measured. Never a player setting.
bool any_player_model() {
    static const bool enabled = [] {
        const char* value = std::getenv("LAMBO_RT_SHADOW_ANY_MODEL");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, size_t(fraction * double(values.size())))];
}
}

ShadowSettings production_shadow_settings(int rays, double softness_degrees) {
    ShadowSettings settings;
    settings.strength = native_overlay_strength;
    settings.samples = uint32_t(rays);
    settings.angular_radius = float(softness_degrees * 3.14159265358979323846 / 180.0);
    settings.any_player_model = any_player_model();
    return settings;
}

void SunShadowProduction::remember(uint64_t workload_id, const std::optional<TaskSunProbe>& task, bool enabled,
        const ShadowSettings& settings) {
    std::lock_guard lock(mutex_);
    while (!tasks_.empty() && tasks_.begin()->first + retained_workloads < workload_id) tasks_.erase(tasks_.begin());
    tasks_.erase(workload_id);
    if (!enabled || !task) return;
    Pending pending;
    if (!shadow_task(*task, pending.task)) return;
    pending.settings = settings;
    tasks_.emplace(workload_id, std::move(pending));
}

std::shared_ptr<const RT64::SunShadowWorkload> SunShadowProduction::sunShadow(const RT64::Workload& workload) noexcept {
    try {
        std::lock_guard lock(mutex_);
        const auto found = tasks_.find(workload.workloadId);
        if (found == tasks_.end()) return {};
        Pending& pending = found->second;
        // Ranges do not change when a Workload is re-rendered for interpolation.
        if (!pending.admitted) {
            const auto scene = presented_scene(workload);
            AdmissionStats stats;
            pending.result = admit_sun_shadow(pending.task, scene.draws, scene.lights, pending.settings, stats);
            if (pending.result && inject_as_fault()) {
                auto faulty = std::make_shared<RT64::SunShadowWorkload>(*pending.result);
                faulty->geometry.push_back({0xFFFFFFF0u, 3, 0, true, 0, 1});
                pending.result = faulty;
            }
            pending.admitted = true;
            if (!pending.result) {
                LAMBO_LOG("rt-shadow", "task=%llu native: admission gate %u\n",
                    static_cast<unsigned long long>(pending.task.sequence), uint32_t(stats.gate));
            }
            else if (!pending.result->complete) {
                LAMBO_LOG("rt-shadow", "task=%llu native: incomplete admission rejected=%zu unclassified=%zu overlays=%zu receivers=%zu\n",
                    static_cast<unsigned long long>(pending.task.sequence), stats.rejected, stats.unclassified,
                    stats.overlays, pending.result->receivers.size());
            }
        }
        return pending.result;
    } catch (const std::exception& error) {
        LAMBO_LOG_WARN("rt-shadow", "admission failed: %s\n", error.what());
        return {};
    }
}

void SunShadowProduction::sunShadowResult(const RT64::SunShadowFrameResult& result) noexcept {
    if (result.ready) {
        build_us_.push_back(result.buildMicroseconds);
        receiver_us_.push_back(result.receiverMicroseconds);
        if (build_us_.size() >= cost_window) {
            LAMBO_LOG("rt-shadow", "gpu_cost samples=%zu build_us_p50=%.1f build_us_p95=%.1f receiver_us_p50=%.1f receiver_us_p95=%.1f triangles=%llu\n",
                build_us_.size(), percentile(build_us_, 0.5), percentile(build_us_, 0.95),
                percentile(receiver_us_, 0.5), percentile(receiver_us_, 0.95),
                static_cast<unsigned long long>(result.triangles));
            build_us_.clear();
            receiver_us_.clear();
        }
    }
    const std::string reason = result.reason ? result.reason : "";
    if (result.ready == last_ready_ && reason == last_reason_ && (result.taskSequence % 60) != 0) return;
    last_ready_ = result.ready;
    last_reason_ = reason;
    LAMBO_LOG("rt-shadow", "workload=%llu task=%llu epoch=%llu ready=%d reason=\"%s\" casters=%u receivers=%u overlays=%u triangles=%llu as_bytes=%llu scratch_bytes=%llu\n",
        static_cast<unsigned long long>(result.workloadId), static_cast<unsigned long long>(result.taskSequence),
        static_cast<unsigned long long>(result.sceneEpoch), result.ready ? 1 : 0, reason.c_str(),
        result.casterRanges, result.receiverDraws, result.overlaysReplaced,
        static_cast<unsigned long long>(result.triangles), static_cast<unsigned long long>(result.asBytes),
        static_cast<unsigned long long>(result.scratchBytes));
}

void SunShadowProduction::invalidate() {
    std::lock_guard lock(mutex_);
    tasks_.clear();
}

} // namespace lambo::rt

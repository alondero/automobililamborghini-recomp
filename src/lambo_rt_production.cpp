#define HLSL_CPU
#include "lambo_rt_production.h"

#include "hle/rt64_workload.h"

#include <algorithm>
#include <cstdio>
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

// Developer validation sweep: lets an unvalidated race mode and circuit pair
// reach the production path so its scene can be measured. Never a player setting.
bool validation_sweep() {
    static const bool enabled = [] {
        const char* value = std::getenv("LAMBO_RT_SHADOW_SWEEP");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

// Names the first few unclassified draws so a native-fallback log line says
// which presented draw and native object kept the scene incomplete.
std::string describe_unclassified(const RT64::SunShadowWorkload& result, const PresentedScene& scene,
        const ShadowTask& task) {
    std::string text;
    size_t shown = 0;
    for (const auto& range : result.unclassified) {
        if (shown++ == 4) {
            text += " ...";
            break;
        }
        char entry[192];
        int length = std::snprintf(entry, sizeof(entry), " r%u/p%u/n%u", range.rejection, range.projectionType,
            range.indexCount);
        for (const auto& draw : scene.draws) {
            if (draw.call != range.draw) continue;
            const uint32_t object_id = draw.matrix_id & 0xFFFFu;
            if (object_id < task.objects.size()) {
                const auto& object = task.objects[object_id];
                length += std::snprintf(entry + length, sizeof(entry) - size_t(length), "/obj%u:f%X:l%08X:k%d:p%d",
                    object_id, unsigned(object.flags), object.list, int(object.kind), int(object.parent));
            }
            const auto& m = draw.material;
            std::snprintf(entry + length, sizeof(entry) - size_t(length), "/m%08X:%08X:%08X:g%X:z%X:%d%d%d%d%d",
                m.otherL, m.combineW0, m.combineW1, draw.projection.geometry, m.zMode, int(m.alphaBlend),
                int(m.forceBlend), int(m.coverageAlpha), int(m.zCompare), int(m.zUpdate));
            break;
        }
        text += entry;
    }
    return text;
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
    settings.validation_sweep = validation_sweep();
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
                LAMBO_LOG("rt-shadow", "task=%llu native: admission gate %u circuit=%d phase=%d players=%d mode=%d model=%d\n",
                    static_cast<unsigned long long>(pending.task.sequence), uint32_t(stats.gate),
                    pending.task.circuit, pending.task.phase, pending.task.players, pending.task.race_mode,
                    int(pending.task.model_cursors[0]));
            }
            else if (!pending.result->complete) {
                LAMBO_LOG("rt-shadow", "task=%llu native: incomplete admission rejected=%zu unclassified=%zu overlays=%zu receivers=%zu%s\n",
                    static_cast<unsigned long long>(pending.task.sequence), stats.rejected, stats.unclassified,
                    stats.overlays, pending.result->receivers.size(),
                    describe_unclassified(*pending.result, scene, pending.task).c_str());
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

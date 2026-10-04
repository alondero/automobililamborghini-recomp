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

// Appends one formatted field; output longer than a field is truncated.
template <class... Args>
void append_format(std::string& text, const char* format, Args... args) {
    char field[96];
    const int length = std::snprintf(field, sizeof(field), format, args...);
    if (length > 0) text.append(field, std::min(size_t(length), sizeof(field) - 1));
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
        append_format(text, " r%u/p%u/n%u", range.rejection, range.projectionType, range.indexCount);
        for (const auto& draw : scene.draws) {
            if (draw.call != range.draw) continue;
            const uint32_t object_id = presented_object_index(draw.matrix_id);
            if (object_id < task.objects.size()) {
                const auto& object = task.objects[object_id];
                append_format(text, "/obj%u:f%X:l%08X:k%d:p%d", object_id, unsigned(object.flags), object.list,
                    int(object.kind), int(object.parent));
            }
            const auto& m = draw.material;
            append_format(text, "/m%08X:%08X:%08X:g%X:z%X:%d%d%d%d%d", m.otherL, m.combineW0, m.combineW1,
                draw.projection.geometry, m.zMode, int(m.alphaBlend), int(m.forceBlend), int(m.coverageAlpha),
                int(m.zCompare), int(m.zUpdate));
            break;
        }
    }
    return text;
}

struct CostPercentiles { double p50 = 0, p95 = 0; };

// Sorts the caller's window in place; it is cleared after each summary.
CostPercentiles cost_percentiles(std::vector<double>& samples) {
    if (samples.empty()) return {};
    std::sort(samples.begin(), samples.end());
    const auto at = [&samples](double fraction) {
        return samples[std::min(samples.size() - 1, size_t(fraction * double(samples.size())))];
    };
    return {at(0.5), at(0.95)};
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

void SunShadowProduction::remember(const std::optional<TaskSunProbe>& task, bool enabled,
        const ShadowSettings& settings) {
    std::optional<Pending> pending;
    if (enabled && task) {
        pending.emplace();
        pending->settings = settings;
        if (!shadow_task(*task, pending->task)) pending.reset();
    }
    std::lock_guard lock(mutex_);
    tasks_.stage(std::move(pending));
}

void SunShadowProduction::workloadPublished(uint64_t workload_id) noexcept {
    std::lock_guard lock(mutex_);
    tasks_.published(workload_id);
}

void SunShadowProduction::finish_task() {
    std::lock_guard lock(mutex_);
    tasks_.finish();
}

std::shared_ptr<const RT64::SunShadowWorkload> SunShadowProduction::sunShadow(const RT64::Workload& workload) noexcept {
    try {
        std::lock_guard lock(mutex_);
        Pending* found = tasks_.find(workload.workloadId);
        if (!found) return {};
        Pending& pending = *found;
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
                LAMBO_LOG("rt-shadow", "task=%llu native: incomplete admission rejected=%zu unclassified=%zu overlays=%zu receivers=%zu%s%s\n",
                    static_cast<unsigned long long>(pending.task.sequence), stats.rejected, stats.unclassified,
                    stats.overlays, pending.result->receivers.size(), rejection_summary(stats).c_str(),
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
            const size_t samples = build_us_.size();
            const CostPercentiles build = cost_percentiles(build_us_);
            const CostPercentiles receiver = cost_percentiles(receiver_us_);
            LAMBO_LOG("rt-shadow", "gpu_cost samples=%zu build_us_p50=%.1f build_us_p95=%.1f receiver_us_p50=%.1f receiver_us_p95=%.1f triangles=%llu\n",
                samples, build.p50, build.p95, receiver.p50, receiver.p95,
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
    tasks_.invalidate();
}

} // namespace lambo::rt

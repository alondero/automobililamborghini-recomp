#define HLSL_CPU
#include "lambo_rt_production.h"

#include "hle/rt64_workload.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
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

// Developer validation sweep: lets unvalidated race modes and cars reach the
// production path so their scenes can be measured. Never a player setting.
bool validation_sweep() {
    static const bool enabled = [] {
        const char* value = std::getenv("LAMBO_RT_SHADOW_SWEEP");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

// Developer tuning: LAMBO_RT_SHADOW_<name> overrides one ambient value.
float tuned(const char* name, float fallback) {
    const std::string key = std::string("LAMBO_RT_SHADOW_") + name;
    const char* value = std::getenv(key.c_str());
    if (!value || !*value) return fallback;
    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    return end && *end == 0 ? parsed : fallback;
}

// Log keys are single words so tools can parse the summary line.
std::string reason_key(const char* reason) {
    std::string key;
    for (const char* c = reason; c && *c; ++c) key += std::isalnum(static_cast<unsigned char>(*c)) ? *c : '_';
    return key.empty() ? "unknown" : key;
}

// Appends one formatted field; output longer than a field is truncated.
template <class... Args>
void append_format(std::string& text, const char* format, Args... args) {
    char field[96];
    const int length = std::snprintf(field, sizeof(field), format, args...);
    if (length > 0) text.append(field, std::min(size_t(length), sizeof(field) - 1));
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
    // Renderer world units: the car is about 4.35 long and 1.13 tall. A
    // receiver with less than half its sky open (tunnel, deep underpass) is
    // lit by the art's baked lamps, not the sun. docs/rt-shadows.md.
    settings.ambient.contactStrength = tuned("CONTACT", 0.55f);
    settings.ambient.contactDistance = tuned("CONTACT_DISTANCE", 1.6f);
    settings.ambient.skyDistance = tuned("SKY_DISTANCE", 30.0f);
    settings.ambient.skyOpenFull = tuned("SKY_OPEN", 0.5f);
    settings.ambient.debugMode = uint32_t(std::clamp(tuned("DEBUG", 0.0f), 0.0f, 255.0f));
    // Scenery that left the view keeps casting, so shadows do not change with
    // the camera. 2 traces only the remembered scenery, to check its coverage.
    settings.ambient.staticCache = uint32_t(std::clamp(tuned("CACHE", 1.0f), 0.0f, 255.0f));
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
            pending.settings.key_luminance = key_luminance_;
            pending.result = admit_sun_shadow(pending.task, scene.draws, scene.lights, pending.settings, stats);
            if (pending.result && inject_as_fault()) {
                auto faulty = std::make_shared<RT64::SunShadowWorkload>(*pending.result);
                faulty->geometry.push_back({0xFFFFFFF0u, 3, 0, true, 0, 1});
                pending.result = faulty;
            }
            pending.admitted = true;
            if (stats.car_key_seen) {
                const float luminance = light_luminance(stats.car_key);
                if (std::isfinite(luminance) && luminance > 0) key_luminance_ = luminance;
            }
            log_car_light(pending.task.sequence, stats);
            if (!pending.result) {
                log_lap(lap_.count(false, "gate_" + std::to_string(uint32_t(stats.gate))));
                LAMBO_LOG("rt-shadow", "task=%llu native: admission gate %u circuit=%d phase=%d players=%d mode=%d\n",
                    static_cast<unsigned long long>(pending.task.sequence), uint32_t(stats.gate),
                    pending.task.circuit, pending.task.phase, pending.task.players, pending.task.race_mode);
            }
            else if (stats.rejected != 0 || stats.unclassified != 0) {
                LAMBO_LOG_TRACE("rt-shadow", "task=%llu excluded rejected=%zu untraced=%zu receivers=%zu key=%d%s\n",
                    static_cast<unsigned long long>(pending.task.sequence), stats.rejected, stats.unclassified,
                    pending.result->receivers.size(), int(stats.car_key_seen), rejection_summary(stats).c_str());
            }
        }
        return pending.result;
    } catch (const std::exception& error) {
        LAMBO_LOG_WARN("rt-shadow", "admission failed: %s\n", error.what());
        return {};
    }
}

// How the game lights the player's car changes in tunnels and in-car views;
// one line per change ties those places to the lap's task numbers.
void SunShadowProduction::log_car_light(uint64_t sequence, const AdmissionStats& stats) {
    std::string light = "none";
    if (stats.car_light_seen) {
        const PresentedLight& l = stats.car_light;
        light.clear();
        append_format(light, "dir=%.0f,%.0f,%.0f rgb=%.3f,%.3f,%.3f key=%d", l.x, l.y, l.z, l.r, l.g, l.b,
            int(stats.car_key_seen));
    }
    if (light == last_car_light_) return;
    last_car_light_ = light;
    LAMBO_LOG("rt-shadow", "task=%llu car_light %s\n", static_cast<unsigned long long>(sequence), light.c_str());
}

void SunShadowProduction::log_lap(const std::optional<LapSummary>& summary) {
    if (!summary) return;
    std::string reasons;
    for (const auto& [key, count] : summary->reasons) reasons += " " + key + "=" + std::to_string(count);
    LAMBO_LOG("rt-shadow", "lap_summary tasks=%zu ready=%zu%s\n", summary->tasks, summary->ready, reasons.c_str());
}

void SunShadowProduction::sunShadowResult(const RT64::SunShadowFrameResult& result) noexcept {
    // The renderer thread calls this while queue threads admit tasks, so every
    // member below is shared under mutex_.
    std::lock_guard lock(mutex_);
    // Interpolated presentations re-render a Workload; count each task once.
    try {
        log_lap(lap_.record(result.workloadId, result.ready, reason_key(result.reason)));
    } catch (const std::exception& error) {
        LAMBO_LOG_WARN("rt-shadow", "task count failed: %s\n", error.what());
    }
    if (result.ready) {
        build_us_.push_back(result.buildMicroseconds);
        receiver_us_.push_back(result.receiverMicroseconds);
        cache_us_.push_back(result.cacheMicroseconds);
        if (build_us_.size() >= cost_window) {
            const size_t samples = build_us_.size();
            const CostPercentiles build = cost_percentiles(build_us_);
            const CostPercentiles receiver = cost_percentiles(receiver_us_);
            const CostPercentiles cache = cost_percentiles(cache_us_);
            LAMBO_LOG("rt-shadow", "gpu_cost samples=%zu build_us_p50=%.1f build_us_p95=%.1f receiver_us_p50=%.1f receiver_us_p95=%.1f triangles=%llu static_triangles=%llu static_version=%llu cache_us_p50=%.1f cache_us_p95=%.1f\n",
                samples, build.p50, build.p95, receiver.p50, receiver.p95,
                static_cast<unsigned long long>(result.triangles),
                static_cast<unsigned long long>(result.staticTriangles),
                static_cast<unsigned long long>(result.staticVersion), cache.p50, cache.p95);
            build_us_.clear();
            receiver_us_.clear();
            cache_us_.clear();
        }
    }
    const std::string reason = result.reason ? result.reason : "";
    if (result.ready == last_ready_ && reason == last_reason_ && (result.taskSequence % 60) != 0) return;
    last_ready_ = result.ready;
    last_reason_ = reason;
    LAMBO_LOG("rt-shadow", "workload=%llu task=%llu epoch=%llu ready=%d reason=\"%s\" casters=%u receivers=%u overlays=%u triangles=%llu static_triangles=%llu as_bytes=%llu scratch_bytes=%llu\n",
        static_cast<unsigned long long>(result.workloadId), static_cast<unsigned long long>(result.taskSequence),
        static_cast<unsigned long long>(result.sceneEpoch), result.ready ? 1 : 0, reason.c_str(),
        result.casterRanges, result.receiverDraws, result.overlaysReplaced,
        static_cast<unsigned long long>(result.triangles), static_cast<unsigned long long>(result.staticTriangles),
        static_cast<unsigned long long>(result.asBytes), static_cast<unsigned long long>(result.scratchBytes));
}

void SunShadowProduction::invalidate() {
    std::lock_guard lock(mutex_);
    tasks_.invalidate();
    // A run's tail is a real part of its lap; log it before the next run starts.
    try {
        log_lap(lap_.flush());
    } catch (const std::exception& error) {
        LAMBO_LOG_WARN("rt-shadow", "lap summary flush failed: %s\n", error.what());
    }
    lap_.reset();
    key_luminance_ = ShadowSettings{}.key_luminance;
}

void SunShadowProduction::flush_lap_summary() noexcept {
    std::lock_guard lock(mutex_);
    try {
        log_lap(lap_.flush());
    } catch (const std::exception& error) {
        LAMBO_LOG_WARN("rt-shadow", "lap summary flush failed: %s\n", error.what());
    }
}

namespace {
// The one live production object, so process exit can reach it without the renderer.
std::atomic<SunShadowProduction*> live_production{nullptr};
} // namespace

SunShadowProduction::SunShadowProduction() {
    live_production.store(this);
}

SunShadowProduction::~SunShadowProduction() {
    SunShadowProduction* self = this;
    live_production.compare_exchange_strong(self, nullptr);
}

} // namespace lambo::rt

extern "C" void lambo_rt_flush_lap_summary_at_exit() {
    if (lambo::rt::SunShadowProduction* production = lambo::rt::live_production.load()) production->flush_lap_summary();
}

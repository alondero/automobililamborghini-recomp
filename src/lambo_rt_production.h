#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "lambo_rt_admission.h"
#include "lambo_rt_binding.h"
#include "lambo_rt_lap_window.h"
#include "lambo_rt_shadows.h"
#include "rhi/rt64_render_hooks.h"

namespace lambo::rt {

// Production sunlight provider. HLE remembers each task's producer-owned
// identity together with the settings snapshot in force when the task was
// consumed; RT64 binds it to the id of the Workload the task publishes, and
// the queue thread admits that Workload from those values. Its owner must
// clear the RT64 registration after queue threads join.
class SunShadowProduction final : public RT64::SunShadowProvider {
public:
    SunShadowProduction();
    ~SunShadowProduction();
    // HLE thread, before processDisplayLists. finish_task follows it.
    void remember(const std::optional<TaskSunProbe>& task, bool enabled, const ShadowSettings& settings);
    void workloadPublished(uint64_t workload_id) noexcept override;
    void finish_task();
    std::shared_ptr<const RT64::SunShadowWorkload> sunShadow(const RT64::Workload& workload) noexcept override;
    void sunShadowResult(const RT64::SunShadowFrameResult& result) noexcept override;
    void invalidate();
    // Logs the partial lap window now. The harness and normal quit exit with
    // _Exit, which skips renderer teardown, so main calls this first.
    void flush_lap_summary() noexcept;

private:
    // Every summary_window counted tasks, logs how many used the traced path
    // and why the rest drew the native shadow (tools/check_rt_shadow_lap.py).
    // Callers hold mutex_.
    void log_lap(const std::optional<LapSummary>& summary);
    void log_car_light(uint64_t sequence, const AdmissionStats& stats);
    static constexpr size_t summary_window = 120;

    struct Pending {
        ShadowTask task;
        ShadowSettings settings;
        bool admitted = false;
        std::shared_ptr<const RT64::SunShadowWorkload> result;
    };
    std::mutex mutex_;
    PublishedTasks<Pending> tasks_;
    bool last_ready_ = false;
    std::string last_reason_;
    // Queue-thread GPU samples, summarized and cleared every 120 ready Workloads.
    std::vector<double> build_us_, receiver_us_, cache_us_;
    LapWindow lap_{summary_window};
    std::string last_car_light_;
    // The car's last open-road key, which scales tunnel lamp shadows.
    float key_luminance_ = ShadowSettings{}.key_luminance;
};

// Settings snapshot for production shadows: measured native contrast,
// configured rays and authored angular radius (degrees -> radians).
ShadowSettings production_shadow_settings(int rays, double softness_degrees);

} // namespace lambo::rt

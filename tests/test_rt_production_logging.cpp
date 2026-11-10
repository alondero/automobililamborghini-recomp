#include "lambo_rt_production.h"
#include "lambo_rt_presented.h"
#include "lambo_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>

namespace {
bool count_allocations = false;
size_t allocations = 0;
std::vector<std::string> lines;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool logged(const std::string& expected) {
    for (const auto& line : lines) if (line == expected) return true;
    return false;
}
}

void* operator new(size_t size) {
    if (count_allocations) ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

extern "C" {
volatile int g_log_threshold = LAMBO_LEVEL_WARN;
void lambo_log_write(LamboLogLevel, const char*, const char* format, ...) {
    char text[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    lines.emplace_back(text);
}
}

// This fixture drives renderer results only. Admission has its own host tests;
// fail if this fixture accidentally starts depending on a presented workload.
namespace lambo::rt {
bool shadow_task(const TaskSunProbe&, ShadowTask&) { std::abort(); }
PresentedScene presented_scene(const RT64::Workload&) { std::abort(); }
std::shared_ptr<RT64::SunShadowWorkload> admit_sun_shadow(const ShadowTask&,
        const std::vector<PresentedDraw>&, const std::vector<PresentedLight>&,
        const ShadowSettings&, AdmissionStats&) { std::abort(); }
std::string rejection_summary(const AdmissionStats&) { std::abort(); }
}

int main() {
    try {
        lambo::rt::SunShadowProduction production;
        RT64::SunShadowFrameResult result{};
        result.reason = "missing receiver coverage for this workload";
        count_allocations = true;
        for (uint64_t task = 1; task <= 121; ++task) {
            result.workloadId = result.taskSequence = task;
            result.ready = (task % 2) != 0;
            production.sunShadowResult(result);
        }
        production.flush_lap_summary();
        production.invalidate();
        count_allocations = false;
        require(allocations == 0 && lines.empty(), "disabled result diagnostics allocated or logged");

        g_log_threshold = LAMBO_LEVEL_DEBUG;
        result.reason = "ready";
        result.ready = true;
        result.buildMicroseconds = result.receiverMicroseconds = result.cacheMicroseconds = 1;
        result.triangles = 7;
        result.staticTriangles = 3;
        result.staticVersion = 9;
        for (uint64_t task = 1; task <= 120; ++task) {
            result.workloadId = result.taskSequence = task;
            production.sunShadowResult(result);
        }
        require(logged("lap_summary tasks=120 ready=120\n"), "full summary missing or changed");
        require(logged("gpu_cost samples=120 build_us_p50=1.0 build_us_p95=1.0 receiver_us_p50=1.0 receiver_us_p95=1.0 triangles=7 static_triangles=3 static_version=9 cache_us_p50=1.0 cache_us_p95=1.0\n"),
            "GPU summary missing or changed");
        require(logged("workload=1 task=1 epoch=0 ready=1 reason=\"ready\" casters=0 receivers=0 overlays=0 triangles=7 static_triangles=3 as_bytes=0 scratch_bytes=0\n"),
            "first result line missing or changed");
        require(lines.size() == 5, "result cadence changed (first, 60th, 120th, lap and GPU summaries)");
        result.workloadId = result.taskSequence = 121;
        result.ready = false;
        result.reason = "missing receiver coverage";
        production.sunShadowResult(result);
        production.sunShadowResult(result); // Interpolation must not double-count.
        production.flush_lap_summary();
        require(logged("lap_summary tasks=1 ready=0 missing_receiver_coverage=1\n"),
            "final partial summary or reason key changed");
        const auto count = lines.size();
        production.flush_lap_summary();
        require(lines.size() == count, "tail summary emitted twice");
        std::puts("PASS: disabled diagnostics allocate nothing; debug cadence, formats and tail preserved");
        return 0;
    } catch (const std::exception& error) {
        count_allocations = false;
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

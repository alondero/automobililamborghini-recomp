#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace lambo::rt {

// Readiness of a run of race tasks, printed as one lap_summary log line.
struct LapSummary {
    size_t tasks = 0;
    size_t ready = 0;
    std::map<std::string, size_t> reasons; // Fallback reason -> task count.
};

// Counts each Workload once into fixed-size windows. The tail of a lap is kept
// by flush(), so a lap that is not a multiple of the window is still counted.
class LapWindow {
public:
    explicit LapWindow(size_t window) : window_(window) {}

    // A task that never became a Workload (an admission fallback).
    std::optional<LapSummary> count(bool ready, const std::string& reason) {
        ++pending_.tasks;
        if (ready) ++pending_.ready;
        else ++pending_.reasons[reason];
        if (pending_.tasks < window_) return std::nullopt;
        return take();
    }

    // An interpolated presentation re-renders a Workload; only its first sight counts.
    std::optional<LapSummary> record(uint64_t workload, bool ready, const std::string& reason) {
        if (workload == last_workload_) return std::nullopt;
        last_workload_ = workload;
        return count(ready, reason);
    }

    // Everything counted since the last summary, or nothing when the window is empty.
    std::optional<LapSummary> flush() {
        if (pending_.tasks == 0) return std::nullopt;
        return take();
    }

    void reset() {
        pending_ = {};
        last_workload_ = 0;
    }

private:
    std::optional<LapSummary> take() {
        LapSummary summary = std::move(pending_);
        pending_ = {};
        return summary;
    }

    size_t window_;
    LapSummary pending_;
    uint64_t last_workload_ = 0;
};

} // namespace lambo::rt

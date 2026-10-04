#pragma once

#include <cstdint>
#include <map>
#include <optional>

namespace lambo::rt {

// Binds one graphics task's value to the Workload id RT64 actually assigns.
// HLE stages the value before processing the task's display list; RT64
// reports each numbered Workload before a queue thread can see it; HLE then
// finishes the task. Only the task's first Workload takes the value: a task
// that publishes none or several never lends its identity to another
// Workload, which then keeps native shadows. Not synchronized.
template <class T>
class PublishedTasks {
public:
    void stage(std::optional<T> value) { staged_ = std::move(value); }

    void published(uint64_t workload_id) {
        while (!bound_.empty() && bound_.begin()->first + retained_workloads < workload_id) {
            bound_.erase(bound_.begin());
        }
        bound_.erase(workload_id);
        if (staged_) bound_.emplace(workload_id, std::move(*staged_));
        staged_.reset();
    }

    void finish() { staged_.reset(); }

    T* find(uint64_t workload_id) {
        const auto found = bound_.find(workload_id);
        return found == bound_.end() ? nullptr : &found->second;
    }

    void invalidate() {
        staged_.reset();
        bound_.clear();
    }

private:
    // Workloads may be queued or re-rendered for interpolation; older ids can
    // no longer be rendered.
    static constexpr uint64_t retained_workloads = 16;
    std::optional<T> staged_;
    std::map<uint64_t, T> bound_;
};

} // namespace lambo::rt

#include "lambo_rt_lap_window.h"

#include <cstdio>
#include <stdexcept>

namespace {
using lambo::rt::LapWindow;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        LapWindow window(3);
        require(!window.record(1, true, ""), "a partial window reported early");
        require(!window.record(2, false, "gate_3"), "a partial window reported early");
        const auto full = window.record(3, true, "");
        require(full && full->tasks == 3 && full->ready == 2, "a full window was not reported");
        require(full->reasons.size() == 1 && full->reasons.at("gate_3") == 1, "a fallback reason was lost");

        // A repeated workload is an interpolated re-presentation, not a new task.
        require(!window.record(3, true, ""), "a repeated workload was counted twice");
        require(!window.flush(), "a repeated workload left a pending task");

        require(!window.record(4, true, ""), "a partial window reported early");
        const auto tail = window.flush();
        require(tail && tail->tasks == 1 && tail->ready == 1, "a partial window was not flushed");
        require(!window.flush(), "a flushed window was flushed again");

        // A lap that is not a multiple of the window keeps its tail.
        LapWindow lap(120);
        size_t counted = 0, summaries = 0;
        for (uint64_t id = 1; id <= 150; ++id) {
            if (const auto summary = lap.record(id, true, "")) {
                counted += summary->tasks;
                ++summaries;
            }
        }
        const auto remainder = lap.flush();
        require(summaries == 1 && counted == 120, "the first full window was wrong");
        require(remainder && remainder->tasks == 30, "the 30 trailing tasks of a lap were dropped");

        // A new session starts with an empty window.
        LapWindow session(120);
        session.record(200, false, "gate_1");
        session.reset();
        require(!session.flush(), "pending tasks survived a session reset");

        std::puts("PASS: ray-traced shadow lap window");
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}

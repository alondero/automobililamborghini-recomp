// Lock-scope regression for the main-thread frame pump (issue #401). The
// presentation thread takes the frontend mutex for every UI draw, so only the
// phase that shares UI state may run while the pump holds it.
#include "lambo_frame_pump.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

struct TrackingMutex {
    int depth = 0;
    void lock() { ++depth; }
    void unlock() { --depth; }
};

struct RecordingHost {
    TrackingMutex& mutex;
    std::vector<std::pair<std::string, bool>> calls;

    void record(const char* phase) { calls.emplace_back(phase, mutex.depth > 0); }
    void pump_audio() { record("audio"); }
    void collect_events() { record("collect"); }
    void update_frontend() { record("frontend"); }
    void show_advisory() { record("advisory"); }
};

} // namespace

int main() {
    TrackingMutex mutex;
    RecordingHost host{mutex, {}};
    lambo::frame_pump::run(mutex, host);

    const std::vector<std::pair<std::string, bool>> expected = {
        {"audio", false},     // device open/close/recovery can block
        {"collect", false},   // SDL_PollEvent can block in the OS event loop
        {"frontend", true},   // shares recompui/recompinput/config state with draws
        {"advisory", false},  // modal message box blocks until dismissed
    };
    expect(host.calls.size() == expected.size(), "every pump phase runs once");
    for (size_t i = 0; i < expected.size(); ++i) {
        expect(host.calls[i].first == expected[i].first, "pump phases run in order");
        if (host.calls[i].second != expected[i].second) {
            std::fprintf(stderr, "FAIL: phase '%s' ran %s the frontend mutex\n",
                         expected[i].first.c_str(),
                         host.calls[i].second ? "holding" : "without");
            return 1;
        }
    }
    expect(mutex.depth == 0, "the pump releases the frontend mutex");
    std::puts("frame pump lock-scope tests passed");
    return 0;
}

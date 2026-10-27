#pragma once

// Android present-stall report. When presented frames stop, list every thread's
// kernel wait so a priority-inheritance lock names its holder. See
// docs/ANDROID.md, "Present stall report".

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lambo::stall {

// Graphics thread, once per presented frame.
void beat();

// Idempotent. Starts the monitor on Android only; reports go to stderr, which the
// launcher keeps as native-errors.log.
void start();

// /proc/<pid>/task/<tid>/syscall
struct Syscall {
    bool running = false;
    long number = -1;
    uint64_t args[6] = {};
};
std::optional<Syscall> parse_syscall(std::string_view line);

struct FutexWait {
    uint64_t address = 0;
    unsigned command = 0;
    bool priority_inheritance = false;
};
std::optional<FutexWait> futex_wait(const Syscall& call, long futex_number);
uint32_t pi_owner(uint32_t word);
std::string format_futex(const FutexWait& wait, std::optional<uint32_t> word);
// `holder` describes the owner thread, or is nullopt when this process has no such thread.
std::string format_holder(uint32_t owner, const std::optional<std::string>& holder);

// /proc/<pid>/task/<tid>/stat
struct ThreadStat {
    std::string name;
    char state = '?';
    uint64_t cpu_ticks = 0;
};
std::optional<ThreadStat> parse_stat(std::string_view line);

class StallMonitor {
public:
    StallMonitor(std::chrono::milliseconds threshold, int max_reports)
        : threshold_(threshold), reports_left_(max_reports) {}

    // True once per stall, when `beats` has not changed for the threshold.
    bool observe(uint64_t beats, std::chrono::steady_clock::time_point now);

private:
    std::chrono::milliseconds threshold_;
    int reports_left_;
    bool started_ = false;
    bool reported_ = false;
    uint64_t beats_ = 0;
    std::chrono::steady_clock::time_point changed_{};
};

} // namespace lambo::stall

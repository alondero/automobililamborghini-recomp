#ifndef LAMBO_HARNESS_REPORT_H
#define LAMBO_HARNESS_REPORT_H

#include <cstdint>
#include <string>

namespace lambo::harness {

struct Snapshot {
    int threads{};
    int vis{};
    bool first_vi{};
    int max_state{};
    int current_state{};
    int current_menu_screen{-1};
    int loaded_circuit{};
    int swaps{};
    int player_vehicle{-1};
    int player_speed{};
    int max_abs_player_speed{};
};

struct Outcome {
    std::string reason;
    int exit_code{};
};

void note_thread_created();
int vis_count();
int sample_vi();
Snapshot snapshot();
void log_boot_summary();

Outcome finalize_outcome(const char* reason, int exit_code);
void write_result(const Outcome& outcome);

} // namespace lambo::harness

#endif // LAMBO_HARNESS_REPORT_H

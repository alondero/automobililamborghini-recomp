#ifndef LAMBO_REPLAY_RUNTIME_H
#define LAMBO_REPLAY_RUNTIME_H

#include <cstdint>
#include <string>

#include "lambo_replay.h"

namespace lambo::replay_runtime {

struct Status {
    bool configured{};
    bool recording{};
    bool active{};
    bool complete{};
    bool failed{};
    bool exit_on_end{};
    std::uint64_t total_frames{};
    std::uint64_t frames_consumed{};
    std::uint64_t guest_frames_verified{};
    std::uint64_t dispatcher_ticks{};
};

// Parse the replay/recording environment before recomp::start creates guest
// threads. Returns false for an invalid configuration or trace.
bool initialize_from_environment();
std::string last_error();

// Playback owns port 0 from its first synchronized frame onward, including the
// neutral frame after EOF. Before synchronization, normal menu/warp input works.
bool playback_owns_input();
bool playback_frame(replay::InputFrame& output);

// Publish both physical pedal channels only if playback has not acquired them.
// The ownership check and writes are serialized with replay activation so a
// main-thread sample cannot overwrite frame zero after observing stale ownership.
void publish_physical_analog(bool throttle_analog, float throttle,
                             bool brake_analog, float brake);

Status status();
bool should_exit();
std::string terminal_reason();

// Flush an active recording. Safe to call more than once from immediate-exit
// paths; only the first call performs work.
void finalize();

} // namespace lambo::replay_runtime

// The input hook stages a frame immediately after controller decoding and
// before the ROM derives its held/pressed masks. The dispatcher hooks verify
// and delimit the exact subsequent game update that consumes that frame.
extern "C" void lambo_replay_dispatch_begin(std::uint8_t* rdram);
extern "C" void lambo_replay_dispatch_end(std::uint8_t* rdram);
extern "C" void lambo_replay_input_tick(std::uint8_t* rdram);

// A RAM restore cannot rewind host-side replay state. The save-state module
// reports successful loads so a mid-replay restore becomes an explicit failure.
extern "C" void lambo_replay_state_loaded();

#endif

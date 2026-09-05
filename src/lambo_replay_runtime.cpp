#include "lambo_replay_runtime.h"

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "lambo_analog_brake.h"
#include "lambo_analog_throttle.h"
#include "lambo_input_gate.h"
#include "lambo_input_quantize.h"
#include "lambo_log.h"
#include "recomp.h"

namespace {

constexpr gpr kGameStateAddress = (gpr)(int32_t)0x800CE6ACu;
constexpr gpr kPortZeroPadAddress = (gpr)(int32_t)0x800A39E0u;
constexpr gpr kPortZeroHeldAddress = (gpr)(int32_t)0x800A39F8u;
constexpr gpr kPortZeroPressedAddress = (gpr)(int32_t)0x800A3A00u;

struct RuntimeState {
    std::mutex mutex;
    std::optional<lambo::replay::Trace> trace;
    std::unique_ptr<lambo::replay::Recorder> recorder;
    lambo::replay::InputFrame playback_input{};
    lambo::replay::InputFrame record_input{};
    struct PhysicalAnalog {
        bool throttle_analog{};
        std::uint16_t throttle{};
        bool brake_analog{};
        std::uint16_t brake{};
    } physical_analog;
    std::string error;
    std::string terminal_reason;

    bool configured{};
    bool recording{};
    bool active{};
    bool owns_input{};
    bool complete{};
    bool failed{};
    bool exit_on_end{true};
    bool block_physical_analog{};
    bool finalized{};

    std::uint64_t frames_consumed{};
    std::uint64_t guest_frames_verified{};
    std::uint64_t dispatcher_ticks{};
    std::uint64_t load_generation{};

    int start_state{8};
    std::uint64_t start_delay{};
    std::uint64_t eligible_ticks{};
    std::uint64_t cursor{};
    std::uint64_t seen_load_generation{};
    bool frame_staged{};
    bool frame_in_dispatch{};
    bool frame_verified{};
    bool bootstrap_neutral_in_dispatch{};
    bool input_was_suppressed{};
};

RuntimeState g_state;

// A single atomic is reserved for the exceptional path because a guest hook
// must never let a C++ exception escape into recompiled code.
std::atomic<bool> g_hook_exception{false};

void set_error_locked(std::string message, std::string reason) {
    g_state.error = std::move(message);
    g_state.terminal_reason = std::move(reason);
    g_state.failed = true;
}

void set_error(std::string message, std::string reason) {
    std::lock_guard lock(g_state.mutex);
    set_error_locked(std::move(message), std::move(reason));
}

void mark_hook_exception() noexcept {
    g_hook_exception.store(true, std::memory_order_release);
}

template <typename Function>
void invoke_guest_hook(Function&& function) noexcept {
    try {
        std::forward<Function>(function)();
    } catch (...) {
        mark_hook_exception();
    }
}

bool parse_integer_env(const char* name, long long default_value,
                       long long minimum, long long maximum, long long& output) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        output = default_value;
        return true;
    }
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(value, &end, 10);
    if (errno == ERANGE || end == value || end == nullptr || *end != '\0' ||
        parsed < minimum || parsed > maximum) {
        set_error(std::string(name) + " must be an integer in [" +
                  std::to_string(minimum) + ", " + std::to_string(maximum) + "]",
                  "invalid_configuration");
        return false;
    }
    output = parsed;
    return true;
}

bool parse_bool_env(const char* name, bool default_value, bool& output) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        output = default_value;
        return true;
    }
    const std::string text(value);
    if (text == "1" || text == "true" || text == "TRUE" ||
        text == "yes" || text == "YES" || text == "on" || text == "ON") {
        output = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "FALSE" ||
        text == "no" || text == "NO" || text == "off" || text == "OFF") {
        output = false;
        return true;
    }
    set_error(std::string(name) + " must be 0/1, true/false, yes/no, or on/off",
              "invalid_configuration");
    return false;
}

void publish_analog(const lambo::replay::InputFrame& input) {
    constexpr float kU16Max = 65535.0f;
    lambo::analog_throttle::publish(0, input.throttle_analog,
                                    static_cast<float>(input.throttle) / kU16Max);
    lambo::analog_brake::publish(0, input.brake_analog,
                                 static_cast<float>(input.brake) / kU16Max);
}

struct GuestPad {
    std::uint16_t buttons{};
    std::int8_t stick_x{};
    std::int8_t stick_y{};
};

GuestPad read_guest_pad(std::uint8_t* rdram) {
    GuestPad pad;
    pad.buttons = static_cast<std::uint16_t>(MEM_HU(0, kPortZeroPadAddress));
    pad.stick_x = static_cast<std::int8_t>(MEM_B(2, kPortZeroPadAddress));
    pad.stick_y = static_cast<std::int8_t>(MEM_B(3, kPortZeroPadAddress));
    return pad;
}

RuntimeState::PhysicalAnalog sample_physical_analog() {
    RuntimeState::PhysicalAnalog analog;
    float value = 0.0f;
    analog.throttle_analog = lambo::analog_throttle::sample(0, value);
    analog.throttle = lambo::input::quantize_normalized(value);
    value = 0.0f;
    analog.brake_analog = lambo::analog_brake::sample(0, value);
    analog.brake = lambo::input::quantize_normalized(value);
    return analog;
}

lambo::replay::InputFrame compose_input(const GuestPad& pad,
                                        const RuntimeState::PhysicalAnalog& analog) {
    lambo::replay::InputFrame input{};
    input.buttons = pad.buttons;
    input.stick_x = pad.stick_x;
    input.stick_y = pad.stick_y;
    input.throttle_analog = analog.throttle_analog;
    input.throttle = analog.throttle;
    input.brake_analog = analog.brake_analog;
    input.brake = analog.brake;
    return input;
}

std::uint16_t button_mask_with_stick(const lambo::replay::InputFrame& input) {
    std::uint16_t mask = input.buttons;
    if (input.stick_x < -50) mask |= 0x0200u;
    if (input.stick_x > 50) mask |= 0x0100u;
    if (input.stick_y < -50) mask |= 0x0400u;
    if (input.stick_y > 50) mask |= 0x0800u;
    return mask;
}

void write_guest_input(std::uint8_t* rdram, const lambo::replay::InputFrame& input) {
    MEM_H(0, kPortZeroPadAddress) = input.buttons;
    MEM_B(2, kPortZeroPadAddress) = input.stick_x;
    MEM_B(3, kPortZeroPadAddress) = input.stick_y;
}

bool effective_input_matches(const lambo::replay::InputFrame& expected,
                             const lambo::replay::InputFrame& observed) {
    const bool buttons_match = observed.buttons == expected.buttons ||
        observed.buttons == button_mask_with_stick(expected);
    return buttons_match &&
           expected.stick_x == observed.stick_x &&
           expected.stick_y == observed.stick_y &&
           expected.throttle_analog == observed.throttle_analog &&
           (!expected.throttle_analog || expected.throttle == observed.throttle) &&
           expected.brake_analog == observed.brake_analog &&
           (!expected.brake_analog || expected.brake == observed.brake);
}

void install_neutral_playback_frame_locked(std::uint8_t* rdram = nullptr) {
    const lambo::replay::InputFrame neutral{};
    g_state.playback_input = neutral;
    publish_analog(neutral);
    if (rdram != nullptr) write_guest_input(rdram, neutral);
}

bool install_playback_frame_locked(std::uint8_t* rdram, std::uint64_t frame,
                                   bool acquire_ownership = false) {
    lambo::replay::InputFrame input{};
    if (!g_state.trace->frame_at(frame, input)) {
        install_neutral_playback_frame_locked(rdram);
        set_error_locked("replay frame " + std::to_string(frame) +
                             " is outside the loaded trace",
                         "replay_frame_out_of_range");
        return false;
    }
    g_state.playback_input = input;
    if (acquire_ownership) g_state.owns_input = true;
    publish_analog(input);
    write_guest_input(rdram, input);
    return true;
}

void neutralize_guest_for_dispatch_locked(std::uint8_t* rdram) {
    const lambo::replay::InputFrame neutral{};
    publish_analog(neutral);
    write_guest_input(rdram, neutral);
    MEM_H(0, kPortZeroHeldAddress) = 0;
    MEM_H(0, kPortZeroPressedAddress) = 0;
}

void release_recording_frame_locked() {
    g_state.block_physical_analog = false;
}

void stage_recording_frame_locked(std::uint8_t* rdram) {
    g_state.record_input = compose_input(read_guest_pad(rdram), g_state.physical_analog);
    publish_analog(g_state.record_input);
    g_state.block_physical_analog = true;
    g_state.frame_staged = true;
}

bool recorder_observe_locked(const lambo::replay::InputFrame& input) {
    if (g_state.recorder == nullptr || g_state.finalized) {
        set_error_locked("recording is no longer accepting input", "record_failed");
        return false;
    }
    if (!g_state.recorder->observe(input)) {
        set_error_locked(g_state.recorder->error(), "record_failed");
        return false;
    }
    const std::uint64_t count = g_state.recorder->total_frames();
    g_state.frames_consumed = count;
    g_state.guest_frames_verified = count;
    return true;
}

void update_start_eligibility_locked(std::uint8_t* rdram) {
    if (g_state.active) return;
    const int state = static_cast<std::int16_t>(MEM_H(0, kGameStateAddress));
    if (lambo::input_gate::guest_input_suppressed() || state != g_state.start_state) {
        g_state.eligible_ticks = 0;
        return;
    }
    if (g_state.eligible_ticks != UINT64_MAX) ++g_state.eligible_ticks;
    (void)rdram;
}

void dispatch_begin_impl(std::uint8_t* rdram) {
    std::lock_guard lock(g_state.mutex);
    if (!g_state.configured) return;

    ++g_state.dispatcher_ticks;
    if (g_state.failed) return;

    const bool state_was_loaded = g_state.load_generation != g_state.seen_load_generation;
    const bool input_session_active = g_state.owns_input ||
        (g_state.recording && g_state.active);
    if (state_was_loaded && input_session_active) {
        g_state.frame_staged = false;
        g_state.frame_in_dispatch = false;
        g_state.frame_verified = false;
        if (g_state.trace) {
            install_neutral_playback_frame_locked(rdram);
            set_error_locked("a save-state was loaded after replay playback started",
                             "state_loaded_during_replay");
        } else {
            release_recording_frame_locked();
            set_error_locked("a save-state was loaded after input recording started",
                             "state_loaded_during_recording");
        }
        return;
    }
    g_state.seen_load_generation = g_state.load_generation;

    if (g_state.complete) {
        if (g_state.trace) neutralize_guest_for_dispatch_locked(rdram);
        return;
    }

    const bool input_suppressed = lambo::input_gate::guest_input_suppressed();
    const int state = static_cast<std::int16_t>(MEM_H(0, kGameStateAddress));
    if (g_state.trace && !g_state.active) {
        if (state_was_loaded && !input_suppressed && state == g_state.start_state) {
            g_state.block_physical_analog = true;
            g_state.bootstrap_neutral_in_dispatch = true;
            neutralize_guest_for_dispatch_locked(rdram);
        }
        return;
    }

    if (g_state.trace && input_suppressed) {
        g_state.input_was_suppressed = true;
        neutralize_guest_for_dispatch_locked(rdram);
        return;
    }

    if (g_state.trace && g_state.input_was_suppressed) {
        g_state.input_was_suppressed = false;
        g_state.frame_staged = false;
        neutralize_guest_for_dispatch_locked(rdram);
        return;
    }

    if (!g_state.active) return;
    if (!g_state.frame_staged) {
        if (g_state.trace) install_neutral_playback_frame_locked(rdram);
        else release_recording_frame_locked();
        set_error_locked("an active input session reached the dispatcher without a staged frame",
                         "replay_clock_error");
        return;
    }
    if (g_state.frame_in_dispatch) {
        if (g_state.trace) install_neutral_playback_frame_locked(rdram);
        else release_recording_frame_locked();
        set_error_locked("dispatcher re-entered before the previous input frame completed",
                         "replay_clock_error");
        return;
    }

    const lambo::replay::InputFrame expected = g_state.trace
        ? g_state.playback_input : g_state.record_input;
    const lambo::replay::InputFrame observed = compose_input(
        read_guest_pad(rdram),
        g_state.trace ? sample_physical_analog() : g_state.physical_analog);
    if (!effective_input_matches(expected, observed)) {
        g_state.frame_staged = false;
        if (g_state.trace) install_neutral_playback_frame_locked(rdram);
        else release_recording_frame_locked();
        set_error_locked("staged input changed before the game dispatcher consumed it",
                         "replay_input_overwritten");
        return;
    }
    g_state.frame_verified = true;
    g_state.frame_in_dispatch = true;
}

void dispatch_end_impl(std::uint8_t* rdram) {
    std::lock_guard lock(g_state.mutex);
    if (!g_state.configured) return;

    if (g_state.bootstrap_neutral_in_dispatch) {
        g_state.bootstrap_neutral_in_dispatch = false;
        release_recording_frame_locked();
    }
    update_start_eligibility_locked(rdram);
    if (g_state.failed || !g_state.frame_in_dispatch) return;

    g_state.frame_in_dispatch = false;
    if (!g_state.frame_verified) {
        if (!g_state.trace) release_recording_frame_locked();
        set_error_locked("dispatcher completed an unverified input frame",
                         "replay_clock_error");
        return;
    }
    g_state.frame_verified = false;
    g_state.frame_staged = false;

    if (g_state.trace) {
        const std::uint64_t consumed = g_state.cursor + 1;
        g_state.frames_consumed = consumed;
        g_state.guest_frames_verified = consumed;
        if (consumed >= g_state.trace->total_frames()) {
            g_state.complete = true;
            install_neutral_playback_frame_locked();
            LAMBO_LOG_INFO("replay", "playback complete after %llu game frame(s)\n",
                           static_cast<unsigned long long>(consumed));
        }
        return;
    }

    (void)recorder_observe_locked(g_state.record_input);
    release_recording_frame_locked();
}

void input_tick_impl(std::uint8_t* rdram) {
    std::lock_guard lock(g_state.mutex);
    if (!g_state.configured || g_state.failed) return;

    if (g_state.trace && g_state.complete) {
        install_neutral_playback_frame_locked(rdram);
        return;
    }

    if (g_state.trace && lambo::input_gate::guest_input_suppressed()) {
        if (g_state.owns_input) {
            g_state.input_was_suppressed = true;
            neutralize_guest_for_dispatch_locked(rdram);
        }
        return;
    }
    g_state.input_was_suppressed = false;

    if (!g_state.active) {
        if (g_state.eligible_ticks <= g_state.start_delay) return;
        g_state.active = true;
        const int state = static_cast<std::int16_t>(MEM_H(0, kGameStateAddress));
        LAMBO_LOG_INFO("replay", "%s started at game state %d\n",
                       g_state.trace ? "playback" : "recording", state);
        if (g_state.trace) g_state.cursor = 0;
    }

    if (g_state.frame_staged) {
        if (g_state.trace) {
            (void)install_playback_frame_locked(rdram, g_state.cursor);
        } else {
            write_guest_input(rdram, g_state.record_input);
            publish_analog(g_state.record_input);
        }
        return;
    }

    if (g_state.trace) {
        g_state.cursor = g_state.frames_consumed;
        if (!install_playback_frame_locked(rdram, g_state.cursor, !g_state.owns_input)) {
            return;
        }
        g_state.frame_staged = true;
    } else {
        stage_recording_frame_locked(rdram);
    }
}

} // namespace

namespace lambo::replay_runtime {

bool initialize_from_environment() {
    try {
        const char* replay_path = std::getenv("LAMBO_INPUT_REPLAY");
        const char* record_path = std::getenv("LAMBO_INPUT_RECORD");
        const bool wants_replay = replay_path != nullptr && replay_path[0] != '\0';
        const bool wants_record = record_path != nullptr && record_path[0] != '\0';
        if (wants_replay && wants_record) {
            set_error("LAMBO_INPUT_REPLAY and LAMBO_INPUT_RECORD are mutually exclusive",
                      "invalid_configuration");
            return false;
        }
        if (!wants_replay && !wants_record) return true;

        long long start_state = 8;
        long long start_delay = 0;
        if (!parse_integer_env("LAMBO_INPUT_START_STATE", 8, INT16_MIN, INT16_MAX,
                               start_state) ||
            !parse_integer_env("LAMBO_INPUT_START_DELAY", 0, 0, LLONG_MAX, start_delay)) {
            return false;
        }
        bool exit_on_end = true;
        if (!parse_bool_env("LAMBO_INPUT_EXIT_ON_END", true, exit_on_end)) return false;

        std::optional<replay::Trace> trace;
        std::unique_ptr<replay::Recorder> recorder;
        if (wants_replay) {
            replay::LoadResult loaded = replay::load_trace(std::filesystem::path(replay_path));
            if (!loaded) {
                set_error(loaded.error, "invalid_trace");
                return false;
            }
            trace.emplace(std::move(*loaded.trace));
        } else {
            recorder = std::make_unique<replay::Recorder>(std::filesystem::path(record_path));
            if (!recorder->ready()) {
                set_error(recorder->error(), "record_open_failed");
                return false;
            }
        }

        std::lock_guard lock(g_state.mutex);
        g_state.start_state = static_cast<int>(start_state);
        g_state.start_delay = static_cast<std::uint64_t>(start_delay);
        g_state.exit_on_end = exit_on_end;
        g_state.trace = std::move(trace);
        g_state.recorder = std::move(recorder);
        g_state.recording = wants_record;
        g_state.configured = true;
        if (g_state.trace) {
            LAMBO_LOG_INFO("replay", "armed %llu game frame(s) from %s; start_state=%d delay=%llu\n",
                           static_cast<unsigned long long>(g_state.trace->total_frames()),
                           replay_path, g_state.start_state,
                           static_cast<unsigned long long>(g_state.start_delay));
        } else {
            LAMBO_LOG_INFO("replay", "recording armed for %s; start_state=%d delay=%llu\n",
                           record_path, g_state.start_state,
                           static_cast<unsigned long long>(g_state.start_delay));
        }
        return true;
    } catch (...) {
        set_error("unexpected exception while initializing replay", "invalid_configuration");
        return false;
    }
}

std::string last_error() {
    if (g_hook_exception.load(std::memory_order_acquire)) {
        return "replay runtime exception was contained at the guest hook boundary";
    }
    std::lock_guard lock(g_state.mutex);
    return g_state.error;
}

bool playback_owns_input() {
    std::lock_guard lock(g_state.mutex);
    return g_state.owns_input;
}

bool playback_frame(replay::InputFrame& output) {
    std::lock_guard lock(g_state.mutex);
    if (!g_state.owns_input) return false;
    output = g_state.playback_input;
    publish_analog(output);
    return true;
}

void publish_physical_analog(bool throttle_analog, float throttle,
                             bool brake_analog, float brake) {
    std::lock_guard lock(g_state.mutex);
    g_state.physical_analog = RuntimeState::PhysicalAnalog{
        throttle_analog, lambo::input::quantize_normalized(throttle),
        brake_analog, lambo::input::quantize_normalized(brake)};
    if (g_state.owns_input || g_state.block_physical_analog) return;
    lambo::analog_throttle::publish(0, throttle_analog, throttle);
    lambo::analog_brake::publish(0, brake_analog, brake);
}

Status status() {
    std::lock_guard lock(g_state.mutex);
    Status output;
    output.configured = g_state.configured;
    output.recording = g_state.recording;
    output.active = g_state.active;
    output.complete = g_state.complete;
    output.failed = g_state.failed || g_hook_exception.load(std::memory_order_acquire);
    output.exit_on_end = g_state.exit_on_end;
    output.total_frames = g_state.trace
        ? g_state.trace->total_frames()
        : (g_state.recorder ? g_state.recorder->total_frames() : 0);
    output.frames_consumed = g_state.frames_consumed;
    output.guest_frames_verified = g_state.guest_frames_verified;
    output.dispatcher_ticks = g_state.dispatcher_ticks;
    return output;
}

bool should_exit() {
    if (g_hook_exception.load(std::memory_order_acquire)) return true;
    std::lock_guard lock(g_state.mutex);
    return g_state.failed || (g_state.complete && g_state.exit_on_end);
}

std::string terminal_reason() {
    if (g_hook_exception.load(std::memory_order_acquire)) {
        return "replay_runtime_exception";
    }
    std::lock_guard lock(g_state.mutex);
    if (!g_state.terminal_reason.empty()) return g_state.terminal_reason;
    if (g_state.complete) return "replay_complete";
    return {};
}

void finalize() {
    std::lock_guard lock(g_state.mutex);
    if (g_state.finalized) return;
    g_state.finalized = true;
    if (g_state.recorder == nullptr) return;
    try {
        if (!g_state.recorder->finalize()) {
            set_error_locked(g_state.recorder->error(), "record_finalize_failed");
            return;
        }
        LAMBO_LOG_INFO("replay", "recording finalized: %llu game frame(s)\n",
                       static_cast<unsigned long long>(g_state.recorder->total_frames()));
    } catch (...) {
        set_error_locked("unexpected exception while finalizing recording",
                         "record_finalize_failed");
    }
}

} // namespace lambo::replay_runtime

extern "C" void lambo_replay_state_loaded() noexcept {
    invoke_guest_hook([] {
        std::lock_guard lock(g_state.mutex);
        ++g_state.load_generation;
    });
}

extern "C" void lambo_replay_dispatch_begin(std::uint8_t* rdram) noexcept {
    invoke_guest_hook([rdram] { dispatch_begin_impl(rdram); });
}

extern "C" void lambo_replay_dispatch_end(std::uint8_t* rdram) noexcept {
    invoke_guest_hook([rdram] { dispatch_end_impl(rdram); });
}

extern "C" void lambo_replay_input_tick(std::uint8_t* rdram) noexcept {
    invoke_guest_hook([rdram] { input_tick_impl(rdram); });
}

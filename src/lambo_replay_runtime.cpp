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

#include "lambo_analog_brake.h"
#include "lambo_analog_throttle.h"
#include "lambo_input_gate.h"
#include "lambo_log.h"
#include "recomp.h"

namespace {

constexpr gpr kGameStateAddress = (gpr)(int32_t)0x800CE6ACu;
constexpr gpr kPortZeroPadAddress = (gpr)(int32_t)0x800A39E0u;
constexpr gpr kPortZeroHeldAddress = (gpr)(int32_t)0x800A39F8u;
constexpr gpr kPortZeroPressedAddress = (gpr)(int32_t)0x800A3A00u;

std::optional<lambo::replay::Trace> g_trace;
std::unique_ptr<lambo::replay::Recorder> g_recorder;

std::atomic<bool> g_configured{false};
std::atomic<bool> g_recording{false};
std::atomic<bool> g_active{false};
std::atomic<bool> g_owns_input{false};
std::atomic<bool> g_complete{false};
std::atomic<bool> g_failed{false};
std::atomic<bool> g_exit_on_end{true};
std::atomic<bool> g_block_physical_analog{false};
std::atomic<std::uint64_t> g_frames_consumed{0};
std::atomic<std::uint64_t> g_guest_frames_verified{0};
std::atomic<std::uint64_t> g_dispatcher_ticks{0};
std::atomic<std::uint64_t> g_load_generation{0};

std::mutex g_input_mutex;
lambo::replay::InputFrame g_playback_input{};
lambo::replay::InputFrame g_record_input{};
std::mutex g_analog_mutex;

std::mutex g_error_mutex;
std::string g_error;
std::string g_terminal_reason;

std::mutex g_recorder_mutex;
bool g_finalized = false;

// The fields below are owned by the game-dispatch thread.
int g_start_state = 8;
std::uint64_t g_start_delay = 0;
std::uint64_t g_eligible_ticks = 0;
std::uint64_t g_cursor = 0;
std::uint64_t g_seen_load_generation = 0;
bool g_frame_staged = false;
bool g_frame_in_dispatch = false;
bool g_frame_verified = false;
bool g_bootstrap_neutral_in_dispatch = false;
bool g_input_was_suppressed = false;

void set_error(std::string message, std::string reason) {
    {
        std::lock_guard lock(g_error_mutex);
        g_error = std::move(message);
        g_terminal_reason = std::move(reason);
    }
    g_failed.store(true, std::memory_order_release);
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

void publish_analog_unlocked(const lambo::replay::InputFrame& input) {
    constexpr float kU16Max = 65535.0f;
    lambo::analog_throttle::publish(0, input.throttle_analog,
        static_cast<float>(input.throttle) / kU16Max);
    lambo::analog_brake::publish(0, input.brake_analog,
        static_cast<float>(input.brake) / kU16Max);
}

std::uint16_t normalized_u16(float value) {
    constexpr float kU16Max = 65535.0f;
    if (value <= 0.0f) return 0;
    if (value >= 1.0f) return UINT16_MAX;
    return static_cast<std::uint16_t>(value * kU16Max + 0.5f);
}

lambo::replay::InputFrame read_guest_input(std::uint8_t* rdram) {
    lambo::replay::InputFrame input{};
    input.buttons = static_cast<std::uint16_t>(MEM_HU(0, kPortZeroPadAddress));
    input.stick_x = static_cast<std::int8_t>(MEM_B(2, kPortZeroPadAddress));
    input.stick_y = static_cast<std::int8_t>(MEM_B(3, kPortZeroPadAddress));
    float analog = 0.0f;
    input.throttle_analog = lambo::analog_throttle::sample(0, analog);
    input.throttle = normalized_u16(analog);
    analog = 0.0f;
    input.brake_analog = lambo::analog_brake::sample(0, analog);
    input.brake = normalized_u16(analog);
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
    // In aggregate-controller mode the stock decoder may fold stick directions
    // into the raw button halfword after our staging hook. Accept only that
    // measured transform; all other fields must remain exact.
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

void install_playback_frame(std::uint8_t* rdram, std::uint64_t frame,
                            bool acquire_ownership = false) {
    const lambo::replay::InputFrame input = g_trace->frame_at(frame);
    {
        std::lock_guard lock(g_input_mutex);
        g_playback_input = input;
    }
    std::lock_guard analog_lock(g_analog_mutex);
    if (acquire_ownership) {
        g_owns_input.store(true, std::memory_order_release);
    }
    publish_analog_unlocked(input);
    write_guest_input(rdram, input);
}

void install_neutral_playback_frame(std::uint8_t* rdram = nullptr) {
    const lambo::replay::InputFrame neutral{};
    {
        std::lock_guard lock(g_input_mutex);
        g_playback_input = neutral;
    }
    std::lock_guard analog_lock(g_analog_mutex);
    publish_analog_unlocked(neutral);
    if (rdram != nullptr) write_guest_input(rdram, neutral);
}

void neutralize_guest_for_dispatch(std::uint8_t* rdram) {
    const lambo::replay::InputFrame neutral{};
    {
        std::lock_guard analog_lock(g_analog_mutex);
        publish_analog_unlocked(neutral);
    }
    write_guest_input(rdram, neutral);
    MEM_H(0, kPortZeroHeldAddress) = 0;
    MEM_H(0, kPortZeroPressedAddress) = 0;
}

bool recorder_observe_and_publish(const lambo::replay::InputFrame& input) {
    std::lock_guard lock(g_recorder_mutex);
    if (g_recorder == nullptr || g_finalized) return false;
    if (!g_recorder->observe(input)) {
        set_error(g_recorder->error(), "record_failed");
        return false;
    }
    // Finalization takes the same mutex. Publish every observable counter
    // before releasing it so an immediate-exit thread cannot serialize N
    // frames but snapshot N-1 consumed/verified frames in the result.
    const std::uint64_t count = g_recorder->total_frames();
    g_frames_consumed.store(count, std::memory_order_release);
    g_guest_frames_verified.store(count, std::memory_order_release);
    return true;
}

void stage_recording_frame(std::uint8_t* rdram) {
    // Freeze the final pedal values atomically with the sample. The dispatcher
    // consumes them later than the controller decoder; without this ownership
    // window, a main-thread publication could make the trace disagree with the
    // physics update it claims to represent.
    {
        std::lock_guard analog_lock(g_analog_mutex);
        g_record_input = read_guest_input(rdram);
        g_block_physical_analog.store(true, std::memory_order_release);
    }
    g_frame_staged = true;
}

void release_recording_frame() {
    std::lock_guard analog_lock(g_analog_mutex);
    g_block_physical_analog.store(false, std::memory_order_release);
}

void update_start_eligibility(std::uint8_t* rdram) {
    if (g_active.load(std::memory_order_acquire)) return;

    const int state = static_cast<std::int16_t>(MEM_H(0, kGameStateAddress));
    if (lambo::input_gate::guest_input_suppressed() || state != g_start_state) {
        g_eligible_ticks = 0;
        return;
    }
    if (g_eligible_ticks != UINT64_MAX) ++g_eligible_ticks;
}

} // namespace

namespace lambo::replay_runtime {

bool initialize_from_environment() {
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
    if (!parse_integer_env("LAMBO_INPUT_START_STATE", 8, INT16_MIN, INT16_MAX, start_state) ||
        !parse_integer_env("LAMBO_INPUT_START_DELAY", 0, 0, LLONG_MAX, start_delay)) {
        return false;
    }
    g_start_state = static_cast<int>(start_state);
    g_start_delay = static_cast<std::uint64_t>(start_delay);
    bool exit_on_end = true;
    if (!parse_bool_env("LAMBO_INPUT_EXIT_ON_END", true, exit_on_end)) return false;
    g_exit_on_end.store(exit_on_end, std::memory_order_relaxed);

    if (wants_replay) {
        replay::LoadResult loaded = replay::load_trace(std::filesystem::path(replay_path));
        if (!loaded) {
            set_error(loaded.error, "invalid_trace");
            return false;
        }
        g_trace.emplace(std::move(*loaded.trace));
        LAMBO_LOG_INFO("replay", "armed %llu game frame(s) from %s; start_state=%d delay=%llu\n",
            static_cast<unsigned long long>(g_trace->total_frames()), replay_path,
            g_start_state, static_cast<unsigned long long>(g_start_delay));
    } else {
        g_recorder = std::make_unique<replay::Recorder>(std::filesystem::path(record_path));
        if (!g_recorder->ready()) {
            set_error(g_recorder->error(), "record_open_failed");
            g_recorder.reset();
            return false;
        }
        g_recording.store(true, std::memory_order_release);
        LAMBO_LOG_INFO("replay", "recording armed for %s; start_state=%d delay=%llu\n",
            record_path, g_start_state, static_cast<unsigned long long>(g_start_delay));
    }

    g_configured.store(true, std::memory_order_release);
    return true;
}

std::string last_error() {
    std::lock_guard lock(g_error_mutex);
    return g_error;
}

bool playback_owns_input() {
    return g_owns_input.load(std::memory_order_acquire);
}

bool playback_frame(replay::InputFrame& output) {
    if (!playback_owns_input()) return false;
    {
        std::lock_guard lock(g_input_mutex);
        output = g_playback_input;
    }
    // Refresh at the controller-read seam as well as the dispatcher tick. This closes the
    // one-frame ownership handoff race with the SDL input publisher.
    {
        std::lock_guard analog_lock(g_analog_mutex);
        publish_analog_unlocked(output);
    }
    return true;
}

void publish_physical_analog(bool throttle_analog, float throttle,
                             bool brake_analog, float brake) {
    std::lock_guard analog_lock(g_analog_mutex);
    if (g_owns_input.load(std::memory_order_acquire) ||
        g_block_physical_analog.load(std::memory_order_acquire)) return;
    lambo::analog_throttle::publish(0, throttle_analog, throttle);
    lambo::analog_brake::publish(0, brake_analog, brake);
}

Status status() {
    Status output;
    output.configured = g_configured.load(std::memory_order_acquire);
    output.recording = g_recording.load(std::memory_order_acquire);
    output.active = g_active.load(std::memory_order_acquire);
    output.complete = g_complete.load(std::memory_order_acquire);
    output.failed = g_failed.load(std::memory_order_acquire);
    output.exit_on_end = g_exit_on_end.load(std::memory_order_acquire);
    if (g_trace) {
        output.total_frames = g_trace->total_frames();
    } else {
        std::lock_guard lock(g_recorder_mutex);
        output.total_frames = g_recorder ? g_recorder->total_frames() : 0;
    }
    output.frames_consumed = g_frames_consumed.load(std::memory_order_acquire);
    output.guest_frames_verified =
        g_guest_frames_verified.load(std::memory_order_acquire);
    output.dispatcher_ticks = g_dispatcher_ticks.load(std::memory_order_acquire);
    return output;
}

bool should_exit() {
    if (g_failed.load(std::memory_order_acquire)) return true;
    return g_complete.load(std::memory_order_acquire) &&
           g_exit_on_end.load(std::memory_order_acquire);
}

std::string terminal_reason() {
    std::lock_guard lock(g_error_mutex);
    if (!g_terminal_reason.empty()) return g_terminal_reason;
    if (g_complete.load(std::memory_order_acquire)) return "replay_complete";
    return {};
}

void finalize() {
    std::lock_guard lock(g_recorder_mutex);
    if (g_finalized) return;
    g_finalized = true;
    if (g_recorder == nullptr) return;

    if (!g_recorder->finalize()) {
        set_error(g_recorder->error(), "record_finalize_failed");
        return;
    }
    LAMBO_LOG_INFO("replay", "recording finalized: %llu game frame(s)\n",
        static_cast<unsigned long long>(g_recorder->total_frames()));
}

} // namespace lambo::replay_runtime

extern "C" void lambo_replay_state_loaded() {
    g_load_generation.fetch_add(1, std::memory_order_acq_rel);
}

extern "C" void lambo_replay_dispatch_begin(std::uint8_t* rdram) {
    using namespace lambo::replay_runtime;
    if (!g_configured.load(std::memory_order_acquire)) return;

    g_dispatcher_ticks.fetch_add(1, std::memory_order_relaxed);
    if (g_failed.load(std::memory_order_acquire)) return;

    const std::uint64_t load_generation =
        g_load_generation.load(std::memory_order_acquire);
    const bool state_was_loaded = load_generation != g_seen_load_generation;
    const bool input_session_active =
        g_owns_input.load(std::memory_order_acquire) ||
        (g_recording.load(std::memory_order_acquire) &&
         g_active.load(std::memory_order_acquire));
    if (state_was_loaded && input_session_active) {
        g_frame_staged = false;
        g_frame_in_dispatch = false;
        g_frame_verified = false;
        if (g_trace) {
            install_neutral_playback_frame(rdram);
            set_error("a save-state was loaded after replay playback started",
                      "state_loaded_during_replay");
        } else {
            release_recording_frame();
            set_error("a save-state was loaded after input recording started",
                      "state_loaded_during_recording");
        }
        return;
    }
    g_seen_load_generation = load_generation;

    if (g_complete.load(std::memory_order_acquire)) {
        // exit_on_end=0 deliberately leaves the game running. The normal pad
        // decoder executes between dispatcher calls, so reassert neutral input
        // on every later update rather than letting its final sample leak in.
        if (g_trace) neutralize_guest_for_dispatch(rdram);
        return;
    }

    const bool input_suppressed = lambo::input_gate::guest_input_suppressed();
    const int state = static_cast<std::int16_t>(MEM_H(0, kGameStateAddress));
    if (g_trace && !g_active.load(std::memory_order_acquire)) {
        // Playback frames are staged at the controller-decode seam later in
        // the outer loop. If a load lands directly on start_state (or the game
        // was already there), make this one bootstrap update explicitly
        // neutral instead of consuming stale input from the loaded RAM.
        if (state_was_loaded && !input_suppressed && state == g_start_state) {
            g_block_physical_analog.store(true, std::memory_order_release);
            g_bootstrap_neutral_in_dispatch = true;
            neutralize_guest_for_dispatch(rdram);
        }
        return;
    }

    if (g_trace && input_suppressed) {
        // Once active, the overlay pauses movie consumption. This update is
        // neutral; the staged frame is re-applied after the overlay closes.
        g_input_was_suppressed = true;
        neutralize_guest_for_dispatch(rdram);
        return;
    }

    if (g_trace && g_input_was_suppressed) {
        // Suppression can end after the last controller decode. Keep that
        // transition update neutral, discard only the staging claim (not the
        // trace cursor), and let the following decoder install the same frame
        // through the stock held/pressed synthesis path.
        g_input_was_suppressed = false;
        g_frame_staged = false;
        neutralize_guest_for_dispatch(rdram);
        return;
    }

    if (!g_active.load(std::memory_order_acquire)) return;
    if (!g_frame_staged) {
        if (g_trace) install_neutral_playback_frame(rdram);
        else release_recording_frame();
        set_error("an active input session reached the dispatcher without a staged frame",
                  "replay_clock_error");
        return;
    }
    if (g_frame_in_dispatch) {
        if (g_trace) install_neutral_playback_frame(rdram);
        else release_recording_frame();
        set_error("dispatcher re-entered before the previous input frame completed",
                  "replay_clock_error");
        return;
    }

    lambo::replay::InputFrame expected;
    if (g_trace) {
        std::lock_guard lock(g_input_mutex);
        expected = g_playback_input;
    } else {
        expected = g_record_input;
    }
    if (!effective_input_matches(expected, read_guest_input(rdram))) {
        g_frame_staged = false;
        if (g_trace) install_neutral_playback_frame(rdram);
        else release_recording_frame();
        set_error("staged input changed before the game dispatcher consumed it",
                  "replay_input_overwritten");
        return;
    }
    g_frame_verified = true;
    g_frame_in_dispatch = true;
}

extern "C" void lambo_replay_dispatch_end(std::uint8_t* rdram) {
    if (!g_configured.load(std::memory_order_acquire)) return;

    if (g_bootstrap_neutral_in_dispatch) {
        g_bootstrap_neutral_in_dispatch = false;
        release_recording_frame();
    }
    update_start_eligibility(rdram);
    if (g_failed.load(std::memory_order_acquire) || !g_frame_in_dispatch) return;

    g_frame_in_dispatch = false;
    if (!g_frame_verified) {
        if (!g_trace) release_recording_frame();
        set_error("dispatcher completed an unverified input frame", "replay_clock_error");
        return;
    }
    g_frame_verified = false;

    g_frame_staged = false;
    if (g_trace) {
        const std::uint64_t consumed = g_cursor + 1;
        g_frames_consumed.store(consumed, std::memory_order_release);
        g_guest_frames_verified.store(consumed, std::memory_order_release);
        if (consumed >= g_trace->total_frames()) {
            g_complete.store(true, std::memory_order_release);
            install_neutral_playback_frame();
            LAMBO_LOG_INFO("replay", "playback complete after %llu game frame(s)\n",
                static_cast<unsigned long long>(consumed));
        }
        return;
    }

    (void)recorder_observe_and_publish(g_record_input);
    release_recording_frame();
}

extern "C" void lambo_replay_input_tick(std::uint8_t* rdram) {
    using namespace lambo::replay_runtime;
    if (!g_configured.load(std::memory_order_acquire) ||
        g_failed.load(std::memory_order_acquire)) return;

    if (g_trace && g_complete.load(std::memory_order_acquire)) {
        install_neutral_playback_frame(rdram);
        return;
    }

    if (g_trace && lambo::input_gate::guest_input_suppressed()) {
        if (playback_owns_input()) {
            g_input_was_suppressed = true;
            neutralize_guest_for_dispatch(rdram);
        }
        return;
    }

    // If an unsuppressed controller decode occurs before the next dispatcher,
    // it is itself the safe re-staging seam; no extra neutral transition update
    // is needed.
    g_input_was_suppressed = false;

    if (!g_active.load(std::memory_order_acquire)) {
        if (g_eligible_ticks <= g_start_delay) return;

        g_active.store(true, std::memory_order_release);
        const int state = static_cast<std::int16_t>(MEM_H(0, kGameStateAddress));
        if (g_trace) {
            g_cursor = 0;
            LAMBO_LOG_INFO("replay", "playback started at game state %d\n", state);
        } else {
            LAMBO_LOG_INFO("replay", "recording started at game state %d\n", state);
        }
    }

    if (g_frame_staged) {
        // A controller decode happened without an intervening dispatcher.
        // Preserve the pending frame instead of advancing the trace/recording.
        if (g_trace) {
            install_playback_frame(rdram, g_cursor);
        } else {
            write_guest_input(rdram, g_record_input);
            std::lock_guard analog_lock(g_analog_mutex);
            publish_analog_unlocked(g_record_input);
        }
        return;
    }

    if (g_trace) {
        g_cursor = g_frames_consumed.load(std::memory_order_acquire);
        install_playback_frame(rdram, g_cursor, !playback_owns_input());
    } else {
        stage_recording_frame(rdram);
    }
    g_frame_staged = true;
}

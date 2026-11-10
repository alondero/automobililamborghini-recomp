#include "lambo_analog_brake.h"
#include "lambo_analog_throttle.h"
#include "lambo_replay_runtime.h"
#include "lambo_driving_assists.h"
#include "lambo_driving_assists_runtime.h"
#include "lambo_input_gate.h"

#include <cmath>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

namespace {
void set_environment(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void clear_environment(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}
}

int playback_test(const std::string& mode) {
    using namespace lambo::replay_runtime;
    auto expect = [](bool value, const char* message) {
        if (!value) std::cerr << message << '\n';
        return value;
    };
    // Unconfigured hooks must be harmless, even without guest RAM.
    lambo_replay_dispatch_begin(nullptr);
    lambo_replay_dispatch_end(nullptr);
    lambo_replay_input_tick(nullptr);
    lambo_replay_state_loaded();
    lambo::replay::InputFrame output{};
    if (!expect(!should_exit() && !playback_owns_input() && !playback_frame(output),
                "unconfigured replay must leave input and exit alone")) return 1;
    if (mode == "invalid") {
        set_environment("LAMBO_INPUT_REPLAY", "unused");
        set_environment("LAMBO_INPUT_RECORD", "unused");
        return expect(!initialize_from_environment() && should_exit() && status().failed &&
                      terminal_reason() == "invalid_configuration",
                      "initialization failure must publish exit before configuration") ? 0 : 1;
    }
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("lambo-replay-fast-paths-" + std::to_string(nonce) + ".jsonl");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); }
    } cleanup{path};
    const std::vector<lambo::replay::InputFrame> frames{
        {0x8000, 12, -15, true, 30000, false, 0},
        {0x4000, -25, 30, false, 0, true, 20000},
        {0, 0, 0, false, 0, false, 0}};
    {
        lambo::replay::Recorder recorder(path);
        for (const auto& frame : frames) if (!recorder.observe(frame)) return 1;
        if (!recorder.finalize()) return 1;
    }
    set_environment("LAMBO_INPUT_RECORD", "");
    set_environment("LAMBO_INPUT_REPLAY", path.string());
    set_environment("LAMBO_INPUT_START_STATE", "8");
    set_environment("LAMBO_INPUT_START_DELAY", "0");
    set_environment("LAMBO_INPUT_EXIT_ON_END", mode == "stay" ? "0" : "1");
    if (!initialize_from_environment()) return 1;
    std::vector<std::uint8_t> ram(0x924000);
    const std::int16_t race = 8;
    std::memcpy(ram.data() + ((0x800CE6ACu ^ 2u) & 0xffffffu), &race, 2);
    lambo_replay_dispatch_end(ram.data()); // start eligibility
    for (std::size_t i = 0; i < frames.size(); ++i) {
        lambo_replay_input_tick(ram.data());
        if (!expect(playback_frame(output) && output == frames[i],
                    "every staged frame must retain its digital and analog input")) return 1;
        publish_physical_throttle(true, 0.9f);
        publish_physical_brake(true, 0.9f);
        if (mode == "load" && i == 1) lambo_replay_state_loaded();
        if (mode == "overwrite" && i == 1) ram[(0x800A39E2u ^ 3u) & 0xffffffu] = 42;
        lambo_replay_dispatch_begin(ram.data());
        lambo_replay_dispatch_end(ram.data());
        const auto state = status();
        if (i == 1 && (mode == "load" || mode == "overwrite")) {
            const auto reason = mode == "load" ? "state_loaded_during_replay" : "replay_input_overwritten";
            return expect(should_exit() && state.failed && !state.complete &&
                          state.frames_consumed == 1 && state.guest_frames_verified == 1 &&
                          terminal_reason() == reason,
                          "failure must preserve reason and stop before verifying another frame") ? 0 : 1;
        }
        if (!expect(!state.failed && state.frames_consumed == i + 1 &&
                    state.guest_frames_verified == i + 1,
                    "playback must verify and consume every frame in order")) return 1;
    }
    if (!expect(status().complete && should_exit() == (mode != "stay") &&
                terminal_reason() == "replay_complete", "completion must respect exit_on_end")) return 1;
    lambo_replay_input_tick(ram.data());
    lambo_replay_dispatch_begin(ram.data());
    lambo_replay_dispatch_end(ram.data());
    return expect(playback_frame(output) && output == lambo::replay::InputFrame{} &&
                  status().guest_frames_verified == frames.size(),
                  "completed playback must retain neutral ownership without consuming more frames") ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc > 1) return playback_test(argv[1]);
    lambo::replay_runtime::publish_physical_throttle(true, 0.5f);
    lambo::replay_runtime::publish_physical_brake(true, 0.25f);
    float throttle = 0.0f;
    float brake = 0.0f;
    const bool throttle_analog = lambo::analog_throttle::sample(0, throttle);
    const bool brake_analog = lambo::analog_brake::sample(0, brake);
    if (!throttle_analog || !brake_analog || std::abs(throttle - 0.5f) > 1.0f / 65535.0f ||
        std::abs(brake - 0.25f) > 1.0f / 65535.0f) {
        std::cerr << "runtime must publish both physical analog channels\n";
        return 1;
    }
    std::vector<std::uint8_t> ram(0x924000);
    auto half = [&](std::uint32_t address, std::int16_t value) {
        std::memcpy(ram.data() + ((address ^ 2u) & 0xffffffu), &value, 2);
    };
    auto pad = [&]() {
        std::uint16_t value;
        std::memcpy(&value, ram.data() + ((0x800A39E0u ^ 2u) & 0xffffffu), 2);
        return value;
    };
    auto prepare_pad = [&]() {
        half(0x800A39E0, 0);
        ram[(0x800A39E2u ^ 3u) & 0xffffffu] = 0;
    };
    auto tick_replay = [&]() {
        lambo_replay_input_tick(ram.data());
    };
    auto tick = [&]() {
        prepare_pad();
        lambo_driving_assists_tick(ram.data());
        tick_replay();
    };
    auto expect = [&](bool value, const char* message) {
        if (!value) std::cerr << message << '\n';
        return value;
    };
    lambo::replay_runtime::publish_physical_brake(false, 0);
    lambo::driving::publish({true, 50, true});
    half(0x800CE6AC, 8);
    half(0x800CE6B0, 3);
    prepare_pad();
    lambo::driving::set_replay_playback(true);
    lambo_driving_assists_tick(ram.data());
    if (!expect(pad() == 0 && !lambo::driving::racing(),
                "driving assists must yield when replay owns guest input")) return 1;
    lambo::driving::set_replay_playback(false);
    tick_replay();
    if (!expect(pad() == 0 && !lambo::driving::racing(),
                "replay hook alone must not apply driving assists or update race state")) return 1;
    tick();
    if (!expect(pad() == 0x8000 && ram[(0x800A39E2u ^ 3u) & 0xffffffu] == 50,
                "dedicated driving hook must apply assists before replay observes input")) return 1;
    for (auto [address, value] : {std::pair{0x800CE6ACu, 6}, {0x800CE6B0u, 2},
                                 {0x800CE808u, 1}, {0x800CE6B4u, 4}, {0x80902086u, 1}}) {
        half(address, value);
        tick();
        if (!expect(pad() == 0 && ram[(0x800A39E2u ^ 3u) & 0xffffffu] == 0 && !lambo::driving::racing(),
                    "guest menu/countdown/pause/demo/finish must reject stale assist snapshot")) return 1;
        half(address, address == 0x800CE6ACu ? 8 : address == 0x800CE6B0u ? 3 : 0);
    }
    lambo::replay_runtime::publish_physical_brake(true, .2f);
    tick();
    if (!expect(pad() == 0, "guest analog braking must suppress auto acceleration")) return 1;
    lambo::input_gate::set_ui_capture(true);
    tick();
    if (!expect(pad() == 0 && ram[(0x800A39E2u ^ 3u) & 0xffffffu] == 0,
                "overlay capture must suppress all assists")) return 1;

    // Arm an actual recorder and verify it observes the assisted pad after the
    // dedicated hook has run at the same guest input merge point.
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto record_path = std::filesystem::temp_directory_path() /
        ("lambo-driving-assists-" + std::to_string(nonce) + ".json");
    set_environment("LAMBO_INPUT_REPLAY", "");
    set_environment("LAMBO_INPUT_RECORD", record_path.string());
    set_environment("LAMBO_INPUT_START_STATE", "8");
    set_environment("LAMBO_INPUT_START_DELAY", "0");
    set_environment("LAMBO_INPUT_EXIT_ON_END", "1");
    if (!expect(lambo::replay_runtime::initialize_from_environment(),
                "recording test configuration must initialize")) return 1;

    lambo::input_gate::set_ui_capture(false);
    lambo::input_gate::publish_physical_snapshot(0);
    lambo::input_gate::publish_physical_snapshot(0);
    lambo::replay_runtime::publish_physical_throttle(false, 0);
    lambo::replay_runtime::publish_physical_brake(false, 0);
    lambo::driving::publish({true, 50, true});
    half(0x800CE6AC, 8);
    half(0x800CE6B0, 3);
    half(0x800CE6B4, 0);
    half(0x800CE808, 0);
    half(0x80902086, 0);
    lambo_replay_dispatch_end(ram.data()); // first eligible active-race tick
    prepare_pad();
    lambo_driving_assists_tick(ram.data());
    lambo_replay_input_tick(ram.data());
    lambo_replay_dispatch_begin(ram.data());
    lambo_replay_dispatch_end(ram.data());
    lambo::replay_runtime::finalize();

    auto recorded = lambo::replay::load_trace(record_path);
    lambo::replay::InputFrame recorded_frame{};
    if (!expect(bool(recorded) && recorded.trace->frame_at(0, recorded_frame) &&
                recorded_frame.buttons == 0x8000 && recorded_frame.stick_x == 50,
                "replay recording must capture final gyro and auto-accelerate pad state")) return 1;
    std::error_code cleanup_error;
    std::filesystem::remove(record_path, cleanup_error);
    clear_environment("LAMBO_INPUT_REPLAY");
    clear_environment("LAMBO_INPUT_RECORD");
    clear_environment("LAMBO_INPUT_START_STATE");
    clear_environment("LAMBO_INPUT_START_DELAY");
    clear_environment("LAMBO_INPUT_EXIT_ON_END");
    return 0;
}

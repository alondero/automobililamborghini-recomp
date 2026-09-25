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

int main() {
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
    std::vector<std::uint8_t> ram(8 * 1024 * 1024);
    auto half = [&](std::uint32_t address, std::int16_t value) {
        std::memcpy(ram.data() + ((address ^ 2u) & 0x7fffffu), &value, 2);
    };
    auto pad = [&]() {
        std::uint16_t value;
        std::memcpy(&value, ram.data() + ((0x800A39E0u ^ 2u) & 0x7fffffu), 2);
        return value;
    };
    auto prepare_pad = [&]() {
        half(0x800A39E0, 0);
        ram[(0x800A39E2u ^ 3u) & 0x7fffffu] = 0;
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
    if (!expect(pad() == 0x8000 && ram[(0x800A39E2u ^ 3u) & 0x7fffffu] == 50,
                "dedicated driving hook must apply assists before replay observes input")) return 1;
    for (auto [address, value] : {std::pair{0x800CE6ACu, 6}, {0x800CE6B0u, 2},
                                 {0x800CE808u, 1}, {0x800CE6B4u, 4}, {0x800A5F6Eu, 1}}) {
        half(address, value);
        tick();
        if (!expect(pad() == 0 && ram[(0x800A39E2u ^ 3u) & 0x7fffffu] == 0 && !lambo::driving::racing(),
                    "guest menu/countdown/pause/demo/finish must reject stale assist snapshot")) return 1;
        half(address, address == 0x800CE6ACu ? 8 : address == 0x800CE6B0u ? 3 : 0);
    }
    lambo::replay_runtime::publish_physical_brake(true, .2f);
    tick();
    if (!expect(pad() == 0, "guest analog braking must suppress auto acceleration")) return 1;
    lambo::input_gate::set_ui_capture(true);
    tick();
    if (!expect(pad() == 0 && ram[(0x800A39E2u ^ 3u) & 0x7fffffu] == 0,
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
    half(0x800A5F6E, 0);
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

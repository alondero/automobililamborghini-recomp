#include "lambo_analog_brake.h"
#include "lambo_analog_throttle.h"
#include "lambo_replay_runtime.h"
#include "lambo_driving_assists.h"
#include "lambo_input_gate.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
#include <cstring>

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
    auto tick = [&]() {
        half(0x800A39E0, 0);
        ram[(0x800A39E2u ^ 3u) & 0x7fffffu] = 0;
        lambo_replay_input_tick(ram.data());
    };
    auto expect = [&](bool value, const char* message) {
        if (!value) std::cerr << message << '\n';
        return value;
    };
    lambo::replay_runtime::publish_physical_brake(false, 0);
    lambo::driving::publish({true, 50, true});
    half(0x800CE6AC, 8);
    half(0x800CE6B0, 3);
    tick();
    if (!expect(pad() == 0x8000 && ram[(0x800A39E2u ^ 3u) & 0x7fffffu] == 50,
                "guest hook must apply assists before replay recording")) return 1;
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
    return 0;
}

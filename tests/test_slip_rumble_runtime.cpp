#include "lambo_slip_rumble.h"
#include "lambo_driving_assists.h"
#include "lambo_input_gate.h"
#include "lambo_vehicle.h"
#include "lambo_race_field.h"
#include "recomp.h"
#include <array>
#include <cstring>
#include <iostream>
#include <vector>

static bool enabled = true;
namespace lambo::config { bool slip_rumble() { return enabled; } }
extern "C" void lambo_slip_rumble_observe(std::uint8_t*);

int main() {
    std::vector<std::uint8_t> ram(0x1000000);
    auto* rdram = ram.data();
    auto address = [](std::uint32_t value) -> gpr { return static_cast<std::int32_t>(value); };
    auto half = [&](std::uint32_t a, int value) { MEM_H(0, address(a)) = value; };
    auto word = [&](std::uint32_t a, std::uint32_t value) { MEM_W(0, address(a)) = value; };
    auto floating = [&](std::uint32_t a, float value) {
        std::uint32_t bits; std::memcpy(&bits, &value, 4); word(a, bits);
    };
    int failures = 0;
    auto expect = [&](bool value, const char* message) {
        if (!value) { std::cerr << message << '\n'; ++failures; }
    };
    half(0x800CE6AC, 8); half(0x800CE6B0, 3);
    half(LAMBO_GUEST_CURRENT_VEHICLE_ADDR, 5);
    const auto vehicle = LAMBO_VEHICLE_BASE + 5 * sizeof(LamboVehicleRecord);
    half(vehicle + 0x0e, 1);
    floating(vehicle + 0x40, 1); floating(vehicle + 0x74, 1);
    word(vehicle + 0x90, 40);
    auto& slip = lambo::rumble::slip();
    auto output = [&] { return slip.output(false, true, true, false, lambo::rumble::now_ms()); };
    output(); // Main thread permits new publication.
    const auto before = ram;
    lambo_slip_rumble_observe(rdram);
    expect(output(), "word-swapped guest sample reaches the mailbox");
    expect(ram == before, "observer is read-only");
    half(vehicle + 0x0e, 2); lambo_slip_rumble_observe(rdram);
    expect(!output(), "loss of the published owner clears immediately");
    half(vehicle + 0x0e, 1); lambo_slip_rumble_observe(rdram);
    expect(output(), "ownership return starts a new pulse");
    half(LAMBO_GUEST_CURRENT_VEHICLE_ADDR, 6);
    half(LAMBO_VEHICLE_BASE + 6 * sizeof(LamboVehicleRecord) + 0x0e, 2);
    lambo_slip_rumble_observe(rdram);
    expect(output(), "another player's update does not cancel port zero");
    half(LAMBO_GUEST_CURRENT_VEHICLE_ADDR, 5);
    for (auto [a, blocked, normal] : {
             std::array<std::uint32_t, 3>{0x800CE6AC, 7, 8},
             {0x800CE6B0, 2, 3}, {0x800CE808, 1, 0}, {0x800CE6B4, 4, 0},
             {LAMBO_FIELD_AI + 0x86, 1, 0},
             {LAMBO_GUEST_CURRENT_VEHICLE_ADDR, 64, 5},
             {LAMBO_GUEST_CURRENT_VEHICLE_ADDR, 0xffff, 5}}) {
        half(a, blocked); lambo_slip_rumble_observe(rdram);
        expect(!output(), "invalid lifecycle/owner clears publication");
        half(a, normal); lambo_slip_rumble_observe(rdram);
        expect(output(), "valid lifecycle resumes at phase zero");
    }
    enabled = false; lambo_slip_rumble_observe(rdram);
    expect(!output(), "disabled setting clears publication"); enabled = true;
    lambo::driving::set_replay_playback(true); lambo_slip_rumble_observe(rdram);
    expect(!output(), "replay excluded"); lambo::driving::set_replay_playback(false);
    lambo::input_gate::set_ui_capture(true); lambo_slip_rumble_observe(rdram);
    expect(!output(), "guest suppression excluded");
    lambo::input_gate::set_ui_capture(false);
    slip.clear(); half(vehicle + 0x0e, 2); lambo_slip_rumble_observe(rdram);
    expect(!output(), "other human channel cannot publish to port zero");
    lambo_slip_rumble_observe(nullptr);
    expect(!output(), "null memory rejected");
    return failures ? 1 : 0;
}

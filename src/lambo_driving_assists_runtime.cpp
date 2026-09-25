#include "lambo_driving_assists_runtime.h"

#include "lambo_analog_brake.h"
#include "lambo_driving_assists.h"
#include "lambo_input_gate.h"
#include "recomp.h"

#include <cstdint>

namespace {
constexpr gpr kGameStateAddress = static_cast<gpr>(static_cast<std::int32_t>(0x800CE6ACu));
constexpr gpr kRacePhaseAddress = static_cast<gpr>(static_cast<std::int32_t>(0x800CE6B0u));
constexpr gpr kGameModeAddress = static_cast<gpr>(static_cast<std::int32_t>(0x800CE6B4u));
constexpr gpr kPauseStateAddress = static_cast<gpr>(static_cast<std::int32_t>(0x800CE808u));
constexpr gpr kPlayerOneFinishedAddress = static_cast<gpr>(static_cast<std::int32_t>(0x800A5F6Eu));
constexpr gpr kPortZeroPadAddress = static_cast<gpr>(static_cast<std::int32_t>(0x800A39E0u));
}

extern "C" void lambo_driving_assists_tick(std::uint8_t* rdram) noexcept {
    if (rdram == nullptr) return;

    // These are game-thread-only, word-swapped USA ROM values. State 8 is the
    // race dispatcher; phase 3 is active driving, pause 0 excludes menus, and
    // mode 4 is attract playback. See docs/gyro-steering-research.md.
    const bool driving = lambo::driving::race_allows_assists(
        MEM_H(0, kGameStateAddress), MEM_H(0, kRacePhaseAddress),
        MEM_H(0, kPauseStateAddress), MEM_H(0, kGameModeAddress));
    const bool player_finished = MEM_H(0, kPlayerOneFinishedAddress) != 0;
    const bool allowed = driving && !player_finished &&
        !lambo::input_gate::guest_input_suppressed() && !lambo::driving::replay_playback();
    lambo::driving::set_racing(allowed);
    if (!allowed) return;

    const auto demand = lambo::driving::sample();
    if (!demand.gyro_valid && !demand.auto_accelerate) return;

    std::uint16_t buttons = static_cast<std::uint16_t>(MEM_HU(0, kPortZeroPadAddress));
    std::int8_t stick_x = static_cast<std::int8_t>(MEM_B(2, kPortZeroPadAddress));
    float brake = 0.0f;
    const bool analog_braking = demand.auto_accelerate &&
        lambo::analog_brake::sample(0, brake) && brake > 0.0f;
    lambo::driving::apply(demand, true, analog_braking, buttons, stick_x);

    MEM_H(0, kPortZeroPadAddress) = buttons;
    MEM_B(2, kPortZeroPadAddress) = stick_x;
}

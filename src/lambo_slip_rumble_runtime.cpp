#include "lambo_slip_rumble.h"
#include "lambo_config.h"
#include "lambo_driving_assists.h"
#include "lambo_input_gate.h"
#include "lambo_vehicle.h"
#include "lambo_race_field.h"
#include "recomp.h"
#include <cstring>

namespace {
constexpr gpr addr(std::uint32_t value) { return static_cast<std::int32_t>(value); }
}

extern "C" void lambo_slip_rumble_observe(std::uint8_t* rdram) {
    // Synchronous post-physics observer in the human branch of the USA guest
    // updater. All halfwords/float32 reads use word-swapped RDRAM on its owner
    // thread. Unknown vectors retain offset names until trace validation.
    const bool allowed = lambo::config::slip_rumble() && rdram &&
        lambo::driving::race_allows_assists(
            MEM_H(0, addr(0x800CE6AC)), MEM_H(0, addr(0x800CE6B0)),
            MEM_H(0, addr(0x800CE808)), MEM_H(0, addr(0x800CE6B4))) &&
        MEM_H(0, addr(LAMBO_FIELD_AI + 0x86)) == 0 &&
        !lambo::input_gate::guest_input_suppressed() &&
        !lambo::driving::replay_playback();
    if (!allowed) { lambo_slip_rumble_reset(); return; }
    const int index = MEM_H(0, addr(LAMBO_GUEST_CURRENT_VEHICLE_ADDR));
    if (index < 0 || index >= LAMBO_VEHICLE_CAPACITY) {
        lambo_slip_rumble_reset(); return;
    }
    const gpr vehicle = addr(LAMBO_VEHICLE_BASE + index * sizeof(LamboVehicleRecord));
    if (MEM_H(0x0E, vehicle) != 1) {
        // Other players must not cancel this player, but losing the published
        // vehicle's ownership must clear its old pulse immediately.
        lambo::rumble::slip().release_vehicle(index);
        return;
    }
    const auto field = [&](int offset) {
        const std::uint32_t bits = MEM_W(offset, vehicle);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    };
    lambo::rumble::slip().publish({field(0x40), field(0x44), field(0x6C),
        field(0x74), static_cast<std::int32_t>(MEM_W(0x90, vehicle)), index},
        lambo::rumble::now_ms());
}

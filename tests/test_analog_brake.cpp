#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

#include "lambo_analog_brake.h"

namespace {

constexpr uint32_t kCurrentVehicleAddr = 0x80098398u;
constexpr uint32_t kCurrentChannelAddr = 0x800CE6AAu;
constexpr uint32_t kSelectedPadPtrAddr = 0x800A39DCu;
constexpr uint32_t kPadBase = 0x800A39E0u;
constexpr uint32_t kVehicleBase = 0x800B69A8u;
constexpr uint32_t kVehicleStride = 0x10Cu;
constexpr uint32_t kBrakeOffset = 0xA0u;
constexpr uint32_t kLatchOffset = 0xACu;

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

int16_t& halfword(std::vector<uint8_t>& rdram, uint32_t address) {
    return *reinterpret_cast<int16_t*>(&rdram[(address ^ 2u) & 0x7FFFFFu]);
}

int32_t& word(std::vector<uint8_t>& rdram, uint32_t address) {
    return *reinterpret_cast<int32_t*>(&rdram[address & 0x7FFFFFu]);
}

uint32_t latch_address(int vehicle) {
    return kVehicleBase + static_cast<uint32_t>(vehicle) * kVehicleStride + kLatchOffset;
}

float read_brake(const std::vector<uint8_t>& rdram, int vehicle) {
    const uint32_t address =
        kVehicleBase + static_cast<uint32_t>(vehicle) * kVehicleStride + kBrakeOffset;
    uint32_t bits = static_cast<uint32_t>(
        word(const_cast<std::vector<uint8_t>&>(rdram), address));
    float out;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

void write_brake(std::vector<uint8_t>& rdram, int vehicle, float value) {
    const uint32_t address =
        kVehicleBase + static_cast<uint32_t>(vehicle) * kVehicleStride + kBrakeOffset;
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    word(rdram, address) = static_cast<int32_t>(bits);
}

void set_context(std::vector<uint8_t>& rdram, int vehicle, int channel) {
    halfword(rdram, kCurrentVehicleAddr) = static_cast<int16_t>(vehicle);
    halfword(rdram, kCurrentChannelAddr) = static_cast<int16_t>(channel);
    word(rdram, kSelectedPadPtrAddr) = static_cast<int32_t>(kPadBase +
        static_cast<uint32_t>(channel - 1) * 6u);
    halfword(rdram, kPadBase + static_cast<uint32_t>(channel - 1) * 6u) = 0;
}

// Mirrors the stock ROM behaviour around the apply hook: while B is held the
// demand ramps +1 toward 16 and the latch bit sets; on release the demand snaps
// to 0 and the latch clears on the next pass.
void race_step(std::vector<uint8_t>& rdram, int vehicle, bool digital_pressed) {
    float demand = read_brake(rdram, vehicle);
    if (digital_pressed) {
        demand = std::min(demand + 1.0f, 16.0f);
        halfword(rdram, latch_address(vehicle)) |= 0x0001;
    } else {
        demand = 0.0f;
        halfword(rdram, latch_address(vehicle)) &= static_cast<int16_t>(~0x0001);
    }
    write_brake(rdram, vehicle, demand);
    lambo_analog_brake_apply(rdram.data());
}

} // namespace

int main() {
    using namespace lambo::analog_brake;

    expect(scale_to_guest(0) == 0.0f, "zero scales to zero");
    expect(std::abs(scale_to_guest(UINT16_MAX / 4) - 4.0f) < 0.01f,
           "quarter input scales to a quarter of 16");
    expect(std::abs(scale_to_guest(UINT16_MAX / 2) - 8.0f) < 0.01f,
           "half input scales to half of 16");
    expect(scale_to_guest(UINT16_MAX) == 16.0f, "full input reaches the stock ceiling");

    std::vector<uint8_t> rdram(8 * 1024 * 1024);
    set_context(rdram, 1, 1);
    write_brake(rdram, 1, 5.0f);

    publish(0, false, 1.0f);
    race_step(rdram, 1, false);
    expect(read_brake(rdram, 1) == 0.0f,
           "digital mode leaves the ROM's released snap-to-zero untouched");

    publish(0, true, 0.5f);
    write_brake(rdram, 1, 0.0f);
    race_step(rdram, 1, false);
    expect(read_brake(rdram, 1) == 1.0f,
           "analog mode preserves the ROM's one-unit rising ramp");
    for (int frame = 0; frame < 7; ++frame) race_step(rdram, 1, false);
    expect(std::abs(read_brake(rdram, 1) - 8.0f) < 0.01f,
           "analog mode settles at the normalized demand without overshooting");
    expect((halfword(rdram, latch_address(1)) & 0x0001) != 0,
           "analog braking latches the physics gate like held digital B");

    halfword(rdram, kCurrentVehicleAddr) = -1;
    write_brake(rdram, 1, 3.0f);
    lambo_analog_brake_apply(rdram.data());
    expect(read_brake(rdram, 1) == 3.0f,
           "inactive ROM vehicle sentinel leaves guest memory untouched");
    halfword(rdram, kCurrentVehicleAddr) = 1;

    halfword(rdram, kPadBase) = static_cast<int16_t>(0x4000u);
    for (int frame = 0; frame < 20; ++frame) race_step(rdram, 1, true);
    expect(read_brake(rdram, 1) == 16.0f,
           "stock B remains a full-brake fallback in analog mode");
    halfword(rdram, kPadBase) = 0;

    set_context(rdram, 2, 2);
    write_brake(rdram, 2, 12.0f);
    publish(0, true, 0.25f);
    publish(1, true, 1.0f);
    for (int frame = 0; frame < 6; ++frame) race_step(rdram, 2, false);
    // Channel 2 -> port 1 (target full brake). The native pedal starts its own
    // ramp from neutral regardless of the stale guest value, and port 0's
    // quarter sample must not be consulted.
    expect(read_brake(rdram, 2) == 6.0f,
           "one-based guest channel selects the matching zero-based native port");
    expect((halfword(rdram, latch_address(2)) & 0x0001) != 0,
           "the physics gate latches while the analog demand is active");

    // Release: the stock released path snaps to zero, so a falling target snaps
    // immediately instead of inventing a ramp the ROM does not have.
    publish(1, true, 0.0f);
    race_step(rdram, 2, false);
    expect(read_brake(rdram, 2) == 0.0f &&
           (halfword(rdram, latch_address(2)) & 0x0001) == 0,
           "analog release snaps to neutral like the stock release handler");
    // A partial reduction also lands on target within one update.
    publish(1, true, 0.5f);
    write_brake(rdram, 2, 0.0f);
    for (int frame = 0; frame < 8; ++frame) race_step(rdram, 2, false);
    publish(1, true, 0.25f);
    race_step(rdram, 2, false);
    expect(std::abs(read_brake(rdram, 2) - 4.0f) < 0.01f,
           "easing off mid-brake snaps to the lower normalized target");

    float sampled = -1.0f;
    publish(3, true, std::numeric_limits<float>::quiet_NaN());
    expect(sample(3, sampled) && sampled == 0.0f, "non-finite host samples clamp to neutral");
    publish(3, false, 1.0f);
    expect(!sample(3, sampled), "digital snapshots are reported as inactive");

    return failures == 0 ? 0 : 1;
}

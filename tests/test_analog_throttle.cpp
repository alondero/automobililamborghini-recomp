#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include "lambo_analog_throttle.h"
#include "lambo_vehicle.h"

namespace {

constexpr uint32_t kCurrentChannelAddr = 0x800CE6AAu;
constexpr uint32_t kSelectedPadPtrAddr = 0x800A39DCu;
constexpr uint32_t kPadBase = 0x800A39E0u;
constexpr uint32_t kLimitBase = 0x800A5F2Cu;
constexpr uint32_t kLimitStride = 0x84u;

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

uint32_t demand_address(int vehicle) {
    return LAMBO_VEHICLE_BASE + static_cast<uint32_t>(vehicle) * sizeof(LamboVehicleRecord) +
           offsetof(LamboVehicleRecord, throttle_demand);
}

void set_context(std::vector<uint8_t>& rdram, int vehicle, int channel, int limit) {
    halfword(rdram, LAMBO_GUEST_CURRENT_VEHICLE_ADDR) = static_cast<int16_t>(vehicle);
    halfword(rdram, kCurrentChannelAddr) = static_cast<int16_t>(channel);
    word(rdram, kSelectedPadPtrAddr) = static_cast<int32_t>(kPadBase +
        static_cast<uint32_t>(channel - 1) * 6u);
    halfword(rdram, kPadBase + static_cast<uint32_t>(channel - 1) * 6u) = 0;
    halfword(rdram, kLimitBase + static_cast<uint32_t>(channel) * kLimitStride) =
        static_cast<int16_t>(limit);
}

void race_step(std::vector<uint8_t>& rdram, int vehicle, int limit, bool digital_pressed) {
    lambo_analog_throttle_begin(rdram.data());
    int demand = halfword(rdram, demand_address(vehicle));
    demand = digital_pressed ? std::min(demand + 10, limit) : std::max(demand - 10, 0);
    halfword(rdram, demand_address(vehicle)) = static_cast<int16_t>(demand);
    lambo_analog_throttle_apply(rdram.data());
}

} // namespace

int main() {
    using namespace lambo::analog_throttle;

    expect(scale_to_guest(0, 100) == 0, "zero scales to zero");
    expect(scale_to_guest(16384, 100) == 25, "quarter input scales to 25 percent");
    expect(scale_to_guest(32768, 100) == 50, "half input scales to 50 percent");
    expect(scale_to_guest(49151, 100) == 75, "three-quarter input scales to 75 percent");
    expect(scale_to_guest(UINT16_MAX, 100) == 100, "full input reaches the dynamic limit");
    expect(scale_to_guest(UINT16_MAX, -1) == 0, "negative guest limits are neutral");

    std::vector<uint8_t> rdram(8 * 1024 * 1024);
    set_context(rdram, 1, 1, 100);
    halfword(rdram, demand_address(1)) = 37;

    publish(0, false, 1.0f);
    race_step(rdram, 1, 100, false);
    expect(halfword(rdram, demand_address(1)) == 27,
           "digital mode leaves the ROM's released ramp step untouched");

    publish(0, true, 0.25f);
    halfword(rdram, demand_address(1)) = 0;
    race_step(rdram, 1, 100, false);
    expect(halfword(rdram, demand_address(1)) == 10,
           "analog mode preserves the ROM's ten-unit rising ramp");
    race_step(rdram, 1, 100, false);
    race_step(rdram, 1, 100, false);
    expect(halfword(rdram, demand_address(1)) == 25,
           "analog mode settles at the normalized demand");

    halfword(rdram, LAMBO_GUEST_CURRENT_VEHICLE_ADDR) = -1;
    halfword(rdram, demand_address(1)) = 26;
    lambo_analog_throttle_begin(rdram.data());
    lambo_analog_throttle_apply(rdram.data());
    expect(halfword(rdram, demand_address(1)) == 26,
           "inactive ROM vehicle sentinel leaves guest memory untouched");
    halfword(rdram, LAMBO_GUEST_CURRENT_VEHICLE_ADDR) = 1;

    halfword(rdram, kPadBase) = static_cast<int16_t>(0x8000u);
    for (int frame = 0; frame < 8; ++frame) race_step(rdram, 1, 100, true);
    expect(halfword(rdram, demand_address(1)) == 100,
           "stock A remains a full-throttle fallback in analog mode");
    halfword(rdram, kPadBase) = 0;

    set_context(rdram, 2, 2, 80);
    halfword(rdram, demand_address(2)) = 11;
    publish(0, true, 1.0f);
    publish(1, true, 0.75f);
    for (int frame = 0; frame < 5; ++frame) race_step(rdram, 2, 80, false);
    expect(halfword(rdram, demand_address(2)) == 60,
           "one-based guest channel selects the matching zero-based native port");

    disconnect(1, true);
    for (int frame = 0; frame < 6; ++frame) race_step(rdram, 2, 80, false);
    expect(halfword(rdram, demand_address(2)) == 0,
           "disconnect publishes a neutral analog value");

    float sampled = -1.0f;
    publish(3, true, std::numeric_limits<float>::quiet_NaN());
    expect(sample(3, sampled) && sampled == 0.0f, "non-finite host samples clamp to neutral");
    publish(3, false, 1.0f);
    expect(!sample(3, sampled), "digital snapshots are reported as inactive");

    return failures == 0 ? 0 : 1;
}

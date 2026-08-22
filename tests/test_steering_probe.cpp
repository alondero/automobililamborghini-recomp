#include <cstdint>
#include <iostream>
#include <vector>

#include "lambo_steering_probe.h"

namespace {

constexpr uint32_t kCurrentVehicleAddr = 0x80098398u;
constexpr uint32_t kSelectedPadPtrAddr = 0x800A39DCu;
constexpr uint32_t kPadBase = 0x800A39E0u;
constexpr uint32_t kSteeringWorkAddr = 0x80098694u;
constexpr uint32_t kCurveSelectorAddr = 0x800CE79Cu;
constexpr uint32_t kVehicleBase = 0x800B69A8u;
constexpr uint32_t kVehicleStride = 0x10Cu;
constexpr uint32_t kSpeedOffset = 0x90u;
constexpr uint32_t kSteerDemandOffset = 0xB2u;

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

uint8_t& byte(std::vector<uint8_t>& rdram, uint32_t address) {
    return rdram[(address ^ 3u) & 0x7FFFFFu];
}

uint32_t demand_address(int vehicle) {
    return kVehicleBase + static_cast<uint32_t>(vehicle) * kVehicleStride +
           kSteerDemandOffset;
}

void set_context(std::vector<uint8_t>& rdram, int vehicle, int selector) {
    halfword(rdram, kCurrentVehicleAddr) = static_cast<int16_t>(vehicle);
    word(rdram, kSelectedPadPtrAddr) = static_cast<int32_t>(kPadBase);
    halfword(rdram, kCurveSelectorAddr) = static_cast<int16_t>(selector);
}

} // namespace

int main() {
    using lambo::steering_probe::last_sample;

    std::vector<uint8_t> rdram(8 * 1024 * 1024);
    set_context(rdram, 1, 0);
    lambo::steering_probe::set_enabled(true);

    byte(rdram, kPadBase + 2u) = 53;
    halfword(rdram, kSteeringWorkAddr) = 53;
    lambo_steering_probe_capture_raw(rdram.data());
    halfword(rdram, kSteeringWorkAddr) = 49;
    lambo_steering_probe_capture_trim(rdram.data(), 1);
    halfword(rdram, kSteeringWorkAddr) = 37;
    lambo_steering_probe_capture_curve(rdram.data());
    halfword(rdram, demand_address(1)) = 37;
    word(rdram, kVehicleBase + kVehicleStride + kSpeedOffset) = 123;
    lambo_steering_probe_capture_demand(rdram.data());

    auto sample = last_sample();
    expect(sample.complete, "all four stages produce a complete sample");
    expect(sample.frame == 1, "the first complete steering sample is frame one");
    expect(sample.vehicle == 1, "sample records the current vehicle");
    expect(sample.raw == 53, "raw stage reads the signed pad stick byte");
    expect(sample.trimmed == 49, "positive trim stage preserves its sign");
    expect(sample.curved == 37, "curve stage records the ROM lookup result");
    expect(sample.demand == 37, "demand stage records vehicle offset 0xB2");
    expect(sample.speed == 123, "sample includes vehicle speed as trace context");
    expect(sample.selector == 0 && sample.curve == 'B',
           "zero selector identifies response curve B");

    set_context(rdram, 2, 3);
    byte(rdram, kPadBase + 2u) = static_cast<uint8_t>(-53);
    halfword(rdram, kSteeringWorkAddr) = -53;
    lambo_steering_probe_capture_raw(rdram.data());
    halfword(rdram, kSteeringWorkAddr) = 49;
    lambo_steering_probe_capture_trim(rdram.data(), -1);
    halfword(rdram, kSteeringWorkAddr) = -42;
    lambo_steering_probe_capture_curve(rdram.data());
    halfword(rdram, demand_address(2)) = -42;
    lambo_steering_probe_capture_demand(rdram.data());

    sample = last_sample();
    expect(sample.frame == 2, "complete samples increment the frame number");
    expect(sample.raw == -53, "raw stage sign-extends negative stick input");
    expect(sample.trimmed == -49, "negative trim stage restores its sign");
    expect(sample.curved == -42 && sample.demand == -42,
           "negative curve and demand remain signed");
    expect(sample.selector == 3 && sample.curve == 'A',
           "nonzero selector identifies response curve A");

    set_context(rdram, 1, 0);
    byte(rdram, kPadBase + 2u) = 3;
    halfword(rdram, kSteeringWorkAddr) = 3;
    lambo_steering_probe_capture_raw(rdram.data());
    halfword(rdram, kSteeringWorkAddr) = 0;
    lambo_steering_probe_capture_curve(rdram.data());
    halfword(rdram, demand_address(1)) = 0;
    lambo_steering_probe_capture_demand(rdram.data());

    sample = last_sample();
    expect(sample.trimmed == 0 && sample.curved == 0 && sample.demand == 0,
           "the deadzone path records zero without passing a curve lookup");

    byte(rdram, kPadBase + 2u) = 40;
    halfword(rdram, kSteeringWorkAddr) = 40;
    lambo_steering_probe_capture_raw(rdram.data());
    byte(rdram, kPadBase + 2u) = 41;
    halfword(rdram, kSteeringWorkAddr) = 41;
    lambo_steering_probe_capture_raw(rdram.data());
    halfword(rdram, kSteeringWorkAddr) = 37;
    lambo_steering_probe_capture_trim(rdram.data(), 1);
    halfword(rdram, kSteeringWorkAddr) = 26;
    lambo_steering_probe_capture_curve(rdram.data());
    halfword(rdram, demand_address(1)) = 26;
    lambo_steering_probe_capture_demand(rdram.data());
    expect(last_sample().frame == 5,
           "an incomplete hook chain leaves a detectable gap in frame numbering");

    lambo::steering_probe::set_enabled(false);
    byte(rdram, kPadBase + 2u) = 40;
    lambo_steering_probe_capture_raw(rdram.data());
    expect(last_sample().frame == 5, "disabled probe ignores later hook calls");

    return failures == 0 ? 0 : 1;
}

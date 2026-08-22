#include "lambo_steering_probe.h"

#include <atomic>
#include <cstdio>

#include "recomp.h"

namespace {

constexpr gpr kCurrentVehicleAddr = (gpr)(int32_t)0x80098398u;
constexpr gpr kSelectedPadPtrAddr = (gpr)(int32_t)0x800A39DCu;
constexpr gpr kSteeringWorkAddr = (gpr)(int32_t)0x80098694u;
constexpr gpr kCurveSelectorAddr = (gpr)(int32_t)0x800CE79Cu;
constexpr uint32_t kVehicleBase = 0x800B69A8u;
constexpr uint32_t kVehicleStride = 0x10Cu;
constexpr uint32_t kSpeedOffset = 0x90u;
constexpr uint32_t kSteerDemandOffset = 0xB2u;

std::atomic<bool> g_enabled{false};
lambo::steering_probe::Sample g_sample;
bool g_raw_valid = false;
bool g_trim_valid = false;
bool g_curve_valid = false;

bool enabled() {
    return g_enabled.load(std::memory_order_acquire);
}

int current_vehicle(uint8_t* rdram) {
    return static_cast<int16_t>(MEM_H(0, kCurrentVehicleAddr));
}

} // namespace

namespace lambo::steering_probe {

void set_enabled(bool enabled_value) {
    if (enabled_value) {
        g_sample = {};
        g_sample.vehicle = -1;
        g_raw_valid = false;
        g_trim_valid = false;
        g_curve_valid = false;
    }
    g_enabled.store(enabled_value, std::memory_order_release);
}

Sample last_sample() {
    return g_sample;
}

} // namespace lambo::steering_probe

extern "C" void lambo_steering_probe_capture_raw(uint8_t* rdram) {
    if (!enabled()) return;

    const uint32_t selected_pad = static_cast<uint32_t>(MEM_W(0, kSelectedPadPtrAddr));
    ++g_sample.frame;
    g_sample.vehicle = current_vehicle(rdram);
    g_sample.raw = static_cast<int8_t>(MEM_B(2, (gpr)(int32_t)selected_pad));
    g_sample.trimmed = 0;
    g_sample.curved = 0;
    g_sample.demand = 0;
    g_sample.speed = 0;
    g_sample.selector = static_cast<int16_t>(MEM_H(0, kCurveSelectorAddr));
    g_sample.curve = g_sample.selector == 0 ? 'B' : 'A';
    g_sample.complete = false;
    g_raw_valid = true;
    g_trim_valid = false;
    g_curve_valid = false;
}

extern "C" void lambo_steering_probe_capture_trim(uint8_t* rdram, int sign) {
    if (!enabled() || !g_raw_valid) return;
    const int magnitude = static_cast<int16_t>(MEM_H(0, kSteeringWorkAddr));
    g_sample.trimmed = sign < 0 ? -magnitude : magnitude;
    g_trim_valid = true;
}

extern "C" void lambo_steering_probe_capture_curve(uint8_t* rdram) {
    if (!enabled() || !g_raw_valid) return;
    if (!g_trim_valid) g_sample.trimmed = 0;
    g_sample.curved = static_cast<int16_t>(MEM_H(0, kSteeringWorkAddr));
    g_curve_valid = true;
}

extern "C" void lambo_steering_probe_capture_demand(uint8_t* rdram) {
    if (!enabled() || !g_raw_valid || !g_curve_valid || g_sample.vehicle < 0) return;

    const gpr demand_addr = (gpr)(int32_t)(kVehicleBase +
        static_cast<uint32_t>(g_sample.vehicle) * kVehicleStride + kSteerDemandOffset);
    const gpr speed_addr = (gpr)(int32_t)(kVehicleBase +
        static_cast<uint32_t>(g_sample.vehicle) * kVehicleStride + kSpeedOffset);
    g_sample.demand = static_cast<int16_t>(MEM_H(0, demand_addr));
    g_sample.speed = static_cast<int32_t>(MEM_W(0, speed_addr));
    g_sample.complete = true;
    std::fprintf(stderr,
        "[probe] steering chain: frame=%u vehicle=%d raw=%d trimmed=%d curved=%d "
        "demand=%d speed=%d selector=%d curve=%c\n",
        g_sample.frame, g_sample.vehicle, g_sample.raw, g_sample.trimmed,
        g_sample.curved, g_sample.demand, g_sample.speed, g_sample.selector,
        g_sample.curve);
    g_raw_valid = false;
    g_trim_valid = false;
    g_curve_valid = false;
}

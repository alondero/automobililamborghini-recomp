#include "lambo_analog_throttle.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>

#include "recomp.h"
#include "lambo_log.h"

namespace {

constexpr uint32_t kAnalogEnabled = 0x80000000u;
constexpr uint32_t kValueMask = 0x0000FFFFu;
constexpr gpr kCurrentVehicleAddr = (gpr)(int32_t)0x80098398u;
constexpr gpr kCurrentChannelAddr = (gpr)(int32_t)0x800CE6AAu;
constexpr gpr kSelectedPadPtrAddr = (gpr)(int32_t)0x800A39DCu;
constexpr uint32_t kVehicleBase = 0x800B69A8u;
constexpr uint32_t kVehicleStride = 0x10Cu;
constexpr uint32_t kSpeedOffset = 0x90u;
constexpr uint32_t kThrottleOffset = 0xAAu;
constexpr uint32_t kLimitBase = 0x800A5F2Cu;
constexpr uint32_t kLimitStride = 0x84u;

std::array<std::atomic<uint32_t>, lambo::analog_throttle::kPortCount> g_ports{};
std::atomic<bool> g_probe_enabled{false};
std::array<std::atomic<int>, lambo::analog_throttle::kPortCount> g_probe_last{
    std::atomic<int>{-1}, std::atomic<int>{-1}, std::atomic<int>{-1}, std::atomic<int>{-1}};
std::array<std::atomic<unsigned>, lambo::analog_throttle::kPortCount> g_probe_frames{};
std::array<int16_t, lambo::analog_throttle::kPortCount> g_previous_demand{};
std::array<bool, lambo::analog_throttle::kPortCount> g_previous_valid{};

uint16_t quantize(float value) {
    if (!std::isfinite(value) || value <= 0.0f) return 0;
    if (value >= 1.0f) return UINT16_MAX;
    return static_cast<uint16_t>(std::lround(value * static_cast<float>(UINT16_MAX)));
}

} // namespace

namespace lambo::analog_throttle {

void publish(unsigned port, bool analog_mode, float effective_value) {
    if (port >= kPortCount) return;
    const uint32_t packed = analog_mode
        ? kAnalogEnabled | static_cast<uint32_t>(quantize(effective_value))
        : 0u;
    g_ports[port].store(packed, std::memory_order_release);
}

void disconnect(unsigned port, bool analog_mode) {
    publish(port, analog_mode, 0.0f);
}

void set_probe(bool enabled) {
    for (auto& previous : g_probe_last) previous.store(-1, std::memory_order_relaxed);
    for (auto& frame : g_probe_frames) frame.store(0, std::memory_order_relaxed);
    g_probe_enabled.store(enabled, std::memory_order_release);
}

bool sample(unsigned port, float& effective_value) {
    effective_value = 0.0f;
    if (port >= kPortCount) return false;
    const uint32_t packed = g_ports[port].load(std::memory_order_acquire);
    if ((packed & kAnalogEnabled) == 0) return false;
    effective_value = static_cast<float>(packed & kValueMask) /
                      static_cast<float>(UINT16_MAX);
    return true;
}

int16_t scale_to_guest(uint16_t normalized, int16_t limit) {
    if (limit <= 0) return 0;
    const uint32_t scaled = static_cast<uint32_t>(normalized) *
                            static_cast<uint32_t>(limit);
    return static_cast<int16_t>((scaled + UINT16_MAX / 2u) / UINT16_MAX);
}

} // namespace lambo::analog_throttle

extern "C" void lambo_analog_throttle_begin(uint8_t* rdram) {
    const int channel = static_cast<int16_t>(MEM_H(0, kCurrentChannelAddr));
    if (channel < 1 || channel > static_cast<int>(lambo::analog_throttle::kPortCount)) return;
    const unsigned port = static_cast<unsigned>(channel - 1);
    const uint32_t packed = g_ports[port].load(std::memory_order_acquire);
    if ((packed & kAnalogEnabled) == 0 &&
        !g_probe_enabled.load(std::memory_order_acquire)) {
        g_previous_valid[port] = false;
        return;
    }
    const int vehicle = static_cast<int16_t>(MEM_H(0, kCurrentVehicleAddr));
    if (vehicle < 0) {
        g_previous_valid[port] = false;
        return;
    }
    const gpr demand_addr = (gpr)(int32_t)(kVehicleBase +
        static_cast<uint32_t>(vehicle) * kVehicleStride + kThrottleOffset);
    g_previous_demand[port] = static_cast<int16_t>(MEM_H(0, demand_addr));
    g_previous_valid[port] = true;
}

extern "C" void lambo_analog_throttle_apply(uint8_t* rdram) {
    const int channel = static_cast<int16_t>(MEM_H(0, kCurrentChannelAddr));
    if (channel < 1 || channel > static_cast<int>(lambo::analog_throttle::kPortCount)) return;

    const unsigned port = static_cast<unsigned>(channel - 1);
    const uint32_t packed = g_ports[port].load(std::memory_order_acquire);
    const bool analog_mode = (packed & kAnalogEnabled) != 0;
    const bool probing = g_probe_enabled.load(std::memory_order_acquire);
    if (!analog_mode && !probing) return;

    const int vehicle = static_cast<int16_t>(MEM_H(0, kCurrentVehicleAddr));
    // -1 is the ROM's inactive-vehicle sentinel. Other indices follow the
    // updater's own signed stride calculation immediately before this hook.
    if (vehicle < 0) return;

    const gpr limit_addr = (gpr)(int32_t)(kLimitBase +
        static_cast<uint32_t>(channel) * kLimitStride);
    const int16_t limit = static_cast<int16_t>(MEM_H(0, limit_addr));
    uint16_t normalized = analog_mode ? static_cast<uint16_t>(packed & kValueMask) : 0;
    const uint32_t selected_pad = static_cast<uint32_t>(MEM_W(0, kSelectedPadPtrAddr));
    const uint32_t expected_pad = 0x800A39E0u + port * 6u;
    bool digital_pressed = false;
    if (selected_pad == expected_pad) {
        const uint16_t buttons = static_cast<uint16_t>(MEM_HU(0, (gpr)(int32_t)selected_pad));
        digital_pressed = (buttons & 0xA000u) != 0;
        if (digital_pressed) normalized = UINT16_MAX; // stock A or Z fallback
    }
    const gpr demand_addr = (gpr)(int32_t)(kVehicleBase +
        static_cast<uint32_t>(vehicle) * kVehicleStride + kThrottleOffset);
    int16_t demand = static_cast<int16_t>(MEM_H(0, demand_addr));
    if (analog_mode) {
        const int16_t target = lambo::analog_throttle::scale_to_guest(normalized, limit);
        if (!digital_pressed) {
            // Preserve the ROM's +/-10 pedal ramp. The begin hook captured the field before
            // the stock digital branch changed it; move that value one stock step toward the
            // continuous target. Digital A/Z fallback keeps the already-executed stock path.
            const int previous = g_previous_valid[port] ? g_previous_demand[port] : demand;
            if (previous < target) demand = static_cast<int16_t>(std::min(previous + 10, int(target)));
            else if (previous > target) demand = static_cast<int16_t>(std::max(previous - 10, int(target)));
            else demand = target;
        }
        MEM_H(0, demand_addr) = demand;
    }
    g_previous_valid[port] = false;
    if (probing) {
        const unsigned frame = g_probe_frames[port].fetch_add(1, std::memory_order_relaxed) + 1;
        const gpr speed_addr = (gpr)(int32_t)(kVehicleBase +
            static_cast<uint32_t>(vehicle) * kVehicleStride + kSpeedOffset);
        const int32_t speed = static_cast<int32_t>(MEM_W(0, speed_addr));
        const int previous = g_probe_last[port].exchange(demand, std::memory_order_relaxed);
        if (previous == demand && frame % 60u != 0) return;
        LAMBO_LOG_DEBUG("probe",
            "analog throttle field: mode=%s port=%u channel=%d vehicle=%d frame=%u "
            "normalized=%u limit=%d demand=%d speed=%d\n",
            analog_mode ? "analog" : "digital", port, channel, vehicle, frame,
            static_cast<unsigned>(normalized), static_cast<int>(limit),
            static_cast<int>(demand), speed);
    }
}

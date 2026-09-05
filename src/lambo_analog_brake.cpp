#include "lambo_analog_brake.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

#include "recomp.h"
#include "lambo_log.h"
#include "lambo_input_quantize.h"

// Guest seam (measured live, see docs/analog-brake.md):
// - Brake demand is the float at vehicle offset 0xA0. Stock digital play ramps
//   it +1/update to 16 while B is held (func_80029DCC) and the release handler
//   (func_8002A070) snaps it to 0 on the first B-free frame.
// - The physics pass (func_8001EC1C, vram 0x8001F618/0x80022810) applies
//   deceleration of demand * 10 only while the SIGNED halfword at offset 0xAC
//   is > 0 (bit 0 = braking latch, set by the stock pressed branch).
// The ROM's own handlers rewrite both fields every frame, so instead of
// fighting them the ramp state lives host-side and is written absolutely at
// the merge hook, which runs before the physics pass consumes the values.

namespace {

constexpr uint32_t kAnalogEnabled = 0x80000000u;
constexpr uint32_t kValueMask = 0x0000FFFFu;
constexpr gpr kCurrentVehicleAddr = (gpr)(int32_t)0x80098398u;
constexpr gpr kCurrentChannelAddr = (gpr)(int32_t)0x800CE6AAu;
constexpr gpr kSelectedPadPtrAddr = (gpr)(int32_t)0x800A39DCu;
constexpr uint32_t kVehicleBase = 0x800B69A8u;
constexpr uint32_t kVehicleStride = 0x10Cu;
constexpr uint32_t kSpeedOffset = 0x90u;
constexpr uint32_t kBrakeOffset = 0xA0u;
constexpr uint32_t kBrakeLatchOffset = 0xACu;
constexpr uint16_t kBrakeLatchBit = 0x0001u;
constexpr float kBrakeMax = 16.0f;
constexpr float kBrakeStep = 1.0f;

std::array<std::atomic<uint32_t>, lambo::analog_brake::kPortCount> g_ports{};
std::atomic<bool> g_probe_enabled{false};
// Game-thread-only pedal state: the continuous demand currently applied per port.
std::array<float, lambo::analog_brake::kPortCount> g_ramp{};
std::array<std::atomic<unsigned>, lambo::analog_brake::kPortCount> g_probe_frames{};

float bits_to_float(uint32_t bits) {
    static_assert(sizeof(float) == sizeof(uint32_t));
    float out;
    __builtin_memcpy(&out, &bits, sizeof(out));
    return out;
}

uint32_t float_to_bits(float value) {
    uint32_t out;
    __builtin_memcpy(&out, &value, sizeof(out));
    return out;
}

} // namespace

namespace lambo::analog_brake {

void publish(unsigned port, bool analog_mode, float effective_value) {
    if (port >= kPortCount) return;
    const uint32_t packed = analog_mode
        ? kAnalogEnabled | static_cast<uint32_t>(lambo::input::quantize_normalized(effective_value))
        : 0u;
    g_ports[port].store(packed, std::memory_order_release);
}

void set_probe(bool enabled) {
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

float scale_to_guest(uint16_t normalized) {
    return static_cast<float>(normalized) / static_cast<float>(UINT16_MAX) * kBrakeMax;
}

} // namespace lambo::analog_brake

extern "C" void lambo_analog_brake_apply(uint8_t* rdram) {
    const int channel = static_cast<int16_t>(MEM_H(0, kCurrentChannelAddr));
    if (channel < 1 || channel > static_cast<int>(lambo::analog_brake::kPortCount)) return;

    const unsigned port = static_cast<unsigned>(channel - 1);
    const uint32_t packed = g_ports[port].load(std::memory_order_acquire);
    const bool analog_mode = (packed & kAnalogEnabled) != 0;
    const bool probing = g_probe_enabled.load(std::memory_order_acquire);
    if (!analog_mode && !probing) return;

    const int vehicle = static_cast<int16_t>(MEM_H(0, kCurrentVehicleAddr));
    // -1 is the ROM's inactive-vehicle sentinel (same meaning as the throttle hook).
    if (vehicle < 0) return;

    uint16_t normalized = analog_mode ? static_cast<uint16_t>(packed & kValueMask) : 0;
    const uint32_t selected_pad = static_cast<uint32_t>(MEM_W(0, kSelectedPadPtrAddr));
    const uint32_t expected_pad = 0x800A39E0u + port * 6u;
    bool digital_pressed = false;
    if (selected_pad == expected_pad) {
        const uint16_t buttons = static_cast<uint16_t>(MEM_HU(0, (gpr)(int32_t)selected_pad));
        digital_pressed = (buttons & 0x4000u) != 0;
        if (digital_pressed) normalized = UINT16_MAX; // stock B fallback keeps the ROM path
    }
    if (analog_mode && !digital_pressed) {
        // Translate the stock one-directional pedal: rising demand follows the
        // ROM's +1-per-update pressed ramp; falling demand snaps straight to
        // the target exactly as the stock release handler does (there is no
        // stock falling ramp to preserve). Write the absolute demand + braking
        // latch after the ROM's own handlers so the physics pass consumes them
        // later in this same frame.
        const float target = lambo::analog_brake::scale_to_guest(normalized);
        float& ramp = g_ramp[port];
        if (ramp < target) ramp = std::min(ramp + kBrakeStep, target);
        else ramp = target;
        MEM_W(0, (gpr)(int32_t)(kVehicleBase +
            static_cast<uint32_t>(vehicle) * kVehicleStride + kBrakeOffset)) =
            float_to_bits(ramp);
        const gpr latch_addr = (gpr)(int32_t)(kVehicleBase +
            static_cast<uint32_t>(vehicle) * kVehicleStride + kBrakeLatchOffset);
        const uint16_t flags = static_cast<uint16_t>(MEM_HU(0, latch_addr));
        const uint16_t updated = ramp > 0.0f
            ? static_cast<uint16_t>(flags | kBrakeLatchBit)
            : static_cast<uint16_t>(flags & ~kBrakeLatchBit);
        MEM_H(0, latch_addr) = static_cast<int16_t>(updated);
    } else {
        // Digital mode or held stock B owns the guest field completely; the
        // native pedal restarts its ramp from neutral when analog resumes.
        g_ramp[port] = 0.0f;
    }
    if (probing) {
        const unsigned frame = g_probe_frames[port].fetch_add(1, std::memory_order_relaxed) + 1;
        const gpr demand_addr = (gpr)(int32_t)(kVehicleBase +
            static_cast<uint32_t>(vehicle) * kVehicleStride + kBrakeOffset);
        const float demand = bits_to_float(MEM_W(0, demand_addr));
        const int32_t speed = static_cast<int32_t>(MEM_W(0,
            (gpr)(int32_t)(kVehicleBase +
                static_cast<uint32_t>(vehicle) * kVehicleStride + kSpeedOffset)));
        LAMBO_LOG_DEBUG("probe",
            "analog brake field: mode=%s port=%u channel=%d vehicle=%d frame=%u "
            "normalized=%u demand=%.1f speed=%d\n",
            analog_mode ? "analog" : "digital", port, channel, vehicle, frame,
            static_cast<unsigned>(normalized), static_cast<double>(demand), speed);
    }
}

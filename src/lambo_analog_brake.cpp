#include "lambo_analog_brake.h"

#include <array>
#include <atomic>
#include <cstdint>

#include "recomp.h"
#include "lambo_log.h"
#include "lambo_input_quantize.h"
#include "lambo_vehicle.h"

// The compiled guest updater owns brake demand, latch and ramp history.
// This adapter publishes input and reads guest state only for diagnostics.
// See docs/vehicle-source.md for layout and game-thread timing.

namespace {

constexpr uint32_t kAnalogEnabled = 0x80000000u;
constexpr uint32_t kValueMask = 0x0000FFFFu;
constexpr gpr kCurrentChannelAddr = (gpr)(int32_t)0x800CE6AAu;
constexpr gpr kSelectedPadPtrAddr = (gpr)(int32_t)0x800A39DCu;
constexpr uint32_t kVehicleBase = LAMBO_VEHICLE_BASE;
constexpr uint32_t kVehicleStride = sizeof(LamboVehicleRecord);
constexpr uint32_t kSpeedOffset = offsetof(LamboVehicleRecord, speed);
constexpr uint32_t kBrakeOffset = offsetof(LamboVehicleRecord, brake_demand);

std::array<std::atomic<uint32_t>, lambo::analog_brake::kPortCount> g_ports{};
std::atomic<bool> g_probe_enabled{false};
std::array<std::atomic<unsigned>, lambo::analog_brake::kPortCount> g_probe_frames{};

float bits_to_float(uint32_t bits) {
    static_assert(sizeof(float) == sizeof(uint32_t));
    float out;
    __builtin_memcpy(&out, &bits, sizeof(out));
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

uint32_t guest_snapshot(unsigned port) {
    if (port >= kPortCount) return 0;
    return g_ports[port].load(std::memory_order_acquire) |
        (g_probe_enabled.load(std::memory_order_acquire) ? 0x40000000u : 0u);
}

} // namespace lambo::analog_brake

// Read-only observer used by compiled guest source after its demand write.
extern "C" void lambo_analog_brake_observe(uint8_t* rdram, uint32_t packed) {
    const int channel = static_cast<int16_t>(MEM_H(0, kCurrentChannelAddr));
    if (channel < 1 || channel > 4) return;
    const unsigned port = static_cast<unsigned>(channel - 1);
    const int vehicle = static_cast<int16_t>(MEM_H(0,
        (gpr)(int32_t)LAMBO_GUEST_CURRENT_VEHICLE_ADDR));
    if (vehicle < 0) return;
    const bool analog_mode = (packed & kAnalogEnabled) != 0;
    const bool probing = (packed & 0x40000000u) != 0;
    uint16_t normalized = analog_mode ? static_cast<uint16_t>(packed & kValueMask) : 0;
    const uint32_t pad = static_cast<uint32_t>(MEM_W(0, kSelectedPadPtrAddr));
    if (pad == 0x800A39E0u + port * 6u &&
        (MEM_HU(0, (gpr)(int32_t)pad) & 0x4000u)) normalized = UINT16_MAX;
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

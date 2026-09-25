#include "lambo_driving_assists.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace lambo::driving {
namespace {
std::atomic<std::uint32_t> pending{};
std::atomic<bool> in_race{};
constexpr float pi = 3.14159265358979323846f;
float wrap(float angle) { return std::remainder(angle, 2.0f * pi); }
}

void TiltSteering::reset() { centered_ = false; angle_ = neutral_ = 0; }

bool TiltSteering::sample(float gyro_z, float gravity_x, float gravity_y, float dt) {
    if (!std::isfinite(gyro_z) || !std::isfinite(gravity_x) ||
        !std::isfinite(gravity_y) || !std::isfinite(dt) || dt <= 0 || dt > .25f ||
        std::hypot(gravity_x, gravity_y) < 2.0f) {
        reset(); // Flat phone, suspended app, or bad data: fail to manual input.
        return false;
    }
    const float gravity_angle = std::atan2(gravity_x, gravity_y);
    if (!centered_) {
        angle_ = neutral_ = gravity_angle;
        centered_ = true;
    } else {
        angle_ = wrap(angle_ + gyro_z * dt);
        // One-second gravity correction constant, independent of frame rate.
        angle_ = wrap(angle_ + (1.0f - std::exp(-dt)) * wrap(gravity_angle - angle_));
    }
    return true;
}

std::int8_t TiltSteering::steering(const Settings& settings) const {
    if (!centered_ || !std::isfinite(settings.deadzone_degrees) ||
        !std::isfinite(settings.full_lock_degrees)) return 0;
    const float degrees = wrap(angle_ - neutral_) * (180.0f / pi);
    const float deadzone = std::clamp(settings.deadzone_degrees, 0.0f, 10.0f);
    const float range = std::clamp(settings.full_lock_degrees, 15.0f, 90.0f);
    const float magnitude = std::clamp((std::abs(degrees) - deadzone) / (range - deadzone), 0.0f, 1.0f);
    // Clockwise rotation is right steering; SDL positive Z is counterclockwise.
    const float sign = (degrees < 0 ? 1.0f : -1.0f) * (settings.invert ? -1.0f : 1.0f);
    return static_cast<std::int8_t>(std::lround(sign * magnitude * 80.0f));
}

void publish(Demand demand) {
    pending.store(std::uint8_t(demand.steering) | (demand.gyro_valid ? 0x100u : 0u) |
                  (demand.auto_accelerate ? 0x200u : 0u), std::memory_order_release);
}
Demand sample() {
    const auto value = pending.load(std::memory_order_acquire);
    return {bool(value & 0x100u), std::int8_t(value), bool(value & 0x200u)};
}
void set_racing(bool racing) { in_race.store(racing, std::memory_order_release); }
bool racing() { return in_race.load(std::memory_order_acquire); }
bool race_allows_assists(int state, int phase, int pause, int mode) {
    return state == 8 && phase == 3 && pause == 0 && mode != 4;
}

void apply(Demand demand, bool allowed, bool analog_braking,
           std::uint16_t& buttons, std::int8_t& stick_x) {
    // Start must enter/leave pause without a simultaneous synthetic A press.
    if (!allowed || (buttons & 0x1000u)) return;
    if (demand.gyro_valid) {
        // Manual steering with the larger deflection takes priority.
        if (std::abs(int(demand.steering)) > std::abs(int(stick_x))) stick_x = demand.steering;
    }
    if (demand.auto_accelerate && !analog_braking && !(buttons & 0x4000u)) buttons |= 0x8000u;
}
}

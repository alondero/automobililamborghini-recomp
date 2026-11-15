#include "lambo_slip_rumble.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace lambo::rumble {
float slide_severity(const SlideSample& s) {
    const double hx = s.field_40, hz = s.field_44;
    const double vx = s.field_6c, vz = s.field_74;
    if (!std::isfinite(hx) || !std::isfinite(hz) ||
        !std::isfinite(vx) || !std::isfinite(vz) ||
        std::hypot(hx, hz) < 1e-6 || std::hypot(vx, vz) < 1e-6) return 0;
    // atan2 cancels vector scale and folds reverse travel onto forward travel.
    const double angle = std::atan2(std::abs(vx * hz - vz * hx),
                                   std::abs(vx * hx + vz * hz));
    constexpr double radians = 3.14159265358979323846 / 180.0;
    // Experimental tuning, not a km/h or wheel-RPM claim. See docs/slip-rumble.md.
    return float(std::clamp((angle / radians - 4.0) / 26.0, 0.0, 1.0) *
        std::clamp((std::abs(double(s.speed)) - 10.0) / 20.0, 0.0, 1.0));
}

void SlipRumble::reset() {
    stamp_ = 0;
    vehicle_ = -1;
    phase_ = 0;
    on_ = false;
}
void SlipRumble::clear() {
    std::lock_guard lock(mutex_);
    reset();
}
void SlipRumble::release_vehicle(int vehicle) {
    std::lock_guard lock(mutex_);
    if (vehicle_ == vehicle) reset();
}
void SlipRumble::publish(const SlideSample& sample, std::uint64_t now) {
    std::lock_guard lock(mutex_);
    if (!accepting_ || sample.vehicle < 0 || sample.vehicle >= 64) {
        reset();
        return;
    }
    if (vehicle_ != sample.vehicle || now < stamp_ || now - stamp_ >= timeout_ms)
        reset();
    const float severity = slide_severity(sample);
    if (severity <= 0) { reset(); return; }
    vehicle_ = sample.vehicle;
    stamp_ = now;
    const unsigned ticks = unsigned(std::ceil(severity * 3.0f));
    on_ = phase_ < ticks;
    phase_ = (phase_ + 1) % 6;
}
bool SlipRumble::output(bool original, bool enabled, bool connected,
                        bool captured, std::uint64_t now) {
    std::lock_guard lock(mutex_);
    accepting_ = enabled && connected && !captured;
    if (!accepting_ || now < stamp_ || now - stamp_ >= timeout_ms) reset();
    // Disconnection suppresses only the added source. Preserve the existing
    // original-source API and let the frontend own device selection and strength.
    return (original || on_) && !captured;
}
SlipRumble& slip() { static SlipRumble value; return value; }
std::uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
extern "C" void lambo_slip_rumble_reset() { lambo::rumble::slip().clear(); }

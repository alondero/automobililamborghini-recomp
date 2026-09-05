#include "lambo_analog_override.h"

#include <cmath>
#include <iostream>

int main() {
    const auto values = lambo::analog_override::parse("0.25", "0.75");
    if (!values.throttle || !values.brake || std::abs(*values.throttle - 0.25f) > 0.001f ||
        std::abs(*values.brake - 0.75f) > 0.001f) {
        std::cerr << "both analog overrides must survive parsing\n";
        return 1;
    }
    const auto clamped = lambo::analog_override::parse("-1", "2");
    if (!clamped.throttle || !clamped.brake || *clamped.throttle != 0.0f || *clamped.brake != 1.0f) {
        std::cerr << "analog overrides must clamp to [0, 1]\n";
        return 1;
    }
    const auto invalid = lambo::analog_override::parse("nan", "not-a-number");
    if (invalid.throttle || invalid.brake) {
        std::cerr << "non-finite and malformed overrides must be ignored\n";
        return 1;
    }
    return 0;
}

#include "lambo_analog_brake.h"
#include "lambo_analog_throttle.h"
#include "lambo_replay_runtime.h"

#include <cmath>
#include <cstdint>
#include <iostream>

int main() {
    lambo::replay_runtime::publish_physical_throttle(true, 0.5f);
    lambo::replay_runtime::publish_physical_brake(true, 0.25f);
    float throttle = 0.0f;
    float brake = 0.0f;
    const bool throttle_analog = lambo::analog_throttle::sample(0, throttle);
    const bool brake_analog = lambo::analog_brake::sample(0, brake);
    if (!throttle_analog || !brake_analog || std::abs(throttle - 0.5f) > 1.0f / 65535.0f ||
        std::abs(brake - 0.25f) > 1.0f / 65535.0f) {
        std::cerr << "runtime must publish both physical analog channels\n";
        return 1;
    }
    return 0;
}

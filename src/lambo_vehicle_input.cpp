#include "lambo_analog_throttle.h"
#include "lambo_analog_brake.h"
#include "lambo_steering.h"

// Guest source calls this on the game thread. Input publication remains atomic;
// steering publication retains its existing mutex. No guest state is touched.
extern "C" std::uint32_t lambo_vehicle_input_snapshot(std::uint32_t port, std::uint32_t kind) {
    if (kind == 0) return lambo::analog_throttle::guest_snapshot(port);
    if (kind == 1) return lambo::analog_brake::guest_snapshot(port);
    if (kind == 2) {
        const auto settings = lambo::steering::current();
        return static_cast<std::uint32_t>(settings.native_deadzone) |
            (settings.direct ? 256u : 0u);
    }
    return 0;
}

// Diagnostic reads stay on the game thread. This adapter cannot modify demand,
// accumulators or latches; guest source has already committed those values.
extern "C" void lambo_vehicle_observe_native(std::uint8_t* rdram, std::uint32_t kind,
                                             std::int32_t authored, std::uint32_t snapshot) {
    if (kind == 0) lambo_analog_throttle_observe(rdram, snapshot);
    else if (kind == 1) lambo_analog_brake_observe(rdram, snapshot);
    else if (kind == 2) lambo_steering_observe(rdram, authored, snapshot);
}

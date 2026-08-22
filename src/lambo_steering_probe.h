#pragma once

#include <cstdint>

namespace lambo::steering_probe {

struct Sample {
    unsigned frame = 0;
    int vehicle = -1;
    int raw = 0;
    int trimmed = 0;
    int curved = 0;
    int demand = 0;
    int speed = 0;
    int selector = 0;
    char curve = '?';
    bool complete = false;
};

void set_enabled(bool enabled);
Sample last_sample();

} // namespace lambo::steering_probe

extern "C" {
void lambo_steering_probe_capture_raw(uint8_t* rdram);
void lambo_steering_probe_capture_trim(uint8_t* rdram, int sign);
void lambo_steering_probe_capture_curve(uint8_t* rdram);
void lambo_steering_probe_capture_demand(uint8_t* rdram);
}

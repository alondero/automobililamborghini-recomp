#pragma once

#include <cstdint>

namespace lambo::driving {
struct Settings {
    bool gyro{};
    bool auto_accelerate{};
    bool invert{};
    float full_lock_degrees{35.0f};
    float deadzone_degrees{2.0f};
};

// Phone rotation about the screen normal. Gravity corrects gyro drift; a new
// driving session establishes neutral from the current way the phone is held.
class TiltSteering {
public:
    // Explicitly establish a new neutral on the next valid sample. Invalid
    // sensor readings only suspend steering and must not call this.
    void recenter();
    bool sample(float gyro_z, float gravity_x, float gravity_y, float dt);
    std::int8_t steering(const Settings& settings) const;
private:
    bool centered_{};
    float angle_{};
    float neutral_{};
};

struct Demand {
    bool gyro_valid{};
    std::int8_t steering{};
    bool auto_accelerate{};
};
// SDL/main thread -> guest thread. UI and physical-release gating are applied
// by the caller; synthetic input never holds the physical release barrier.
void publish(Demand demand);
Demand sample();
void set_racing(bool racing);
bool racing();
void set_replay_playback(bool playback);
bool replay_playback();
bool race_allows_assists(int state, int phase, int pause, int mode);
void apply(Demand demand, bool allowed, bool analog_braking,
           std::uint16_t& buttons, std::int8_t& stick_x);
}

#include "lambo_driving_assists.h"
#include <cmath>
#include <iostream>
#include <limits>

int main() {
    using namespace lambo::driving;
    int failures = 0;
    auto expect = [&](bool value, const char* message) {
        if (!value) { std::cerr << message << '\n'; ++failures; }
    };
    Settings settings;
    expect(!settings.gyro && !settings.auto_accelerate, "assists default off");
    expect(race_allows_assists(8, 3, 0, 0), "active human race allowed");
    for (int state = 0; state < 17; ++state)
        if (state != 8) expect(!race_allows_assists(state, 3, 0, 0), "menus/loading excluded");
    for (int phase : {0, 1, 2, 4, 5})
        expect(!race_allows_assists(8, phase, 0, 0), "countdown/results excluded");
    expect(!race_allows_assists(8, 3, 1, 0), "pause excluded");
    expect(!race_allows_assists(8, 3, 0, 4), "attract demo excluded");

    constexpr float pi = 3.14159265358979323846f;
    for (float neutral : {-pi / 2, pi / 2, pi - .1f}) {
        TiltSteering tilt;
        auto step = [&](float angle, float rate, float dt) {
            return tilt.sample(rate, 9.81f * std::sin(angle), 9.81f * std::cos(angle), dt);
        };
        expect(step(neutral, 0, .01f) && tilt.steering(settings) == 0,
               "both landscape orientations and angle wrap center correctly");
        for (int i = 1; i <= 100; ++i) step(neutral - .7f * i / 100, -.7f, .01f);
        expect(tilt.steering(settings) == 80, "clockwise full tilt steers right");
        // Holding a turned wheel must sustain steering, not decay to zero.
        for (int i = 0; i < 500; ++i) step(neutral - .7f, 0, .01f);
        expect(tilt.steering(settings) == 80, "stationary tilted wheel stays turned");
        settings.invert = true;
        expect(tilt.steering(settings) == -80, "inversion reverses steering");
        settings.invert = false;
        for (int i = 1; i <= 100; ++i) step(neutral - .7f + .7f * i / 100, .7f, .01f);
        expect(tilt.steering(settings) == 0, "returning to neutral steers straight");
        expect(!step(neutral, 0, 1), "background gap invalidates sensor state");
        expect(step(neutral + .5f, 0, .01f) && tilt.steering(settings) == 0,
               "resume centers current pose");
        expect(!tilt.sample(0, 0, 0, .01f), "flat phone fails to manual input");
        expect(!tilt.sample(std::numeric_limits<float>::quiet_NaN(), 0, 9.81f, .01f), "NaN rejected");
    }
    std::uint16_t buttons = 0;
    std::int8_t stick = 0;
    Demand demand{true, -60, true};
    publish(demand);
    expect(sample().steering == -60 && sample().gyro_valid && sample().auto_accelerate,
           "atomic publication preserves signed steering and flags");
    apply(demand, false, false, buttons, stick);
    expect(buttons == 0 && stick == 0, "inactive assists leave physical input unchanged");
    apply(demand, true, false, buttons, stick);
    expect(buttons == 0x8000 && stick == -60, "race assists steer and accelerate");
    buttons = 0x4000; stick = 80;
    apply(demand, true, false, buttons, stick);
    expect(buttons == 0x4000 && stick == 80, "braking cancels auto throttle and manual steering wins");
    buttons = 0;
    apply(demand, true, true, buttons, stick);
    expect(buttons == 0, "analog brake cancels auto throttle");
    buttons = 0x1000; stick = 0;
    apply(demand, true, false, buttons, stick);
    expect(buttons == 0x1000 && stick == 0, "Start never coincides with synthetic menu input");
    return failures ? 1 : 0;
}

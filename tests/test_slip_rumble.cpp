#include "lambo_slip_rumble.h"
#include <cmath>
#include <iostream>
#include <limits>

int main() {
    using namespace lambo::rumble;
    int failures = 0;
    auto expect = [&](bool value, const char* message) {
        if (!value) { std::cerr << message << '\n'; ++failures; }
    };
    SlideSample slide{1, 0, 0, 1, 40, 0};
    expect(slide_severity(slide) == 1, "sideways motion reaches maximum");
    for (float direction : {-1.f, 1.f}) {
        auto straight = slide; straight.field_6c = direction; straight.field_74 = 0;
        expect(slide_severity(straight) == 0, "forward and reverse straight travel are silent");
    }
    auto invalid = slide; invalid.field_40 = std::numeric_limits<float>::quiet_NaN();
    expect(slide_severity(invalid) == 0, "NaN rejected");
    invalid = slide; invalid.field_74 = std::numeric_limits<float>::infinity();
    expect(slide_severity(invalid) == 0, "infinity rejected");
    invalid = slide; invalid.field_40 = 0;
    expect(slide_severity(invalid) == 0, "degenerate facing rejected");
    invalid = slide; invalid.field_74 = 0;
    expect(slide_severity(invalid) == 0, "stationary vector rejected");
    invalid = slide; invalid.speed = 10;
    expect(slide_severity(invalid) == 0, "low speed silent");
    invalid.speed = 20;
    expect(slide_severity(invalid) == .5f, "speed fade midpoint");
    auto middle = slide; middle.field_6c = std::cos(17.f * 3.14159265f / 180);
    middle.field_74 = std::sin(17.f * 3.14159265f / 180);
    expect(std::abs(slide_severity(middle) - .5f) < 1e-5f, "angle fade midpoint");

    for (bool original : {false, true}) {
        SlipRumble rumble;
        rumble.output(original, true, true, false, 1000);
        for (int i = 0; i < 12; ++i) {
            rumble.publish(slide, 1000 + i * 33);
            expect(rumble.output(original, true, true, false, 1000 + i * 33) ==
                   (original || i % 6 < 3), "six-tick pulse OR preserves every original on");
        }
        rumble.clear();
        expect(rumble.output(original, true, true, false, 1400) == original,
               "slip clear leaves original untouched");
    }
    SlipRumble rumble;
    rumble.output(false, true, true, false, 2000);
    rumble.publish(slide, 2000);
    expect(rumble.output(false, true, true, false, 2149), "sample fresh before deadline");
    expect(!rumble.output(false, true, true, false, 2150), "sample expires at deadline");
    for (int gate = 0; gate < 3; ++gate) {
        rumble.output(false, true, true, false, 3000);
        rumble.publish(slide, 3000);
        expect(!rumble.output(false, gate != 0, gate != 1, gate == 2, 3001),
               "disable/disconnect/capture clears slip");
        rumble.publish(slide, 3002);
        expect(!rumble.output(false, true, true, false, 3003),
               "re-enable/reconnect/close menu cannot resurrect old samples");
        rumble.publish(slide, 3004);
        expect(rumble.output(false, true, true, false, 3004), "fresh sample resumes at phase zero");
    }
    rumble.clear();
    for (int i = 0; i < 4; ++i) rumble.publish(slide, 4000 + i);
    expect(!rumble.output(false, true, true, false, 4003), "pulse off phase");
    slide.vehicle = 1; rumble.publish(slide, 4004);
    expect(rumble.output(false, true, true, false, 4004), "ownership change resets phase");
    slide.vehicle = 64; rumble.publish(slide, 4005);
    expect(!rumble.output(false, true, true, false, 4005), "invalid owner clears");
    for (int i = 0; i < 128; ++i) {
        expect(rumble.output(i % 3 == 0, false, true, false, 5000 + i) == (i % 3 == 0),
               "disabled trace equals original trace");
    }
    return failures ? 1 : 0;
}

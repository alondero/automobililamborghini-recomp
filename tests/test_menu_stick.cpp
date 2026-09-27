#include <cstdint>
#include <iostream>

#include "lambo_menu_stick.h"

// lambo_menu_stick.cpp reads lambo::config::menu_stick_sensitivity(); stub it here so
// the test exercises the scaling maths without a config file or the runtime.
namespace {
double g_sensitivity = 1.0;
int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
} // namespace

namespace lambo::config {
double menu_stick_sensitivity() { return g_sensitivity; }
} // namespace lambo::config

int main() {
    // 1.0 (stock / disabled) is an exact no-op across the whole pad byte domain.
    g_sensitivity = 1.0;
    for (int value = -128; value <= 127; ++value) {
        if (lambo_menu_stick_scale(value) != value) {
            expect(false, "stock sensitivity must be a byte-for-byte no-op");
            break;
        }
    }

    // 2.0 doubles the deflection and preserves sign; zero stays zero.
    g_sensitivity = 2.0;
    expect(lambo_menu_stick_scale(0) == 0, "zero stays zero");
    expect(lambo_menu_stick_scale(10) == 20, "2x scales small deflections");
    expect(lambo_menu_stick_scale(30) == 60, "2x scales positives");
    expect(lambo_menu_stick_scale(-30) == -60, "2x scales negatives symmetrically");

    // 1.5 rounds to nearest and lifts a moderate push past the ROM's 51 threshold.
    g_sensitivity = 1.5;
    expect(lambo_menu_stick_scale(40) == 60, "1.5x makes a 40-unit push exceed 51");
    expect(lambo_menu_stick_scale(17) == 26, "1.5x rounds to nearest (25.5 -> 26)");
    expect(lambo_menu_stick_scale(-17) == -26, "1.5x rounds negatives symmetrically");

    // Results clamp to the signed pad byte domain.
    g_sensitivity = 2.5;
    expect(lambo_menu_stick_scale(51) == 127, "51 -> 127.5 clamps to 127");
    expect(lambo_menu_stick_scale(80) == 127, "full deflection clamps high");
    expect(lambo_menu_stick_scale(-80) == -127, "full negative deflection clamps low");
    expect(lambo_menu_stick_scale(127) == 127, "already-full input stays clamped");

    if (failures == 0) std::cout << "menu stick scale: all checks passed\n";
    return failures ? 1 : 0;
}

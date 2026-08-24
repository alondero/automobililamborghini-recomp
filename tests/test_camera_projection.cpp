#include <cmath>
#include <cstdlib>
#include <iostream>

#include "lambo_camera_projection.h"

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

double cot_half_fov(double degrees) {
    constexpr double pi = 3.14159265358979323846;
    return 1.0 / std::tan(degrees * pi / 360.0);
}

bool nearly_equal(double a, double b) {
    return std::abs(a - b) < 1.0e-5;
}

} // namespace

int main() {
    constexpr double authored_fovs[] = {20.0, 32.0, 40.0, 52.0};
    constexpr double positive_deltas[] = {5.0, 20.0, 60.0};

    for (double authored : authored_fovs) {
        for (double delta : positive_deltas) {
            const double adjusted_scale = cot_half_fov(authored + delta);
            const double restore = lambo_backdrop_fov_restore_scale(authored, authored + delta);
            expect(nearly_equal(adjusted_scale * restore, cot_half_fov(authored)),
                   "backdrop projection must recover the authored vertical FOV");
        }
    }

    expect(lambo_backdrop_fov_restore_scale(40.0, 40.0) == 1.0,
           "stock FOV must remain bit-for-bit unscaled");
    expect(lambo_backdrop_fov_restore_scale(40.0, 30.0) == 1.0,
           "a narrower camera already overfills the finite backdrop");
    expect(lambo_backdrop_fov_restore_scale(0.0, 20.0) == 1.0,
           "invalid projection scales must fail safe");

    expect(lambo_clamp_vertical_fov(0.0) == 1.0,
           "the lowest configured FOV must not create a singular projection");
    expect(lambo_clamp_vertical_fov(200.0) == 170.0,
           "unexpected future config values must remain projection-safe");

    std::cout << "camera projection policy tests passed\n";
    return 0;
}

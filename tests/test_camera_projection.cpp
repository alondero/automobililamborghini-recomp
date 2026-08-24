#include <cmath>
#include <cstdint>
#include <cstring>
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

    constexpr double authored_cone_cos = kAuthoredViewConeCos;
    expect(lambo_view_cone_cos(authored_cone_cos, 40.0, 40.0) == authored_cone_cos,
           "stock FOV must keep the authored view-cone constant bit-for-bit");
    // Pipeline invariant: the identity result's double bits must equal the ROM
    // constant at 0x8008D8C0 exactly (0x3FEC5A1CAC083127), so the per-frame
    // RDRAM rewrite is a byte-stable no-op at stock FOV.
    {
        const double identity = lambo_view_cone_cos(authored_cone_cos, 40.0, 40.0);
        std::uint64_t bits = 0;
        std::memcpy(&bits, &identity, sizeof(bits));
        expect(bits == 0x3FEC5A1CAC083127ull,
               "stock view-cone cosine must carry the exact ROM double bits");
    }
    expect(lambo_view_cone_cos(authored_cone_cos, 40.0, 0.0) == authored_cone_cos,
           "invalid adjusted FOV must leave the authored view cone untouched");
    expect(lambo_view_cone_cos(authored_cone_cos, 0.0, 50.0) == authored_cone_cos,
           "invalid authored FOV must leave the authored view cone untouched");
    expect(lambo_view_cone_cos(1.5, 40.0, 60.0) == 1.5,
           "out-of-domain cone constants must pass through untouched");

    for (double authored : authored_fovs) {
        double previous = authored_cone_cos;
        for (double delta : positive_deltas) {
            const double widened =
                lambo_view_cone_cos(authored_cone_cos, authored, authored + delta);
            expect(widened < previous && widened > 0.0,
                   "a wider projection must widen the view cone monotonically toward 90 degrees");
            previous = widened;

            // The cone half-angle must track the horizontal frustum edge, whose
            // tangent scales by tan(adjusted_v/2)/tan(authored_v/2) at fixed aspect.
            constexpr double pi = 3.14159265358979323846;
            const double cone_half = std::acos(authored_cone_cos);
            const double expected_half =
                std::atan(std::tan(cone_half) *
                          std::tan((authored + delta) * pi / 360.0) /
                          std::tan(authored * pi / 360.0));
            expect(nearly_equal(widened, std::cos(expected_half)),
                   "the widened cone must preserve the authored margin in tan space");

            const double narrowed =
                lambo_view_cone_cos(authored_cone_cos, authored + delta, authored);
            expect(narrowed > authored_cone_cos,
                   "a narrower projection must tighten the view cone");
        }
    }

    std::cout << "camera projection policy tests passed\n";
    return 0;
}

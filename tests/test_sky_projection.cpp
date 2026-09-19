#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "render/rt64_backdrop_projection.h"
#include "lambo_camera_projection.h"
#include "lambo_sky_panorama.h"

int main() {
    constexpr double pi = 3.14159265358979323846;
    // Fixed compass landmarks must move with camera yaw. The ROM computes
    // panorama heading in units/degree (double 2.8444 at USA 0x8008D910),
    // then splits it into a tile index and a modulo-128 translation. Drive
    // that actual quantized convention, not two copies of a scroll equation.
    for (double yaw : {0.0, 20.0, 44.99, 45.01, 90.0, 179.99, 180.0, 270.0, 359.99}) {
        const double heading = yaw * 2.8444;
        const int center = static_cast<int>(heading / 128.0);
        const double phase = std::fmod(heading, 128.0);
        for (double relative : {-75.0, -45.0, -10.0, 0.0, 10.0, 45.0, 75.0}) {
            const double landmark = yaw + relative;
            const double local = landmark * 1024.0 / 360.0 - center * 128.0;
            const double x = lambo_sky_project_x(local, phase) - phase;
            const double measured = std::atan(x / 220.0) * 180.0 / pi;
            if (std::abs(measured - relative) > 0.007) {
                std::cerr << "Sky compass landmark drifts at yaw " << yaw << '\n';
                return EXIT_FAILURE;
            }
        }
    }
    // The tiny authored approximation leaves 180 degrees just before tile 4.
    // Check unwrapped coordinates so periodic/mirrored artwork cannot hide it.
    if (std::abs(180.0 * 2.8444 / 128.0 - 4.0) > 0.0001 ||
        std::abs(360.0 * 2.8444 / 128.0 - 8.0) > 0.0002) {
        std::cerr << "Half/full turns must traverse half/full panorama rings\n";
        return EXIT_FAILURE;
    }
    for (double authored : {20.0, 32.0, 40.0, 52.0}) {
        for (double delta : {-19.0, 0.0, 20.0, 60.0}) {
            const double fov = lambo_clamp_vertical_fov(authored + delta);
            const double vertical = 1.0 / std::tan(fov * pi / 360.0);
            const double restore = lambo_backdrop_fov_restore_scale(authored, fov);
            for (double aspect : {4.0 / 3.0, 16.0 / 9.0, 21.0 / 9.0, 32.0 / 9.0, 8.0}) {
                for (bool two_player : {false, true}) {
                    const double source_aspect = (4.0 / 3.0) * (two_player ? 2 : 1);
                    const double ratio = aspect / (4.0 / 3.0);
                    std::array<std::array<double, 4>, 4> sky{};
                    sky[0][0] = vertical / source_aspect;
                    sky[1][1] = vertical;
                    sky[2][3] = -1.0;
                    RT64::adjustBackdropProjection(sky, static_cast<float>(ratio), static_cast<float>(restore));
                    // The ROM encodes yaw as a linear model-space scroll. Compare
                    // a feature's screen displacement before/after the same turn,
                    // through the sky policy and through the world's projection.
                    const double sky_motion = 10.0 * sky[0][0] / 220.0;
                    const double world_motion = 10.0 * vertical / source_aspect / ratio / 220.0;
                    if (std::abs(sky_motion - world_motion) > 1e-6 ||
                        std::abs(sky[1][1] - vertical * restore) > 1e-4 || sky[2][3] != -1.0) {
                        std::cerr << "Sky motion differs from world at FOV " << fov
                                  << ", aspect " << aspect << ": " << sky_motion / world_motion << "x\n";
                        return EXIT_FAILURE;
                    }
                    // Sweep the entire scroll phase, including the boundary where
                    // texture selection advances to the next panorama tile.
                    for (double offset : {-512.0, 0.0, 512.0}) {
                        const int radius = lambo_sky_column_radius(fov, aspect, two_player, offset);
                        const double visible_half_width = 220.0 / sky[0][0];
                        const double visible_half_height = 220.0 / sky[1][1];
                        for (double phase : {0.0, 64.0, 127.999, 128.0}) {
                            if (-radius * 128.0 - phase > -visible_half_width ||
                                (radius + 1) * 128.0 - phase < visible_half_width) {
                                std::cerr << "Extended sky does not cover the viewport\n";
                                return EXIT_FAILURE;
                            }
                            // Invert the guest's Z rotation and pitch/heading
                            // translation at every viewport corner, for a full turn.
                            for (int roll = -180; roll <= 180; roll += 5) {
                                const double c = std::cos(roll * pi / 180.0);
                                const double s = std::sin(roll * pi / 180.0);
                                for (int x_sign : {-1, 1}) for (int y_sign : {-1, 1}) {
                                    const double x = x_sign * visible_half_width;
                                    const double y = y_sign * visible_half_height;
                                    const double local_x = c * x + s * y + phase;
                                    const double local_y = -s * x + c * y - offset;
                                    if (local_x < -radius * 128.0 || local_x > (radius + 1) * 128.0 ||
                                        std::abs(local_y) > kSkyEdgeExtent) {
                                        std::cerr << "Tilted sky exposes a viewport corner\n";
                                        return EXIT_FAILURE;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    for (int center = 0; center < 8; ++center) {
        for (int column = -254; column <= 254; ++column) {
            if (lambo_sky_tile_index(center, column) != lambo_sky_tile_index(center + 1, column - 1)) {
                std::cerr << "Panorama jumps at a tile/heading wrap\n";
                return EXIT_FAILURE;
            }
        }
    }
    std::cout << "Sky motion, coverage and panorama wrapping passed\n";
}

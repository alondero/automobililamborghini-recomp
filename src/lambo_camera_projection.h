#pragma once

#include <algorithm>
#include <cmath>

// The scene builder's authored forward view-cone cosine (ROM doubles at
// 0x8008D8C0/C8). A C double literal 0.886 is bit-identical to the ROM value,
// so restoring it verbatim keeps stock behaviour byte-stable.
inline constexpr double kAuthoredViewConeCos = 0.886;

// Keep every camera layout away from guPerspective's singular endpoints. The
// current UI range only reaches the lower endpoint in 2P (20 + -20), but this
// also makes future config-range changes safe.
inline double lambo_clamp_vertical_fov(double degrees) {
    if (!std::isfinite(degrees)) {
        return 40.0;
    }
    return std::clamp(degrees, 1.0, 170.0);
}

// A finite panorama must retain its authored FOV even while the world camera is
// widened. Return the projection multiplier which maps cot(adjusted / 2) back
// to cot(authored / 2). Narrower cameras already overfill the panorama.
inline double lambo_backdrop_fov_restore_scale(double authored_degrees,
                                                double adjusted_degrees) {
    if (!std::isfinite(authored_degrees) || !std::isfinite(adjusted_degrees) ||
        !(adjusted_degrees > authored_degrees)) {
        return 1.0;
    }

    constexpr double pi = 3.14159265358979323846;
    const double authored_radians = authored_degrees * pi / 180.0;
    const double adjusted_radians = adjusted_degrees * pi / 180.0;
    if (!(authored_radians > 0.0) || authored_radians >= pi ||
        !(adjusted_radians > 0.0) || adjusted_radians >= pi) {
        return 1.0;
    }

    const double restore = std::tan(adjusted_radians * 0.5) /
                           std::tan(authored_radians * 0.5);
    return (std::isfinite(restore) && restore > 0.0) ? restore : 1.0;
}

// The scene builder (func_8000A6C0) culls visibility-list entries against an
// authored forward view cone stored as a cosine threshold (kAuthoredViewConeCos,
// ~27.6 degree half-angle). The constant was authored for the N64 frustum:
// widening the projection without widening the cone leaves geometry outside it
// culled while already on screen -- periphery pop-in. Map the authored cone
// half-angle through the same tan-space scaling the horizontal frustum edge
// undergoes at fixed aspect (tan scales linearly with tan(vfov/2)), keeping the
// cull boundary in tan-space lockstep with the screen edge. This matches the
// authored margin exactly along the vertical axis and approximately on the
// diagonals. Narrowing the FOV symmetrically tightens the cone. Any invalid
// input passes the authored constant through untouched.
inline double lambo_view_cone_cos(double authored_cone_cos,
                                  double authored_fov_degrees,
                                  double adjusted_fov_degrees) {
    if (!std::isfinite(authored_cone_cos) || authored_cone_cos < -1.0 ||
        authored_cone_cos > 1.0 || !std::isfinite(authored_fov_degrees) ||
        !std::isfinite(adjusted_fov_degrees)) {
        return authored_cone_cos;
    }

    constexpr double pi = 3.14159265358979323846;
    const double authored_half = authored_fov_degrees * pi / 360.0;
    const double adjusted_half = adjusted_fov_degrees * pi / 360.0;
    if (!(authored_half > 0.0) || authored_half >= pi / 2.0 ||
        !(adjusted_half > 0.0) || adjusted_half >= pi / 2.0) {
        return authored_cone_cos;
    }
    if (adjusted_fov_degrees == authored_fov_degrees) {
        return authored_cone_cos;
    }

    const double cone_half = std::acos(authored_cone_cos);
    const double scale = std::tan(adjusted_half) / std::tan(authored_half);
    const double widened = std::cos(std::atan(std::tan(cone_half) * scale));
    // A near-180-degree projection must not push the threshold negative past
    // the game's own front-half-plane fallbacks; clamp to a hair above zero.
    return std::clamp(widened, 1.0e-4, 1.0);
}

extern "C" unsigned int lambo_camera_backdrop_projection_scale_bits();

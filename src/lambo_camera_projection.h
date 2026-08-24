#pragma once

#include <algorithm>
#include <cmath>

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

extern "C" unsigned int lambo_camera_backdrop_projection_scale_bits();

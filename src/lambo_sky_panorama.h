#pragma once

#include <algorithm>
#include <cmath>

// The USA sky emitter scrolls eight 128-unit tiles at z=-220. Its three
// original columns cover [-128, 256] before subtracting a phase in [0,128).
inline int lambo_sky_tile_index(int center, int column) {
    return ((center + column) % 8 + 8) % 8;
}

inline constexpr int kSkyEdgeExtent = 32512;

inline constexpr double kSkyRadiansPerUnit = 3.14159265358979323846 / 512.0;
inline constexpr int kSkySlicesPerTile = 16;
inline constexpr double kSkySliceWidth = 128.0 / kSkySlicesPerTile;

// A panorama coordinate is a bearing, not a position on a flat wall. Project
// that bearing onto the z=-220 plane; the guest matrix still subtracts phase.
// Clip in panorama space before calling this (tan is singular at +/-90 deg).
inline double lambo_sky_project_x(double panorama, double phase) {
    return 220.0 * std::tan((panorama - phase) * kSkyRadiansPerUnit) + phase;
}

inline double lambo_sky_panorama_at_x(double x, double phase) {
    return std::atan((x - phase) / 220.0) / kSkyRadiansPerUnit + phase;
}

inline int lambo_sky_column_radius(double vertical_fov, double aspect, bool two_player,
                                   double vertical_offset = 0.0) {
    // Cover window resizes while paused, when no new guest display list is built.
    // Larger live windows are covered too. Two-player views have half the height.
    aspect = std::max(8.0, aspect) * (two_player ? 2.0 : 1.0);
    const double half_height = 220.0 * std::tan(vertical_fov * 3.14159265358979323846 / 360.0);
    // A rotated viewport corner can reach the diagonal radius on either axis.
    // Include the guest's pitch translation for either matrix ordering.
    const double half_width = std::hypot(half_height * aspect, half_height) + std::abs(vertical_offset);
    // Keep the copied signed-16-bit vertices representable, including x=128
    // on the right edge. Invalid inputs fall back to conservative stock coverage.
    if (!std::isfinite(half_width) || half_width < 0.0) {
        return 32;
    }
    return static_cast<int>(std::clamp(std::ceil(half_width / 128.0) + 1.0, 2.0, 254.0));
}

// Zero when RT64 is unavailable (the diagnostic renderer uses the stock sky).
extern "C" float lambo_sky_target_aspect();
extern "C" float lambo_camera_sky_vertical_fov();
extern "C" void lambo_sky_extend_panorama(unsigned char* rdram);
extern "C" void lambo_sky_panorama_start(unsigned char* rdram);

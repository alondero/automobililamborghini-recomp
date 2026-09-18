#pragma once

#include <algorithm>
#include <cmath>

// The USA sky emitter scrolls eight 128-unit tiles at z=-220. Its three
// original columns cover [-128, 256] before subtracting a phase in [0,128).
inline int lambo_sky_tile_index(int center, int column) {
    return ((center + column) % 8 + 8) % 8;
}

inline int lambo_sky_column_radius(double vertical_fov, double aspect, bool two_player) {
    // Cover window resizes while paused, when no new guest display list is built.
    // Larger live windows are covered too. Two-player views have half the height.
    aspect = std::max(8.0, aspect) * (two_player ? 2.0 : 1.0);
    const double half_width = 220.0 * std::tan(vertical_fov * 3.14159265358979323846 / 360.0) * aspect;
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

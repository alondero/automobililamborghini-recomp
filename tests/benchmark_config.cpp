// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in CPU measurement, not a timing assertion or a game frame-time test.
#include <chrono>
#include <cstdlib>
#include <iostream>

#include "lambo_config.h"

namespace ultramodern::renderer {
void set_graphics_config(const GraphicsConfig&) {}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Pass an isolated graphics.json path.\n";
        return 1;
    }
#if defined(_WIN32)
    _putenv_s("LAMBO_GRAPHICS_CONFIG", argv[1]);
#else
    setenv("LAMBO_GRAPHICS_CONFIG", argv[1], 1);
#endif
    lambo::config::load_and_apply_graphics();
    double checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 200000; ++i) {
        for (int j = 0; j < 16; ++j) checksum += lambo::config::no_lod();
        checksum += lambo::config::draw_distance(i % 6);
        checksum += lambo::config::fog_scale(i % 6);
        checksum += lambo::config::camera_fov_add();
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    std::cout << "3800000 getter calls: "
              << std::chrono::duration<double, std::milli>(elapsed).count()
              << " ms; checksum=" << checksum << '\n';
    lambo::config::flush_pending_graphics_updates();
}

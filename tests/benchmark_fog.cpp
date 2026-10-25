// Opt-in CPU measurement. No game data, renderer or frame-time claim.
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>
#include "lambo_fog_widescreen.h"

namespace lambo::config {
bool widescreen_fog_match() { return false; }
double fog_scale(int) { return 1.0; }
}

int main() {
    std::vector<uint8_t> ram(0x800000);
    lambo::FogRewriter fog;
    fog.begin(ram.data());
    constexpr unsigned frames = 1000, commands = 20000;
    for (bool custom : {false, true}) {
        fog.reset(ram.data());
        if (custom) fog.command(ram.data(), 0x1000, 0xbc000008, 0x64009d00, {false, 0.5});
        const auto start = std::chrono::steady_clock::now();
        for (unsigned frame = 0; frame < frames; ++frame) {
            for (unsigned i = 0; i < commands; ++i) {
                fog.command(ram.data(), 0x100000 + i * 8, 0xbf000000, i, {false, custom ? 0.5 : 1.0});
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << (custom ? "custom" : "identity") << " fog: " << frames * commands
                  << " non-fog commands; elapsed_ms=" << ms << '\n';
    }
}

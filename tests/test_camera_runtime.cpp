#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "lambo_camera_projection.h"
#include "lambo_log.h"

extern "C" unsigned int lambo_camera_scale_bits(unsigned int, uint8_t*);
extern "C" unsigned int lambo_camera_height_bits(unsigned int, uint8_t*);
extern "C" unsigned int lambo_camera_fov_bits(unsigned int, uint8_t*);
extern "C" float lambo_camera_sky_vertical_fov();
extern "C" unsigned long long lambo_camera_view_cone_cos_bits();

// Only configuration and logging are faked; exercise the production shims.
namespace lambo::config {
double camera_distance_scale() { return 2.0; }
double camera_height_scale() { return 0.5; }
double camera_fov_add() { return 20.0; }
}
volatile int g_log_threshold = -1;
extern "C" void lambo_log_write(LamboLogLevel, const char*, const char*, ...) {}

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
unsigned int bits(float value) {
    unsigned int result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
}

int main() {
    std::vector<uint8_t> ram(8 * 1024 * 1024);
    auto half = [&](uint32_t address, int16_t value) {
        std::memcpy(ram.data() + ((address & 0x7fffff) ^ 2), &value, sizeof(value));
    };
    auto check = [&](bool racing) {
        expect(lambo_camera_scale_bits(bits(900.0f), ram.data()) ==
                   bits(racing ? 1800.0f : 900.0f), "distance follows current sequence");
        expect(lambo_camera_height_bits(bits(300.0f), ram.data()) ==
                   bits(racing ? 150.0f : 300.0f), "height follows current sequence");
        for (float fov : {20.0f, 32.0f, 40.0f, 52.0f}) {
            expect(lambo_camera_fov_bits(bits(fov), ram.data()) ==
                       bits(racing ? fov + 20.0f : fov), "FOV follows current sequence");
            expect(lambo_camera_sky_vertical_fov() == (racing ? fov + 20.0f : fov),
                   "sky FOV follows the current projection");
            expect((lambo_camera_backdrop_projection_scale_bits() == bits(1.0f)) == !racing,
                   "backdrop correction resets on leaving gameplay");
            expect((lambo_camera_view_cone_cos_bits() == 0x3FEC5A1CAC083127ull) == !racing,
                   "view cone resets bit-for-bit on leaving gameplay");
        }
    };
    // Repeat without resetting host state or touching settings: title/demo,
    // countdown, racing, pause, results, and back to intro.
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (int state = 0; state <= 8; ++state) {
            half(0x800CE6AC, state);
            for (int phase = 0; phase <= 5; ++phase) {
                half(0x800CE6B0, phase);
                for (int mode = 0; mode <= 4; ++mode) {
                    half(0x800CE6B4, mode);
                    check(state == 8 && phase == 3 && mode != 4);
                }
            }
        }
        half(0x800CE6AC, 8);
        half(0x800CE6B0, 3);
        half(0x800CE6B4, 0);
        half(0x800CE808, 1);
        check(true); // Pause retains race framing.
        half(0x800CE808, 0);
        half(0x800CE6AC, 0);
        check(false);
    }
    expect(lambo_camera_scale_bits(bits(900.0f), nullptr) == bits(900.0f),
           "missing guest RAM preserves distance");
    expect(lambo_camera_height_bits(bits(300.0f), nullptr) == bits(300.0f),
           "missing guest RAM preserves height");
    expect(lambo_camera_fov_bits(bits(40.0f), nullptr) == bits(40.0f),
           "missing guest RAM preserves FOV");
    std::cout << "camera runtime tests passed\n";
}

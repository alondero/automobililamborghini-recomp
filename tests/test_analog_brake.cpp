#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include "lambo_analog_brake.h"

// Host publication contract. Guest demand/ramp/fallback behavior is tested by
// test_vehicle_update_parity.cpp against the complete original ROM updater.
int main() {
    using namespace lambo::analog_brake;
    int failures = 0;
    auto expect = [&](bool ok, const char* message) {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    struct Sample { float value; uint16_t expected; };
    const Sample samples[] = {{-1.0f, 0}, {0.0f, 0}, {0.25f, 16384},
        {0.5f, 32768}, {0.75f, 49151}, {1.0f, 65535}, {2.0f, 65535},
        {std::numeric_limits<float>::quiet_NaN(), 0},
        {std::numeric_limits<float>::infinity(), 0},
        {-std::numeric_limits<float>::infinity(), 0}};
    set_probe(false);
    for (unsigned port = 0; port < kPortCount; ++port) {
        for (const auto test : samples) {
            publish(port, true, test.value);
            expect(guest_snapshot(port) == (0x80000000u | test.expected),
                   "guest snapshot preserves exact quantization and analog mode");
            float value = -1.0f;
            expect(sample(port, value) &&
                   std::abs(value - float(test.expected) / 65535.0f) < 1e-7f,
                   "native sample uses the same atomic snapshot");
        }
        publish(port, false, 1.0f);
        float value = -1.0f;
        expect(guest_snapshot(port) == 0 && !sample(port, value) && value == 0,
               "digital mode publishes an inactive neutral snapshot");
    }
    publish(0, true, 0.25f);
    publish(1, true, 0.75f);
    expect(guest_snapshot(0) == 0x80004000u && guest_snapshot(1) == 0x8000bfffu,
           "ports publish independently");
    const auto before = guest_snapshot(0);
    publish(kPortCount, true, 1.0f);
    float invalid = -1.0f;
    expect(guest_snapshot(kPortCount) == 0 && !sample(kPortCount, invalid) &&
           invalid == 0 && guest_snapshot(0) == before,
           "invalid ports neither read nor overwrite a valid port");
    set_probe(true);
    expect(guest_snapshot(0) == (before | 0x40000000u),
           "diagnostic bit preserves the exact input payload");
    expect(guest_snapshot(2) == 0x40000000u,
           "diagnostics remain available in digital mode");
    set_probe(false);
    return failures == 0 ? 0 : 1;
}

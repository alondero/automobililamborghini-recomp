#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "lambo_steering.h"

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_settings() {
    using namespace lambo::steering;
    const Settings stock{};
    expect(!stock.direct && stock.native_deadzone == kAuthoredDeadzone,
           "defaults are the authored response");
    expect(sanitize(stock) == stock, "defaults are inside every range");
    Settings wild{};
    wild.native_deadzone = 99;
    expect(sanitize(wild).native_deadzone == kMaxDeadzone, "native deadzone clamps high");
    wild.native_deadzone = -5;
    expect(sanitize(wild).native_deadzone == 0, "native deadzone clamps low");
}

void test_overrides() {
    using namespace lambo::steering;
    const Settings base{};
    const Settings set = with_overrides(base, "1", "0");
    expect(set.direct && set.native_deadzone == 0, "direct and deadzone overrides apply");
    const Settings partial = with_overrides(base, nullptr, "8");
    expect(!partial.direct && partial.native_deadzone == 8, "a partial override keeps the rest");
    Settings on = base;
    on.direct = true;
    expect(!with_overrides(on, "0", nullptr).direct, "0 forces direct off");
    expect(with_overrides(base, nullptr, "50").native_deadzone == kMaxDeadzone,
           "an out-of-range deadzone override is clamped");
    const Settings junk = with_overrides(base, "yes", "4x");
    expect(junk == base, "malformed overrides are ignored");
}

void test_diagnostic_index() {
    using namespace lambo::steering;
    expect(curve_index(0, 0) == -1,
           "diagnostics report no curve entry for centred input at deadzone 0");
    expect(curve_index(30, 4) == 26 && curve_index(-30, 0) == 30 && curve_index(4, 4) == 0 &&
           curve_index(80, 4) == 61, "curve index trims and caps like the ROM");
    expect(curve_index(3, 4) == -1 && curve_index(0, 4) == -1,
           "inside the deadzone there is no curve entry");
}

void test_publication() {
    using namespace lambo::steering;
    publish(Settings{true, 9});
    expect(current() == Settings{true, 9}, "published settings reach game-thread snapshots");
    publish(Settings{false, 99});
    expect(current() == Settings{false, kMaxDeadzone}, "publication clamps an invalid deadzone");
    publish(Settings{});
    expect(current() == Settings{}, "defaults can be restored live");
}

} // namespace

int main() {
    for (const char* name : {"LAMBO_STEERING_DIRECT", "LAMBO_STEERING_DEADZONE",
                             "LAMBO_STEERING_TRACE"}) {
        if (std::getenv(name) != nullptr) {
            std::cerr << "unset " << name << " before running this test\n";
            return 2;
        }
    }
    test_settings();
    test_overrides();
    test_diagnostic_index();
    test_publication();
    if (failures != 0) {
        std::cerr << failures << " steering test(s) failed\n";
        return 1;
    }
    std::cout << "steering tests passed\n";
    return 0;
}

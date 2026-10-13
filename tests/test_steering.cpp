#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "lambo_steering.h"
#include "lambo_vehicle.h"

namespace {

constexpr uint32_t kCurrentChannelAddr = 0x800CE6AAu;
constexpr uint32_t kSelectedPadPtrAddr = 0x800A39DCu;
constexpr uint32_t kPadBase = 0x800A39E0u;
constexpr uint32_t kSchemeBase = 0x800A3A0Eu;
constexpr uint32_t kWorkValueAddr = 0x80098694u;
constexpr uint32_t kCurveA = 0x80089174u;
constexpr uint32_t kCurveB = 0x800891F0u;

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

int16_t& halfword(std::vector<uint8_t>& rdram, uint32_t address) {
    return *reinterpret_cast<int16_t*>(&rdram[(address ^ 2u) & 0x7FFFFFu]);
}

int32_t& word(std::vector<uint8_t>& rdram, uint32_t address) {
    return *reinterpret_cast<int32_t*>(&rdram[address & 0x7FFFFFu]);
}

int8_t& byte(std::vector<uint8_t>& rdram, uint32_t address) {
    return *reinterpret_cast<int8_t*>(&rdram[(address ^ 3u) & 0x7FFFFFu]);
}

uint32_t accumulator_address(int vehicle) {
    return LAMBO_VEHICLE_BASE + static_cast<uint32_t>(vehicle) * sizeof(LamboVehicleRecord) +
           offsetof(LamboVehicleRecord, steering_accumulator);
}

// Player one's updater context plus distinguishable response curves:
// A[i] = 100 + i, B[i] = 200 + i.
void set_player_one(std::vector<uint8_t>& rdram, int vehicle, int selector) {
    halfword(rdram, kCurrentChannelAddr) = 1;
    word(rdram, kSelectedPadPtrAddr) = static_cast<int32_t>(kPadBase);
    halfword(rdram, kSchemeBase + 2) = 1;
    halfword(rdram, LAMBO_GUEST_CURRENT_VEHICLE_ADDR) = static_cast<int16_t>(vehicle);
    halfword(rdram, LAMBO_GUEST_RACE_DIFFICULTY_ADDR) = static_cast<int16_t>(selector);
    for (int i = 0; i <= lambo::steering::kCurveMaxIndex; ++i) {
        halfword(rdram, kCurveA + static_cast<uint32_t>(i) * 2u) = static_cast<int16_t>(100 + i);
        halfword(rdram, kCurveB + static_cast<uint32_t>(i) * 2u) = static_cast<int16_t>(200 + i);
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

void test_natives(std::vector<uint8_t>& rdram) {
    using namespace lambo::steering;
    bool identity = true;
    for (int value = -128; value <= 127; ++value)
        identity &= reshape_raw(value, kAuthoredDeadzone) == value;
    expect(identity, "the authored native deadzone keeps the raw register");
    expect(reshape_raw(3, 0) == 7 && reshape_raw(-3, 0) == -7,
           "deadzone 0 lifts small input past the ROM's -4 trim");
    expect(reshape_raw(0, 0) == 0 && curve_index(0, 0) == -1,
           "a centred stick never reaches the curve, even with deadzone 0");
    expect(reshape_raw(1, 0) == 5, "deadzone 0 starts the curve at the first unit");
    expect(reshape_raw(7, 8) == 0 && reshape_raw(-7, 8) == 0,
           "input inside a larger deadzone steers nothing");
    expect(reshape_raw(8, 8) == 4 && reshape_raw(10, 8) == 6 && reshape_raw(-10, 8) == -6,
           "a larger deadzone starts the curve exactly at its edge");
    expect(curve_index(30, 4) == 26 && curve_index(-30, 0) == 30 && curve_index(4, 4) == 0 &&
           curve_index(80, 4) == 61, "curve index trims and caps like the ROM");
    expect(curve_index(3, 4) == -1 && curve_index(0, 4) == -1,
           "inside the deadzone there is no curve entry");

    set_player_one(rdram, 2, 1);
    publish(Settings{});
    expect(lambo_steering_raw(rdram.data(), 3) == 3, "the stock raw hook is a no-op");
    Settings zero{};
    zero.native_deadzone = 0;
    publish(zero);
    expect(lambo_steering_raw(rdram.data(), 3) == 7, "the raw hook applies the native deadzone");
    halfword(rdram, kSchemeBase + 2) = 2;
    expect(lambo_steering_raw(rdram.data(), 3) == 3, "the curve-free scheme stays stock");
    halfword(rdram, kSchemeBase + 2) = 1;
    halfword(rdram, kCurrentChannelAddr) = 2;
    expect(lambo_steering_raw(rdram.data(), 3) == 3, "other players stay stock");
    set_player_one(rdram, 2, 1);

    // Direct mode off: the authored work value and accumulator are untouched.
    publish(Settings{});
    byte(rdram, kPadBase + 2) = 30;
    halfword(rdram, kWorkValueAddr) = 7;
    halfword(rdram, accumulator_address(2)) = 5;
    lambo_steering_direct(rdram.data());
    expect(halfword(rdram, kWorkValueAddr) == 7 && halfword(rdram, accumulator_address(2)) == 5,
           "stock direct hook leaves the authored chain alone");

    Settings direct{};
    direct.direct = true;
    publish(direct);
    lambo_steering_direct(rdram.data());
    expect(halfword(rdram, kWorkValueAddr) == 126, "direct mode reads curve A for selector 1");
    expect(halfword(rdram, accumulator_address(2)) == 126,
           "direct mode also overwrites the slew accumulator");
    byte(rdram, kPadBase + 2) = -30;
    lambo_steering_direct(rdram.data());
    expect(halfword(rdram, kWorkValueAddr) == -126 && halfword(rdram, accumulator_address(2)) == -126,
           "direct mode applies the stick sign");
    halfword(rdram, LAMBO_GUEST_RACE_DIFFICULTY_ADDR) = 0;
    byte(rdram, kPadBase + 2) = 4;
    lambo_steering_direct(rdram.data());
    expect(halfword(rdram, kWorkValueAddr) == 200, "selector 0 reads curve B at the trimmed index");
    byte(rdram, kPadBase + 2) = 0;
    lambo_steering_direct(rdram.data());
    expect(halfword(rdram, kWorkValueAddr) == 0 && halfword(rdram, accumulator_address(2)) == 0,
           "a centred stick steers nothing in direct mode");
    byte(rdram, kPadBase + 2) = -3;
    lambo_steering_direct(rdram.data());
    expect(halfword(rdram, kWorkValueAddr) == 0, "input inside the deadzone steers nothing");
    direct.native_deadzone = 0;
    publish(direct);
    byte(rdram, kPadBase + 2) = 80;
    lambo_steering_direct(rdram.data());
    expect(halfword(rdram, kWorkValueAddr) == 261, "direct mode caps the curve index at 61");
    byte(rdram, kPadBase + 2) = 10;
    lambo_steering_direct(rdram.data());
    expect(halfword(rdram, kWorkValueAddr) == 210, "direct mode honours the native deadzone");

    halfword(rdram, kWorkValueAddr) = 7;
    halfword(rdram, accumulator_address(2)) = 5;
    word(rdram, kSelectedPadPtrAddr) = static_cast<int32_t>(kPadBase + 6);
    lambo_steering_direct(rdram.data());
    expect(halfword(rdram, kWorkValueAddr) == 7, "a foreign pad stays stock");
    set_player_one(rdram, -1, 0);
    lambo_steering_direct(rdram.data());
    expect(halfword(rdram, kWorkValueAddr) == 7 && halfword(rdram, accumulator_address(2)) == 5,
           "the inactive-vehicle sentinel stays stock");
    publish(Settings{});
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
    std::vector<uint8_t> rdram(8 * 1024 * 1024);
    test_settings();
    test_overrides();
    test_natives(rdram);
    if (failures != 0) {
        std::cerr << failures << " steering test(s) failed\n";
        return 1;
    }
    std::cout << "steering tests passed\n";
    return 0;
}

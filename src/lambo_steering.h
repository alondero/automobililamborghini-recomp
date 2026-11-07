#ifndef LAMBO_STEERING_H
#define LAMBO_STEERING_H

#include <cstdint>

// Opt-in steering response for player one (docs/steering.md). Both settings act
// inside the compiled guest human steering updater (func_80019D20),
// and both defaults reproduce the authored game exactly:
//   - the native deadzone replaces the ROM's fixed 4-unit stick trim;
//   - direct mode replaces the slew-limited, speed-suppressed demand with the
//     selected response-curve entry for the current stick position.
namespace lambo::steering {

// The pad stick byte reaches +-80 at full deflection (N64_STICK_MAX in main.cpp).
constexpr int kStickMax = 80;
// The ROM subtracts this from |stick| before its curve lookup.
constexpr int kAuthoredDeadzone = 4;
// Largest native deadzone offered, in stick units.
constexpr int kMaxDeadzone = 20;
// Largest index the ROM uses in either response curve (it caps the trimmed value).
constexpr int kCurveMaxIndex = 61;

struct Settings {
    bool direct{false};                     // bypass the ROM's slew limit and speed suppression
    int native_deadzone{kAuthoredDeadzone}; // [0, kMaxDeadzone] stick units trimmed by the ROM
    bool operator==(const Settings&) const = default;
};

// Clamp every field into its documented range.
Settings sanitize(Settings settings);

// Environment overrides for headless runs (null = not set):
//   LAMBO_STEERING_DIRECT=1/0
//   LAMBO_STEERING_DEADZONE=<native deadzone>
// Malformed values are ignored. The result is sanitized.
Settings with_overrides(Settings settings, const char* direct, const char* deadzone);

// Diagnostic curve index for this raw stick and deadzone: -1 for a centred
// stick or inside the deadzone (no curve entry), else 0..61.
int curve_index(std::int32_t raw, int native_deadzone);

// Frontend (main thread) publication; the game thread reads current().
void publish(const Settings& settings);
// Published settings with the process environment overrides applied.
Settings current();

} // namespace lambo::steering

// Read-only diagnostic after the guest has applied steering. The authored
// demand and sampled settings are passed explicitly to preserve trace meaning.
extern "C" void lambo_steering_observe(std::uint8_t* rdram, std::int32_t authored,
                                      std::uint32_t settings);

#endif

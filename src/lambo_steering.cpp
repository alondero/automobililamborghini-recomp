#include "lambo_steering.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "recomp.h"
#include "lambo_vehicle.h"

// Guest seams (USA ROM, func_80019D20; see docs/steering.md):
// - 0x800CE6AA s16  current controller channel (1 = player one)
// - 0x800A39DC u32  selected pad pointer; player one is 0x800A39E0
// - 0x800A39E2 s8   port-zero pad stick X, +-80 at full deflection
// - 0x800A3A0E+ch*2 u16 per-channel scheme; 2 takes a separate -7/half path
//                       (0x8001B594) without the curve or accumulator
// - 0x80098694 s16  steering work value carried through the chain
// - 0x800CE79C s16  race difficulty: nonzero selects curve A, zero curve B
// - 0x80089174 / 0x800891F0  s16[62] response curves A (Expert) / B (Novice)
// - vehicle+0xB4 s16 slew accumulator; vehicle+0xB2 s16 steering demand
// All are game-thread state; the natives run inside the updater.

namespace {

constexpr gpr guest(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

constexpr std::uint32_t kCurrentChannelAddr = 0x800CE6AAu;
constexpr std::uint32_t kSelectedPadPtrAddr = 0x800A39DCu;
constexpr std::uint32_t kPortZeroPadAddr = 0x800A39E0u;
constexpr std::uint32_t kSchemeBaseAddr = 0x800A3A0Eu;
constexpr std::uint32_t kWorkValueAddr = 0x80098694u;
constexpr std::uint32_t kCurveAAddr = 0x80089174u;
constexpr std::uint32_t kCurveBAddr = 0x800891F0u;
constexpr std::uint32_t kAccumulatorOffset = offsetof(LamboVehicleRecord, steering_accumulator);
constexpr std::uint32_t kSpeedOffset = offsetof(LamboVehicleRecord, speed);
constexpr std::uint16_t kSchemeWithoutCurve = 2;

std::mutex g_settings_mutex;
lambo::steering::Settings g_published;

int sign_of(std::int32_t value) { return value < 0 ? -1 : 1; }

// The updater is reading player one's pad (channel 1, port-zero pad record).
bool player_one_pad(std::uint8_t* rdram) {
    return static_cast<std::int16_t>(MEM_H(0, guest(kCurrentChannelAddr))) == 1 &&
        static_cast<std::uint32_t>(MEM_W(0, guest(kSelectedPadPtrAddr))) == kPortZeroPadAddr;
}

// Player one's human update with the curve path active, or false to stay stock.
bool player_one_update(std::uint8_t* rdram, int& vehicle) {
    if (!player_one_pad(rdram)) return false;
    const std::uint32_t scheme_addr = kSchemeBaseAddr + 2u; // channel 1
    if (static_cast<std::uint16_t>(MEM_HU(0, guest(scheme_addr))) == kSchemeWithoutCurve)
        return false;
    vehicle = static_cast<std::int16_t>(MEM_H(0, guest(LAMBO_GUEST_CURRENT_VEHICLE_ADDR)));
    return vehicle >= 0; // -1 is the ROM's inactive-vehicle sentinel
}

std::uint32_t vehicle_field(int vehicle, std::uint32_t offset) {
    return LAMBO_VEHICLE_BASE + static_cast<std::uint32_t>(vehicle) *
        static_cast<std::uint32_t>(sizeof(LamboVehicleRecord)) + offset;
}

// Optional per-update CSV trace (LAMBO_STEERING_TRACE=<path>). Game thread only.
struct Trace {
    std::FILE* file{};
    bool checked{};
    unsigned long update{};
};
Trace g_trace;

void close_trace() {
    if (g_trace.file == nullptr) return;
    std::fflush(g_trace.file);
    std::fclose(g_trace.file);
    g_trace.file = nullptr;
}

std::FILE* trace_file() {
    if (!g_trace.checked) {
        g_trace.checked = true;
        const char* path = std::getenv("LAMBO_STEERING_TRACE");
        if (path != nullptr && path[0] != '\0') {
            g_trace.file = std::fopen(path, "w");
            if (g_trace.file == nullptr) {
                std::fprintf(stderr, "[steering] cannot open trace %s: %s\n", path,
                             std::strerror(errno));
            } else {
                std::fputs("update,raw,selector,index,curve,authored,demand,accumulator,speed,direct\n",
                           g_trace.file);
                std::atexit(close_trace);
            }
        }
    }
    return g_trace.file;
}

} // namespace

namespace lambo::steering {

Settings sanitize(Settings s) {
    s.native_deadzone = std::clamp(s.native_deadzone, 0, kMaxDeadzone);
    return s;
}

Settings with_overrides(Settings s, const char* direct, const char* deadzone) {
    if (direct != nullptr) {
        if (std::strcmp(direct, "1") == 0) s.direct = true;
        else if (std::strcmp(direct, "0") == 0) s.direct = false;
    }
    if (deadzone != nullptr) {
        char* end = nullptr;
        const long value = std::strtol(deadzone, &end, 10);
        if (end != deadzone && *end == '\0')
            s.native_deadzone = static_cast<int>(std::clamp(value, 0L, long(kMaxDeadzone)));
    }
    return sanitize(s);
}

// The ROM steers 0 while |raw| - 4 < 0 and uses curve[|raw| - 4] from there,
// so |raw| == 4 already reads curve[0] (1 in both tables). Shifting by the
// configured deadzone keeps that exact shape: 0 below it, curve[0] at it. A
// centred stick always steers 0, including with deadzone 0.
std::int32_t reshape_raw(std::int32_t raw, int native_deadzone) {
    if (native_deadzone == kAuthoredDeadzone) return raw;
    const std::int32_t magnitude = std::abs(raw);
    if (raw == 0 || magnitude < native_deadzone) return 0;
    return sign_of(raw) * (magnitude - native_deadzone + kAuthoredDeadzone);
}

int curve_index(std::int32_t raw, int native_deadzone) {
    const std::int32_t trimmed = std::abs(raw) - native_deadzone;
    return raw == 0 || trimmed < 0 ? -1 : std::min<std::int32_t>(trimmed, kCurveMaxIndex);
}

void publish(const Settings& settings) {
    std::lock_guard lock(g_settings_mutex);
    g_published = sanitize(settings);
}

Settings current() {
    Settings published;
    {
        std::lock_guard lock(g_settings_mutex);
        published = g_published;
    }
    static const char* const direct = std::getenv("LAMBO_STEERING_DIRECT");
    static const char* const deadzone = std::getenv("LAMBO_STEERING_DEADZONE");
    return with_overrides(published, direct, deadzone);
}

} // namespace lambo::steering

extern "C" std::int32_t lambo_steering_raw(std::uint8_t* rdram, std::int32_t raw) {
    const int deadzone = lambo::steering::current().native_deadzone;
    if (deadzone == lambo::steering::kAuthoredDeadzone) return raw;
    int vehicle = -1;
    if (!player_one_update(rdram, vehicle)) return raw;
    return lambo::steering::reshape_raw(raw, deadzone);
}

extern "C" void lambo_steering_direct(std::uint8_t* rdram) {
    using namespace lambo::steering;
    const Settings settings = current();
    std::FILE* trace = trace_file();
    if (!settings.direct && trace == nullptr) return;
    int vehicle = -1;
    if (!player_one_update(rdram, vehicle)) return;

    const std::int32_t raw = static_cast<std::int8_t>(MEM_B(2, guest(kPortZeroPadAddr)));
    const int selector = static_cast<std::int16_t>(MEM_H(0,
        guest(LAMBO_GUEST_RACE_DIFFICULTY_ADDR)));
    const int index = curve_index(raw, settings.native_deadzone);
    // Same selection as the ROM's beq on the difficulty word (0x8001AD10).
    const std::uint32_t table = selector != 0 ? kCurveAAddr : kCurveBAddr;
    const std::int32_t curve = index < 0 ? 0 : static_cast<std::int16_t>(MEM_H(0,
        guest(table + static_cast<std::uint32_t>(index) * 2u)));
    const std::int32_t authored = static_cast<std::int16_t>(MEM_H(0, guest(kWorkValueAddr)));
    std::int32_t demand = authored;
    if (settings.direct) {
        // Overwrite the accumulator too: the next update's slew clamp measures
        // from it, so direct response stays 1:1 instead of fighting the clamp.
        demand = raw < 0 ? -curve : curve;
        MEM_H(0, guest(kWorkValueAddr)) = static_cast<std::int16_t>(demand);
        MEM_H(0, guest(vehicle_field(vehicle, kAccumulatorOffset))) =
            static_cast<std::int16_t>(demand);
    }
    if (trace != nullptr) {
        const int accumulator = static_cast<std::int16_t>(MEM_H(0,
            guest(vehicle_field(vehicle, kAccumulatorOffset))));
        const long speed = static_cast<std::int32_t>(MEM_W(0,
            guest(vehicle_field(vehicle, kSpeedOffset))));
        std::fprintf(trace, "%lu,%d,%d,%d,%d,%d,%d,%d,%ld,%d\n", ++g_trace.update,
                     static_cast<int>(raw), selector, index, static_cast<int>(curve),
                     static_cast<int>(authored), static_cast<int>(demand), accumulator, speed,
                     settings.direct ? 1 : 0);
    }
}

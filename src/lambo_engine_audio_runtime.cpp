// SPDX-License-Identifier: GPL-3.0-or-later
#include "lambo_engine_audio.h"
#include "lambo_race_field.h"
#include "lambo_vehicle.h"
#include "recomp.h"
#include "lambo_log.h"
#include <algorithm>
#include <bit>
#include <cstdlib>

namespace {
using namespace lambo::engine_audio;
gpr guest(uint32_t address) { return gpr(int32_t(address)); }

// Recompiled jal does not maintain $ra, so the wrappers' saved return address
// is stale. Call sites are recognised by their arguments instead; see
// docs/engine-audio.md. The vehicle updater (jal 0x80026960 in func_8002622C)
// sends pitch+volume with constant volume 18000 and the pan sentinel.
constexpr int player_update_volume = 0x4650;
constexpr uint32_t nearby_engine_id = 0x3f;           // func_800030F8, jal 0x80002A10/0x80002B00
constexpr uint32_t nearby_vehicle_addr = 0x8009869C;  // guest/symbols.ld current_vehicle
constexpr uint32_t model_descriptors = 0x8013D490;    // 24 bytes; first halfword = category

// Reads one validated vehicle into the control, or marks it unavailable so
// the synth derives engine speed from the cartridge pitch.
void sample_vehicle(uint8_t* rdram, uint32_t index_addr, int expected_channel, Control& c) {
    c.rpm = 0;
    c.throttle = -1;
    const int index = int16_t(MEM_H(0, guest(index_addr)));
    if (index < 0 || index >= LAMBO_VEHICLE_CAPACITY) return;
    const uint32_t base = LAMBO_VEHICLE_BASE + uint32_t(index) * sizeof(LamboVehicleRecord);
    const int channel = int16_t(MEM_H(offsetof(LamboVehicleRecord, channel), guest(base)));
    if (expected_channel > 0 ? channel != expected_channel : channel < 5 || channel > 16) return;
    const int category = uint16_t(MEM_H(offsetof(LamboVehicleRecord, category), guest(base)));
    if (category >= int(profile_count)) return;
    c.profile = category;
    const int rpm = int16_t(MEM_H(0x9E, guest(base)));
    if (rpm > 0 && rpm < 12000) c.rpm = float(rpm);
    int limit = int16_t(MEM_H(0, guest(LAMBO_FIELD_AI + 0x44 + uint32_t(channel) * 0x84)));
    if (limit <= 0) limit = 100;
    const int demand = int16_t(MEM_H(offsetof(LamboVehicleRecord, throttle_demand), guest(base)));
    c.throttle = std::clamp(float(demand) / float(limit), 0.0f, 1.0f);
}
}

// USA sound-player seam. Called on the guest game thread only after the
// cartridge accepts a start/update. Stack offsets refer to its 0x18-byte frame;
// MEM_* preserves word-swizzled RDRAM. See docs/engine-audio.md for evidence.
extern "C" void lambo_engine_sound(uint8_t* rdram, recomp_context* ctx, int operation) {
    if (!enabled()) return;
    const int id = MEM_W(0x18, ctx->r29);
    if (id < 0x3e || id > 0x41) return;
    const size_t voice = id - 0x3e;
    if (operation == 2) { stop(voice); return; }
    const bool starting = operation == 0 || operation == 3;
    unsigned flags = starting ? set_pitch | set_gain | set_pan : MEM_BU(0x1f, ctx->r29);
    Control control;
    control.pitch = std::bit_cast<float>(uint32_t(MEM_W(starting ? 0x1c : 0x20, ctx->r29)));
    const int volume_offset = starting ? 0x22 : 0x26;
    // Immediate play sees volume already scaled by the wrapper. Delayed start
    // and update observe the raw argument before the ROM percentage multiply.
    control.gain = float(MEM_H(volume_offset, ctx->r29)) / 32767.0f
                 * (operation == 0 ? 1.0f : float(MEM_H(0, int32_t(0x800a2be2))) * 0.01f);
    control.pan = float(MEM_BU(starting ? 0x27 : 0x2b, ctx->r29)) / 127.0f;
    const int raw_volume = MEM_H(volume_offset, ctx->r29);
    const char* site = "other";
    if (operation == 1 && flags == (set_pitch | set_gain) && raw_volume == player_update_volume &&
        MEM_BU(0x2b, ctx->r29) == 0xff) {
        site = "player";
        sample_vehicle(rdram, LAMBO_GUEST_CURRENT_VEHICLE_ADDR, int(voice) + 1, control);
        flags |= set_vehicle;
    } else if (uint32_t(id) == nearby_engine_id && (operation == 0 ||
               (operation == 1 && flags == (set_pitch | set_gain | set_pan)))) {
        site = "nearby";
        sample_vehicle(rdram, nearby_vehicle_addr, 0, control);
        flags |= set_vehicle;
    } else if (operation == 3) {
        // The race start (jal 0x8000764C) is the only delayed engine play.
        site = "race-start";
        // Before the first race update, the player's selected model names the car.
        const int model = int16_t(MEM_H(0, guest(LAMBO_GUEST_PLAYER_MODEL_CURSOR_ADDR +
                                                 uint32_t(voice) * LAMBO_GUEST_PLAYER_MODEL_CURSOR_STRIDE)));
        const int category = model >= 0 && model < 24
            ? uint16_t(MEM_H(0, guest(model_descriptors + uint32_t(model) * 24))) : -1;
        if (category < int(profile_count)) control.profile = category;
    }
    if (starting) start(voice, control, operation == 3 ? float(MEM_W(0x2c, ctx->r29)) / 1000000 : 0);
    else publish(voice, flags, control);
    static const bool trace = std::getenv("LAMBO_ENGINE_AUDIO_TRACE") != nullptr;
    static unsigned updates[voice_count]{};
    if (trace && (starting || (++updates[voice] % 120 == 1))) {
        LAMBO_LOG("engine", "%s id=%02x site=%s flags=%u pitch=%.3f gain=%.3f pan=%.3f "
                  "rpm=%.0f throttle=%.2f car=%d pitch_rpm=%.0f\n",
                  starting ? "start" : "update", id, site, flags, control.pitch, control.gain,
                  control.pan, control.rpm, control.throttle, control.profile,
                  rpm_from_pitch(control.pitch, profile(control.profile).idle_rpm));
    }
    // Preserve allocation, playback status, pitch, pan and stop semantics. Only
    // the delayed/update amplitude is zeroed before its volume event posts.
    // Immediate play was already muted by lambo_engine_mute_volume.
    if ((flags & set_gain) && operation != 0) MEM_H(volume_offset, ctx->r29) = 0;
}

extern "C" void lambo_engine_sound_reset() { lambo::engine_audio::stop_all(); }

// This setter's only cartridge callers are start, delayed start and update.
// Before its prologue, sp still points at the caller's shared 0x18-byte frame.
// Mute the register passed to the volume event, keeping the scaled stack value
// available to the subsequent immediate-play observer. This keeps the public
// func_80066F84 wrapper hookable by the bundled weather mod.
extern "C" void lambo_engine_mute_volume(uint8_t* rdram, recomp_context* ctx) {
    if (!lambo::engine_audio::enabled()) return;
    const int id = MEM_W(0x18, ctx->r29);
    if (id >= 0x3e && id <= 0x41) ctx->r5 = 0;
}

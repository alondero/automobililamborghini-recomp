// SPDX-License-Identifier: GPL-3.0-or-later
#include "lambo_engine_audio.h"
#include "recomp.h"
#include "lambo_log.h"
#include <bit>
#include <cstdlib>

// USA sound-player seam. Called on the guest game thread only after the
// cartridge accepts a start/update. Stack offsets refer to its 0x18-byte frame;
// MEM_* preserves word-swizzled RDRAM. See docs/engine-audio.md for evidence.
extern "C" void lambo_engine_sound(uint8_t* rdram, recomp_context* ctx, int operation) {
    using namespace lambo::engine_audio;
    if (!enabled()) return;
    const int id = MEM_W(0x18, ctx->r29);
    if (id < 0x3e || id > 0x41) return;
    const size_t voice = id - 0x3e;
    if (operation == 2) { stop(voice); return; }
    const bool starting = operation == 0 || operation == 3;
    const unsigned flags = starting ? 7 : MEM_BU(0x1f, ctx->r29);
    Control control;
    control.pitch = std::bit_cast<float>(uint32_t(MEM_W(starting ? 0x1c : 0x20, ctx->r29)));
    const int volume_offset = starting ? 0x22 : 0x26;
    // Immediate play sees volume already scaled by the wrapper. Delayed start
    // and update observe the raw argument before the ROM percentage multiply.
    control.gain = float(MEM_H(volume_offset, ctx->r29)) / 32767.0f
                 * (operation == 0 ? 1.0f : float(MEM_H(0, int32_t(0x800a2be2))) * 0.01f);
    control.pan = float(MEM_BU(starting ? 0x27 : 0x2b, ctx->r29)) / 127.0f;
    if (starting) start(voice, control, operation == 3 ? float(MEM_W(0x2c, ctx->r29)) / 1000000 : 0);
    else publish(voice, flags, control);
    static const bool trace = std::getenv("LAMBO_ENGINE_AUDIO_TRACE") != nullptr;
    static unsigned updates = 0;
    if (trace && (starting || (++updates % 120 == 1))) {
        LAMBO_LOG("engine", "%s id=%02x flags=%u pitch=%.3f gain=%.3f pan=%.3f\n",
                  starting ? "start" : "update", id, flags, control.pitch, control.gain, control.pan);
    }
    // Preserve allocation, playback status, pitch, pan and stop semantics. Only
    // the delayed/update amplitude is zeroed before its volume event posts.
    // Immediate play was already muted by lambo_engine_mute_volume.
    if ((flags & 2) && operation != 0) MEM_H(volume_offset, ctx->r29) = 0;
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

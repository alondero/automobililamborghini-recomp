// SPDX-License-Identifier: GPL-3.0-or-later
#include "lambo_engine_audio.h"
#include "lambo_race_field.h"
#include "lambo_vehicle.h"
#include "recomp.h"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <vector>
extern "C" void lambo_engine_sound(uint8_t*, recomp_context*, int);
extern "C" void lambo_engine_sound_reset();
extern "C" void lambo_engine_mute_volume(uint8_t*, recomp_context*);
using namespace lambo::engine_audio;
static void require(bool ok, const char* why) {
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}
static gpr guest(uint32_t address) { return gpr(int32_t(address)); }
// Writes one vehicle record in the relocated pool plus its channel's throttle
// limit, using the same word-swizzled MEM_* layout as the cartridge.
static void vehicle(uint8_t* rdram, int index, int channel, int category, int rpm, int throttle) {
    const uint32_t base = LAMBO_VEHICLE_BASE + uint32_t(index) * sizeof(LamboVehicleRecord);
    MEM_H(offsetof(LamboVehicleRecord, channel), guest(base)) = int16_t(channel);
    MEM_H(offsetof(LamboVehicleRecord, category), guest(base)) = int16_t(category);
    MEM_H(0x9E, guest(base)) = int16_t(rpm);
    MEM_H(offsetof(LamboVehicleRecord, throttle_demand), guest(base)) = int16_t(throttle);
    MEM_H(0, guest(LAMBO_FIELD_AI + 0x44 + uint32_t(channel) * 0x84)) = 100;
}
int main() {
    std::vector<uint8_t> memory(16 * 1024 * 1024);
    uint8_t* rdram = memory.data(); recomp_context context{}, *ctx = &context;
    ctx->r29 = int32_t(0x80010000);
    MEM_H(0, int32_t(0x800a2be2)) = 50;
    MEM_W(0x18, ctx->r29) = 0x3e;
    MEM_W(0x1c, ctx->r29) = std::bit_cast<uint32_t>(0.6f);
    MEM_W(0x20, ctx->r29) = 9000;
    MEM_W(0x24, ctx->r29) = 64;
    ctx->r5 = 9000;
    lambo_engine_mute_volume(rdram, ctx);
    require(ctx->r5 == (enabled() ? 0u : 9000u), "low-level engine volume follows replacement switch");
    require(MEM_H(0x22, ctx->r29) == 9000, "muting must preserve the scaled stack argument");
    lambo_engine_sound(rdram, ctx, 0);
    if (!enabled()) {
        require(MEM_W(0x20, ctx->r29) == 9000, "original mode must not mute cartridge voice");
        std::vector<int16_t> pcm(1024, 42), original = pcm;
        mix(pcm.data(), pcm.size() / 2, 22050, 1);
        require(pcm == original, "original mode must preserve PCM");
        return 0;
    }
    require(MEM_H(0x22, ctx->r29) == 9000, "immediate observer must preserve scaled volume");
    require(MEM_W(0x1c, ctx->r29) == std::bit_cast<uint32_t>(0.6f), "preserve guest pitch");
    Controls expected; expected[0].pitch = 0.6f; expected[0].gain = 18000.0f / 32767 * 0.5f;
    expected[0].pan = 64.0f / 127;
    auto compare = [&](uint64_t generation, const char* why) {
        std::vector<int16_t> actual(44100), reference(44100); Synth synth;
        mix(actual.data(), 22050, 22050, generation);
        synth.mix(reference.data(), 22050, 22050, expected);
        require(actual == reference, why);
    };
    compare(1, "guest pitch/volume/pan must arrive as one correct snapshot");
    MEM_W(0x1c, ctx->r29) = 1; // pitch only; sentinel volume and pan are untouched.
    MEM_W(0x20, ctx->r29) = std::bit_cast<uint32_t>(1.2f);
    MEM_W(0x24, ctx->r29) = -1;
    MEM_W(0x28, ctx->r29) = 255;
    lambo_engine_sound(rdram, ctx, 1); expected[0].pitch = 1.2f;
    require(MEM_W(0x24, ctx->r29) == -1, "pitch-only updates must preserve other fields");
    compare(2, "pitch-only update from an unknown caller must keep vehicle state");

    // Player engine update from the vehicle updater (jal 0x80026960) has a
    // unique signature: pitch+volume flags, constant volume 18000, pan sentinel.
    // Recompiled calls do not maintain $ra, so the call site cannot be read.
    MEM_H(0, guest(LAMBO_GUEST_CURRENT_VEHICLE_ADDR)) = 3;
    vehicle(rdram, 3, 1, 6, 4200, 50);
    MEM_W(0x1c, ctx->r29) = 3;
    MEM_W(0x20, ctx->r29) = std::bit_cast<uint32_t>(0.1f + 4200 * 1.5f / 7020);
    MEM_W(0x24, ctx->r29) = 18000;
    const auto before_vehicle = MEM_W(0x9E, guest(LAMBO_VEHICLE_BASE + 3 * 0x10C));
    lambo_engine_sound(rdram, ctx, 1);
    expected[0].pitch = 0.1f + 4200 * 1.5f / 7020; expected[0].gain = 18000.0f / 32767 * 0.5f;
    expected[0].rpm = 4200; expected[0].throttle = 0.5f; expected[0].profile = 6;
    compare(3, "player update must publish vehicle engine speed, throttle and layout");
    require(MEM_W(0x9E, guest(LAMBO_VEHICLE_BASE + 3 * 0x10C)) == before_vehicle,
            "vehicle sampling must not write guest state");

    // A vehicle whose channel is not this voice's player is rejected; the
    // synth falls back to pitch-derived revs while keeping the car layout.
    vehicle(rdram, 3, 2, 6, 4200, 50);
    require(MEM_H(0x26, ctx->r29) == 0, "update must mute its cartridge amplitude");
    MEM_W(0x24, ctx->r29) = 18000; // The game passes fresh arguments per call.
    lambo_engine_sound(rdram, ctx, 1);
    expected[0].rpm = 0; expected[0].throttle = -1;
    compare(4, "mismatched player vehicle must fall back to pitch");
    vehicle(rdram, 3, 1, 4, 2000, 0);
    MEM_W(0x24, ctx->r29) = 9000; // flags 3 with another volume is not the vehicle updater
    lambo_engine_sound(rdram, ctx, 1);
    expected[0].gain = 9000.0f / 32767 * 0.5f;
    compare(41, "updates without the vehicle-updater signature keep the vehicle sample");

    // Nearby-car engine in one-player races (jal 0x80002B00) shares voice 0x3F
    // and identifies its AI vehicle through the scratch current vehicle.
    MEM_W(0x18, ctx->r29) = 0x3f;
    MEM_H(0, guest(0x8009869C)) = 7;
    vehicle(rdram, 7, 6, 2, 6000, 100);
    MEM_W(0x1c, ctx->r29) = 7;
    MEM_W(0x20, ctx->r29) = std::bit_cast<uint32_t>(1.1f);
    MEM_W(0x24, ctx->r29) = 12000;
    MEM_W(0x28, ctx->r29) = 30;
    lambo_engine_sound(rdram, ctx, 1);
    expected[1].pitch = 1.1f; expected[1].gain = 12000.0f / 32767 * 0.5f; expected[1].pan = 30.0f / 127;
    expected[1].rpm = 6000; expected[1].throttle = 1; expected[1].profile = 2;
    compare(5, "nearby-car update must sample its AI vehicle");
    MEM_H(0, guest(0x8009869C)) = 99;
    MEM_W(0x24, ctx->r29) = 12000;
    lambo_engine_sound(rdram, ctx, 1);
    expected[1].rpm = 0; expected[1].throttle = -1;
    compare(6, "out-of-range nearby vehicle index must fall back to pitch");

    MEM_W(0x18, ctx->r29) = 0x42;
    auto before = memory;
    lambo_engine_sound(rdram, ctx, 0);
    require(memory == before, "non-engine sounds must remain byte-identical");
    compare(7, "unrelated effects must not change engine controls");
    MEM_W(0x18, ctx->r29) = 0x3e;
    lambo_engine_sound(rdram, ctx, 2); expected[0].gain = 0; compare(8, "stop must silence its voice");
    MEM_W(0x18, ctx->r29) = 0x3f;
    lambo_engine_sound(rdram, ctx, 2); expected[1].gain = 0; compare(9, "stop must silence its voice");
    lambo_engine_sound_reset(); expected = {}; compare(10, "reset clears every voice");

    // Countdown uses the distinct delayed-play API (500000 microseconds). The
    // player's selected model gives the car layout before the race updates it.
    MEM_W(0x18, ctx->r29) = 0x3e;
    MEM_H(0, guest(LAMBO_GUEST_PLAYER_MODEL_CURSOR_ADDR)) = 18;
    MEM_H(0, guest(0x8013D490 + 18 * 24)) = 6;
    MEM_W(0x1c, ctx->r29) = std::bit_cast<uint32_t>(0.25f);
    MEM_W(0x20, ctx->r29) = 18000;
    MEM_W(0x24, ctx->r29) = 64;
    MEM_W(0x2c, ctx->r29) = 500000;
    lambo_engine_sound(rdram, ctx, 3);
    require(MEM_H(0x22, ctx->r29) == 0, "delayed start must also mute cartridge amplitude");
    std::vector<int16_t> delayed(48000 * 2);
    mix(delayed.data(), 48000, 48000, 11);
    require(std::all_of(delayed.begin(), delayed.begin() + 48000, [](auto s) { return s == 0; }),
            "scheduled engine must remain silent for half a second");
    require(std::any_of(delayed.begin() + 48000, delayed.end(), [](auto s) { return s != 0; }),
            "scheduled engine must sound after its delay");
    expected = {};
    expected[0].pitch = 0.25f; expected[0].gain = 18000.0f / 32767 * 0.5f; expected[0].pan = 64.0f / 127;
    expected[0].delay_seconds = 0.5f; expected[0].start_revision = 1; expected[0].profile = 6;
    compare(12, "delayed start must carry the selected car layout");
    lambo_engine_sound_reset(); expected = {}; compare(13, "reset after a delayed start");
    std::cout << "engine bridge tests passed\n";
}

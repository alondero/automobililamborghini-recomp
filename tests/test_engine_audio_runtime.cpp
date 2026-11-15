// SPDX-License-Identifier: GPL-3.0-or-later
#include "lambo_engine_audio.h"
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
int main() {
    std::vector<uint8_t> memory(8 * 1024 * 1024);
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
    Controls expected; expected[0] = {0.6f, 18000.0f / 32767 * 0.5f, 64.0f / 127};
    auto compare = [&](uint64_t generation) {
        std::vector<int16_t> actual(44100), reference(44100); Synth synth;
        mix(actual.data(), 22050, 22050, generation);
        synth.mix(reference.data(), 22050, 22050, expected);
        require(actual == reference, "guest pitch/volume/pan must arrive as one correct snapshot");
    };
    compare(1);
    MEM_W(0x1c, ctx->r29) = 1; // pitch only; sentinel volume and pan are untouched.
    MEM_W(0x20, ctx->r29) = std::bit_cast<uint32_t>(1.2f);
    MEM_W(0x24, ctx->r29) = -1;
    MEM_W(0x28, ctx->r29) = 255;
    lambo_engine_sound(rdram, ctx, 1); expected[0].pitch = 1.2f;
    require(MEM_W(0x24, ctx->r29) == -1, "pitch-only updates must preserve other fields");
    compare(2);
    MEM_W(0x18, ctx->r29) = 0x42;
    auto before = memory;
    lambo_engine_sound(rdram, ctx, 0);
    require(memory == before, "non-engine sounds must remain byte-identical");
    compare(3);
    MEM_W(0x18, ctx->r29) = 0x3e;
    lambo_engine_sound(rdram, ctx, 2); expected[0].gain = 0; compare(4);
    lambo_engine_sound_reset(); expected = {}; compare(5);
    // Countdown uses the distinct delayed-play API (500000 microseconds).
    MEM_W(0x1c, ctx->r29) = std::bit_cast<uint32_t>(0.25f);
    MEM_W(0x20, ctx->r29) = 18000;
    MEM_W(0x24, ctx->r29) = 64;
    MEM_W(0x2c, ctx->r29) = 500000;
    lambo_engine_sound(rdram, ctx, 3);
    require(MEM_H(0x22, ctx->r29) == 0, "delayed start must also mute cartridge amplitude");
    std::vector<int16_t> delayed(48000 * 2);
    mix(delayed.data(), 48000, 48000, 6);
    require(std::all_of(delayed.begin(), delayed.begin() + 48000, [](auto s) { return s == 0; }),
            "scheduled engine must remain silent for half a second");
    require(std::any_of(delayed.begin() + 48000, delayed.end(), [](auto s) { return s != 0; }),
            "scheduled engine must sound after its delay");
    lambo_engine_sound_reset(); expected = {}; compare(7);
    std::cout << "engine bridge tests passed\n";
}

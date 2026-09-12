#include "lambo_vi.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
int failures = 0;
bool black = false;
uint32_t framebuffer = 0;
uint32_t mode = 0;
int mode_calls = 0;
int black_calls = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

extern "C" void osViSwapBuffer(uint8_t*, int32_t value) { framebuffer = uint32_t(value); }
extern "C" void osViSetMode(uint8_t*, int32_t value) { mode = uint32_t(value); ++mode_calls; }
extern "C" void osViBlack(uint8_t active) { black = active != 0; ++black_calls; }

int main() {
    // Real word-swapped RDRAM layout: state is the upper half of the word at +0,
    // while retraceCount occupies the lower half. Replay the measured boot state.
    std::vector<uint32_t> memory(0x800000 / sizeof(uint32_t));
    auto word = [&](uint32_t address) -> uint32_t& { return memory[(address - 0x80000000u) / 4]; };
    auto* rdram = reinterpret_cast<uint8_t*>(memory.data());
    lambo::vi::promote_context(nullptr);
    lambo::vi::promote_context(rdram);
    expect(mode_calls == 0 && black_calls == 0, "uninitialized contexts do not reach native VI");

    constexpr uint32_t curr = 0x8008D140;
    constexpr uint32_t next = 0x8008D170;
    word(0x8008D1A0) = curr;
    word(0x8008D1A4) = next;
    word(next) = 0x00210001;
    word(next + 4) = 0x80000000;
    word(next + 8) = 0x8008C4A0;
    for (uint32_t off = 0x0C; off < 0x30; off += 4)
        word(next + off) = 0xA5A50000u | off;
    lambo::vi::promote_context(rdram);
    expect(black, "boot scanout is black while framebuffer points at program memory");
    expect(framebuffer == 0x80000000 && mode == 0x8008C4A0, "boot mode and buffer reach runtime");
    for (uint32_t off = 0; off < 0x30; off += 4)
        expect(word(curr + off) == word(next + off), "whole context is promoted for scheduler");

    // A corrupted private-context pointer near the end of RDRAM must be rejected
    // before the 0x30-byte promotion can overrun the host allocation.
    word(0x8008D1A0) = 0x807FFFF0;
    const int mode_calls_before_boundary = mode_calls;
    lambo::vi::promote_context(rdram);
    expect(mode_calls == mode_calls_before_boundary, "context promotion checks its full upper bound");
    word(0x8008D1A0) = curr;

    // The first valid framebuffer clears BLACK without changing the video mode.
    word(next) = 0x00110001;
    word(next + 4) = 0x8036A000;
    lambo::vi::promote_context(rdram);
    expect(!black && black_calls == 2, "first game frame unblanks without a mode change");
    expect(mode_calls == 1 && framebuffer == 0x8036A000, "ordinary swap preserves mode caching");

    word(next) = 0x00210001;
    lambo::vi::promote_context(rdram);
    expect(black, "later game-requested blanking is honored");
    word(next) = 0x00010020;
    lambo::vi::promote_context(rdram);
    expect(!black, "retraceCount bits must not be mistaken for the state halfword");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

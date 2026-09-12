#include "lambo_vi.h"

extern "C" void osViSwapBuffer(uint8_t* rdram, int32_t framebuffer);
extern "C" void osViSetMode(uint8_t* rdram, int32_t mode);
extern "C" void osViBlack(uint8_t active);

void lambo::vi::promote_context(uint8_t* rdram) {
    if (rdram == nullptr) return;
    auto gw = [&](uint32_t a) -> uint32_t { return *(uint32_t*)(rdram + (a - 0x80000000u)); };
    auto sw = [&](uint32_t a, uint32_t v) { *(uint32_t*)(rdram + (a - 0x80000000u)) = v; };
    auto in_rdram = [](uint32_t a) { return a >= 0x80000000u && a < 0x80800000u; };
    uint32_t curr = gw(0x8008D1A0); // __osViCurr
    uint32_t next = gw(0x8008D1A4); // __osViNext
    if (!in_rdram(curr) || !in_rdram(next)) return;

    // The game's scheduler reads these private libultra contexts, bypassing the
    // native VI API. Its task gate requires curr->buffer == next->buffer. Mirror
    // __osViSwapContext's retrace promotion so the gate reopens after each swap
    // (#58), preserving the whole context as observed in the ares reference.
    for (uint32_t off = 0; off < 0x30; off += 4) sw(curr + off, gw(next + off));

    // Forward the promoted state to ultramodern for RT64 scanout. Mode changes
    // reconfigure the display; framebuffer and blanking state can change without
    // a mode transition. This bridge also runs in headless harnesses.
    uint32_t modep = gw(next + 0x8);
    uint32_t buf = gw(next + 0x4);
    static uint32_t last_modep = 0;
    if (in_rdram(modep) && modep != last_modep) {
        osViSetMode(rdram, (int32_t)modep);
        last_modep = modep;
    }
    if (in_rdram(buf)) osViSwapBuffer(rdram, (int32_t)buf);
    // __osViInit starts with VI_STATE_BLACK and framep=K0BASE. Omitting BLACK
    // scans program memory out as colored noise until the first framebuffer.
    // RDRAM is word-swapped: the u16 state at +0 is the word's upper half.
    constexpr uint32_t vi_state_black = 0x20;
    osViBlack(((gw(next) >> 16) & vi_state_black) != 0);
}

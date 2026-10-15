// Behavioral checks must also run in Release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../src/lambo_hud_texture_bounds.cpp"

int main() {
    std::vector<uint8_t> ram(0x800000);
    auto cmd = [&](uint32_t off, uint32_t a, uint32_t b) {
        std::memcpy(ram.data()+off, &a, 4);
        std::memcpy(ram.data()+off+4, &b, 4);
    };
    auto value = [&](uint32_t off) {
        uint32_t v; std::memcpy(&v, ram.data()+off, 4); return v;
    };
    // Source state crosses a nested, segmented call, then a tail branch.
    cmd(0x100, 0xBC000406, 0x1000);
    cmd(0x108, 0xFD680000, 0x80124020);
    cmd(0x110, 0x06000000, 0x01000000);
    cmd(0x118, 0xB8000000, 0);
    cmd(0x1000, 0xF5600A00, 0x0005C170);
    cmd(0x1008, 0x06010000, 0x80002000);
    cmd(0x2000, 0xF2000000, 0x0013C13C);
    cmd(0x2008, 0xB8000000, 0);
    lambo_hud_texture_bounds(ram.data(), 0x80000100);
    assert(value(0x1004) == 0x000DC370);
    lambo_hud_texture_bounds(ram.data(), 0x80000100);
    assert(value(0x1004) == 0x000DC370); // idempotent for static lists
    // Identical geometry with an unrelated source is never patched.
    cmd(0x108, 0xFD680000, 0x80124028);
    cmd(0x1000, 0xF5600A00, 0x0005C170);
    lambo_hud_texture_bounds(ram.data(), 0x80000100);
    assert(value(0x1004) == 0x0005C170);
    cmd(0x108, 0xFD500000, 0x80165028);
    cmd(0x1000, 0xF5400C00, 0x00050170);
    cmd(0x2000, 0xF2000000, 0x0017C01C);
    lambo_hud_texture_bounds(ram.data(), 0x80000100);
    assert(value(0x1004) == 0x000D0370);
    // Malformed targets and cycles terminate without leaving RDRAM.
    lambo_hud_texture_bounds(ram.data(), 0x807FFFFC);
    cmd(0x100, 0x06010000, 0x80000100);
    lambo_hud_texture_bounds(ram.data(), 0x80000100);
}

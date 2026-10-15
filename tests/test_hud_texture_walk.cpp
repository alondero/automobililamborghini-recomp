// Synthetic word-swapped RAM exercises the actual producer hook, without a ROM.
// Assertions must remain live in Release.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <vector>
#include "lambo_f3dex.h"
#include "lambo_log.h"

extern "C" void lambo_graphics_task_prepare(uint8_t*, uint32_t);
namespace {
bool match_fog = false;
double fog_scale = 1.0;
unsigned warnings = 0;
}
namespace lambo::config {
bool widescreen_fog_match() { return match_fog; }
double fog_scale(int) { return ::fog_scale; }
}
volatile int g_log_threshold = LAMBO_LEVEL_WARN;
extern "C" void lambo_log_write(LamboLogLevel level, const char*, const char*, ...) {
    if (level == LAMBO_LEVEL_WARN) ++warnings;
}

int main() {
    using lambo::f3dex::read;
    using lambo::f3dex::write;
    std::vector<uint8_t> memory(0x800000);
    auto* ram = memory.data();
    constexpr uint32_t task = 0xbf1e8, root = 0xbf400;
    auto cmd = [&](uint32_t off, uint32_t a, uint32_t b) {
        write(ram, off, a); write(ram, off + 4, b);
    };
    auto prepare = [&] { lambo_graphics_task_prepare(ram, task | 0x80000000); };
    auto init = [&] {
        std::memset(ram, 0, 0x800000);
        write(ram, task, 1);
        write(ram, task + 0x10, 0x80086b60);
        write(ram, task + 0x18, 0x8008f210);
        write(ram, task + 0x1c, 0x800);
        write(ram, task + 0x30, root);
    };
    struct Tile { uint32_t source, w0, w1, bounds; };
    const Tile tiles[] = {
        {0x160140, 0xf5181400, 0x0000c060, 0x0009c014},
        {0x160508, 0xf5180800, 0x00018040, 0x0003c0e0},
        {0x121a88, 0xf5600a00, 0x0001c070, 0x0013c13c},
        {0x122710, 0xf5600a00, 0x0001c070, 0x0013c13c},
        {0x123398, 0xf5600a00, 0x0001c070, 0x0013c13c},
        {0x124020, 0xf5600a00, 0x0001c070, 0x0013c13c},
        {0x124ca8, 0xf5600a00, 0x0001c070, 0x0013c13c},
        {0x125930, 0xf5600a00, 0x0001c070, 0x0013c13c},
        {0x165028, 0xf5400c00, 0x00010070, 0x0017c01c},
    };
    auto tile_list = [&](uint32_t at, Tile t) {
        cmd(at, 0xfd000000, t.source | 0x80000000);
        cmd(at + 8, 0xf5180000, 0x07000000); // load tile stays untouched
        cmd(at + 16, t.w0, t.w1);
        cmd(at + 24, 0xf2000000, t.bounds);
        cmd(at + 32, 0xb8000000, 0);
    };
    // All HUD contracts are reachable from the submitted root, without any
    // widescreen bracket. Calls/branches share source and render-tile state.
    for (auto tile : tiles) {
        for (unsigned mirror : {0u, 0x00040100u}) {
            if (tile.source < 0x161000 && tile.source > 0x160000 && mirror) continue;
            init();
            tile.w1 |= mirror;
            tile_list(root, tile);
            const auto original = memory;
            prepare();
            assert(read(ram, root + 12) == 0x07000000);
            assert(read(ram, root + 20) == (tile.w1 | 0x80200));
            auto expected = original;
            write(expected.data(), root + 20, tile.w1 | 0x80200);
            assert(memory == expected); // only the clamp bits changed
            prepare();
            assert(memory == expected); // static lists are idempotent
        }
    }
    // The alternating second task slot follows the same producer path.
    init();
    constexpr uint32_t second_task = task + 0x7a50, second_root = root + 0x7a50;
    for (unsigned offset = 0; offset < 0x40; offset += 4)
        write(ram, second_task + offset, read(ram, task + offset));
    write(ram, second_task + 0x30, second_root);
    tile_list(second_root, tiles[0]);
    lambo_graphics_task_prepare(ram, second_task | 0x80000000);
    assert(read(ram, second_root + 20) == (tiles[0].w1 | 0x80200));

    // Near matches must not patch an unrelated source, stride, mask, tile or size.
    for (unsigned field = 0; field < 5; ++field) {
        init();
        auto tile = tiles[0];
        if (field == 0) tile.source += 4;
        if (field == 1) tile.w0 ^= 0x200;
        if (field == 2) tile.w1 ^= 0x10;
        if (field == 3) tile.w1 |= 0x01000000;
        if (field == 4) tile.bounds += 4;
        tile_list(root, tile);
        const auto original = memory;
        prepare(); assert(memory == original);
    }
    init();
    cmd(root, 0xbc000406, 0x1000);
    cmd(root + 8, 0xfd680000, 0x80124020);
    cmd(root + 16, 0x06020000, 0x01000000); // bit 17 is still a push
    cmd(root + 24, 0xf2000000, 0x0013c13c);
    cmd(root + 32, 0xb8000000, 0);
    cmd(0x1000, 0x64000006, 0); // rect-align payload resembles ENDDL
    cmd(0x1008, 0xb8000000, 0);
    cmd(0x1010, 0xf5600a00, 0x0005c170);
    cmd(0x1018, 0x06010000, 0x80002000); // tail branch
    cmd(0x2000, 0xb8000000, 0);
    prepare();
    assert(read(ram, 0x1014) == 0x000dc370);

    // Fog and HUD use this same walk, with player/circuit values sampled here.
    init(); match_fog = true;
    write(ram, 0xce6a4, 4u << 16);
    cmd(root, 0x06000000, 0x80001000);
    tile_list(root + 8, tiles[0]);
    cmd(0x1000, 0xbc000008, 0x12345678);
    cmd(0x1008, 0xf8000000, 0xabcdef12);
    cmd(0x1010, 0xb8000000, 0);
    prepare();
    assert(read(ram, 0x1004) == 0x64009d00);
    assert(read(ram, 0x100c) == 0x39303712);
    assert(read(ram, root + 28) == (tiles[0].w1 | 0x80200));
    match_fog = false; ::fog_scale = 0.5;
    cmd(0x1000, 0xbc000008, 0x64009d00);
    prepare(); assert(read(ram, 0x1004) == 0x3200ce80);
    ::fog_scale = 1.0;

    // The native builder does not author ucode_size; it cannot gate identity.
    init(); tile_list(root, tiles[0]);
    write(ram, task + 0x14, 0xdeadbeef);
    prepare(); assert(read(ram, root + 20) == (tiles[0].w1 | 0x80200));

    // Unknown tasks/microcodes/root pointers fail closed before touching lists.
    for (auto offset : {0u, 0x10u, 0x18u, 0x1cu, 0x30u}) {
        init(); tile_list(root, tiles[0]);
        write(ram, task + offset, read(ram, task + offset) + 4);
        const auto original = memory;
        prepare(); assert(memory == original);
    }
    init(); tile_list(root, tiles[0]);
    const auto original = memory;
    lambo_graphics_task_prepare(ram, 0x80000100);
    lambo_graphics_task_prepare(nullptr, task);
    assert(memory == original);

    // Depth 16 is accepted; depth 17 stops the whole walk and emits a warning.
    for (unsigned depth : {16u, 17u}) {
        init();
        uint32_t at = root;
        for (unsigned d = 0; d < depth; ++d) {
            const auto next = 0x1000 + d * 0x40;
            cmd(at, 0x06000000, next | 0x80000000);
            cmd(at + 8, 0xb8000000, 0);
            at = next;
        }
        tile_list(at, tiles[0]);
        warnings = 0; prepare();
        assert(read(ram, at + 20) == (tiles[0].w1 | (depth == 16 ? 0x80200 : 0)));
        assert(warnings == (depth == 17 ? 1u : 0u));
    }
    init(); cmd(root, 0x06010000, root | 0x80000000);
    warnings = 0; prepare(); assert(warnings == 1);
    const auto cycle = lambo::f3dex::walk(ram, root | 0x80000000, [](auto, auto, auto) {});
    assert(cycle.budget_limit && !cycle.depth_limit && cycle.commands == 200000);
    // Invalid and unaligned pointers are rejected without reading RAM.
    for (auto address : {0x807ffffcu, 0x80800000u, 0x10000000u}) {
        const auto result = lambo::f3dex::walk(ram, address, [](auto, auto, auto) {});
        assert(result.commands == 0);
    }
}

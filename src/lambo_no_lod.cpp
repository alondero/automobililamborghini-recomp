// #87/#91: remove the ROM's per-mode LOD reductions. The scene builder
// func_8000A6C0 draws each track segment as up to three sub-DLs from the
// segment record (+0x4 road, +0x8 walls, +0xC far scenery) but emits the
// scenery layer only when the player count at 0x800CE6A4 is < 2
// (`slti $at, players, 2` / `beq $at, $zero` at 0x8000CFA0/0x8000CFA4 in the
// segment loop and 0x8000D834/0x8000D838 for the camera's own segment), so
// 2P-4P races lose the distant canyon walls entirely -- seen as "short draw
// distance" and pop-in. [[patches.hook]]s before each beq route $at through
// here: returning 1 makes every mode take the branch the way 1P takes it.
// The emit still self-gates on the record's scenery pointer being non-null,
// and the scenery DLs are streamed in all modes (verified from a 3P save
// state: record+0xC pointers populated), so no geometry is synthesised here.

#include <cstdint>
#include <cstring>

#include "recomp.h"

#include "lambo_config.h"

extern "C" uint32_t lambo_no_lod_scenery_guard(uint8_t* rdram, uint32_t at) {
    (void)rdram;
    return lambo::config::no_lod() ? 1u : at;
}

// Distance pop-in (the other half of the same builder): each entry of a segment's
// 10-slot visibility list is culled against a per-circuit, per-player-count radius
// from a float[6][5] table at 0x80088FD0 (coarse test 0x8000D370, fine 16-sub-point
// test 0x8000D568). The radii are N64 fill-rate budgets -- the city circuits are
// authored shortest (35000 vs 55000 on circuit 1), so whole blocks pop in at the
// radius edge. Hooked per frame on the world-draw path (0x8000CD3C, before the first
// table read) rather than once at load because a savestate restore brings the ROM
// values back. The radii are scaled by the draw_distance config (0 = unlimited) from
// a compiled-in copy of the authored table: the live table can't be trusted as the
// baseline because this hook rewrites it every frame and a savestate captured while
// it ran would restore the rewritten values. The forward-cone/half-plane tests and
// the per-frame visibility walk still decide what is drawn.
extern "C" void lambo_no_lod_draw_distance(uint8_t* rdram) {
    if (!lambo::config::no_lod()) {
        return;
    }
    // ROM copy of the float[6][5] at 0x80088FD0 ([circuit][player-count column]),
    // extracted from the .z64 at 0x89BD0 (= vram - 0x80000000 + 0xC00).
    static constexpr float kAuthored[6][5] = {
        {55000.0f, 55000.0f, 50000.0f, 25000.0f, 25000.0f},
        {50000.0f, 50000.0f, 40000.0f, 20000.0f, 20000.0f},
        {40000.0f, 40000.0f, 30000.0f, 20000.0f, 20000.0f},
        {45000.0f, 45000.0f, 30000.0f, 25000.0f, 25000.0f},
        {35000.0f, 35000.0f, 27500.0f, 25000.0f, 25000.0f},
        {35000.0f, 35000.0f, 27500.0f, 25000.0f, 25000.0f},
    };
    constexpr uint32_t kTableAddr = 0x80088FD0u;
    constexpr float kUnlimited = 1e9f;  // beyond any on-track distance
    for (int c = 0; c < 6; c++) {
        const double scale = lambo::config::draw_distance(c);
        for (int p = 0; p < 5; p++) {
            float r = scale <= 0.0 ? kUnlimited : (float)(kAuthored[c][p] * scale);
            if (r > kUnlimited) r = kUnlimited;
            int32_t bits;
            std::memcpy(&bits, &r, sizeof(bits));
            MEM_W((c * 5 + p) * 4, (gpr)(int32_t)kTableAddr) = bits;
        }
    }
}

// Full-track draw (the last pop-in axis): even with the radii lifted, the builder only
// ever draws segments listed in the camera segment's authored PVS row -- 10 fixed slots
// with -1 holes -- and the city circuits' rows are trimmed hard for N64 fill rate (the
// row can omit a parallel street 1k units away, which then pops in on the next segment
// boundary). All segment sub-DLs are resident for the whole race, so the walk itself is
// the only limit. Two hooks bend the existing loop: the row-entry fetch (0x8000D058)
// is overridden with entries from a synthesized all-segments row, and the 10-iteration
// cap (0x8000D904) is widened to its length. Authored entries come first so the
// per-frame drawn-segment list keeps its stock prefix; the per-frame cone tests still
// run per entry, so this changes reach, not view culling.

namespace {

constexpr gpr kHdrPtrAddr  = (gpr)(int32_t)0x80098238u;  // track asset header pointer
constexpr gpr kCamSegAddr  = (gpr)(int32_t)0x800BF1CCu;  // camera segment (this viewport)
constexpr gpr kPvsPtrAddr  = (gpr)(int32_t)0x800CE678u;  // PVS base (header+0x4 copy)
constexpr gpr kRecListAddr = (gpr)(int32_t)0x800BF1D0u;  // 64-byte segment records (viewport)

int16_t s_synth_row[256];
int s_synth_n = -1;  // -1 = passthrough (feature off or sanity check failed)

bool valid_guest_ptr(int32_t p) {
    uint32_t u = (uint32_t)p;
    return u >= 0x80000000u && u < 0x80800000u;
}

// Rebuild the synthesized row for the viewport walk that is starting. The segment
// count is not stored anywhere by the game -- it is (header+0x8 - header+0x4) / 20,
// the size of the PVS block itself (verified exact on circuit 5: 1100/20 = 55 rows,
// with a null 56th record as sentinel). Any sanity failure disables the override for
// this walk and the authored row is used untouched.
void build_synth_row(uint8_t* rdram) {
    s_synth_n = -1;
    int32_t hdr = MEM_W(0, kHdrPtrAddr);
    int32_t pvs = MEM_W(0, kPvsPtrAddr);
    int32_t recs = MEM_W(0, kRecListAddr);
    if (!valid_guest_ptr(hdr) || !valid_guest_ptr(pvs) || !valid_guest_ptr(recs)) return;
    if (MEM_W(0x4, (gpr)hdr) != pvs) return;  // header and live PVS ptr disagree
    int32_t pvs_end = MEM_W(0x8, (gpr)hdr);
    if (!valid_guest_ptr(pvs_end) || pvs_end <= pvs) return;
    int32_t bytes = pvs_end - pvs;
    if (bytes % 20 != 0) return;
    int n = bytes / 20;
    if (n < 2 || n > 200) return;
    int cam = MEM_H(0, kCamSegAddr);
    if (cam < 0 || cam >= n) return;

    bool listed[256] = {};
    int out = 0;
    listed[cam] = true;  // the camera's own segment is drawn by its dedicated path
    for (int i = 0; i < 10; i++) {  // authored row first: stock drawn-list prefix
        int e = MEM_H(cam * 20 + i * 2, (gpr)pvs);
        if (e < 0 || e >= n || listed[e]) continue;
        listed[e] = true;
        s_synth_row[out++] = (int16_t)e;
    }
    for (int s = 0; s < n; s++) {
        if (listed[s]) continue;
        // Skip records with no road sub-DL (defends against sentinel/padding rows).
        if (!valid_guest_ptr(MEM_W(s * 64 + 0x4, (gpr)recs))) continue;
        s_synth_row[out++] = (int16_t)s;
    }
    s_synth_n = out;
}

}  // namespace

// Hooked after the row-entry fetch (before 0x8000D05C): replaces the authored entry
// with the synthesized row's. idx is the loop counter the fetch used ($t4).
extern "C" uint32_t lambo_no_lod_pvs_entry(uint8_t* rdram, uint32_t orig, uint32_t idx) {
    if (!lambo::config::no_lod()) {
        s_synth_n = -1;
        return orig;
    }
    int i = (int32_t)idx;
    if (i == 0) {
        build_synth_row(rdram);
    }
    if (s_synth_n < 0) return orig;
    if (i < 0 || i >= s_synth_n) return (uint32_t)(int32_t)-1;
    return (uint32_t)(int32_t)s_synth_row[i];
}

// Hooked over the loop-cap test result (before 0x8000D908): keep looping until the
// synthesized row is exhausted instead of stopping at 10. next is i+1 ($t4).
extern "C" uint32_t lambo_no_lod_pvs_more(uint8_t* rdram, uint32_t orig, uint32_t next) {
    (void)rdram;
    if (!lambo::config::no_lod() || s_synth_n < 0) return orig;
    return (int32_t)next < s_synth_n ? 1u : 0u;
}

// Hooked over the drawn-segment-list index (before 0x8000D8D0 / 0x8000D920): the
// per-frame list at 0x800B6758 has 21 slots (0x800B6782 is the next global) and the
// stock walk writes at most 12; the widened walk must not scribble past it. Excess
// appends collapse onto the last slot, which the -1 terminator then overwrites, so
// list consumers (they scan to the terminator) see the stock-shaped prefix.
extern "C" uint32_t lambo_no_lod_seg_list_clamp(uint8_t* rdram, uint32_t count) {
    (void)rdram;
    if (!lambo::config::no_lod()) return count;
    return (int32_t)count > 20 ? 20u : count;
}

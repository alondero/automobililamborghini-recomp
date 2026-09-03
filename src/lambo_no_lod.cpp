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
#include "lambo_no_lod_policy.h"

extern "C" unsigned long long lambo_camera_view_cone_cos_bits();

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
// it ran would restore the rewritten values. The half-plane tests and the
// per-frame visibility walk still decide what is drawn; the forward-cone constant
// is rewritten in this hook so it tracks camera_fov_add (see the cone block below).
extern "C" void lambo_no_lod_draw_distance(uint8_t* rdram) {
    // ROM copy of the float[6][5] at 0x80088FD0 ([circuit][player-count column]),
    // extracted from the .z64 at 0x89BD0 (= vram - 0x80000000 + 0xC00).
    // Basics: index 0-2 (1P radii 55000/50000/40000), pros 3-5 (45000/35000/35000).
    // Index 4 is the city track (second pro) — the one we routinely tune down via
    // draw_distance_circuit[4] in graphics.json. See docs/TRACK_INDEX.md for the
    // full 0-based/1-based/F-key mapping.
    static constexpr float kAuthored[6][5] = {
        {55000.0f, 55000.0f, 50000.0f, 25000.0f, 25000.0f},  // circuit 1 (basic)
        {50000.0f, 50000.0f, 40000.0f, 20000.0f, 20000.0f},  // circuit 2 (basic)
        {40000.0f, 40000.0f, 30000.0f, 20000.0f, 20000.0f},  // circuit 3 (basic)
        {45000.0f, 45000.0f, 30000.0f, 25000.0f, 25000.0f},  // circuit 4 (pro 1)
        {35000.0f, 35000.0f, 27500.0f, 25000.0f, 25000.0f},  // circuit 5 (pro 2, city)
        {35000.0f, 35000.0f, 27500.0f, 25000.0f, 25000.0f},  // circuit 6 (pro 3)
    };
    constexpr uint32_t kTableAddr = 0x80088FD0u;
    constexpr float kUnlimited = 1e9f;  // beyond any on-track distance
    const bool enabled = lambo::config::no_lod();
    for (int c = 0; c < 6; c++) {
        // This hook runs every frame, so switching the enhancement off must also
        // put the ROM-authored radii back. An early return would leave the most
        // recently expanded table live until the next track load.
        const double scale = enabled ? lambo::config::draw_distance(c) : 1.0;
        for (int p = 0; p < 5; p++) {
            float r = scale <= 0.0 ? kUnlimited : (float)(kAuthored[c][p] * scale);
            if (r > kUnlimited) r = kUnlimited;
            int32_t bits;
            std::memcpy(&bits, &r, sizeof(bits));
            MEM_W((c * 5 + p) * 4, (gpr)(int32_t)kTableAddr) = bits;
        }
    }

    // Forward-view-cone rewrite (the FOV-boost pop-in axis): both cull paths above
    // are AND-ed with a forward-cone cosine stored as a ROM double at 0x8008D8C0
    // (coarse, read at 0x8000D38C) and 0x8008D8C8 (fine sub-point test, read at
    // 0x8000D588) -- authored 0.886, a ~27.6-degree half-angle matched to the N64
    // frustum. These two loads are the constants' only readers in the ROM. With
    // camera_fov_add the rendered frustum widens past the cone, so peripheral
    // segments stay culled until they cross the authored boundary -- visible
    // pop-in at the screen edges regardless of draw distance. The bits are
    // computed from each guPerspective call's authored/adjusted FOV pair
    // (lambo_camera.cpp) and rewritten here per frame, same savestate rationale
    // as the radii: a restore brings the ROM doubles back.
    //
    // Deliberately NOT gated on no_lod(): the cone is an FOV-frustum coupling,
    // not an LOD reduction -- it must track camera_fov_add even for users who
    // keep the ROM's authored radii and PVS rows. At camera_fov_add == 0 the
    // stored bits are the exact ROM double, so this is a byte-stable no-op then.
    {
        const unsigned long long dbits = lambo_camera_view_cone_cos_bits();
        const int32_t hi = (int32_t)(uint32_t)(dbits >> 32);
        const int32_t lo = (int32_t)(uint32_t)dbits;
        MEM_W(0, (gpr)(int32_t)0x8008D8C0u) = hi;
        MEM_W(4, (gpr)(int32_t)0x8008D8C0u) = lo;
        MEM_W(0, (gpr)(int32_t)0x8008D8C8u) = hi;
        MEM_W(4, (gpr)(int32_t)0x8008D8C8u) = lo;
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
constexpr gpr kCurCircuitAddr = (gpr)(int32_t)0x800CE794u;  // current circuit (no_lod_audit.md §8)

int16_t s_synth_row[256];
int s_synth_n = -1;  // -1 = passthrough (feature off or sanity check failed)

bool valid_guest_ptr(int32_t p) {
    uint32_t u = (uint32_t)p;
    return u >= 0x80000000u && u < 0x80800000u;
}

struct SegmentRecordView {
    uint8_t* rdram;
    gpr records;
};

bool segment_is_renderable(int segment, const void* context) {
    const auto& view = *static_cast<const SegmentRecordView*>(context);
    uint8_t* rdram = view.rdram;
    return valid_guest_ptr(MEM_W(segment * 64 + 0x4, view.records));
}

// Rebuild the synthesized row for the viewport walk that is starting. The segment
// count is not stored anywhere by the game -- it is (header+0x8 - header+0x4) / 20,
// the size of the PVS block itself (verified exact on circuit 5: 1100/20 = 55 rows,
// with a null 56th record as sentinel). Any sanity failure disables the override for
// this walk and the authored row is used untouched.
void build_synth_row(uint8_t* rdram) {
    s_synth_n = -1;
    // Per-circuit gate (2026-07-23): the PVS synth surfaces back-of-building /
    // cross-track geometry on the pro tracks that the authored PVS rows
    // deliberately hid. See lambo_config.h for the basic/pro default. The
    // radius cull and 2P+ scenery sub-DL stay gated on the global no_lod()
    // so they still work on tracks where the synth is off.
    int32_t cur_circuit = MEM_H(0, kCurCircuitAddr);
    if (!lambo::config::no_lod_circuit(cur_circuit)) return;
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

    int16_t authored[10];
    for (int i = 0; i < 10; i++) {  // authored row first: stock drawn-list prefix
        authored[i] = static_cast<int16_t>(MEM_H(cam * 20 + i * 2, (gpr)pvs));
    }
    SegmentRecordView records{rdram, (gpr)recs};
    // The pure row policy keeps authored membership ahead of exclusions: a
    // PVS-gated segment is withheld only when it would be a synthesized extra.
    s_synth_n = lambo::no_lod::build_synthesized_row(
        cur_circuit, n, cam, std::span<const int16_t>(authored),
        &segment_is_renderable, &records, std::span<int16_t>(s_synth_row));
}

}  // namespace

// Hooked after the row-entry fetch (before 0x8000D05C): replaces the authored entry
// with the synthesized row's. idx is the loop counter the fetch used ($t4).
// s_synth_n < 0 here means "passthrough" -- one of: global no_lod off, per-circuit
// no_lod_circuit off (city/pro tracks), or build_synth_row failed its sanity
// checks. All three fall back to the ROM-authored 10-slot PVS row.
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
// synthesized row is exhausted instead of stopping at 10. next is i+1 ($t4). Like
// pvs_entry, s_synth_n < 0 here means passthrough (see pvs_entry comment).
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

// Per-car model LOD (issue #165, the last distance axis -- a user report that car
// models still swap with distance after the scenery work shipped). The same scene
// builder draws the cars and keeps a per-car scaled camera distance as a halfword
// at 0x80098720: each frame it computes sqrt of the scaled dx^2+dy^2+dz^2 between
// the viewport position (indexed by 0x800CE6A6 into 0x800A2DD0) and the per-car
// record at 0x800B69B0 + i*268 (the struct pointer -- a sibling field at
// 0x800B69BC carries the position), truncs to int (0x8000C0AC) and stores it
// there (0x8000C0BC; the 3P/4P pass multiplies by 1.5 and stores again at
// 0x8000C104). Every car-model choice hangs off that one halfword, all inside
// this builder: an 8-entry threshold ladder over the car-type struct's halfwords
// (first index whose threshold exceeds the distance) selects a display list from
// the struct's models[8] array (ladder at 0x8000C17C-0x8000C1DC, emit at
// 0x8000C218-0x8000C25C), a second consumer repeats the pattern for its own
// overlay pass (0x8000E210-0x8000E2C4), and a >=150 check gates the far path
// (0x8000C2C4). The structs are runtime asset data (e.g. 0x8018F0C0:
// thresholds [25,10,50,32,200,...], models [full, mid, mid, low...]) -- the low
// entries are the simplified meshes that swap in with distance.
//
// Fix: zero the halfword after both stores (hooked at 0x8000C108, before any
// reader -- 0x8000C0BC stores, 0x8000C104 re-stores in 3P/4P, first read at
// 0x8000C160; verified in ares with a read-watchpoint on the struct threshold
// table) when no_lod() is on, so every consumer takes its closest/most-detailed
// branch: level 0 on both ladders (for typical positive-threshold structs;
// structs with a zero or negative threshold[0] would still pick level 0 since
// "dist<threshold" with dist=0 fails for any negative threshold) and the near
// path on the 150 gate. Per frame, not once at load -- the builder recomputes
// the value every car, every frame. With no_lod() off the native returns
// without touching RDRAM and the ROM's authored LOD selection runs bit-for-bit.
extern "C" void lambo_no_lod_car_detail(uint8_t* rdram) {
    if (!lambo::config::no_lod()) return;
    MEM_H(0, (gpr)(int32_t)0x80098720u) = 0;
}

// Headless stub renderer for the first-VI boot probe (#57).
//
// ultramodern requires a non-null `renderer::create_render_context` callback,
// but reaching the first VI retrace needs no actual rendering (the VI timer is
// a wall-clock thread, and the per-frame ScreenUpdateAction queue is unbounded
// and non-blocking). So we hand back a RendererContext whose every method is a
// no-op. RT64 replaces this in phase #58.

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <memory>
#include <algorithm>
#include <vector>

#include "ultramodern/renderer_context.hpp"

#include "lambo_rt64.h" // RT64 default presenter (#58); LAMBO_HEADLESS=1 keeps swrender

// Set by create_render_context so the game-specific VI retrace hook (vi_cb in main.cpp)
// can reach RDRAM. ultramodern's events thread owns the only rdram pointer otherwise.
uint8_t* g_lambo_rdram = nullptr;

namespace headless {

// ---------------------------------------------------------------------------
// DL introspector (renderer-seam debug infra, gated by LAMBO_DL_INSPECT).
//
// The pivot is headless: send_dl drops the display list. But that OSTask carries
// the REAL command stream the game built in RDRAM this frame -- the exact input an
// HLE renderer (RT64, #58) will walk. With NO renderer wired, this is the only way
// to answer "is what the game renders at state 8 real 3D geometry, or empty?" -- i.e.
// whether the game-logic->DL pipeline is faithful (W102 proved the game LOGIC is;
// this checks its rendered output). MEASURED (W103, 2026-07-01): the state-8 DL is
// F3DEX (Fast3DEX v1), NOT F3DEX2 -- 0xB1=G_TRI2 present, 0x01=G_MTX/0x04=G_VTX low
// opcodes. This matters for RT64: the HLE must select the F3DEX ucode profile.
// Env-gated so the DEFAULT build is byte-unchanged. LAMBO_DL_INSPECT=1 dumps a one-shot
// summary at LAMBO_DL_INSPECT_STATE (default 8). Walks G_DL branches + gsSPSegment addressing.
namespace dlinspect {

// F3DEX (Fast3DEX v1) command opcodes -- measured from this game's state-8 DL
// (0xB1=G_TRI2 present => F3DEX, not F3D; 0x01=G_MTX & 0x04=G_VTX => not F3DEX2).
// Low ops = "immediate" geometry, high 0xBx = SP, 0xE.-0xF. = shared RDP.
enum : uint8_t {
    // immediate (low) ops
    G_SPNOOP    = 0x00,
    G_MTX       = 0x01,
    G_MOVEMEM   = 0x03,
    G_VTX       = 0x04,
    G_DL        = 0x06,
    G_SPRITE2D  = 0x09,
    // SP (high 0xBx) ops
    G_TRI2      = 0xB1,
    G_RDPHALF_2 = 0xB3,
    G_RDPHALF_1 = 0xB4,
    G_QUAD      = 0xB5,
    G_CLRGEOMETRYMODE = 0xB6,
    G_SETGEOMETRYMODE = 0xB7,
    G_ENDDL     = 0xB8,
    G_SETOTHERMODE_L = 0xB9,
    G_SETOTHERMODE_H = 0xBA,
    G_TEXTURE   = 0xBB,
    G_MOVEWORD  = 0xBC,
    G_POPMTX    = 0xBD,
    G_CULLDL    = 0xBE,
    G_TRI1      = 0xBF,
    // shared RDP ops
    G_TEXRECT   = 0xE4,
    G_SETTIMG   = 0xFD,
    G_SETTILE   = 0xF5,
    G_LOADTLUT  = 0xF0,
    G_LOADBLOCK = 0xF3,
    G_LOADTILE  = 0xF4,
    G_SETTILESIZE = 0xF2,
    G_FILLRECT  = 0xF6,
    G_SETFILLCOLOR = 0xF7,
    G_SETCIMG   = 0xFF,
    G_SETZIMG   = 0xFE,
    G_SETCOMBINE = 0xFC,
    G_SETPRIMCOLOR = 0xFA,
    G_SETENVCOLOR  = 0xFB,
};

// G_MOVEWORD index for gsSPSegment (segment table write) -- F3DEX: index in low byte.
static constexpr uint8_t G_MW_SEGMENT = 0x06;

struct Stats {
    uint32_t cmds = 0;
    uint32_t vtx_loads = 0, vtx_total = 0;
    uint32_t tris = 0;          // triangles (TRI1=1, TRI2=2, QUAD=2)
    uint32_t mtx = 0, popmtx = 0, moveword = 0, movemem = 0, texture = 0, geommode = 0;
    uint32_t settimg = 0, settile = 0, loadblock = 0, loadtile = 0, settilesize = 0;
    uint32_t texrect = 0, fillrect = 0, dl_calls = 0, enddl = 0, unknown = 0;
    uint32_t first_timg = 0;    // first texture image address seen (SETTIMG w1)
    uint32_t max_depth = 0;
    // Distinct texture working-set (SETTIMG addr + fmt/siz). Bounded set for the manifest.
    static constexpr int kMaxTex = 48;
    uint32_t tex_addr[kMaxTex] = {0};
    uint8_t  tex_fmt[kMaxTex] = {0};
    uint8_t  tex_siz[kMaxTex] = {0};
    int      tex_count = 0;
    void note_texture(uint32_t addr, uint8_t fmt, uint8_t siz) {
        for (int i = 0; i < tex_count; ++i)
            if (tex_addr[i] == addr) return;          // already recorded
        if (tex_count < kMaxTex) {
            tex_addr[tex_count] = addr; tex_fmt[tex_count] = fmt; tex_siz[tex_count] = siz;
            ++tex_count;
        }
    }
};

static const char* fmt_name(uint8_t f) {
    switch (f) { case 0: return "RGBA"; case 1: return "YUV"; case 2: return "CI";
                 case 3: return "IA"; case 4: return "I"; default: return "?"; }
}
static const char* siz_name(uint8_t s) {
    switch (s) { case 0: return "4b"; case 1: return "8b"; case 2: return "16b";
                 case 3: return "32b"; default: return "?"; }
}

// Convert a guest (KSEG) or segmented DL address to an RDRAM byte offset. Returns
// false if the address is not resolvable to a valid in-RDRAM location.
static bool resolve(uint32_t addr, const uint32_t seg[16], uint32_t* out_off) {
    uint32_t hi = (addr >> 24) & 0xFF;
    uint32_t phys;
    if (hi >= 0x80 && hi <= 0x9F) {          // KSEG0/1 direct pointer
        phys = addr & 0x1FFFFFFF;
    } else if (hi < 0x10) {                   // segmented: seg id in top byte
        uint32_t base = seg[hi] & 0x1FFFFFFF;
        phys = base + (addr & 0x00FFFFFF);
    } else {
        return false;
    }
    if (phys >= 0x00800000) return false;     // outside 8 MB RDRAM
    *out_off = phys;
    return true;
}

// Walk one DL (following G_DL/G_ENDDL) accumulating stats. Depth-bounded; segment
// table shared+mutated across the walk (gsSPSegment persists like on hardware).
static void walk(const uint8_t* rdram, uint32_t start_addr, uint32_t seg[16],
                 Stats& st, int depth) {
    if (depth > 12) return;
    if ((uint32_t)depth > st.max_depth) st.max_depth = depth;
    uint32_t off;
    if (!resolve(start_addr, seg, &off)) return;
    for (uint32_t i = 0; i < 200000; ++i) {          // hard cap: runaway guard
        if (off + 8 > 0x00800000) return;
        uint32_t w0 = *(const uint32_t*)(rdram + off);
        uint32_t w1 = *(const uint32_t*)(rdram + off + 4);
        uint8_t  op = (w0 >> 24) & 0xFF;
        ++st.cmds;
        switch (op) {
            case G_VTX: {
                // F3DEX G_VTX: vertex count is bits 10-15 of w0 (verified vs cmd 0x0400103F=4
                // verts feeding the TRI2 quad at 0x800BF400+29). v0/end index is the low 10 bits.
                uint32_t numv = (w0 >> 10) & 0x3F;
                ++st.vtx_loads; st.vtx_total += numv;
                break;
            }
            case G_TRI1: ++st.tris; break;
            case G_TRI2: st.tris += 2; break;
            case G_QUAD: st.tris += 2; break;
            case G_MTX: ++st.mtx; break;
            case G_POPMTX: ++st.popmtx; break;
            case G_MOVEWORD: {
                ++st.moveword;
                // F3DEX gsMoveWd: index in low byte, offset in (w0>>8)&0xFFFF.
                // gsSPSegment(seg,base) = gsMoveWd(G_MW_SEGMENT, seg<<2, base).
                uint8_t index = w0 & 0xFF;
                if (index == G_MW_SEGMENT) {
                    uint32_t segnum = (((w0 >> 8) & 0xFFFF) >> 2) & 0xF;
                    seg[segnum] = w1;
                }
                break;
            }
            case G_MOVEMEM: ++st.movemem; break;
            case G_TEXTURE: ++st.texture; break;
            case G_SETGEOMETRYMODE:
            case G_CLRGEOMETRYMODE: ++st.geommode; break;
            case G_SETTIMG: {
                ++st.settimg;
                if (st.first_timg == 0) st.first_timg = w1;
                uint8_t fmt = (w0 >> 21) & 0x7;
                uint8_t siz = (w0 >> 19) & 0x3;
                st.note_texture(w1, fmt, siz);
                break;
            }
            case G_SETTILE: ++st.settile; break;
            case G_LOADBLOCK: ++st.loadblock; break;
            case G_LOADTILE: ++st.loadtile; break;
            case G_SETTILESIZE: ++st.settilesize; break;
            case G_TEXRECT: ++st.texrect; break;
            case G_FILLRECT: ++st.fillrect; break;
            case G_DL: {
                ++st.dl_calls;
                // F3DEX gsSPDisplayList (byte1==0, call/push) vs gsSPBranchList (byte1!=0, jump).
                bool branch = (((w0 >> 16) & 0xFF) != 0);
                if (branch) { start_addr = w1; if (!resolve(w1, seg, &off)) return; continue; }
                walk(rdram, w1, seg, st, depth + 1);
                break;
            }
            case G_ENDDL: ++st.enddl; return;
            case G_SPNOOP: break;
            default:
                // RDP + other set-state commands we don't itemise; count as "other".
                if (op != G_SETCIMG && op != G_SETZIMG && op != G_SETFILLCOLOR &&
                    op != G_SETCOMBINE && op != G_SETOTHERMODE_L && op != G_SETOTHERMODE_H &&
                    op != G_RDPHALF_1 && op != G_RDPHALF_2)
                    ++st.unknown;
                break;
        }
        off += 8;
    }
}

static const char* opname(uint8_t op) {
    switch (op) {
        case G_SPNOOP: return "SPNOOP"; case G_MTX: return "MTX"; case G_MOVEMEM: return "MOVEMEM";
        case G_VTX: return "VTX"; case G_DL: return "DL"; case G_SPRITE2D: return "SPRITE2D";
        case G_TRI1: return "TRI1"; case G_TRI2: return "TRI2"; case G_QUAD: return "QUAD";
        case G_CULLDL: return "CULLDL"; case G_POPMTX: return "POPMTX"; case G_MOVEWORD: return "MOVEWORD";
        case G_TEXTURE: return "TEXTURE"; case G_ENDDL: return "ENDDL";
        case G_SETGEOMETRYMODE: return "SETGEOMMODE"; case G_CLRGEOMETRYMODE: return "CLRGEOMMODE";
        case G_SETOTHERMODE_H: return "SETOTHERMODE_H"; case G_SETOTHERMODE_L: return "SETOTHERMODE_L";
        case G_RDPHALF_1: return "RDPHALF_1"; case G_RDPHALF_2: return "RDPHALF_2";
        case G_SETTIMG: return "SETTIMG"; case G_SETTILE: return "SETTILE"; case G_LOADBLOCK: return "LOADBLOCK";
        case G_LOADTILE: return "LOADTILE"; case G_SETTILESIZE: return "SETTILESIZE"; case G_TEXRECT: return "TEXRECT";
        case G_FILLRECT: return "FILLRECT"; case G_SETCIMG: return "SETCIMG"; case G_SETZIMG: return "SETZIMG";
        case G_SETFILLCOLOR: return "SETFILLCOLOR"; case G_SETCOMBINE: return "SETCOMBINE";
        case 0xE7: return "PIPESYNC"; case 0xE6: return "LOADSYNC"; case 0xE8: return "TILESYNC";
        case 0xE9: return "FULLSYNC"; case 0xFB: return "SETENVCOLOR"; case 0xFA: return "SETPRIMCOLOR";
        case 0xF9: return "SETBLENDCOLOR"; case 0xF8: return "SETFOGCOLOR"; case 0xEF: return "RDPSETOTHERMODE";
        default: return "?";
    }
}

// Raw first-N command dump: the ground-truth view (aggregates can mislead if the
// walk overruns). Bounded strictly by the buffer's byte length.
static void dump_raw(const uint8_t* rdram, uint32_t dl_addr, uint32_t byte_len, int max_cmds) {
    uint32_t off;
    uint32_t noseg[16] = {0};
    if (!resolve(dl_addr, noseg, &off)) { std::fprintf(stderr, "[dl-inspect]   (unresolvable)\n"); return; }
    uint32_t end = off + byte_len;
    int n = 0;
    for (uint32_t o = off; o + 8 <= end && o + 8 <= 0x00800000 && n < max_cmds; o += 8, ++n) {
        uint32_t w0 = *(const uint32_t*)(rdram + o);
        uint32_t w1 = *(const uint32_t*)(rdram + o + 4);
        std::fprintf(stderr, "[dl-inspect]   %3d  %08X %08X  %s\n", n, w0, w1, opname((w0 >> 24) & 0xFF));
    }
}

static void dump_summary(const uint8_t* rdram, const OSTask* t) {
    uint32_t seg[16] = {0};
    Stats st;
    uint32_t dl_addr = (uint32_t)(int32_t)t->t.data_ptr;
    std::fprintf(stderr, "[dl-inspect] --- raw first commands (bounded by data_size) ---\n");
    dump_raw(rdram, dl_addr, (uint32_t)t->t.data_size, 64);
    std::fprintf(stderr, "[dl-inspect] --- unbounded walk aggregate (may overrun if no in-buffer ENDDL) ---\n");
    walk(rdram, dl_addr, seg, st, 0);
    std::fprintf(stderr,
        "[dl-inspect] state-gated one-shot DL @ 0x%08X (size=%u)\n"
        "  cmds=%u  vtx_loads=%u vtx_total=%u  tris=%u\n"
        "  mtx=%u popmtx=%u moveword=%u movemem=%u texture=%u geommode=%u\n"
        "  settimg=%u settile=%u loadblock=%u loadtile=%u settilesize=%u  texrect=%u fillrect=%u\n"
        "  dl_calls=%u enddl=%u unknown=%u  max_depth=%u  first_texture_img=0x%08X\n",
        dl_addr, (unsigned)t->t.data_size,
        st.cmds, st.vtx_loads, st.vtx_total, st.tris,
        st.mtx, st.popmtx, st.moveword, st.movemem, st.texture, st.geommode,
        st.settimg, st.settile, st.loadblock, st.loadtile, st.settilesize, st.texrect, st.fillrect,
        st.dl_calls, st.enddl, st.unknown, st.max_depth, st.first_timg);
    std::fprintf(stderr, "[dl-inspect] texture working-set: %d distinct (first %d shown)\n",
                 st.tex_count, st.tex_count);
    for (int i = 0; i < st.tex_count; ++i)
        std::fprintf(stderr, "[dl-inspect]   tex[%2d] addr=0x%08X  fmt=%s siz=%s\n",
                     i, st.tex_addr[i], fmt_name(st.tex_fmt[i]), siz_name(st.tex_siz[i]));
}

} // namespace dlinspect

// ---------------------------------------------------------------------------
// The pivot's software reference renderer -- runs on the DEFAULT path, every frame.
//
// W103 proved the state-8 demo-race DL is a real, textured, transformed F3DEX 3D
// scene (1213 tris, 1831 verts, 45 matrices). RT64 (ADR 0002, #58) is the eventual
// HLE renderer but is not vendored and is a multi-session lift, so this is the in-tree
// renderer for now: walk the game's real DL, apply the real G_MTX matrices, transform
// the G_VTX verts, and rasterize TRI1/TRI2/QUAD into an RGBA framebuffer.
//
// It is a SCAFFOLD toward RT64 (correct-but-slow) and is TRACKED for retirement when RT64
// lands (#53/#54). W105 added per-pixel texturing (RGBA16 + CI4/CI8 via TLUT, sampled from
// the source image in RDRAM, perspective-correct, modulated by Gouraud shade); untextured or
// unhandled-format tris fall back to Gouraud vertex colour. But per this
// project's rules it is NOT gated off: the DEFAULT build IS the integration target,
// and a renderer hidden behind a flag would be the "gated A/B scaffold, not durable
// default-path convergence" failure mode ADR 0002 was created to escape. So it renders
// on every send_dl -- the trunk is what gets stress-tested. It only READS RDRAM (never
// writes), so it cannot perturb game logic; that is a correctness property, NOT a
// "byte-unchanged" success gate (byte-identicality between builds is explicitly a
// REJECTED metric -- faithfulness = convergence to ares). The one knob is frame
// CAPTURE-to-file (a diagnostic, not a gate): save the first frame at a chosen state
// for the port-vs-ares FB-diff harness.
namespace swrender {

// Reuse the F3DEX opcode enum + address resolver from dlinspect (same translation unit).
using namespace dlinspect;

// ---- byte-order-safe RDRAM readers -------------------------------------------------
// RDRAM is byte-swapped: an aligned uint32 read yields the correct N64 value (no XOR),
// but s16/u16 need the byte offset XOR'd with 2 and bytes XOR'd with 3 (N64Recomp
// MEM_H / MEM_B addressing, main.cpp:95-108). Getting this wrong silently corrupts
// every vertex coordinate, so it is centralised here.
static inline int16_t  rd_s16(const uint8_t* r, uint32_t off) { return *(const int16_t*)(r + (off ^ 2)); }
static inline uint16_t rd_u16(const uint8_t* r, uint32_t off) { return *(const uint16_t*)(r + (off ^ 2)); }
static inline uint8_t  rd_u8 (const uint8_t* r, uint32_t off) { return *(const uint8_t*)(r + (off ^ 3)); }

// ---- small 4x4 matrix helpers (N64 row-major, row-vector convention v*M) ------------
struct Mat { float m[16]; };
static void mat_identity(Mat& o) {
    for (int i = 0; i < 16; ++i) o.m[i] = 0.0f;
    o.m[0] = o.m[5] = o.m[10] = o.m[15] = 1.0f;
}
// C = A * B (row-major). Composes A-then-B for row vectors: (v*A)*B == v*(A*B).
static void mat_mul(Mat& o, const Mat& a, const Mat& b) {
    Mat t;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            t.m[i * 4 + j] = s;
        }
    o = t;
}
// Decode a 64-byte N64 fixed-point matrix: 16 s16 integer parts (bytes 0..31) then
// 16 u16 fractional parts (bytes 32..63), row-major. value[i] = int_hi + frac/65536.
static void decode_n64_matrix(const uint8_t* rdram, uint32_t phys, Mat& o) {
    for (int i = 0; i < 16; ++i) {
        int16_t  ihi = rd_s16(rdram, phys + i * 2);
        uint16_t frc = rd_u16(rdram, phys + 32 + i * 2);
        o.m[i] = (float)ihi + (float)frc / 65536.0f;
    }
}

// ---- transformed vertex cache ------------------------------------------------------
struct SVtx {
    // Clip-space coords (pre perspective-divide) so triangles that cross the near plane
    // (a vertex with w<=0, behind the eye) can be CLIPPED before projection instead of
    // dropped whole. The divide + viewport map happen later, in project(), on the
    // clipped polygon's vertices.
    float cx, cy, cz, cw;
    float s, t;       // texel coords (S10.5 vertex ST / 32, unity G_TEXTURE scale)
    uint8_t r, g, b, a;
    bool loaded;
};

// ---- framebuffer -------------------------------------------------------------------
struct Framebuffer {
    int W, H;
    std::unique_ptr<uint8_t[]> rgb;   // W*H*3, top-to-bottom
    std::unique_ptr<float[]>   depth; // W*H
    void init(int w, int h) {
        W = w; H = h;
        rgb = std::make_unique<uint8_t[]>((size_t)W * H * 3);
        depth = std::make_unique<float[]>((size_t)W * H);
        clear();
    }
    void clear() {   // reset colour + depth each frame (double-buffer semantics)
        for (int i = 0; i < W * H; ++i) {
            rgb[i * 3 + 0] = 12; rgb[i * 3 + 1] = 12; rgb[i * 3 + 2] = 24; // dark blue clear
            depth[i] = 1e30f;
        }
    }
};

// Renderer state carried across the whole DL walk (persists like RSP state).
struct RState {
    const uint8_t* rdram;
    uint32_t seg[16] = {0};
    Mat proj;                       // projection (no stack, like the legacy renderer)
    Mat mv_stack[10];               // modelview stack
    int mv_top = 0;
    // viewport (screen-space affine): sx = ndc_x*vp_sx + vp_tx ; likewise y.
    float vp_sx, vp_sy, vp_tx, vp_ty;
    bool vp_set = false;
    uint32_t geom_mode = 0;         // accumulated G_SETGEOMETRYMODE / G_CLRGEOMETRYMODE bits
    SVtx vtx[128];
    Framebuffer* fb;
    // --- texture pipeline state (tracked across the DL walk, like RDP TMEM state) ---
    // W105: the state-8 scene is textured (RGBA16 majority + CI4/CI8 via TLUT). We sample
    // the SOURCE image in RDRAM directly (no TMEM byte-array model): every texture group
    // re-issues SETTIMG(texel) right before its render tile + draw, so the "current SETTIMG"
    // IS this group's texel source. The two-tile idiom means tile 7 is the LOADBLOCK load
    // tile and tile 0 is the render tile -- we only track tile 0 (the one G_TEXTURE selects).
    uint32_t timg_addr = 0;         // most recent SETTIMG source (== current group's texels)
    uint32_t tlut_addr = 0;         // palette source: the SETTIMG that preceded the last LOADTLUT
    uint32_t tlut_count = 0;        // palette entries (16 => CI4, 256 => CI8)
    uint8_t  rt_fmt = 0, rt_siz = 0;// render tile (tile 0) format/size, decoded per this game
    uint32_t rt_w = 0, rt_h = 0;    // render tile texel dims from SETTILESIZE
    uint8_t  rt_cmS = 0, rt_cmT = 0;// render-tile clamp/mirror bits (bit0=G_TX_MIRROR, bit1=G_TX_CLAMP)
    bool     tex_on = false;        // G_TEXTURE enable
    // --- colour combiner + register colours (W107): the state-8 scene uses NINE distinct
    // SETCOMBINE muxes, not just TEXEL0*SHADE. The sky is TEX0-only (no shade), road is
    // 2-cycle LOD*SHADE, many body panels are TEX0*PRIM. Track the raw mux + PRIM/ENV so
    // raster can evaluate the real (a-b)*c+d per pixel instead of forcing modulate.
    uint32_t cc_w0 = 0xFFFFFFFF;    // SETCOMBINE w0 (default = TEX0-only global from sub-DL)
    uint32_t cc_w1 = 0xFFFCF83C;    // SETCOMBINE w1
    uint8_t  prim_r = 255, prim_g = 255, prim_b = 255, prim_a = 255;
    uint8_t  env_r  = 255, env_g  = 255, env_b  = 255, env_a  = 255;
    // --- fog + blender render mode (W109): the state-8 scene is a DUSK RACE WITH FOG.
    // The RSP folds a z-derived fog coefficient into vertex alpha (see G_FOG above); the
    // blender then mixes the combiner output toward fog_color by that coefficient on the
    // surfaces whose render-mode cycle-1 P input is CLR_FOG. Track fm/fo (G_MW_FOG), the
    // fog colour (SETFOGCOLOR), and the othermode-low render mode (SETOTHERMODE_L / EF).
    int16_t  fog_mul = 0, fog_off = 0;      // raw s16 fog multiplier / offset
    uint8_t  fog_r = 0, fog_g = 0, fog_b = 0;
    uint32_t othermode_lo = 0;              // accumulated render mode (blender lives in bits 16-31)
    // --- real N64 vertex lighting (W110): the state-8 scene loads 2 directional lights +
    // 1 ambient via G_MOVEMEM (indices G_MV_L0=0x86, L1=0x88, ambient at 0x86+num*2). The RSP
    // lambert-shades each vertex normal against these COLOURED lights; the port previously
    // faked it with a single grey headlight, flattening the dusk key/fill and darkening the
    // whole scene. num_lights comes from G_MW_NUMLIGHT. dir[] is stored pre-normalised.
    struct Light { float dx, dy, dz; float r, g, b; };
    Light   lights[8] = {};                 // slot k = G_MV_(L0+2k); slot[num_lights] = ambient
    int     num_lights = 0;                 // directional light count (G_MW_NUMLIGHT)
    bool    lights_loaded = false;          // any light struct seen -> use real lighting
    // stats
    uint32_t tris_in = 0, tris_drawn = 0, tris_clipped = 0, pixels = 0, verts_loaded = 0;
    uint32_t tex_pixels = 0;        // pixels shaded from a decoded texel (vs Gouraud)
    uint32_t fog_pixels = 0;        // pixels the fog blender touched (permanent render stat)
};

static void set_viewport_default(RState& s) {
    // Centre a 320x240-style viewport in the framebuffer if the DL never sets one.
    s.vp_sx = s.fb->W * 0.5f;  s.vp_tx = s.fb->W * 0.5f;
    s.vp_sy = -s.fb->H * 0.5f; s.vp_ty = s.fb->H * 0.5f;  // N64 y is flipped
}

static void load_viewport(RState& s, uint32_t phys) {
    // 16-byte viewport: 4 s16 scale then 4 s16 trans, values are 2x half-size in 14.2
    // fixed-point (/4). The libultra Vp stores a POSITIVE vscale_y (e.g. SCREEN_HT*2 =>
    // +120 for a 240-tall frame -- verified from this game's state-8 DL, G_MOVEMEM idx 0x80,
    // scale=[640,480,...]). The vertical y-flip (framebuffer y grows DOWN, NDC y grows UP)
    // is applied by the RSP as a SUBTRACT: screen_y = vtrans_y - ndc_y*vscale_y. Our
    // xform_vertex maps with an ADD (sy = ndc_y*vp_sy + vp_ty), so we bake the flip in by
    // NEGATING vscale_y here -- matching set_viewport_default's (already-negative) convention.
    // Getting this wrong renders the whole scene upside-down (car high+inverted, sky at bottom).
    float scale_x = (float)rd_s16(s.rdram, phys + 0) / 4.0f;
    float scale_y = (float)rd_s16(s.rdram, phys + 2) / 4.0f;
    float trans_x = (float)rd_s16(s.rdram, phys + 8) / 4.0f;
    float trans_y = (float)rd_s16(s.rdram, phys + 10) / 4.0f;
    s.vp_sx = scale_x;  s.vp_tx = trans_x;
    s.vp_sy = -scale_y; s.vp_ty = trans_y;   // negate: RSP subtracts the y term (framebuffer y-flip)
    s.vp_set = true;
}

// F3DEX geometry-mode bit for hardware lighting. When set, a vertex's cn[0..2] bytes
// hold a signed normal (not RGB), so drawing them as colour yields the rainbow artifact.
static constexpr uint32_t G_LIGHTING = 0x00020000;
// F3DEX (v1) backface-cull geometry-mode bits. Honouring these stops back-facing
// triangles (e.g. the far side of the road/terrain) from overdrawing the front-facing
// surface -- the noise the near-plane clip exposed in the close-up foreground.
static constexpr uint32_t G_CULL_FRONT = 0x00001000;
static constexpr uint32_t G_CULL_BACK  = 0x00002000;
// F3DEX G_FOG geometry-mode bit. When set, the real RSP OVERWRITES each vertex's alpha
// byte with a fog coefficient computed from the projected screen-z (clamp(ndc_z*fm+fo)).
// Our renderer reads vertex data straight from RDRAM -- BEFORE the RSP would touch it --
// so the stored alpha is the authored value, not fog; we must recompute it (xform_vertex).
static constexpr uint32_t G_FOG = 0x00010000;
// RDP set-state opcodes used by the fog/blender path (not in the dlinspect enum's core set).
static constexpr uint8_t G_SETFOGCOLOR    = 0xF8;
static constexpr uint8_t G_RDPSETOTHERMODE = 0xEF;   // full 64-bit othermode set (hi=w0, lo=w1)

// Transform one object-space vertex through modelview-top then projection, perspective
// divide, and viewport map. Fills an SVtx. eye = v*MV ; clip = eye*P (row-vector).
// When `lighting`, the colour bytes are a normal: shade with a single headlight against
// the modelview-rotated normal (an APPROXIMATION -- real N64 lighting uses light structs
// loaded via G_MOVEMEM; this scaffold just makes lit geometry legible as a shaded solid
// instead of rainbow-coloured normals, matching the legacy port's "fake shade" choice).
static void xform_vertex(RState& s, uint32_t phys, SVtx& out, bool lighting) {
    float x = (float)rd_s16(s.rdram, phys + 0);
    float y = (float)rd_s16(s.rdram, phys + 2);
    float z = (float)rd_s16(s.rdram, phys + 4);
    const Mat& mv = s.mv_stack[s.mv_top];
    const Mat& pr = s.proj;
    float eye[4], clip[4];
    for (int r = 0; r < 4; ++r)
        eye[r] = mv.m[0 * 4 + r] * x + mv.m[1 * 4 + r] * y + mv.m[2 * 4 + r] * z + mv.m[3 * 4 + r];
    for (int r = 0; r < 4; ++r)
        clip[r] = pr.m[0 * 4 + r] * eye[0] + pr.m[1 * 4 + r] * eye[1] +
                  pr.m[2 * 4 + r] * eye[2] + pr.m[3 * 4 + r] * eye[3];
    // Keep clip-space coords; near-plane clipping + perspective divide + viewport map
    // are deferred to project() so w<=0 vertices are clipped, not divided by.
    out.cx = clip[0]; out.cy = clip[1]; out.cz = clip[2]; out.cw = clip[3];
    // Texel coords: vertex bytes 8,10 are S,T in S10.5 fixed point (texel<<5). Every enabled
    // G_TEXTURE in this scene uses unity scale (S=T=0xFFFF), so texel = raw/32 with no scale mul.
    out.s = (float)rd_s16(s.rdram, phys + 8)  / 32.0f;
    out.t = (float)rd_s16(s.rdram, phys + 10) / 32.0f;
    uint8_t cr = rd_u8(s.rdram, phys + 12);
    uint8_t cg = rd_u8(s.rdram, phys + 13);
    uint8_t cb = rd_u8(s.rdram, phys + 14);
    if (lighting && s.lights_loaded) {
        // Real N64 lighting (W110): the cn bytes are a signed normal. Rotate it into eye space
        // by the modelview upper-3x3, normalise, then accumulate ambient + per-directional-light
        // lambert, each weighted by the light's own RGB colour. This restores the dusk key
        // (warm yellow from above) + fill (cool blue) that the old grey headlight flattened.
        float nx = (float)(int8_t)cr, ny = (float)(int8_t)cg, nz = (float)(int8_t)cb;
        float ex = nx * mv.m[0] + ny * mv.m[4] + nz * mv.m[8];
        float ey = nx * mv.m[1] + ny * mv.m[5] + nz * mv.m[9];
        float ez = nx * mv.m[2] + ny * mv.m[6] + nz * mv.m[10];
        float len = std::sqrt(ex * ex + ey * ey + ez * ez);
        if (len > 1e-4f) { ex /= len; ey /= len; ez /= len; }
        const RState::Light& amb = s.lights[s.num_lights];   // ambient stored past the directionals
        float rr = amb.r, gg = amb.g, bb = amb.b;
        for (int li = 0; li < s.num_lights; ++li) {
            const RState::Light& L = s.lights[li];
            float d = ex * L.dx + ey * L.dy + ez * L.dz;     // lambert; light dir points TO light
            if (d < 0.0f) d = 0.0f;
            rr += L.r * d; gg += L.g * d; bb += L.b * d;
        }
        auto c8 = [](float v) { v *= 255.0f; return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f); };
        out.r = c8(rr); out.g = c8(gg); out.b = c8(bb);
    } else if (lighting) {
        // Fallback headlight approximation for scenes with no loaded light structs.
        float nx = (float)(int8_t)cr, ny = (float)(int8_t)cg, nz = (float)(int8_t)cb;
        float ex = nx * mv.m[0] + ny * mv.m[4] + nz * mv.m[8];
        float ey = nx * mv.m[1] + ny * mv.m[5] + nz * mv.m[9];
        float ez = nx * mv.m[2] + ny * mv.m[6] + nz * mv.m[10];
        float len = std::sqrt(ex * ex + ey * ey + ez * ez);
        float d = (len > 1e-4f) ? std::fabs(ez) / len : 0.0f;
        float intensity = 0.35f + 0.65f * d;
        uint8_t v = (uint8_t)(230.0f * intensity);
        out.r = out.g = out.b = v;
    } else {
        out.r = cr; out.g = cg; out.b = cb;
    }
    // Fog fold (W109): when G_FOG is set, the RSP replaces vertex alpha with a fog
    // coefficient derived from the projected screen-z, NOT the authored byte. Formula
    // (matches GLideN64/fast3d): fog = clamp(ndc_z * fm + fo, 0, 255), ndc_z = clip_z/clip_w
    // in [-1,1]. fm/fo are the raw s16 gSPFogPosition values (fm>0, fo<0), so fog rises
    // from 0 at the fog-near plane to 255 at the far plane -- distant surfaces mix toward
    // fog_color. Interpolated linearly in screen space (like shade) by the rasterizer.
    if (s.geom_mode & G_FOG) {
        float w = clip[3];
        float ndc_z = (std::fabs(w) > 1e-6f) ? clip[2] / w : 1.0f;
        float fog = ndc_z * (float)s.fog_mul + (float)s.fog_off;
        if (fog < 0.0f) fog = 0.0f; else if (fog > 255.0f) fog = 255.0f;
        out.a = (uint8_t)(fog + 0.5f);
    } else {
        out.a = rd_u8(s.rdram, phys + 15);
    }
    out.loaded = true;
}

static inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

// Decode a 16-bit RGBA5551 texel to 8-bit RGBA. N64 RGBA16: RRRRRGGGGGBBBBBA (BE).
static inline void decode_rgba5551(uint16_t px, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    r = (uint8_t)(((px >> 11) & 0x1F) << 3);
    g = (uint8_t)(((px >> 6)  & 0x1F) << 3);
    b = (uint8_t)(((px >> 1)  & 0x1F) << 3);
    a = (px & 1) ? 255 : 0;
}

// Sample the current render tile (tile 0) at texel coords (fs,ft). Returns false if there
// is no usable texture (texturing off, no dims, unhandled format, or out-of-RDRAM read) so
// the caller can fall back to Gouraud vertex colour. We sample the SOURCE image in RDRAM
// directly rather than modelling TMEM: this scene loads each texture via a full LOADBLOCK
// right before its draw, so the source bytes are the tile's texels. Coords wrap (G_TX_WRAP).
// N64 tile S/T addressing: wrap (cm=0, i mod size), mirror (cm bit0, reflect every `size`
// texels -> [0..size-1, size-1..0]), or clamp (cm bit1, hold the edge). Mirror is how ROMs
// halve symmetric art (e.g. a car's rear); rendering it as plain wrap laterally shifts the
// mirrored half -- the "stripes/tail-lights the wrong way round" artifact.
static inline int wrap_axis(int i, int size, uint8_t cm) {
    if (size <= 0) return 0;
    if (cm & 0x2) {                                  // clamp
        return i < 0 ? 0 : (i >= size ? size - 1 : i);
    }
    if (cm & 0x1) {                                  // mirror
        int period = 2 * size;
        int m = i % period; if (m < 0) m += period;
        return (m < size) ? m : (period - 1 - m);
    }
    int m = i % size; if (m < 0) m += size;          // wrap
    return m;
}

static bool sample_tex(const RState& s, float fs, float ft,
                       uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    if (!s.tex_on || s.rt_w == 0 || s.rt_h == 0) return false;
    uint32_t tphys;
    if (!resolve(s.timg_addr, s.seg, &tphys)) return false;
    int tw = (int)s.rt_w, th = (int)s.rt_h;
    int tx = wrap_axis((int)std::floor(fs), tw, s.rt_cmS);
    int ty = wrap_axis((int)std::floor(ft), th, s.rt_cmT);
    uint32_t px = (uint32_t)(ty * tw + tx);
    if (s.rt_fmt == 0) {                          // RGBA
        if (s.rt_siz == 2) {                      // RGBA16 (5551)
            uint32_t o = tphys + px * 2;
            if (o + 2 > 0x00800000) return false;
            decode_rgba5551(rd_u16(s.rdram, o), r, g, b, a);
            return true;
        }
        if (s.rt_siz == 3) {                      // RGBA32
            uint32_t o = tphys + px * 4;
            if (o + 4 > 0x00800000) return false;
            r = rd_u8(s.rdram, o + 0); g = rd_u8(s.rdram, o + 1);
            b = rd_u8(s.rdram, o + 2); a = rd_u8(s.rdram, o + 3);
            return true;
        }
        return false;
    }
    if (s.rt_fmt == 2) {                          // CI (color-index) -> TLUT (RGBA16 entries)
        uint8_t idx;
        if (s.rt_siz == 0) {                      // CI4: 2 texels/byte, high nibble first
            uint32_t o = tphys + (px >> 1);
            if (o + 1 > 0x00800000) return false;
            uint8_t byte = rd_u8(s.rdram, o);
            idx = (px & 1) ? (byte & 0xF) : (byte >> 4);
        } else if (s.rt_siz == 1) {               // CI8
            uint32_t o = tphys + px;
            if (o + 1 > 0x00800000) return false;
            idx = rd_u8(s.rdram, o);
        } else {
            return false;
        }
        uint32_t pphys;
        if (!resolve(s.tlut_addr, s.seg, &pphys)) return false;
        uint32_t po = pphys + (uint32_t)idx * 2;
        if (po + 2 > 0x00800000) return false;
        decode_rgba5551(rd_u16(s.rdram, po), r, g, b, a);
        return true;
    }
    return false;                                 // IA / I not yet handled -> Gouraud fallback
}

// A vertex in homogeneous clip space (pre-divide) carrying the interpolatable attributes.
// Used only during near-plane clipping; colour is float here so edge intersections lerp cleanly.
struct ClipV { float x, y, z, w, s, t, r, g, b, a; };

// A vertex after perspective divide + viewport map -- what the rasterizer consumes.
struct ScreenV { float sx, sy, z, w, s, t, r, g, b, a; };

static inline ClipV clip_lerp(const ClipV& A, const ClipV& B, float u) {
    return ClipV{
        A.x + (B.x - A.x) * u, A.y + (B.y - A.y) * u, A.z + (B.z - A.z) * u,
        A.w + (B.w - A.w) * u, A.s + (B.s - A.s) * u, A.t + (B.t - A.t) * u,
        A.r + (B.r - A.r) * u, A.g + (B.g - A.g) * u, A.b + (B.b - A.b) * u,
        A.a + (B.a - A.a) * u};
}

// Sutherland-Hodgman clip of a convex polygon against the TRUE near plane, z_ndc = -1,
// i.e. z_clip + w_clip >= 0. This game's viewport maps NDC z [-1,1] -> depth [0,1022]
// (z-scale = z-trans = 511 = G_MAXZ/2, verified from the state-8 viewport [.,.,511,.]),
// so the near plane is z_ndc=-1. Clipping HERE (not at w~=0) keeps the new vertices'
// perspective divide finite (z/w and s/w stay bounded), avoiding the exploded-depth /
// smeared-UV foreground garbage that clipping at the eye (w>=epsilon) produces.
// Returns the new vertex count (0 or >=3); `out` must hold n+1 vertices.
static int clip_near_plane(const ClipV* in, int n, ClipV* out) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const ClipV& A = in[i];
        const ClipV& B = in[(i + 1) % n];
        float da = A.z + A.w, db = B.z + B.w;      // signed distance to the near plane
        bool ina = da >= 0.0f, inb = db >= 0.0f;
        if (ina) out[m++] = A;
        if (ina != inb) out[m++] = clip_lerp(A, B, da / (da - db));   // edge crosses the plane
    }
    return m;
}

// Sutherland-Hodgman clip against the FAR plane, z_ndc = +1 (keep z_clip <= w_clip).
// The real RSP clip-codes vertices against ALL +/-w planes and geometry beyond the far
// plane never renders. This game RELIES on that: its fog window is ndc_z in [0.99, 1.0]
// (fm=25600 fo=-25344), i.e. distant terrain fades to fog AT the far plane and anything
// past it must VANISH so the dusk-sky backdrop shows through at the horizon. Without
// this clip the port draws that terrain as a fully-fogged dark curtain over the lower
// sky (W112: port horizon band y=60-95 was (56,50,43) == fog colour, where live ares
// shows the bright sky (170-185,140-150,115-122)).
static int clip_far_plane(const ClipV* in, int n, ClipV* out) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const ClipV& A = in[i];
        const ClipV& B = in[(i + 1) % n];
        float da = A.w - A.z, db = B.w - B.z;      // signed distance to the far plane
        bool ina = da >= 0.0f, inb = db >= 0.0f;
        if (ina) out[m++] = A;
        if (ina != inb) out[m++] = clip_lerp(A, B, da / (da - db));
    }
    return m;
}

// Perspective divide + viewport map one clip-space vertex into a screen vertex.
static inline void project(const RState& s, const ClipV& c, ScreenV& o) {
    float inv = 1.0f / c.w;
    o.sx = c.x * inv * s.vp_sx + s.vp_tx;
    o.sy = c.y * inv * s.vp_sy + s.vp_ty;
    o.z  = c.z * inv;
    o.w  = c.w;
    o.s = c.s; o.t = c.t; o.r = c.r; o.g = c.g; o.b = c.b; o.a = c.a;
}

// --- N64 colour combiner (W107) -------------------------------------------------
// The RDP combiner computes out = (A - B) * C + D per cycle, in two cycles, with the
// cycle-0 result feeding cycle-1 as COMBINED. The state-8 scene uses nine distinct
// muxes; forcing TEX0*SHADE darkened the sky (a TEX0-only surface) and mis-tinted the
// TEX0*PRIM body panels. We evaluate the real mux with resolved inputs. TEX1 and the
// LOD fraction are approximated (no mipmap): TEX1==TEX0 makes the road's
// (TEX1-TEX0)*LOD+TEX0 collapse to TEX0 regardless of LOD, which is what we want.
struct RGBA { float r, g, b, a; };
static inline float cc_chan(const RGBA& x, int ch) { return ch == 0 ? x.r : ch == 1 ? x.g : x.b; }
// A/B/D use a 4/4/3-bit table; C (mul) uses a 5-bit table with alpha-scalar entries.
static inline float cc_pick_abd(int idx, int ch, bool isA,
        const RGBA& t0, const RGBA& sh, const RGBA& pr, const RGBA& en, const RGBA& cb) {
    switch (idx) {
        case 0: return cc_chan(cb, ch);
        case 1: case 2: return cc_chan(t0, ch);       // TEX0 / TEX1(~TEX0)
        case 3: return cc_chan(pr, ch);
        case 4: return cc_chan(sh, ch);
        case 5: return cc_chan(en, ch);
        case 6: return isA ? 1.0f : 0.0f;             // A:'1' / B:'CENTER'(~0) / D:'1'
        default: return 0.0f;                          // NOISE/K4/'0'
    }
}
static inline float cc_pick_c(int idx, int ch,
        const RGBA& t0, const RGBA& sh, const RGBA& pr, const RGBA& en, const RGBA& cb) {
    switch (idx) {
        case 0: return cc_chan(cb, ch);
        case 1: case 2: return cc_chan(t0, ch);
        case 3: return cc_chan(pr, ch);
        case 4: return cc_chan(sh, ch);
        case 5: return cc_chan(en, ch);
        case 6: return 1.0f;
        case 7: return cb.a;
        case 8: case 9: return t0.a;
        case 10: case 14: return pr.a;
        case 11: return sh.a;
        case 12: return en.a;
        case 13: case 15: return 1.0f;                 // LOD frac / K5 (~1)
        default: return 0.0f;
    }
}
// The ALPHA combiner is a parallel (A-B)*C+D over the 3-bit alpha-input table. Needed so
// translucent surfaces (the car's XLU shadow, C8104A50) get their real per-surface alpha
// (the shadow's is PRIM_A ~0x80, NOT shade alpha -- which on a fog surface is the near-zero
// fog coefficient, i.e. would make the shadow vanish). idx: 0=COMBINED 1/2=TEX0/1 3=PRIM
// 4=SHADE 5=ENV 6=1 7=0.
static inline float cc_pick_alpha(int idx, const RGBA& t0, const RGBA& sh,
        const RGBA& pr, const RGBA& en, const RGBA& cb) {
    switch (idx) {
        case 0: return cb.a;
        case 1: case 2: return t0.a;
        case 3: return pr.a;
        case 4: return sh.a;
        case 5: return en.a;
        case 6: return 1.0f;
        default: return 0.0f;
    }
}
static inline RGBA combine(uint32_t w0, uint32_t w1,
        const RGBA& t0, const RGBA& sh, const RGBA& pr, const RGBA& en) {
    int a0 = (w0 >> 20) & 0xF, c0 = (w0 >> 15) & 0x1F, a1 = (w0 >> 5) & 0xF, c1 = w0 & 0x1F;
    int b0 = (w1 >> 28) & 0xF, b1 = (w1 >> 24) & 0xF, d0 = (w1 >> 15) & 0x7, d1 = (w1 >> 6) & 0x7;
    // alpha combiner selects (3-bit): a in w0, the rest in w1.
    int Aa0 = (w0 >> 12) & 7, Ac0 = (w0 >> 9) & 7, Aa1 = (w1 >> 21) & 7, Ac1 = (w1 >> 18) & 7;
    int Ab0 = (w1 >> 12) & 7, Ad0 = (w1 >> 9) & 7, Ab1 = (w1 >> 3) & 7, Ad1 = w1 & 7;
    RGBA cb = {0, 0, 0, 0};
    for (int pass = 0; pass < 2; ++pass) {
        int A = pass ? a1 : a0, B = pass ? b1 : b0, C = pass ? c1 : c0, D = pass ? d1 : d0;
        RGBA o;
        for (int ch = 0; ch < 3; ++ch) {
            float va = cc_pick_abd(A, ch, true,  t0, sh, pr, en, cb);
            float vb = cc_pick_abd(B, ch, false, t0, sh, pr, en, cb);
            float vc = cc_pick_c  (C, ch,        t0, sh, pr, en, cb);
            float vd = cc_pick_abd(D, ch, true,  t0, sh, pr, en, cb);
            float v = (va - vb) * vc + vd;
            (ch == 0 ? o.r : ch == 1 ? o.g : o.b) = v;
        }
        int Aa = pass ? Aa1 : Aa0, Ab = pass ? Ab1 : Ab0, Ac = pass ? Ac1 : Ac0, Ad = pass ? Ad1 : Ad0;
        float aa = cc_pick_alpha(Aa, t0, sh, pr, en, cb), ab = cc_pick_alpha(Ab, t0, sh, pr, en, cb);
        float ac = cc_pick_alpha(Ac, t0, sh, pr, en, cb), ad = cc_pick_alpha(Ad, t0, sh, pr, en, cb);
        o.a = (aa - ab) * ac + ad;
        cb = o;
    }
    return cb;
}
// Does any colour term of this combiner reference TEX0/TEX1? (decides whether to sample)
static inline bool cc_uses_tex(uint32_t w0, uint32_t w1) {
    int a0 = (w0 >> 20) & 0xF, c0 = (w0 >> 15) & 0x1F, a1 = (w0 >> 5) & 0xF, c1 = w0 & 0x1F;
    int b0 = (w1 >> 28) & 0xF, b1 = (w1 >> 24) & 0xF, d0 = (w1 >> 15) & 0x7, d1 = (w1 >> 6) & 0x7;
    auto isT = [](int i) { return i == 1 || i == 2; };
    auto isTc = [](int i) { return i == 1 || i == 2 || i == 8 || i == 9; };  // incl TEX0/1 alpha
    return isT(a0) || isT(b0) || isTc(c0) || isT(d0) ||
           isT(a1) || isT(b1) || isTc(c1) || isT(d1);
}

// Rasterize one already-projected, near-clipped triangle (Gouraud/texture + z-buffer).
// Returns true if it lit any pixel. w>0 is guaranteed by the caller (near-plane clip).
static bool raster_screen_tri(RState& s, const ScreenV& a, const ScreenV& b, const ScreenV& c) {
    Framebuffer& fb = *s.fb;
    float minx = std::min(a.sx, std::min(b.sx, c.sx));
    float maxx = std::max(a.sx, std::max(b.sx, c.sx));
    float miny = std::min(a.sy, std::min(b.sy, c.sy));
    float maxy = std::max(a.sy, std::max(b.sy, c.sy));
    int x0 = std::max(0, (int)std::floor(minx)), x1 = std::min(fb.W - 1, (int)std::ceil(maxx));
    int y0 = std::max(0, (int)std::floor(miny)), y1 = std::min(fb.H - 1, (int)std::ceil(maxy));
    if (x0 > x1 || y0 > y1) return false;               // fully off-screen
    float area = edge(a.sx, a.sy, b.sx, b.sy, c.sx, c.sy);
    if (area == 0.0f) return false;
    // Backface cull per the F3DEX geometry mode. With the viewport y-flip baked into vp_sy,
    // a front-facing triangle yields a POSITIVE signed area here (empirically verified vs the
    // ares state-8 frame); G_CULL_BACK culls the negative-area (back-facing) triangles.
    if ((s.geom_mode & G_CULL_BACK) && area < 0.0f) return false;
    if ((s.geom_mode & G_CULL_FRONT) && area > 0.0f) return false;
    float inv_area = 1.0f / area;
    // Perspective-correct texture setup: interpolate s/w, t/w, 1/w (linear in screen space)
    // then divide per-pixel. w>0 is guaranteed (near-plane cull above), so 1/w is finite.
    bool tex_avail = s.tex_on && s.rt_w != 0 && s.rt_h != 0 &&
                     (s.rt_fmt == 0 || s.rt_fmt == 2);
    bool want_tex = cc_uses_tex(s.cc_w0, s.cc_w1);      // does the mux reference TEX0/TEX1?
    bool textured = tex_avail && want_tex;
    RGBA prim = {s.prim_r / 255.0f, s.prim_g / 255.0f, s.prim_b / 255.0f, s.prim_a / 255.0f};
    RGBA env  = {s.env_r  / 255.0f, s.env_g  / 255.0f, s.env_b  / 255.0f, s.env_a  / 255.0f};
    // Fog blender (W109): a surface is fogged iff its render-mode cycle-1 P colour input is
    // CLR_FOG (bits 31-30 of othermode_lo == 3, e.g. the CB023038 fog-add mode). For those,
    // mix the combiner output toward fog_color by the per-pixel fog coefficient (shade.a,
    // which xform_vertex folded z-derived fog into). Non-fog surfaces are untouched.
    bool fog_surface = ((s.othermode_lo >> 30) & 3) == 3;
    RGBA fogc = {s.fog_r / 255.0f, s.fog_g / 255.0f, s.fog_b / 255.0f, 1.0f};
    // Translucency (W109): a surface alpha-blends against the framebuffer iff its blender
    // cycle-2 reads CLR_MEM weighted by (1-A) -- M2==CLR_MEM(1) && B2==1MA(0). This is the
    // faithful test (NOT ALPHA_CVG_SEL). The car's soft shadow (C8104A50) passes; the opaque
    // fogged body/road (CB023038, cyc2 M2==CLR_IN) does not. XLU surfaces z-TEST but don't
    // z-WRITE, so they don't occlude each other (matching the N64 XLU render modes here).
    bool xlu_surface = ((s.othermode_lo >> 20) & 3) == 1 && ((s.othermode_lo >> 16) & 3) == 0;
    float a_iw = 1.0f / a.w, b_iw = 1.0f / b.w, c_iw = 1.0f / c.w;
    float a_sw = a.s * a_iw, b_sw = b.s * b_iw, c_sw = c.s * c_iw;
    float a_tw = a.t * a_iw, b_tw = b.t * b_iw, c_tw = c.t * c_iw;
    auto clamp8 = [](float v) -> uint8_t {
        v *= 255.0f; if (v < 0) v = 0; if (v > 255) v = 255; return (uint8_t)(v + 0.5f);
    };
    bool drew = false;
    for (int y = y0; y <= y1; ++y) {
        float py = y + 0.5f;
        for (int x = x0; x <= x1; ++x) {
            float px = x + 0.5f;
            float w0 = edge(b.sx, b.sy, c.sx, c.sy, px, py) * inv_area;
            float w1 = edge(c.sx, c.sy, a.sx, a.sy, px, py) * inv_area;
            float w2 = edge(a.sx, a.sy, b.sx, b.sy, px, py) * inv_area;
            // Accept either winding (backface cull already applied at the triangle level).
            bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inside) continue;
            float zz = w0 * a.z + w1 * b.z + w2 * c.z;
            int idx = y * fb.W + x;
            if (zz >= fb.depth[idx]) continue;
            // SHADE input = perspective-agnostic Gouraud vertex colour (N64 shades linearly
            // in screen space, so no /w here -- matches the RSP's flat-in-screen shade).
            RGBA shade = {(w0 * a.r + w1 * b.r + w2 * c.r) / 255.0f,
                          (w0 * a.g + w1 * b.g + w2 * c.g) / 255.0f,
                          (w0 * a.b + w1 * b.b + w2 * c.b) / 255.0f,
                          (w0 * a.a + w1 * b.a + w2 * c.a) / 255.0f};
            RGBA tex0 = {1, 1, 1, 1};
            if (textured) {
                float iw = w0 * a_iw + w1 * b_iw + w2 * c_iw;
                if (iw != 0.0f) {
                    float inv = 1.0f / iw;
                    float sc = (w0 * a_sw + w1 * b_sw + w2 * c_sw) * inv;
                    float tc = (w0 * a_tw + w1 * b_tw + w2 * c_tw) * inv;
                    uint8_t tr, tg, tb, ta;
                    if (sample_tex(s, sc, tc, tr, tg, tb, ta)) {
                        if (ta == 0) continue;   // 1-bit / index-0 cutout: skip transparent texels
                        tex0 = {tr / 255.0f, tg / 255.0f, tb / 255.0f, ta / 255.0f};
                        ++s.tex_pixels;
                    }
                }
            }
            // Evaluate the real SETCOMBINE mux: (A-B)*C+D over two cycles.
            RGBA o = combine(s.cc_w0, s.cc_w1, tex0, shade, prim, env);
            // Fog blend: final = combined*(1-f) + fog_color*f, f = per-pixel fog coefficient.
            if (fog_surface) {
                float f = shade.a; if (f < 0.0f) f = 0.0f; else if (f > 1.0f) f = 1.0f;
                o.r = o.r * (1.0f - f) + fogc.r * f;
                o.g = o.g * (1.0f - f) + fogc.g * f;
                o.b = o.b * (1.0f - f) + fogc.b * f;
                if (f > 0.004f) ++s.fog_pixels;
            }
            // Translucent surfaces blend the (fogged) combiner output over the framebuffer by
            // the combiner alpha; then keep the existing depth (z-test done, no z-write).
            if (xlu_surface) {
                float av = o.a; if (av < 0.0f) av = 0.0f; else if (av > 1.0f) av = 1.0f;
                o.r = o.r * av + (fb.rgb[idx * 3 + 0] / 255.0f) * (1.0f - av);
                o.g = o.g * av + (fb.rgb[idx * 3 + 1] / 255.0f) * (1.0f - av);
                o.b = o.b * av + (fb.rgb[idx * 3 + 2] / 255.0f) * (1.0f - av);
            }
            fb.rgb[idx * 3 + 0] = clamp8(o.r);
            fb.rgb[idx * 3 + 1] = clamp8(o.g);
            fb.rgb[idx * 3 + 2] = clamp8(o.b);
            if (!xlu_surface) fb.depth[idx] = zz;
            ++s.pixels;
            drew = true;
        }
    }
    return drew;
}

// Draw one cached triangle: near-plane clip in clip space, then project + rasterize each
// resulting sub-triangle. A triangle straddling the eye plane (some vertex w<=0) is split
// at the plane instead of being dropped whole -- recovering the ~20% of foreground geometry
// (notably around the close-up car) the old whole-triangle w<=0 reject discarded.
static void raster_tri(RState& s, const SVtx& a, const SVtx& b, const SVtx& c) {
    ++s.tris_in;
    if (!a.loaded || !b.loaded || !c.loaded) return;
    ClipV poly[3] = {
        {a.cx, a.cy, a.cz, a.cw, a.s, a.t, (float)a.r, (float)a.g, (float)a.b, (float)a.a},
        {b.cx, b.cy, b.cz, b.cw, b.s, b.t, (float)b.r, (float)b.g, (float)b.b, (float)b.a},
        {c.cx, c.cy, c.cz, c.cw, c.s, c.t, (float)c.r, (float)c.g, (float)c.b, (float)c.a},
    };
    ClipV near_clipped[4];
    int mn = clip_near_plane(poly, 3, near_clipped);
    if (mn < 3) { ++s.tris_clipped; return; }       // entirely behind the near plane
    ClipV clipped[5];
    int m = clip_far_plane(near_clipped, mn, clipped);
    if (m < 3) { ++s.tris_clipped; return; }        // entirely beyond the far plane
    ScreenV sv[5];
    for (int i = 0; i < m; ++i) project(s, clipped[i], sv[i]);
    bool drew = false;
    for (int i = 1; i + 1 < m; ++i)                 // fan-triangulate the clipped polygon
        if (raster_screen_tri(s, sv[0], sv[i], sv[i + 1])) drew = true;
    if (drew) ++s.tris_drawn;
}

// Walk one DL (following G_DL) executing state + drawing. Depth-bounded.
static void walk(RState& s, uint32_t start_addr, int depth) {
    if (depth > 12) return;
    uint32_t off;
    if (!resolve(start_addr, s.seg, &off)) return;
    for (uint32_t i = 0; i < 200000; ++i) {
        if (off + 8 > 0x00800000) return;
        uint32_t w0 = *(const uint32_t*)(s.rdram + off);
        uint32_t w1 = *(const uint32_t*)(s.rdram + off + 4);
        uint8_t  op = (w0 >> 24) & 0xFF;
        switch (op) {
            case G_MTX: {
                uint32_t maddr = w1 & 0xFFFFFFF8;
                uint32_t mphys;
                if (resolve(maddr, s.seg, &mphys)) {
                    Mat m; decode_n64_matrix(s.rdram, mphys, m);
                    uint8_t flags = (w0 >> 16) & 0xFF;
                    bool proj = (flags & 0x01) != 0;
                    bool load = (flags & 0x02) != 0;
                    bool push = (flags & 0x04) != 0;
                    if (proj) {
                        if (load) s.proj = m; else mat_mul(s.proj, m, s.proj);
                    } else {
                        if (push && s.mv_top < 9) { s.mv_stack[s.mv_top + 1] = s.mv_stack[s.mv_top]; ++s.mv_top; }
                        if (load) s.mv_stack[s.mv_top] = m;
                        else { Mat r; mat_mul(r, m, s.mv_stack[s.mv_top]); s.mv_stack[s.mv_top] = r; }
                    }
                }
                break;
            }
            case G_POPMTX: {
                uint32_t n = w1 / 64; if (n == 0) n = 1;
                while (n-- && s.mv_top > 0) --s.mv_top;
                break;
            }
            case G_MOVEWORD: {
                uint8_t index = w0 & 0xFF;
                if (index == G_MW_SEGMENT) {
                    uint32_t segnum = (((w0 >> 8) & 0xFFFF) >> 2) & 0xF;
                    s.seg[segnum] = w1;
                } else if (index == 0x08) {           // G_MW_FOG: w1 = (fm<<16)|fo (both s16)
                    s.fog_mul = (int16_t)(w1 >> 16);
                    s.fog_off = (int16_t)(w1 & 0xFFFF);
                } else if (index == 0x02) {           // G_MW_NUMLIGHT: w1 = ((n+1)*32)|0x80000000
                    s.num_lights = (int)((w1 & 0xFFFF) / 32) - 1;
                    if (s.num_lights < 0) s.num_lights = 0;
                    if (s.num_lights > 7) s.num_lights = 7;
                }
                break;
            }
            case G_MOVEMEM: {
                // viewport = G_MV_VIEWPORT (mem index 0x80 in byte1 for F3DEX).
                uint8_t mem_idx = (w0 >> 16) & 0xFF;
                if (mem_idx == 0x80) {
                    uint32_t vphys;
                    if (resolve(w1, s.seg, &vphys)) load_viewport(s, vphys);
                } else if (mem_idx >= 0x86 && mem_idx <= 0x94 && (mem_idx & 1) == 0) {
                    // Light struct (G_MV_L0=0x86, L1=0x88, ...): 16-byte Light_t = col[3],pad,
                    // colc[3],pad,dir[3](s8),pad. Slot k=(idx-0x86)/2; slot[num_lights]=ambient.
                    int slot = (mem_idx - 0x86) / 2;
                    uint32_t lphys;
                    if (slot < 8 && resolve(w1, s.seg, &lphys)) {
                        RState::Light& L = s.lights[slot];
                        L.r = rd_u8(s.rdram, lphys + 0) / 255.0f;
                        L.g = rd_u8(s.rdram, lphys + 1) / 255.0f;
                        L.b = rd_u8(s.rdram, lphys + 2) / 255.0f;
                        float dx = (float)(int8_t)rd_u8(s.rdram, lphys + 8);
                        float dy = (float)(int8_t)rd_u8(s.rdram, lphys + 9);
                        float dz = (float)(int8_t)rd_u8(s.rdram, lphys + 10);
                        float dl = std::sqrt(dx * dx + dy * dy + dz * dz);
                        if (dl > 1e-4f) { dx /= dl; dy /= dl; dz /= dl; }
                        L.dx = dx; L.dy = dy; L.dz = dz;
                        s.lights_loaded = true;
                    }
                }
                break;
            }
            case G_VTX: {
                // F3DEX gSPVertex(v,n,v0): num=(w0>>10)&0x3F ; the DESTINATION cache index v0
                // is in byte 1 as v0*2 (bits 16-23) -- the same "*2" vertex-index convention the
                // TRI commands use. The low 10 bits are the DMA byte-LENGTH (num*16-1), NOT the
                // end index. Deriving v0 from the length only ever yields 0, so cache-packing
                // loads (the sky's 2x3 panel grid loads 4 verts each at v0=0,4,8,... then its
                // TRI2s read slots 4-11) silently read stale slots -- dropping the right half of
                // the sky and ~700 other tris. v0 = byte1/2 is verified against those TRI2 indices.
                uint32_t num = (w0 >> 10) & 0x3F;
                int v0 = (int)(((w0 >> 16) & 0xFF) >> 1);
                if (v0 < 0) v0 = 0;
                uint32_t vphys;
                if (resolve(w1, s.seg, &vphys)) {
                    bool lighting = (s.geom_mode & G_LIGHTING) != 0;
                    for (uint32_t k = 0; k < num; ++k) {
                        int slot = v0 + (int)k;
                        if (slot < 0 || slot >= 128) continue;
                        xform_vertex(s, vphys + k * 16, s.vtx[slot], lighting);
                        ++s.verts_loaded;
                    }
                }
                break;
            }
            case G_SETGEOMETRYMODE: s.geom_mode |= w1; break;
            case G_CLRGEOMETRYMODE: s.geom_mode &= ~w1; break;
            case G_TRI1: {
                int i0 = ((w1 >> 16) & 0xFF) / 2;
                int i1 = ((w1 >> 8) & 0xFF) / 2;
                int i2 = (w1 & 0xFF) / 2;
                if (i0 < 128 && i1 < 128 && i2 < 128)
                    raster_tri(s, s.vtx[i0], s.vtx[i1], s.vtx[i2]);
                break;
            }
            case G_TRI2: {
                int i0 = ((w0 >> 16) & 0xFF) / 2, i1 = ((w0 >> 8) & 0xFF) / 2, i2 = (w0 & 0xFF) / 2;
                int i3 = ((w1 >> 16) & 0xFF) / 2, i4 = ((w1 >> 8) & 0xFF) / 2, i5 = (w1 & 0xFF) / 2;
                if (i0 < 128 && i1 < 128 && i2 < 128) raster_tri(s, s.vtx[i0], s.vtx[i1], s.vtx[i2]);
                if (i3 < 128 && i4 < 128 && i5 < 128) raster_tri(s, s.vtx[i3], s.vtx[i4], s.vtx[i5]);
                break;
            }
            case G_QUAD: {
                int i0 = ((w0 >> 16) & 0xFF) / 2, i1 = ((w0 >> 8) & 0xFF) / 2, i2 = (w0 & 0xFF) / 2;
                int i3 = ((w1 >> 16) & 0xFF) / 2, i4 = ((w1 >> 8) & 0xFF) / 2, i5 = (w1 & 0xFF) / 2;
                if (i0 < 128 && i1 < 128 && i2 < 128) raster_tri(s, s.vtx[i0], s.vtx[i1], s.vtx[i2]);
                if (i3 < 128 && i4 < 128 && i5 < 128) raster_tri(s, s.vtx[i3], s.vtx[i4], s.vtx[i5]);
                break;
            }
            // --- texture pipeline (W105): decode enough of the RDP tile state to sample.
            case G_SETTIMG: {
                // Declares the source image in RDRAM. For this scene the "current" SETTIMG at
                // draw time is the group's texel source (the palette SETTIMG is consumed by the
                // immediately-following LOADTLUT below, which snapshots it into tlut_addr).
                s.timg_addr = w1;
                break;
            }
            case G_LOADTLUT: {
                // The palette source is the SETTIMG that just preceded this command.
                s.tlut_addr = s.timg_addr;
                s.tlut_count = ((w1 >> 14) & 0x3FF) + 1;   // 16 => CI4, 256 => CI8
                break;
            }
            case G_SETTILE: {
                // Two-tile idiom: tile 7 is the LOADBLOCK load tile, tile 0 is the render tile
                // that G_TEXTURE selects. Only the render tile's fmt/siz decode the pixels.
                uint8_t tile = (w1 >> 24) & 0x7;
                if (tile == 0) {
                    s.rt_fmt = (w0 >> 21) & 0x7;
                    s.rt_siz = (w0 >> 19) & 0x3;
                    s.rt_cmS = (w1 >> 8)  & 0x3;   // clamp/mirror S (bit0=mirror, bit1=clamp)
                    s.rt_cmT = (w1 >> 18) & 0x3;   // clamp/mirror T
                }
                break;
            }
            case G_SETTILESIZE: {
                // Texel dims of the render tile (10.2 fixed): width=(lrs-uls)/4+1, likewise height.
                uint8_t tile = (w1 >> 24) & 0x7;
                if (tile == 0) {
                    uint32_t uls = (w0 >> 12) & 0xFFF, ult = w0 & 0xFFF;
                    uint32_t lrs = (w1 >> 12) & 0xFFF, lrt = w1 & 0xFFF;
                    s.rt_w = (lrs >= uls) ? ((lrs - uls) / 4 + 1) : 0;
                    s.rt_h = (lrt >= ult) ? ((lrt - ult) / 4 + 1) : 0;
                }
                break;
            }
            case G_TEXTURE: {
                // Enable bit in the low byte (this game: nonzero => on). Scale is unity (0xFFFF)
                // for every enabled instance, so we ignore w1 and apply vertex ST directly.
                s.tex_on = ((w0 & 0xFF) != 0);
                break;
            }
            case G_SETCOMBINE: {
                // Store the raw 2-cycle mux; raster evaluates (A-B)*C+D per pixel (W107).
                s.cc_w0 = w0; s.cc_w1 = w1;
                break;
            }
            case G_SETPRIMCOLOR: {
                // w1 = RGBA8888 primitive colour (combiner PRIM input).
                s.prim_r = (w1 >> 24) & 0xFF; s.prim_g = (w1 >> 16) & 0xFF;
                s.prim_b = (w1 >> 8)  & 0xFF; s.prim_a = w1 & 0xFF;
                break;
            }
            case G_SETENVCOLOR: {
                s.env_r = (w1 >> 24) & 0xFF; s.env_g = (w1 >> 16) & 0xFF;
                s.env_b = (w1 >> 8)  & 0xFF; s.env_a = w1 & 0xFF;
                break;
            }
            case G_SETFOGCOLOR: {
                s.fog_r = (w1 >> 24) & 0xFF; s.fog_g = (w1 >> 16) & 0xFF;
                s.fog_b = (w1 >> 8)  & 0xFF;   // alpha unused by the fog blend
                break;
            }
            case G_SETOTHERMODE_L: {
                // F3DEX **v1** encoding (this game is F3DEX v1, NOT v2): gsSPSetOtherMode stores
                // the SHIFT directly in byte1 and the bit-LENGTH directly in byte0 (v2 stores
                // 32-shift-len and len-1 -- decoding with the v2 formula computes the wrong mask
                // and silently strips the top blender bits, i.e. the CLR_FOG P1 selector, so
                // every fog/xlu surface reads as an opaque CLR_IN surface). gDPSetRenderMode uses
                // shift=3,len=29 => mask 0xFFFFFFF8, keeping the blender in bits 16-31 including
                // the P1 fog selector in bits 30-31. Masked-merge w1 into othermode_lo.
                uint32_t shift = (w0 >> 8) & 0xFF;
                uint32_t len   = (w0 & 0xFF);
                uint32_t mask;
                if (shift >= 32)                 mask = 0;
                else if (len == 0 || shift + len >= 32) mask = 0xFFFFFFFFu << shift;
                else                             mask = ((1u << len) - 1u) << shift;
                s.othermode_lo = (s.othermode_lo & ~mask) | (w1 & mask);
                break;
            }
            case G_RDPSETOTHERMODE: {
                // Full othermode set: w1 = the low word (render mode + blender) directly.
                s.othermode_lo = w1;
                break;
            }
            case G_DL: {
                bool branch = (((w0 >> 16) & 0xFF) != 0);
                if (branch) { start_addr = w1; if (!resolve(w1, s.seg, &off)) return; continue; }
                walk(s, w1, depth + 1);
                break;
            }
            case G_ENDDL: return;
            default: break;
        }
        off += 8;
    }
}

// Write a 24-bit bottom-up BMP (no deps, Windows/most viewers open it directly).
static bool write_bmp(const char* path, const Framebuffer& fb) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    int row_bytes = fb.W * 3;
    int pad = (4 - (row_bytes % 4)) % 4;
    uint32_t img_size = (uint32_t)((row_bytes + pad) * fb.H);
    uint32_t file_size = 54 + img_size;
    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    *(uint32_t*)(hdr + 2) = file_size;
    *(uint32_t*)(hdr + 10) = 54;      // pixel data offset
    *(uint32_t*)(hdr + 14) = 40;      // DIB header size
    *(int32_t*)(hdr + 18) = fb.W;
    *(int32_t*)(hdr + 22) = fb.H;     // positive -> bottom-up
    *(uint16_t*)(hdr + 26) = 1;       // planes
    *(uint16_t*)(hdr + 28) = 24;      // bpp
    *(uint32_t*)(hdr + 34) = img_size;
    std::fwrite(hdr, 1, 54, f);
    std::unique_ptr<uint8_t[]> row = std::make_unique<uint8_t[]>(row_bytes + pad);
    for (int i = 0; i < pad; ++i) row[row_bytes + i] = 0;
    for (int y = fb.H - 1; y >= 0; --y) {          // bottom-up
        for (int x = 0; x < fb.W; ++x) {
            int idx = y * fb.W + x;
            row[x * 3 + 0] = fb.rgb[idx * 3 + 2];  // B
            row[x * 3 + 1] = fb.rgb[idx * 3 + 1];  // G
            row[x * 3 + 2] = fb.rgb[idx * 3 + 0];  // R
        }
        std::fwrite(row.get(), 1, row_bytes + pad, f);
    }
    std::fclose(f);
    return true;
}

struct RenderStats {
    uint32_t verts_loaded, tris_in, tris_drawn, tris_clipped, pixels, tex_pixels;
    uint32_t fog_pixels;
    int16_t  fog_mul, fog_off;
    uint8_t  fog_r, fog_g, fog_b;
    bool vp_from_dl;
};

// Rasterize one frame's DL into `fb` (cleared first). Called on EVERY send_dl (the
// default renderer), so it is self-contained: a fresh transform stack + vertex cache
// per frame, reusing the caller's persistent framebuffer to avoid per-frame allocation.
static RenderStats render_into(Framebuffer& fb, const uint8_t* rdram, const OSTask* t) {
    fb.clear();
    RState s;
    s.rdram = rdram;
    s.fb = &fb;
    mat_identity(s.proj);
    for (int i = 0; i < 10; ++i) mat_identity(s.mv_stack[i]);
    for (int i = 0; i < 128; ++i) s.vtx[i].loaded = false;
    set_viewport_default(s);
    uint32_t dl_addr = (uint32_t)(int32_t)t->t.data_ptr;
    walk(s, dl_addr, 0);
    return { s.verts_loaded, s.tris_in, s.tris_drawn, s.tris_clipped, s.pixels, s.tex_pixels,
             s.fog_pixels, s.fog_mul, s.fog_off, s.fog_r, s.fog_g, s.fog_b, s.vp_set };
}

} // namespace swrender

class HeadlessRendererContext : public ultramodern::renderer::RendererContext {
public:
    HeadlessRendererContext() {
        setup_result = ultramodern::renderer::SetupResult::Success;
        chosen_api = ultramodern::renderer::GraphicsApi::Auto;
    }

    bool valid() override { return true; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&,
                       const ultramodern::renderer::GraphicsConfig&) override { return true; }
    void enable_instant_present() override {}
    // The game's main loop built a DL and osSpTaskStartGo -> submit_rsp_task delivered it
    // here. The software reference renderer (swrender) rasterizes it into m_fb on the
    // DEFAULT path every frame -- the trunk renders, so the trunk is what gets stress-tested
    // (RT64, #58, will replace swrender). Log the FIRST gfx OSTask once as the "gfx-OSTask
    // seam reached" runtime signal.
    void send_dl(const OSTask* t) override {
        static int count = 0;
        ++count;
        if (count == 1) {
            std::fprintf(stderr, "[gfx] first OSTask submitted to renderer (send_dl); task type=%u\n",
                         t ? (unsigned)t->t.type : 0u);
        }
        // Renderer-seam DL introspection (LAMBO_DL_INSPECT=1). One-shot at a target state
        // (LAMBO_DL_INSPECT_STATE, default 8) so we can characterise what the demo-race
        // actually asks to render without a renderer wired. Default build: env unset -> skipped.
        static const bool s_inspect = (std::getenv("LAMBO_DL_INSPECT") != nullptr);
        if (s_inspect && t && g_lambo_rdram) {
            static bool s_done = false;
            if (!s_done) {
                const char* se = std::getenv("LAMBO_DL_INSPECT_STATE");
                int target = se ? std::atoi(se) : 8;
                // state = high halfword of the word at 0x800CE6AC (MEM_H, matches state_probe()).
                uint32_t w = *(const uint32_t*)(g_lambo_rdram + (0x800CE6AC - 0x80000000u));
                int state = (int)((w >> 16) & 0xFFFF);
                if (state >= target) {
                    std::fprintf(stderr, "[dl-inspect] state=%d (>= target %d), send_dl #%d\n",
                                 state, target, count);
                    dlinspect::dump_summary(g_lambo_rdram, t);
                    s_done = true;
                }
            }
        }
        // Software reference renderer on the DEFAULT path: rasterize EVERY frame's DL into
        // the persistent framebuffer (the trunk renders + gets stress-tested; NOT gated).
        if (t && g_lambo_rdram) {
            if (!m_fb_ready) { m_fb.init(320, 240); m_fb_ready = true; }
            swrender::RenderStats rs = swrender::render_into(m_fb, g_lambo_rdram, t);
            // Frame CAPTURE-to-file (diagnostic knob, not a gate): save the first frame that
            // reaches the capture state for the port-vs-ares FB-diff harness. State + path are
            // configurable (LAMBO_DL_RENDER_STATE default 8, LAMBO_DL_RENDER_OUT default below);
            // they tune a default-on behaviour, they do not switch rendering on/off.
            // LAMBO_DL_RENDER_EVERY=N additionally re-captures every N send_dls after the
            // first hit (numbered .N.bmp suffixes) -- needed to compare ATTRACT DEMO CYCLES
            // against ares (W112: the attract plays different demo tracks per state-8 pass;
            // a single first-frame capture can be a different SCENE than an ares capture).
            static const char* s_every_env = std::getenv("LAMBO_DL_RENDER_EVERY");
            static const int s_every = s_every_env ? std::atoi(s_every_env) : 0;
            if (!m_frame_captured || (s_every > 0 && count >= m_next_capture)) {
                const char* se = std::getenv("LAMBO_DL_RENDER_STATE");
                int target = se ? std::atoi(se) : 8;
                uint32_t w = *(const uint32_t*)(g_lambo_rdram + (0x800CE6AC - 0x80000000u));
                int state = (int)((w >> 16) & 0xFFFF);
                if (state >= target) {
                    const char* out = std::getenv("LAMBO_DL_RENDER_OUT");
                    const char* base = out ? out : "dl_render_state8.bmp";
                    char pathbuf[512];
                    const char* path = base;
                    if (m_frame_captured && s_every > 0) {
                        std::snprintf(pathbuf, sizeof(pathbuf), "%s.%d.bmp", base, count);
                        path = pathbuf;
                    }
                    if (s_every > 0) m_next_capture = count + s_every;
                    bool ok = swrender::write_bmp(path, m_fb);
                    // Demo-track index (D_800CE774, mod-6, +1 per attract pass via func_80038D6C).
                    // Logged so every capture self-identifies its attract demo — W111 chased a
                    // "horizon gap" that was two DIFFERENT demos compared frame-to-frame.
                    uint32_t dw = *(const uint32_t*)(g_lambo_rdram + (0x800CE774 - 0x80000000u));
                    int demo_idx = (int)((dw >> 16) & 0xFFFF);
                    std::fprintf(stderr,
                        "[dl-render] captured state=%d demo_idx=%d frame (send_dl #%d) -> %s (%s)\n"
                        "[dl-render]   verts_loaded=%u tris_in=%u drawn=%u clipped=%u pixels=%u tex_pixels=%u  viewport=%s\n"
                        "[dl-render]   fog: pixels=%u mul=%d off=%d color=(%u,%u,%u)\n",
                        state, demo_idx, count, path, ok ? "written" : "WRITE FAILED",
                        rs.verts_loaded, rs.tris_in, rs.tris_drawn, rs.tris_clipped, rs.pixels, rs.tex_pixels,
                        rs.vp_from_dl ? "from-DL" : "default",
                        rs.fog_pixels, (int)rs.fog_mul, (int)rs.fog_off, rs.fog_r, rs.fog_g, rs.fog_b);
                    m_frame_captured = true;
                }
            }
        }
        // Periodic heartbeat: a SUSTAINED gfx pipeline (not the #58 1-task stall). Before the
        // __osViCurr/__osViNext retrace-promotion fix (vi_cb in main.cpp), this stuck at 1 forever.
        if (count % 30 == 0) {
            std::fprintf(stderr, "[gfx] send_dl count=%d (pipeline sustained)\n", count);
        }
    }
    void update_screen() override {}
    void shutdown() override {}
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }

private:
    swrender::Framebuffer m_fb;         // persistent: allocated once, re-cleared each frame
    bool m_fb_ready = false;
    bool m_frame_captured = false;      // first-frame-at-capture-state BMP already written
    int  m_next_capture = 0;            // next send_dl count for LAMBO_DL_RENDER_EVERY
};

std::unique_ptr<ultramodern::renderer::RendererContext>
create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle,
                      bool developer_mode) {
    g_lambo_rdram = rdram;
    // RT64 is the DEFAULT presenter (#58, flipped 2026-07-02). LAMBO_HEADLESS=1 (harness
    // knob) keeps the headless swrender, which remains the measurement instrument: it
    // rasterises into the RDRAM framebuffer the port-vs-ares harness byte-compares.
    // RT64 setup failure (no Vulkan device / no window) also degrades to swrender so
    // the boot probe still runs.
    if (lambo_rt64::enabled()) {
        auto rt64_ctx = lambo_rt64::create_render_context(rdram, window_handle, developer_mode);
        if (rt64_ctx) {
            std::fprintf(stderr, "[rt64] RT64 renderer ACTIVE (default presenter)\n");
            return rt64_ctx;
        }
        std::fprintf(stderr, "[rt64] RT64 setup failed -- falling back to headless swrender\n");
    }
    (void)window_handle;
    (void)developer_mode;
    return std::make_unique<HeadlessRendererContext>();
}

// ---- deterministic lighting self-test (LAMBO_LIGHTING_SELFTEST=1, no ROM) ----------
// Validates the W110 real-lighting path end-to-end WITHOUT the ROM or ares: builds a
// synthetic byte-swapped RDRAM holding a minimal F3DEX DL (G_MW_NUMLIGHT moveword +
// light-struct G_MOVEMEMs + SHADE-only SETCOMBINE + optional G_MTX + G_VTX + G_TRI1)
// and runs it through the REAL render path (swrender::render_into), printing the
// centre-pixel colour per case. tests/pivot/test_lighting.py recomputes the expected
// lambert sum INDEPENDENTLY in Python and compares -- a deterministic answer to "is
// the lighting math + light decode right" that bypasses the ares frame-misalignment
// trap entirely (W111: the demo-race scene never sets G_LIGHTING, so no ares FB-diff
// can exercise this path at state 8).
namespace selftest {

// Byte-order-mirrored writers (inverse of swrender's rd_* readers: aligned u32 is
// stored native; u16 at off^2; u8 at off^3 -- the N64Recomp MEM_H/MEM_B convention).
struct FakeMem {
    std::vector<uint8_t> buf;
    FakeMem() : buf(0x10000, 0) {}
    void w32(uint32_t off, uint32_t v) { *(uint32_t*)(buf.data() + off) = v; }
    void w16(uint32_t off, uint16_t v) { *(uint16_t*)(buf.data() + (off ^ 2)) = v; }
    void w8 (uint32_t off, uint8_t  v) { buf[off ^ 3] = v; }
};

// The W111-measured state-8 light set (colours 0-255, directions raw s8) -- using the
// real game's values keeps the test representative of the scene that matters.
struct TLight { uint8_t r, g, b; int8_t dx, dy, dz; };
static const TLight kL0  = {241, 254, 153, -11,  55, -101};   // warm dusk key
static const TLight kL1  = { 68,  68, 135,  11,  45,  101};   // cool blue fill
static const TLight kAmb = {  2,   2,   2,   0,   0,    0};

static void write_light(FakeMem& m, uint32_t off, const TLight& L) {
    // Light_t: col[3], pad, colc[3], pad, dir[3] (s8), pad. The renderer reads col + dir.
    m.w8(off + 0, L.r); m.w8(off + 1, L.g); m.w8(off + 2, L.b);
    m.w8(off + 4, L.r); m.w8(off + 5, L.g); m.w8(off + 6, L.b);
    m.w8(off + 8, (uint8_t)L.dx); m.w8(off + 9, (uint8_t)L.dy); m.w8(off + 10, (uint8_t)L.dz);
}

// One case: build the scene, walk it through render_into, return the centre pixel.
// `lit` sets G_LIGHTING (vertex bytes 12-14 = signed normal); otherwise they are RGB.
// `rot_z90` loads a 90-degree-about-Z modelview via G_MTX so the test also pins WHICH
// side of the matrix the normal multiplies on (row-vector n*M, same as positions).
static void run_case(const char* name, bool lit, bool rot_z90,
                     int8_t n0, int8_t n1, int8_t n2) {
    FakeMem m;
    write_light(m, 0x2000, kL0);
    write_light(m, 0x2010, kL1);
    write_light(m, 0x2020, kAmb);
    // 90-degree Z rotation, N64 fixed-point (16 s16 int parts then 16 u16 fracs),
    // row-major rows: [0,1,0,0] [-1,0,0,0] [0,0,1,0] [0,0,0,1].
    static const int16_t kRotZ90[16] = {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (int i = 0; i < 16; ++i) {
        m.w16(0x2800 + i * 2, (uint16_t)kRotZ90[i]);
        m.w16(0x2800 + 32 + i * 2, 0);
    }
    // Full-screen triangle in NDC (identity proj, w=1): contains the centre pixel for
    // both the identity and the Z-rotated modelview. All 3 verts share the normal/colour
    // so the sampled pixel is flat -- no interpolation sensitivity.
    static const int16_t kV[3][3] = {{-1, -1, 0}, {1, -1, 0}, {0, 1, 0}};
    for (int v = 0; v < 3; ++v) {
        uint32_t off = 0x3000 + v * 16;
        m.w16(off + 0, (uint16_t)kV[v][0]); m.w16(off + 2, (uint16_t)kV[v][1]);
        m.w16(off + 4, (uint16_t)kV[v][2]); m.w16(off + 6, 0);
        m.w16(off + 8, 0); m.w16(off + 10, 0);
        m.w8(off + 12, (uint8_t)n0); m.w8(off + 13, (uint8_t)n1); m.w8(off + 14, (uint8_t)n2);
        m.w8(off + 15, 255);
    }
    // SHADE-only combiner, both cycles: colour (0-0)*0+SHADE, alpha (0-0)*0+SHADE_A.
    // (A/B idx 15 and C idx 31 decode to 0 in this renderer's mux tables; D=4=SHADE.)
    const uint32_t cc_w0 = 0xFC000000u | (15u << 20) | (31u << 15) | (7u << 12) |
                           (7u << 9) | (15u << 5) | 31u;
    const uint32_t cc_w1 = (15u << 28) | (15u << 24) | (7u << 21) | (7u << 18) |
                           (4u << 15) | (7u << 12) | (4u << 9) | (4u << 6) | (7u << 3) | 4u;
    uint32_t p = 0x1000;
    auto cmd = [&](uint32_t w0, uint32_t w1) { m.w32(p, w0); m.w32(p + 4, w1); p += 8; };
    cmd(0xBC000002u, 0x80000060u);          // G_MOVEWORD NUMLIGHT: (2+1)*32 -> num_lights=2
    cmd(0x03860010u, 0x80002000u);          // G_MOVEMEM  G_MV_L0
    cmd(0x03880010u, 0x80002010u);          // G_MOVEMEM  G_MV_L1
    cmd(0x038A0010u, 0x80002020u);          // G_MOVEMEM  ambient (idx 0x86 + num*2)
    cmd(cc_w0, cc_w1);                      // G_SETCOMBINE (SHADE passthrough)
    if (rot_z90) cmd(0x01020040u, 0x80002800u);   // G_MTX LOAD modelview (no push)
    if (lit) cmd(0xB7000000u, 0x00020000u); // G_SETGEOMETRYMODE G_LIGHTING
    cmd(0x04000C2Fu, 0x80003000u);          // G_VTX 3 verts at v0=0
    cmd(0xBF000000u, 0x00000204u);          // G_TRI1 0,1,2
    cmd(0xB8000000u, 0x00000000u);          // G_ENDDL

    swrender::Framebuffer fb;
    fb.init(320, 240);
    OSTask task{};
    task.t.type = 1;
    task.t.data_ptr = (int32_t)0x80001000u;
    swrender::RenderStats rs = swrender::render_into(fb, m.buf.data(), &task);
    int idx = (120 * fb.W + 160) * 3;
    std::printf("[light-selftest] case=%s rgb=(%u,%u,%u) tris_drawn=%u pixels=%u\n",
                name, fb.rgb[idx + 0], fb.rgb[idx + 1], fb.rgb[idx + 2],
                rs.tris_drawn, rs.pixels);
}

} // namespace selftest

int run_lighting_selftest() {
    // Case normals: L0/L1 directions reuse the light's own s8 dir bytes (d==1 after both
    // normalise); the back-facing normal has negative dot with BOTH lights (ambient only).
    selftest::run_case("unlit_passthrough", false, false, (int8_t)(uint8_t)200,
                       (int8_t)(uint8_t)100, (int8_t)50);
    selftest::run_case("n_along_L0",  true, false, -11, 55, -101);
    selftest::run_case("n_along_L1",  true, false,  11, 45,  101);
    selftest::run_case("n_back_both", true, false,   0, -128,  0);
    selftest::run_case("rotz90_n_plus_x", true, true, 127, 0, 0);
    std::printf("[light-selftest] done\n");
    return 0;
}

} // namespace headless

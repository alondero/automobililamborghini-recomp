# `no_lod` audit — LOD mechanisms in race scenes (issue #88, map #87)

Audit of every level-of-detail (LOD) reducing mechanism in **race** scenes (state 6/7/8)
across 1P/2P/3P/4P, to scope the `graphics.json` `no_lod` boolean. Evidence per claim is
cited inline; anything not empirically verified is marked **probe needed**.

**Headline result: after the already-shipped baseline (A4 geometry-LOD patch,
`f3dex.forceBranch`, `textureLOD.scale`, #83 fog match, #84 sky match), no confirmed
mechanism remains that produces visible pop-up in race scenes.** Two candidates survive
at hypothesis level (per-mode light count; one gated overlay-sprite emitter), plus one
cheap completeness probe (per-mode car-mesh comparison). The game **never uses RDP
texture LOD/mipmapping at all**, and per-mode **draw distance is measured intact** —
two whole axes the map speculated about are empty.

## 1. Inventory

### Axis A — MIPS distance-test geometry LOD (game code)

`tools/scan_lod_patterns.py` (re-run 2026-07-10) reports 34 distinct functions with an
FP-compare→bc1 fingerprint. Each candidate's compare was located in the recompiled C
(`RecompiledFuncs/funcs_*.c` carries per-instruction VRAM comments) and classified by
what the branch actually gates:

| Candidate (syms.toml name) | Compare VRAM | What the branch gates | LOD? |
|---|---|---|---|
| func_80060464 | 0x8005FA34 | per-car geometry detail vs 100.0u — **the A4 patch**; branch already NOP'd at recompile (`[[patches.instruction]]` vram 0x8005FA3C) | YES — already always-on baseline |
| func_80056318 | 0x8005659C | texrect overlay sprite draw: `(f − 100.0) ≤ g` else per-object flag at 0x800A85F8+2i → skip. Emits G_RDPHALF_1/2 (0xB4/0xB3) + packed 12-bit rect coords via DL cursor 0x800A39CC. Called 3× from func_80050860 (2D overlay dispatcher) | **probe needed** — only surviving pop-up candidate |
| func_80032450 | 0x80031A04 | dist ≤ 100.0 → per-player camera-follow targets (0x800Bxxxx +0x69BC/0x69C0 copied to 0x800A2E48[i] with ±100 offsets) | no — camera math |
| func_800030F8 | 0x80002728 | attract/state timing after a `players < 2` check (players halfword = 0x800CE6A4) | no |
| func_80009158 | 0x8000874C | position-delta > 35.0 sets a flag | no |
| func_8000A6C0, func_8000F068, func_800152BC, func_800165FC | various | ratio/range/normalisation math on shared globals (0x800CE7C8 cluster) | no |
| func_800102D8 (sky emitter) | 0x8000F730 | angle wraparound at 360.0 | no |
| func_800170C4, func_8001EC1C, func_8005AC84 | various | clamps (±151, 6144, 135-fold) | no |
| func_80019D20, func_80028250, func_80037F5C | various | dot-product sign / atan2 quadrant handling | no |
| func_8002622C, func_8002D37C | various | progress/AI table comparisons | no |
| func_8002AE94 | 0x8002BEC8 | lap/position race logic (12.0 threshold on struct+0x58) | no |
| func_80036854 (lens flare, #40) | 0x80035CCC | flare angle bounds ±155 | no |
| func_80038D6C | 0x80039364 | `200 − counter` timer clamp | no |
| func_8004384C (3D viewport builder) | 0x80043264 | angle wraparound at 256.0 | no |
| func_80045BDC | 0x800450D4 | timer thresholds 20.0/12.0 | no |
| func_8005E86C | 0x8005DE08 | stack-local bounds/extent comparison (collision-shaped) | no |
| func_80060DE4, func_80063AE8, func_80064DE0 | various | min/max select over distances returned by func_8006424C (closest-point / closest-opponent search) | no |
| func_800657CC | 0x800654D8 | sqrt == 0 guard | no |
| func_80067770 | 0x80066C58 | RNG threshold → counter increment | no |
| func_8006AE7C | 0x8006A558 | audio ramp on 0x8023xxxx globals | no |
| func_800757B0, func_800758F0 | 0x80074CC8/0x80074E30 | `c.eq.s f,f` NaN guards in libm sin/cos tails | no |
| func_8005F12C (in `stubs`), func_8005F42C (in `ignored`/force_stub.txt) | 0x8005E6DC/0x8005E898 | never execute in the port; raw MIPS shape matches the min-select closest-point helpers above | no (dead in port) |

**Conclusion:** the ROM has exactly **one** geometry-LOD distance test (A4, shipped
always-on) and **one** unresolved sprite-gate candidate (func_80056318).

### Axis B — per-mode (player-count) rendering reductions

The environment-variant mechanism found in #84: func_800030F8 selects **variant 2** for
`players >= 3` (halfword 0x800A2FA0: 0 = 1P/2P, 2 = 3P/4P; per-circuit table ptr at
0x80098238, 28-byte rows — runtime-initialised, not static ROM data). Variant 2 differs
from variant 0 in (measured/disassembled, 2026-07-09/10):

| Knob | 1P/2P (variant 0) | 3P/4P (variant 2) | Status |
|---|---|---|---|
| Fog fm/fo | 25600 / −25344 (fog-near ndc_z 0.990) | 6400 / −6144 (0.960) | **FIXED** #83, `widescreen_fog_match` — stays outside `no_lod` per scope |
| Fog colour | (57,48,55) dusk grey | (4,4,16) near-black | FIXED #83 (same gate) |
| Sky panorama | emitter func_800102D8 called per viewport | call skipped (`slti $at,players,3` @0x80004E90); frame-clear fill colour (4,4,16) doubles as "sky" | **FIXED** #84 / PR #89, `widescreen_sky_match` |
| Scene lights | 3 lights | 2 lights | **REMAINING** — the only confirmed unaddressed per-mode reduction (suggested ticket 1) |
| Segment-2 base | 0x80000060 | 0x80000040 | asset-variant plumbing, not a knob by itself |
| Far clip / draw distance | — | — | **measured INTACT**: with fog widened (#83), distant terrain was already drawn in 3P/4P; smooth fog fade, no clip edge, in both RT64 and swrender |

### Axis C — RT64 knobs

`lib/rt64/src/common/rt64_enhancement_configuration.h` is the complete surface — six
fields total: `framebuffer.reinterpretFixULS`, `presentation.{mode,removeBlackBorders}`,
`rect.fixRectLR`, `f3dex.forceBranch`, `s2dex.{fixBilerpMismatch,framebufferFastPath}`,
`textureLOD.scale`. **The mip-bias / anisotropy / max-LOD / draw-distance knobs the map
speculated about do not exist in this RT64 vintage.** The two LOD-relevant knobs:

- `f3dex.forceBranch` — set `true` at `src/rt64_renderer.cpp:232`. Consumed per-command
  in `rt64_rsp.cpp:838/851` (G_BRANCH_Z/W always taken). Tunable per-frame (RT64's dev
  UI flips it live), so option-gating *would* be technically possible — but it stays
  always-on per scope.
- `textureLOD.scale` — set `true` at `src/rt64_renderer.cpp:234`. Consumed per-workload
  in `rt64_state.cpp:917`. Also per-frame tunable. **Inert for this title** (see Axis D).

### Axis D — texture LOD / mipmapping

**The game never enables RDP texture LOD.** Two independent checks (2026-07-10):

1. Code-built othermode-H words in RecompiledFuncs: the game's `G_SETOTHERMODE_H` (0xBA)
   commands touch only TEXTPERSP (`0x1301`), CYCLETYPE (`0x1402`), TEXTFILT (`0xC02`),
   TEXTLUT (`0xE02`) — zero TEXTLOD (`0x1002`) or TEXTDETAIL (`0x1101`) writes.
2. Whole-ROM scan for embedded DL words `BA001002` / `BA001101`: **0 hits**.

No mipmaps → no mip-pop exists, no per-texture bias to audit, and `textureLOD.scale`
has nothing to scale. This axis is empty.

### Axis E — fog / prim-depth / shading / particles / shadows / reflections

- **Fog** is F3DEX vertex fog, fully ROM-driven (3 code-built `G_SETFOGCOLOR` 0xF8
  emitters); per-mode difference fixed by #83's DL-rewrite. No other fog mechanism found.
- **Prim depth (`G_SETPRIMDEPTH` 0xEE):** zero code-built commands. (64 raw `EE000000`
  words in ROM are unattributed data with no DL context — no evidence of use.)
- **Geometry-mode toggles** (0xB6/0xB7, 18 code-built): shading-style state, not LOD.
- **Particles / shadows / reflections:** no per-mode or distance-gated mechanism
  surfaced in any measurement to date (#79/#83/#84 DL walks, swrender walker, RT64
  captures). No dedicated LOD system found in the scan either. Considered empty unless
  the Axis-B execution ticket's DL diff (below) surfaces something.

## 2. Impact ranking

1. **MEDIUM — 3P/4P scene lights (2 vs 3).** Constant shading difference vs 1P, visible
   in side-by-side comparison (noted "intact, deliberate" in PR #89); not pop-up, but
   per the map's goal ("3P/4P looks identical to 1P in LOD terms") it is the largest
   remaining per-mode reduction.
2. **LOW–MEDIUM (pending probe) — func_80056318 overlay-sprite gate.** A real
   draw/skip threshold on texrect sprites; unknown whether the gated class is ever
   visible as pop in normal race play. One breakpoint or DL-dump diff settles it.
3. **LOW (completeness probe) — per-mode car-mesh identity.** No evidence cars use
   simpler meshes in 3P/4P, but it has never been explicitly checked; a
   `LAMBO_RACE_DL_DUMP` vertex-count diff per mode closes it.
4. **NONE — everything else.** Geometry LOD (A4), G_BRANCH_Z/W (forceBranch), texture
   LOD (doesn't exist), draw distance (measured intact), fog/sky (shipped gates).

## 3. Patch surface (how to gate under `no_lod`)

**Config key** (all tickets share this): follow the `widescreen_fog_match` template —
`bool` global default `true` in `src/lambo_config.cpp`, JSON `no_lod` in
`to_json()`/`from_json()`, accessor `lambo::config::no_lod()` in `lambo_config.h` with
`LAMBO_NO_LOD=1/0` env override. No recompile needed for the key itself.

- **Lights match (ticket 1):** two implementation shapes, decide by measurement first —
  (a) DL-rewrite in the existing `lambo_fog_widescreen.cpp` walker pass (rewrite the
  `G_MW_NUMLIGHT` moveword / light movemem to variant-0 values when
  `no_lod() && players >= 3`), or (b) a `[[patches.hook]]` at the variant-2 light-count
  selection in func_800030F8 (same mechanism as #84's sky guard; needs the exact vram,
  found by diffing the variant rows). (a) touches only shipped C files; (b) needs
  toml + `scripts/gen_syms_toml.py` PATCH_BLOCKS + recompile.
- **Sprite gate (ticket 2, if probe confirms visible pop):** smallest fix is a
  `[[patches.hook]]` at the 0x8005659C compare routing the condition through a native
  helper that returns "always draw" when `no_lod()` (pattern: #84's
  `lambo_sky_match_1p_guard`). Recompile needed.
- **Car-mesh identity (ticket 3):** measurement only; patch surface defined by findings
  (if meshes do differ per mode, it becomes a segment-base / model-pointer hook ticket).

## 4. Per-mode cross-check

| Mechanism | 1P | 2P | 3P | 4P | Evidence |
|---|---|---|---|---|---|
| Env variant | 0 | 0 | 2 | 2 | #84 disassembly: 0x800A2FA0 mapping in func_800030F8 (code read; 2P not separately captured live) |
| Fog fm/fo/colour | 25600/−25344/(57,48,55) | = 1P (variant 0) | 6400/−6144/(4,4,16) | = 3P | #83 measured 1P vs 3P live (headless fog prints); 2P/4P by variant mapping |
| Sky emitter | called | called per viewport (verified) | skipped → fixed PR #89 | skipped → fixed, verified live | #84 session: 1P/2P DL dumps unchanged; 3P+4P RT64 captures before/after |
| Scene lights | 3 | 3 (variant 0) | 2 | 2 | #84 disassembly of variant rows; **live verify in ticket 1** |
| Draw distance | full | full | full (fog was hiding it) | full | #83 measurement 2: RT64 + swrender, no clip edge, no geometry pop when fog widened |
| Geometry LOD (A4) | patched | patched | patched | patched | recompile-time instruction patch, mode-independent |

Explicit-live-capture gaps (2P fog values, 4P fog values, per-mode light count) are
cheap to close with `LAMBO_WARP=1:3:0:<players>` + `LAMBO_RACE_DL_DUMP` — folded into
ticket 1's verification step rather than blocking this audit.

## 5. Suggested tickets (execution order)

1. **`no_lod`: config key + 3P/4P scene-light match** — introduce the `no_lod`
   graphics.json key (template above) and gate a lights-match mechanism on
   `no_lod() && players >= 3`. First step: capture variant-row diff live (read the
   per-circuit table behind 0x80098238 at 1P vs 3P via `LAMBO_RACE_DL_DUMP` RDRAM dump
   or ares) to get the full list of variant-2 deltas — anything else in the 28-byte row
   beyond lights/fog/segment-base joins this ticket's scope. Verify: RT64 3P/4P capture
   shows 1P-equivalent shading; 1P/2P DL dumps byte-identical. (~1 session)
2. **Probe + (conditionally) gate the func_80056318 overlay-sprite threshold** — gdb
   breakpoint on the skip path (or DL-dump diff with the flag forced) during a race
   lap; if a visible sprite class pops, hook the compare under `no_lod()`; if it's
   HUD-internal/invisible, close as no-op with the evidence. (~0.5–1 session)
3. **Per-mode car-mesh identity check** — `LAMBO_RACE_DL_DUMP` at 1P/2P/3P/4P, same
   vantage (LAMBO_WARP + drive_input), diff car-model vertex/tri counts. Expected
   result: identical → close as verified-empty; otherwise file the hook ticket the
   findings define. (~0.5 session, pairs naturally with ticket 1's captures)

No further tickets: axes C/D are empty (nothing exists to gate), and axis A beyond the
above is fully classified as non-LOD.

## 6. #84 verdict

**#84 was a per-mode rendering reduction (LOD-family), and it is already resolved** —
closed via PR #89 (2026-07-10) with its own default-on gate `widescreen_sky_match`,
following the same pattern as #83's `widescreen_fog_match`. It does **not** move under
`no_lod`: re-gating shipped defaults is explicitly out of scope for the map (same
ruling as the fog fix). `no_lod` covers only the *remaining* reductions (tickets above).

---
*Method note: MIPS classification used `tools/scan_lod_patterns.py` output cross-read
against the recompiled C (per-instruction VRAM comments) rather than raw disassembly;
extraction script preserved in the session scratchpad. ROM scans were byte-pattern
searches over the .z64. Per-mode facts cite the #83/#84 measurement sessions
(2026-07-09/10); nothing in this report rests on unverified session notes except where
marked "probe needed".*

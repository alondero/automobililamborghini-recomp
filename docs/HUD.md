# HUD architecture & widescreen pinning (issue #2 findings, 2026-07-05)

Reference for anyone (human or LLM) touching HUD/2D rendering. Everything here was
verified live against a driven 1P race (probe printfs in the recompiled C + decoded
frame captures of the display list). Names are splat-style (`lamborghini.syms.toml`
name space); runtime vram = splat − 0xC00.

## The 2D pipeline

- **DL write cursor: `0x800A39CC`** (runtime address). Every 2D helper allocates 8
  bytes per command: load cursor, store cursor+8 back, write two words through the old
  value. Native code can inject F3DEX (v1) commands at this cursor — this is the seam
  the widescreen brackets use (`src/lambo_hud_widescreen.c`).
- **Per-frame 2D dispatcher: `func_80050860`** (runtime 0x8004FC60), called each frame
  by the game-logic state machine `func_800030F8`. It is a mode-keyed wall of inline
  draw sections: 1P race, 2P split (two variants), pause, results, etc.
- The dispatcher's helpers (all take `(a0=data, a1=x, a2=y, ...)` with x,y in 320×240
  coords): `func_8004CCB4` text/string, `func_8004D468` glyph/number,
  `func_8004DB08` digit sequence, `func_8004C070` scaled text, `func_8005464C`
  table-driven (x,y,idx) draw.

## 1P race HUD element map (all verified live)

| Element | Drawn by | Call site (jal, runtime) | Anchor |
|---|---|---|---|
| Speedo dial face + digital speed + gear | `func_80056318` (runtime 0x80055718) | `0x8004FF60`, args (0, x=0xDC, y=0x98, speed float) | right |
| RANK label+value block | `func_800583B8` (0x800577B8) | `0x8004FF88`, (val, x=0x118, y=0x13) | right |
| LAP label+values block | `func_80058464` (0x80057864) | `0x8004FFA0`, (vals, x=0x28, y=0x13); draws label at x+2, values at x−0x10/x/x+0x10 | left |
| TIME label+value | `func_8004CCB4` pair at `0x800503E8`/`0x80050414` (x=0xA0) | centered — needs no pinning |
| Speedo NEEDLE | geometry built INSIDE `func_80056318`: one `G_MTX LOAD\|MODELVIEW` (word0 `0x01020040`) + rotation MULs + static DL; pivot trans ≈ (1000, −690) model units | right (via matrix nudge, see below) |
| Minimap track outline | polyline geometry, drawn elsewhere in the frame (2D ortho section starting ~`DL 0x8011EFD8`), untagged | centered (unpinned) |
| Minimap car dots + P1 label + player arrow | `func_80054FFC` (0x800543FC), jal `0x80050588`; a2/a3 = cos/sin of CAR HEADING (arrow rotation); dots are 2×2 texrects, arrow is 2 quads via matrices | centered (unpinned, must stay with map) |
| Top translucent bar | static DL `0x80167170` (one 296×6 texrect, teal prim color) via projection MTX at `0x800A2C40` | centered |
| Lens flare | alpha-0x0F/0x1E texrects drawn LAST in the frame | centered — game clips to 4:3, see follow-up |

Traps discovered the hard way:
- `func_800717E0` (0x80070BE0) is a DIAL ORCHESTRATOR for an **alternate dial style /
  other modes** — it does NOT run in a normal 1P race (the 1P path branches around it
  right after the needle/minimap call at `0x80050588`).
- `func_80054FFC` looks speed-driven (cos/sin floats) but is the MINIMAP overlay, not
  the needle. Pinning it detaches dots/arrow from the track outline.
- `func_80058C48(a0=1..5, 160, 112)` correlates with gear but is a centered element
  (draws in menus/other screens too) — not the needle.
- 2P split is TOP/BOTTOM: sections at `0x80050CE8`/`0x80050D00` (y=0x10) and
  `0x80050F2C`/`0x80050F44` (y=0x80) etc. RANK x=0x10E, LAP x=0x28 per half —
  same left/right anchors as 1P, so the same pinning pattern applies (unhooked so far).

## Injection mechanism (no MIPS patch pipeline needed)

`[[patches.hook]]` in `lamborghini.us.toml` (`func`, `before_vram`, `text`) injects raw
C text before the instruction at `before_vram` when N64Recomp emits the function
(vendored N64Recomp `recompilation.cpp:129`). The text runs in the recompiled function
scope (`rdram`, `ctx` available; implicit declaration links against natives).
Rules:
- `before_vram` must not be a branch delay slot.
- Place hooks BEFORE the game's cursor-allocation block for the following command
  (the alloc pattern is ~5 instructions: `lw` cursor / `addiu +8` / `sw` / `sw` to
  stack), or your injected commands land after the game's next command.
- Regenerating RecompiledFuncs: `N64Recomp.exe lamborghini.us.toml` from repo root,
  then delete `build/**/libRecompiledFuncs.a` before rebuilding.

## RT64 extended-GBI facts (verified against lib/rt64 source + live)

- Enable hook: `G_SPNOOP`(0x00 on F3DEX v1) with magic `0x525464`, w1 =
  `(1<<28)|0x64`. Must be emitted before any `0x64` extended command; idempotent.
- `gEXSetRectAlign` affects ONLY rects (texrects/fillrects). LEFT pin =
  `(LEFT, LEFT, 0,0,0,0)`; RIGHT pin = `(RIGHT, RIGHT, -1280, 0, -1280, 0)`
  (offsets are quarter-pixels; −1280 re-bases the 320-wide coordinate space);
  reset = ORIGIN_NONE (0x800). At 4:3 output the math degenerates — no gating needed.
- The game's own scissor is untagged, so RT64 centers it on 4:3 and CROPS moved rects.
  Each bracket must push + set a full-width extended scissor
  (`gEXSetScissor(0, LEFT, RIGHT, 0,0, 0,240)`) and pop on reset.
- **Never wrap a GEOMETRY draw in a wide scissor**: the draw merges the wide scissor
  into the fbPair scissor, flipping RT64's `coversWholeWidth` heuristic
  (`rt64_framebuffer_renderer.cpp:1493`) for every untagged full-width-viewport
  geometry in that frame — the minimap visibly squeezes toward center.
- `gEXSetViewportAlign` with a non-NONE origin puts geometry on a squeezed
  screen-scale path (not a pure translation) — unusable for matching a rect-pinned
  element. Untagged full-width-viewport geometry renders on the STRETCHED path:
  screen_x = game_x × wideWidth/320.
- Geometry elements that must track a rect-pinned element are therefore moved in
  GAME space (edit their modelview translation in RDRAM after the game builds it,
  before the task is submitted). See the needle nudge in `lambo_hud_widescreen.c`:
  walk the emitted DL between bracket cursors for `w0 == 0x01020040`, patch matrix
  element [3][0] (int at byte 24, fraction at 0x44). Calibration (16:9, Clamp16x9):
  needle units ≈ 0.065 game-px through the stretched path; +530 units lands the pivot
  on the RIGHT-pinned dial hub. Gated by `lambo_ws_hud_widescreen_active()`.

## Debug technique that finally worked

Printf probes in the (git-ignored) generated `RecompiledFuncs/funcs_*.c` — insert at
`RECOMP_FUNC void <fn>(` printing `ctx->r4..r7`, rebuild incrementally, drive a race,
group stderr by line. To see actual DL bytes, dump RDRAM around the cursor from a
native hook to a file and decode offline (F3DEX v1 decoder in session scratch;
`tools/` has no committed copy — rederive from this table if needed). The stale
region PAST the cursor is the previous frame's tail — same layout, already-complete.

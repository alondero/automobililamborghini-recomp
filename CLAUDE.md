Native PC port of the N64 game via *static recompilation* (N64Recomp → C, run on `ultramodern`+`librecomp`, rendered with RT64).
Same approach as [Snowboard Kids 2 Recompiled](https://github.com/cdlewis/snowboardkids2-recomp), [Smash Bros Recompiled](https://github.com/zestydevy/smash64r), [Bomberman 64 Recompiled](https://github.com/RevoSucks/BM64Recomp). 

## Mental model

- **Name-routed static recompilation.** N64Recomp translates ROM functions to C by *name*.
  `ultramodern`+`librecomp` provide the libultra/OS layer — there is NO hand-rolled HLE and
  NO CP0/IRQ kernel emulation to maintain. RT64 is the renderer (F3DEX v1 ucode).
- **`force_stub.txt`** lists functions deliberately left as empty stubs (not yet needed or
  routed natively). The porting loop is: run → it crashes on a CP0/MMIO instruction inside
  some primitive → hand that primitive to the native runtime (or un-stub the real function).
- **`RecompiledFuncs/` and `src/aspMain.cpp` are ROM-derived and git-ignored** — regenerated
  by step 3 of BUILDING.md. Never commit them.
- Boots through attract/title/menu to the demo race. Input, audio, rendering wired through RT64.

## Non-negotiable engineering principles

- **Source is ground truth; session notes are corruptible history.** Before reasoning about
  any function, read its actual body. "Per earlier findings" is a signal to re-read source.
- **Recompilation, not game design — NEVER invent mechanics.** No invented timeouts, counter
  seeds, transition values, or idle durations. If behaviour is missing, translate what the ROM
  actually does; read the disassembly first. A hand-rolled shortcut is *scaffolding*: name it
  as such in the comment, pair it with a tracker entry, and remove it when the real code lands.
- **Measure before architecture.** measurement → data → decode → fix. Run the cheap diagnostic
  first; don't stack implementation options without a falsification gate between them.
- **Verify empirically, not by name.** Whether a function is called / emits DL / owns a role —
  prove it with a breakpoint, watchpoint, or grep for the actual primitive constants.
- **Ship code, not notes.** A session whose only output is a note/plan is a failure mode — the
  next session distrusts unverified notes and re-derives them. Land the smallest real increment.
  Never hand the next session an "exact first move"; if you know what to do, do it now.

## Debugging

- **ares is reference.** Invoke the **`ares-debugger` skill** for any live-ROM
  work (read/write RDRAM, "who writes X" watchpoints, per-frame VI/DL capture). An empty
  `mcp__ares__*` response is NOT a blocker — it means use the skill that launches ares. VI base
  = `0xA4400010`; ares RDRAM is big-endian. To check port-vs-ROM data, capture an ares dump this
  way and diff it against a port dump.
- **Live-debug the port** with native gdb on the built binary (`build/lamborghini_modern`).
  Count breakpoint hits with `ignore N <big>` + `info breakpoints`, NOT printf-in-commands
  (silently undercounts in the multithreaded build).
- **Success metric = port-vs-ares convergence**, not screenshot diffs and not byte-identicality
  between builds. Replacing a stub with real translated behaviour moves the port toward ares —
  that divergence is *progress*.

## Toolchain gotchas

- **MinGW `bin` must be on PATH** or `gcc.exe` silently exits 1 (can't find its own DLLs).
- A `cc` shim may sit ahead of gcc in PATH on this box — pin `-DCMAKE_C_COMPILER`/`CXX_COMPILER`
  explicitly when configuring, or `project()` fails with "C compiler is broken".
- If a rebuild links stale recompiled code, delete stale archives under `build/` named
  `libRecompiledFuncs.a`.

## Game facts (verify from source / live-ares before building on them)

- **State machine 0→8:** attract → title → menu → demo race.
- **Audio:** voice alloc/steal search is `func_80080040`; the SDL sink needs a *persistent*
  SDL_AudioStream (stateless per-buffer resampling garbles); guest-word PCM needs a pair-swap
  un-swizzle; `get_frames_remaining` must report AI-rate (22050) frames, not device-rate (48000),
  or music plays ~2.2× slow. State 6 = music; state 8 = quiet music + SFX.
- **Input map:** A = confirm, B = cancel; menu input buffer `D_8011C640` (mind byte order).

## Test discipline

Gate on what changed. Recompilation-config / runtime-glue changes → build + boot smoke.

## Tracker
Use Github Issues

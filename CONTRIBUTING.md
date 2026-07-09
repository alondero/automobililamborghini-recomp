# Contributing to Automobili Lamborghini: Recompiled

Thanks for your interest. This is a single-maintainer port of an N64 game via static
recompilation — please read this before opening an issue or PR.

## Reporting a bug

Use the **Bug report** issue template. The fields are not bureaucracy — each one
maps to a layer the maintainer has to peel back to fix anything (ROM, build,
config, runtime). A save state (F7) + `LAMBO_WARP` recipe + last 30 lines of
stderr cut triage from hours to minutes. See the template for the full list.

## Proposing a change

Open an issue first. The tracker (epic **#26**) already lists the maintainer's
roadmap and tiering; check whether your idea is already on it before writing
code. PRs without a linked issue are likely to be closed with a pointer to the
tracker.

For very small fixes (typos, dead links, one-line bugs), a direct PR with a
clear "Closes #N" or "no linked issue — small cleanup" is fine.

## Building & running

See **[BUILDING.md](./BUILDING.md)**. The dependency-patch step (`git apply
.../0001…`) is the most common cause of "works on my machine" — the local
`build.sh` / `build.ps1` scripts mirror CI exactly, so use them. If you skip
them and hand-roll cmake, your build is unsupported.

## Development conventions

See **[CLAUDE.md](./CLAUDE.md)**. The short version:

- Match the existing comment density and idiom. Comments explain **why**, not
  what — prefer code that doesn't need a comment.
- Verify empirically (breakpoint, watchpoint, grep for DL constants), not by
  name or by analogy to other ports.
- Source is ground truth; session notes are not. Re-read the function before
  reasoning about it.
- Decompile, don't invent. Missing behaviour translates what the ROM does;
  hand-rolled shortcuts are scaffolding, not shipping code.

## Ground-truth reference

[ares](https://ares-emu.org/) is the reference for what the port should
reproduce. The `ares-debugger` skill drives a locally-installed ares build for
live-ROM comparison (read/write RDRAM, watchpoints, per-frame DL capture).
"Port converges to ares" is the success metric — screenshot diffs and
byte-identical builds are not.

> The committed Python harness lives at `tools/emu_instrumentation/`. The ares
> binary itself is **not** shipped in this repo (`tools/emulators/` is
> `.gitignore`d); install ares separately. The scripts default to looking for
> `tools/emulators/ares-base/ares-v147/ares.exe`; `tools/emu_instrumentation/run_ares_debug.py`
> also accepts a `--ares-exe <path>` override.

## Legal

This repository contains no ROM content — you must supply your own copy of
`Automobili Lamborghini (USA).z64` to build. By contributing, you affirm that
your patch is your own work, GPLv3-compatible, and free of upstream ROM bytes
or assets.

Code original to this repository is released under the
[GNU General Public License v3.0](./LICENSE), matching the wider N64Recomp
port ecosystem. Vendored submodules (`lib/N64ModernRuntime`, `lib/rt64`) and
patched dependencies retain their own respective licenses.
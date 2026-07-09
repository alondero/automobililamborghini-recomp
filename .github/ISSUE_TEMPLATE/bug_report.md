---
name: Bug report
about: Report a bug in the port. The fields below map to the layers the maintainer has to peel back to fix anything (ROM, build, config, runtime) — please fill them in.
title: "[BUG] "
labels: "bug"
assignees: ""
---

## Pre-flight

**ROM identifier.** The port only supports the USA release (`Automobili Lamborghini (USA).z64`). Other regions will misbehave.

- ROM filename:
- SHA1 (preferred) or CRC32: <!-- `sha1sum "Automobili Lamborghini (USA).z64"` on Linux, `certutil -hashfile "Automobili Lamborghini (USA).z64" SHA1` on Windows -->

**Build source.** Builds drift when submodules or patches are off the pinned commits — this is the #1 source of "works on my machine".

- Commit SHA (`git rev-parse HEAD`):
- Built with `build.sh` / `build.ps1`, or hand-rolled cmake?
- If hand-rolled, list which dependency patches you applied (the set is platform-specific — see `BUILDING.md` §2):

**Vanilla check.** Does the same thing happen on original N64 hardware, or in ares running the same ROM? *(Optional but huge — distinguishes port bugs from ROM bugs. If yes, link the ares run / save.)*

**GPU driver.** If you're on **Windows + Nvidia and the game crashes on boot, update your driver first** before filing. Old Nvidia drivers are the most common cause of boot crashes here.

- GPU:
- Driver version:

**Mods / local overrides.** *(Answer "no" if none.)*

- Any mods installed?
- Edited `graphics.json` from the defaults in `README.md`?
- Custom `lamborghini.syms.toml`, `force_stub.txt`, or `patches/` edits?

## The bug

**Describe the bug**
A clear and concise description of what the bug is.

**To Reproduce**
Steps to reproduce the behavior. If a `LAMBO_WARP=N` env var reproduces it from cold boot, **say so** — it makes triage much faster than "drive to the bug".

1. Go to '...'
2. Press '...'
3. See error

**Expected behavior**
A clear and concise description of what you expected to happen.

## Artifacts

**Save state.** The port supports full RDRAM snapshot save states (`F7` = save, `F8` = load; also `LAMBO_STATE_SAVE` / `LAMBO_STATE_LOAD` env vars; default file `lambo_savestate.lstate` written to the directory you launched the binary from — override with `LAMBO_STATE_FILE=<path>`). If you can attach a `.lstate` captured at the moment of the bug, it cuts triage from hours to minutes. Drag the file into the issue.

> If the bug is a *crash*, capture the state *just before* the crash triggers (the state file is the RDRAM snapshot — capturing during a faulted frame is unreliable).

**Warp recipe.** If reproducible via `LAMBO_WARP=circuit[:laps[:car[:players]]]` at boot, paste the exact value here.

**Screenshots / short video.** Drag into the issue, or link to a GitHub gist / imgur. Pastebins die.

## Environment

**Desktop:**
- OS: [Windows 10, Windows 11, Linux distro + version]
- CPU:
- GPU:
- GPU driver version:

**Window:** *(copy from your `graphics.json`, or say "defaults")*
- `wm_option` (windowed / fullscreen):
- Monitor refresh rate:
- `res_option`, `ar_option`, `hr_option`, `rr_option`, `msaa_option`, `api_option`:

**Input:**
- Controller type (gamepad / keyboard / other):
- Number of active players (1P / 2P / 3P / 4P):

**Audio affected?** (yes / no / one-line description):
**Are you on the latest release?** (yes / no / commit SHA):

## Logs

**Stderr.** Paste the **last ~30–50 lines of stderr** from `./build/lamborghini_modern`. The port logs warp lines (`[warp] CIRCUIT N: …`), audio warnings, and crash breadcrumbs to stderr — a single `[error]` line often names the function.

> **On Windows**, stderr only appears in the launching terminal by default. Run the binary from a `cmd` / PowerShell window so you can copy the output. If you launched by double-click, re-launch from a terminal.

**Crash output.** If the port hit a native crash, the crash block is **text on stderr** delimited by a banner like:

```
========================= NATIVE CRASH =========================
Reason: ...
```

Paste that whole block. *(The port does not produce a `.dmp` file — it prints a textual backtrace to stderr and `_Exit`s.)* Screenshot the OS crash dialog if one appeared.

## Triage helpers

**First seen in build.** Which commit / release did this first appear in? If you don't know, leave blank — even a rough guess helps bisect.

**Additional context.** Anything else relevant — other apps running, antivirus, recent driver updates, fresh `git pull` vs long-standing checkout, etc.
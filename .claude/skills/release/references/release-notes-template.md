# Release notes template

Skeleton for the body of a release. Replace `<PLACEHOLDER>` text with real
content. The required sections, in order, match the shape of v0.1.0 /
v0.3.0 / v0.4.0 / v0.4.1 / v0.4.2 — keep them in this order so users
comparing releases can scan them in parallel.

```markdown
> **Pre-release quality.** Built from `main` for early testing and feedback. Expect rough
> edges — crashes, audio glitches, and missing native translations are still the rule, not
> the exception. Please open issues rather than running off the latest commit silently.

## ⚠️ You must supply your own ROM

**This release contains no game assets or code from the original cartridge.** It ships
only the recompiled executable. To play, you must obtain a legal copy of
`Automobili Lamborghini (USA).z64` (the North American release is the only one
supported) and place it next to the executable before launching. See **[BUILDING.md]**
for the full instructions; in brief:

1. Extract the archive for your platform into its own folder.
2. Drop `Automobili Lamborghini (USA).z64` into that folder.
3. Launch the binary.

Game saves go alongside the ROM (or under `%LOCALAPPDATA%\LamborghiniRecomp` on
Windows / `~/.config/LamborghiniRecomp` elsewhere — see **README.md**).

## What's working

<One-paragraph headline: what is THE big user-facing change since the
previous release? Match the tone of the v0.4.2 example — "v0.4.1 shipped
a manual escape hatch that required editing `graphics.json`; v0.4.2 makes
it automatic for modern Intel." Quantify if possible (number of fixes,
number of new build knobs, etc). End with "There's still glitches so
please report issues you find." — this line is in every prior release.>

Closed since <PREV_VERSION>:

<Category 1 — e.g. "Rumble", "Stability / driver compatibility",
"Widescreen / multiplayer", "Developer tooling">

- **#<ISSUE> / PR #<PR>** — <One sentence: what changed, with the user
  benefit first. Cite the implementation location if non-obvious
  (patches/NNNN, src/file.c, docs/foo.md). Link-style references like
  `[docs/rumble-triggers.md]` should resolve at v<NEW_VERSION>.>

<Category 2>

- ...

## What's not done yet

This is still pre-1.0 — plenty of `force_stub.txt` still includes real game primitives
rather than no-ops. The tracker is the canonical "what's next" list.

<Optional follow-up paragraph — only if a tracked issue is opened but not
yet fixed in this release. State the gap, the issue number, and the
mitigation (e.g. "still gets the SEVERE advisory popup instead of a
silent black screen").>

If you find a crash, please open an issue with `lamborghini_crash` style notes:

- Output window text (or the `crash-*.txt` dump if the native handler caught it)
- Commit / build provenance (below)
- Approximate in-game location (title screen / car-select / race lap 2 / etc.)

## How to run

### Linux

\`\`\`bash
unzip lamborghini-recomp-linux-x64.zip -d lamborghini-recomp
cp "Automobili Lamborghini (USA).z64" lamborghini-recomp/
./lamborghini-recomp/lamborghini_modern
\`\`\`

Requires `libsdl2-2.0-0` and a Vulkan-capable GPU + driver at runtime. Most
desktop distros ship these already.

### Windows

Extract `lamborghini-recomp-windows-x64.zip` anywhere, drop your ROM in the same
folder, and double-click `lamborghini_modern.exe`. The bundled `SDL2.dll`,
`dxcompiler.dll`, and `dxil.dll` must stay alongside the EXE.

**F11 / Alt+Enter** toggles fullscreen (and remembers the choice next launch).

## Build provenance

- **Commit:** `<SHORT_SHA>` (main, <YYYY-MM-DD>)
- **Workflow run:** [#<RUN_ID>][run] (Build & Release, dispatch from this SHA)
- **Toolchain:** Linux build on `ubuntu-latest` with gcc / cmake / ninja /
  `libsdl2-dev` / `libvulkan-dev`. Windows build on `windows-latest` with
  MinGW-w64 GCC + Ninja (PowerShell, not MSYS bash) per the project's toolchain
  notes.

---

If you'd rather build it yourself from source instead of running a pre-built
binary, see [BUILDING.md]. For graphics settings, see [README.md].

[BUILDING.md]: https://github.com/alondero/automobililamborghini-recomp/blob/<VERSION>/BUILDING.md
[README.md]: https://github.com/alondero/automobililamborghini-recomp/blob/<VERSION>/README.md
[run]: https://github.com/alondero/automobililamborghini-recomp/actions/runs/<RUN_ID>
[<any linked doc>]: https://github.com/alondero/automobililamborghini-recomp/blob/<VERSION>/<path>
```

## Category names actually used in prior releases

Pick from this list — don't invent new categories without a good reason:

- **Rumble**
- **Stability / driver compatibility**
- **Widescreen / multiplayer**
- **Widescreen — skybox / lens flare**
- **Widescreen graphics**
- **Developer tooling (now ships in the binary)**
- **CI / build**
- **Project / developer docs**
- **Release packaging / CI**
- **Texture tooling (developer-facing)**

If a release has only one category (v0.4.2), don't pad — one is fine.

## Tone notes

- Lead each bullet with the user benefit, then the mechanism.
- Quantify with concrete numbers (LAMBO_PAK_TRACE logged 79 / 2395 events;
  5 vs 3-4 segment groups; etc.) — much better than "improved performance".
- Inline code env vars (`LAMBO_PAK_TRACE`, `LAMBO_NO_LOD`) without prose
  explanation; experienced users know.
- Cite issues/PRs as `**#N / PR #N**` — GitHub auto-links both numbers.
- The "Pre-release quality" blockquote is in every release and is the
  *body copy* — it is NOT the GitHub `prerelease` flag. See gotchas.md.
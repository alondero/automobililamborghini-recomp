# Prior release format reference

The shape of v0.1.0 / v0.3.0 / v0.4.0 / v0.4.1 / v0.4.2 release notes,
side-by-side. Use this to match voice and structure when drafting the next
release — readers compare releases by scanning these sections in parallel.

## Common header (every release)

Every prior release opens with this exact blockquote:

> **Pre-release quality.** Built from `main` for early testing and feedback.
> Expect rough edges — crashes, audio glitches, and missing native
> translations are still the rule, not the exception. Please open issues
> rather than running off the latest commit silently.

Don't paraphrase. The "Pre-release quality" word "Pre-release" is the
project's voice; rewriting it dilutes the convention.

The "## ⚠️ You must supply your own ROM" section is also identical across
releases — same three-step "extract → drop ROM → launch" instructions,
same save-path note. The wording is stable; copy it verbatim.

## What's working — headline pattern

The first paragraph of "What's working" is the single most important
sentence in the release. The pattern is:

1. State the high-level capability (game is playable, widescreen works,
   rumble works).
2. Name THE one change that's the headline for this release.
3. Optionally quantify (X fixes, Y new knobs).
4. End with "There's still glitches so please report issues you find." —
   this line is in every prior release.

Examples from real releases:

- **v0.4.0**: "The headline of this release is **3- and 4-player
  split-screen**: the 2×2 quadrant views used to be pillarboxed with dark
  side bars, and now each quarter fills its slice of a wide monitor —
  with the per-quadrant HUD text, minimap, fog, and sky all tracking the
  wide frame."
- **v0.4.1**: "The headline of this release is **N64 Rumble Pak support**:
  the ROM's per-frame PWM rumble engine now drives SDL pad rumble at the
  moments ares rumbles (with the Controller Pak save flow intact in the
  same session). Two follow-up fixes round out this release — ..."
- **v0.4.2**: "This is a small follow-up to the v0.4.1 Intel driver
  advisory (#110): where v0.4.1 shipped a manual escape hatch that
  required editing `graphics.json`, **v0.4.2 makes the fix automatic for
  modern Intel** — affected users no longer need to take any action."

Notice the v0.4.2 example explicitly frames itself as a follow-up — that's
the right move when a release has a single small change.

## Closed since — category names

The "Closed since" list groups by theme, not by PR. Categories that have
actually been used:

| Category | Used in |
|----------|---------|
| Widescreen graphics | v0.3.0 |
| Widescreen — 3P/4P split screen | v0.4.0 |
| Widescreen — skybox | v0.4.0 |
| Rumble | v0.4.1 |
| Stability | v0.4.0 |
| Stability / driver compatibility | v0.4.2 |
| Widescreen / multiplayer | v0.4.1, v0.4.2 |
| Developer tooling | v0.4.0, v0.4.1 |
| Developer tooling (now ships in the binary) | v0.3.0 |
| CI / build | v0.3.0 |
| Release packaging / CI | v0.4.0 |
| Project / developer docs | v0.4.0 |
| Texture tooling (developer-facing) | v0.3.0 |

A single-category release (like v0.4.2) is fine — don't pad. A multi-PR
release (like v0.4.0) usually gets 3-5 categories.

## Bullet format

Each bullet follows the same shape:

```
- **#<ISSUE> / PR #<PR>** — <prose>. <mechanism details>. <numbers>. <links>.
```

Real examples:

- **v0.4.1**: "- **#101 / PR #108** — N64 Rumble Pak now rumbles the SDL
  controller in a real race. The ROM issues raw SI motor frames through a
  custom start/stop pair (audit in #102 / PR #107 — the ROM carries a
  complete per-frame PWM engine with per-channel intensity, every link in
  the chain is emitted and reachable). The port now forces the
  rumble-present flag and runs native `osMotorStart` / `osMotorStop`
  stubs..."

- **v0.4.2**: "- **#109 follow-up / PR #113** — Modern Intel GPUs (Iris Xe
  / Arc) now stay on D3D12 *automatically* — no `graphics.json` edit
  needed. v0.4.1's patch 0010 only worked when the user manually set
  `"api_option": "D3D12"`; two more users hit the silent black-screen
  wall before finding the workaround, so this makes it the default. The
  Intel force-Vulkan fallback in RT64 was originally written for 6th-gen
  HD Graphics (which device-*remove* on D3D12), but it was wrongly
  catching Gen12 Xe / Arc..."

Lead with the user benefit, then the mechanism. Numbers like "79 motor-start
/ 2395 motor-stop events" or "5 vs 3-4 segment groups" are great — they
make the change concrete. Inline code for env vars (`LAMBO_PAK_TRACE`,
`LAMBO_NO_LOD`).

## What's not done yet

The opening paragraph is fixed:

> This is still pre-1.0 — plenty of `force_stub.txt` still includes real
> game primitives rather than no-ops. The tracker is the canonical "what's
> next" list.

Below that, optional follow-up paragraph(s) name specific known gaps with
issue numbers. v0.4.2 has one — about explicit-Vulkan-on-old-Iris-Xe
runtime device-loss (tracked in #114). These are good to include because
they tell users where to report if they hit the edge case.

## How to run

The "How to run" section is identical across every release. Linux: unzip,
copy ROM, run. Windows: extract, drop ROM, double-click. Don't rewrite —
copy verbatim.

## Build provenance

```
- **Commit:** `<SHORT_SHA>` (main, <YYYY-MM-DD>)
- **Workflow run:** [#<RUN_ID>][run] (Build & Release, dispatch from this SHA)
- **Toolchain:** Linux build on `ubuntu-latest` with gcc / cmake / ninja /
  `libsdl2-dev` / `libvulkan-dev`. Windows build on `windows-latest` with
  MinGW-w64 GCC + Ninja (PowerShell, not MSYS bash) per the project's
  toolchain notes.
```

The toolchain note is verbatim across releases — it's a permanent reminder
about the MSYS bash / PowerShell quirk documented in CLAUDE.md.

## Closing

Always end with:

---

If you'd rather build it yourself from source instead of running a pre-built
binary, see [BUILDING.md]. For graphics settings, see [README.md].

[BUILDING.md]: https://github.com/alondero/automobililamborghini-recomp/blob/<VERSION>/BUILDING.md
[README.md]: https://github.com/alondero/automobililamborghini-recomp/blob/<VERSION>/README.md
[run]: https://github.com/alondero/automobililamborghini-recomp/actions/runs/<RUN_ID>

With any additional `[docs/foo.md]:` link references for documents cited in
the body (e.g. `[docs/rumble-triggers.md]:`).

## Reference: actual prior-release SHAs

For diffing/quoting:

- **v0.4.2** — `03e533a1425bf8aa895c67affbb78b22986f411c` — workflow #29203966918
- **v0.4.1** — `1f489fbcc3e6db1c82cd3fcbea27ebaf6ce3c866` — workflow #29198294316
- **v0.4.0** — `5e07da1759fb8aa484a87b0ee210303d3921950b` — workflow #29082039804
- **v0.3.0** — `9bef7dcd7df8aef0c2e5d5356f25ec62255611e2` — workflow #28996890290
- **v0.1.0** — `6f3e7d914ce7b2b2424faec8d4acd56cbd785059` — workflow #28778465982
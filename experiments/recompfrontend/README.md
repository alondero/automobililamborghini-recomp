# RecompFrontend experiment

This is a separate native preview of the real RecompFrontend settings and input
libraries. It does not boot Lamborghini or replace the game's frontend. Its
purpose is to evaluate the shared screens and 1-4-player profile/assignment flow
before migrating the game. Settings are isolated in `preview-config` beside the
preview executable; existing game settings and saves are not imported.

On the development machine, from the repository root:

```powershell
./experiments/recompfrontend/run.ps1
# Rebuild the already configured preview:
./experiments/recompfrontend/run.ps1 -Build
```

Choose **Settings and controller profiles** for General, Graphics, and Controls,
or **Assign 1-4 players** for the framework's device-assignment modal. The Controls
tab also exposes player assignment and controller/keyboard profiles. There is no
gameplay, audio sink, ROM selection, or mod execution in this preview.

New multiplayer keyboard profiles are intentionally blank in upstream. Assign a
keyboard, confirm, then use **Edit Profile** to bind keys. The same keyboard can
be added to further player slots with the small keyboard-plus button.

## Verified locally

The preview built and linked with MinGW GCC 15.2.0, then ran with the existing
patched RT64 on an NVIDIA RTX 3080. General/Graphics/Controls, player assignment,
and the mapping editor were inspected in the native window. Assigning a keyboard
to P1 and mapping N64 A to keyboard A wrote SDL scancode 4 to the P1 multiplayer
profile. After closing and restarting, reassigning the keyboard restored that
binding in the editor. Selecting Original 2x and clicking Apply wrote
`res_option: Original2x` through the framework's graphics persistence.

Closing the preview exited successfully. Four physical controllers and gameplay
have not been tested. The sample remapping remains in the isolated preview config
so it can be inspected; it is not a migrated game profile.

## What the port actually uses

The comment about AeroGauge and [PR #125](https://github.com/alondero/automobililamborghini-recomp/pull/125)
does not describe the current Lamborghini settings surface.

| Area | Current Lamborghini checkout |
| --- | --- |
| Toolkit | RmlUi, already a required submodule; custom RT64 render bridge |
| Shared frontend | RecompFrontend is not linked |
| Settings UI | Custom RmlUi launcher and fullscreen-capable overlay; additional Windows menu bar |
| Navigation | Custom keyboard/controller navigation and input capture |
| Persistence | Port-owned `graphics.json`, `controls.json`, and `player.json`; enhancements are extra graphics keys |
| Controller mapping | Custom SDL adapter and GUID-based controller profiles; keyboard fallback is hardcoded |
| Multiplayer host input | Only controller 0 is accepted by `input_get_input`; other ports report disconnected |
| ROM selection | Default filename can be overridden with a command-line path; validation happens before the launcher, with no picker |
| Audio | SDL audio sink, no host volume settings page |
| Mods | No port calls to scan/load the mod system; texture-pack paths are a separate RT64 feature |

Source: `src/main.cpp`, `src/lambo_config.*`, `src/controls/`, `src/ui/`, and
`assets/ui/`. The shared GraphicsConfig vocabulary comes from ultramodern;
that schema by itself does not supply a frontend or persistence implementation.

## Dependency experiment

Inspected and used revisions:

- RecompFrontend: `b1a1477c6556aeb7ed45defbfb5924f721efebc1`.
- Preview N64ModernRuntime: `cdf5abbd5026fef5c364c676e4667c45e42b6863`.
- Existing game N64ModernRuntime: `ae1ffbb909d9f93c88c41830deb539f7feef5ed2`.
- Existing patched RT64: `f0728a2520d5aa735886240de3fee75cc805f6d6`.
- RmlUi: both the game and frontend pin `7a06f27db04fe5d13a5dacc19b2b4544673a4eca`.
- Peer styles/fonts/icons: Zelda64Recomp `1a9c26613c6e0906140dc8bcca7362cbe00bf1eb`.

The host overrides the peer stylesheet with a font-only rule, keeping the shared
frontend's own theme. Six small original SVG placeholders fill icon names that
the frontend expects but that the peer's asset snapshot does not include.

The current runtime lacks `librecomp/config.hpp`. The upstream profiles source
passes a syntax check against it, but the graphics frontend does not. With the
new runtime, syntax checks pass for profiles, the graphics settings tab, and the
frontend renderer against the existing patched RT64. These checks alone do not
establish linking or gameplay compatibility.

The full preview build needs two frontend fixes: `ui_api_events.cpp` still includes
a game-relative `patches/ui_funcs.h`. `frontend-event-header.patch` changes this to
the event-structure header that RecompFrontend itself already ships. The same patch
selects the bundled FreeType DLL import library; the default MSVC static library
does not link with MinGW because it requires MSVC runtime symbols.

The preview runtime uses this project's patch `0012` for Windows RDRAM allocation.
It deliberately does not apply the game timing/scheduler patch `0001`: that patch
fails a clean application against the new runtime at `ultramodern/src/events.cpp`.
The preview runs no guest code. A game migration must adapt that patch and verify
30 Hz game timing, audio, saves, and rendering before adopting the new runtime.

## Build inputs

This first host is Windows/MinGW-specific and reuses the existing patched RT64
static archives and host shader tools. The frontend, runtime, and RmlUi are built
from source. It is not a portable release build or a replacement game executable.

Check out the revisions above recursively. Apply `frontend-event-header.patch` to
RecompFrontend and `patches/0012-n64modernruntime-lazy-rdram-commit.patch` to the
preview runtime. Keep all these checkouts separate from the game's dependencies.
Configure with MinGW and Ninja available on PATH:

```powershell
cmake -S experiments/recompfrontend -B .recompfrontend-experiment/build -G Ninja `
  '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' -DCMAKE_BUILD_TYPE=Release `
  '-DFRONTEND=<absolute RecompFrontend checkout>' `
  '-DRUNTIME=<absolute preview runtime checkout>' `
  '-DRT64_SOURCE=<absolute patched game RT64 checkout>' `
  '-DRT64_BUILD=<absolute existing game build directory>' `
  '-DPREVIEW_ASSETS=<absolute Zelda64Recomp assets directory>'
cmake --build .recompfrontend-experiment/build --target lambo_frontend_preview -j 8
```

`tools/recompfrontend_probe.py --help` describes the lighter source-compatibility
probe. Its generated include path must contain CMake's `miniz_export.h`.

## What adoption would replace

RecompFrontend supplies config-tab rendering, option dependencies, apply/discard
flows, JSON persistence, keyboard/controller remapping, profiles, and multiplayer
assignment. Those are credible maintenance savings. RmlUi is already present, so
adoption is not removing a UI toolkit; it adds the shared frontend and SVG support.
No CPU, memory, binary-size, or maintenance-effort reduction has been measured.

A real switch still needs a single owner for SDL events, input sampling, render
hooks, and graphics persistence. It must bridge the game's analog throttle/brake,
rumble, input capture/replay, enhancement options, portable paths, and player name.
The two `controls.json` formats differ; do not point the framework at existing
profiles without a migration. The game must report and sample all assigned player
ports, not only player one. Sound controls require a connection to the audio sink;
adding a sound tab alone does not change volume. ROM import and mod APIs likewise
need actual startup/loader integration, not only visible menu entries.

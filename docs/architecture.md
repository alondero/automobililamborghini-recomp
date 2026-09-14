# Architecture

This page describes the current system. It also names the gaps between the
current port and the long-term direction. The long-term direction is a human
decision, not an implementation claim.

## System shape

The current build is a static recompilation pipeline:

~~~text
USA ROM + symbol/config files
            |
            v
N64Recomp and RSPRecomp
            |
            v
generated RecompiledFuncs/ and src/aspMain.cpp
            |
            +--> hand-written port glue
            |        |
            |        +--> N64ModernRuntime / librecomp / ultramodern
            |        +--> RT64 presenter and renderer
            |        +--> RecompFrontend settings and input
            |
            v
desktop window, Android surface, input, audio, saves
~~~

Generated C is not a decompilation of the original source. It is generated from
the supplied ROM and the checked-in recompiler configuration. The port still
contains unnamed symbols, stubs, native hooks, and direct guest-memory access.

## Ownership and boundaries

| Area | Current owner | Boundary that must remain clear |
| --- | --- | --- |
| ROM-derived game functions | N64Recomp output | Never edit generated output by hand. Change symbols, config, or source hooks, then regenerate. |
| RSP audio output | RSPRecomp output plus the audio bridge | Keep guest task data and host audio state separate. |
| Game-specific behavior | src/ port code and source patches | A shortcut must name its invariant, failure mode, and replacement path. |
| Runtime and threads | lib/N64ModernRuntime after local patches | Do not silently change the pinned dependency. Record a patch or upstream proposal. |
| Rendering | RT64 after local patches, plus port display-list hooks | Put generic renderer behavior upstream when possible; keep game-specific projection and display-list policy in the port. |
| Settings and input UI | lib/RecompFrontend adapted by src/ui/ | There must be one owner for persistence and one event path. |
| Saves | Port-owned Controller Pak storage | The game thread produces Pak traffic; host storage publishes it without blocking the emulation thread. |
| Build and patch application | build.ps1, build.sh, and Android script | Manual commands are useful for diagnosis, but the scripts define the supported sequence. |

The game logic runs on a recompiled game thread. Rendering, audio, input, file
storage, and the frontend have host-side threads or queues. A host thread must
not assume that a guest pointer is a normal host pointer. Guest addresses are
offsets into the emulated RDRAM image and use N64 byte order.

## Generated and hand-written files

| Path | Status | Safe change |
| --- | --- | --- |
| RecompiledFuncs/ | Generated and ignored | Change ROM/config inputs. Do not edit. |
| src/aspMain.cpp | Generated and ignored | Change the RSP input or generation config. Do not edit. |
| lamborghini.syms.toml, lamborghini.us.toml | Checked-in generated configuration copies | Regenerate with scripts/gen_syms_toml.py when the source symbol data changes. Review the diff. |
| dump.toml, scripts/n64recomp_race.toml | Hand-maintained generation inputs | Record evidence for every symbol, hook, split, stub, or ignored function. |
| src/ | Hand-written port and tests | Preserve guest layout, units, ownership, and thread assumptions. |
| patches/ | Hand-written dependency deltas | Apply through the build scripts. Do not edit a dependency checkout and forget to update the patch. |
| tests/, scenarios/, recordings/ | Hand-written evidence and regression fixtures | Keep fixtures small, deterministic, and documented. |

force_stub.txt is also part of the generation boundary. A forced stub can keep
the port running, but it is not proof that the original function is unneeded.
Removing one requires a build and a behavior check.

## Guest memory is transitional infrastructure

Port-side code currently reads and writes guest memory for input bridges,
warps, interpolation, VI state, save states, track corrections, and some
renderer workarounds. These writes are fragile because they depend on fixed
addresses, layout offsets, timing, and the current generated code.

Treat each such access as a constrained bridge:

1. State the address, width, byte order, units, and owning game phase.
2. State which thread may access it and what makes the access safe.
3. Add a focused test or capture when a deterministic check is possible.
4. Link the evidence or investigation that established the layout.
5. Name the source-level replacement that would remove the bridge.

For example, the vehicle schema in src/lambo_vehicle.h is a packed guest
layout, not a host object. The save-state tool snapshots guest RAM at a frame
boundary, but it does not serialize native thread stacks or renderer/audio
state. It is a developer diagnostic, not a general quick-save promise.

The desired path is better decompilation and source-level patches, followed by
stable symbols and explicit hook contracts. A future code-mod API should expose
versioned functions and data owned by the game or a documented mod layer. It
should not require arbitrary mods to write raw addresses. No such complete API
exists today.

## Runtime, threads, and failure behavior

- The game thread consumes the final controller state and runs recompiled game
  functions.
- The input/frontend path receives host events and publishes controller
  profiles. Player one has an automatic preferred-controller/keyboard fallback;
  players two through four need assignment.
- The graphics thread consumes display-list work through the runtime and RT64.
  LAMBO_HEADLESS=1 selects the headless path used by scripted scenarios.
- Audio tasks are translated and sent to a host sink. Audio device recovery is
  tested, but device and driver behavior remains platform-specific.
- The host save path owns files. A write must not block the game thread. A save
  failure must be logged and leave the in-memory state explicit.
- A crash or missing dependency is a build/runtime failure to diagnose, not a
  reason to claim that a feature works.

The runtime patches include scheduler dispatch, save-state thread relinking,
lazy RDRAM commitment, host configuration storage, frontend integration, and
game presentation. Their exact scope is listed in the patch inventory.

## Renderer boundary

RT64 provides the general N64 renderer, texture hashing/replacement, and the
normal F3DEX/extended-GBI path. This project adds local patches and game-specific
display-list or projection policy. The distinction matters:

- ordinary texture packs and standard display-list translation belong to the
  RT64 capability set;
- src/stub_renderer.cpp is a separate project-owned software renderer used for
  headless diagnostics and fallback behavior. It is not an RT64 feature or an
  upstream RT64 fork, and its own measurements need their own tests;
- Lamborghini-specific widescreen origins, backdrop tags, FOV matching,
  split-screen policy, and the Intel selection workaround are project-specific
  until an upstream maintainer accepts a general form;
- Windows MinGW and Android build fixes may be useful upstream, but they are
  platform patches until reviewed against the pinned upstream source.

See reference/renderer.md for the patch-by-patch status. Do not describe a
local patch as an RT64 feature without checking the pinned source and recording
the comparison.

## Patches and upstreaming

The build scripts apply patches to pinned public submodules. A patch is not
automatically an upstream contribution. To turn a local delta into an upstream
proposal, a human maintainer must:

1. reduce it to a generic problem and a small reproducer;
2. compare it with current upstream code, not only the pinned checkout;
3. decide whether the behavior is reusable outside this game;
4. add tests or a capture that upstream can run;
5. record the upstream URL and status in docs/upstream-prs.md.

If a change is only needed for Lamborghini's game data, keep it in the port
and explain why.

## Evidence flow

ROM evidence should move through this chain:

~~~text
measurement or source observation
        -> dated investigation
        -> named invariant or hypothesis
        -> source/config patch or hook
        -> focused regression test or capture
        -> stable reference entry
~~~

If a step is missing, label the claim as incomplete. Issue and PR prose can
point to evidence, but it must not be the only place the evidence exists.

# Shared configuration frontend

Status: current integration as described by src/ui and the checked-in frontend
patch. Platform support is defined by README.md and BUILDING.md.

The verification paragraphs below describe the frontend migration checks and
their limits. They are not a replacement for a build or launch result.

The game now uses RecompFrontend's settings modal, controls editor, navigation,
player assignment and input profiles. `src/ui/lambo_frontend.cpp` is the host adapter;
`lambo_frontend_settings.cpp` registers the port's additional settings. RT64 rendering,
game-specific geometry/fog/camera fixes, replay and the guest input-release barrier remain
owned by the port.

Open settings with F1, Escape, or the controller menu button. The same overlay is rendered
in fullscreen. Inside it the D-pad or left stick moves focus, South (A) accepts, West (X) goes
back, and the shoulder buttons switch tabs. Menu actions use the same controller profile the
overlay opens with, so a pad can open and navigate the overlay before it is assigned to a
player; the bindings can be changed in Controls.

Controls > Assign players binds devices to N64 ports 1-4. At startup, player one
uses the connected legacy preferred controller, or the first available controller, with
keyboard fallback when no controller is available. Assign controllers explicitly
before multiplayer. Separate keyboard-player profiles can be configured, but players 2-4
start unbound to avoid overlapping keys. Device assignments are session-local; mappings and
controller profile choices persist.

## Options and storage

| Tab | Options | Persistence |
| --- | --- | --- |
| General | Rumble strength, stick deadzone, background input | Framework `general.json` |
| Graphics | Resolution, 1/2/3/4x downsampling, aspect, HUD placement, window mode, original/display/manual presentation rate, MSAA through 8x, HPFB, graphics API, developer overlay, initial window dimensions, texture pack/dump paths | Existing `graphics.json`, Apply/Discard |
| Enhancements | Multiplayer fog/sky matching, full geometry and six circuit switches, draw distance, fog density, camera distance/height/FOV | Existing `graphics.json`, immediate |
| Controls | N64 button/stick remapping, keyboard/controller profiles, 1-4 player assignment | Framework `controls-framework.json` |
| Driver | Driver-one name, startup launcher preference | Existing `player.json` and `graphics.json` |
| Pedals | Analog/digital throttle and brake, source axis/direction, deadzone and saturation | Framework `pedals.json`, Apply/Discard |

API, developer overlay, initial dimensions and texture paths require restart. Unsupported
MSAA options are disabled using the port renderer's actual device capabilities. Downsampling
only affects Original/Original2x resolution, matching the renderer. Game simulation remains
at its original rate independently of the presentation setting.

Graphics/enhancement persistence stays in the existing port implementation so unknown keys,
per-circuit numeric overrides, portable paths and environment overrides are preserved. The
framework's Config schema has a small downstream external-storage hook; there is no second
graphics/enhancements JSON writer. Native-menu changes and the guest Championship name editor
are synchronized back into the shared settings. An unrelated Apply does not undo a concurrent
fullscreen/native-menu change.

Legacy `controls.json` is left untouched. On first launch its custom controller mappings are
imported as selectable `Imported ...` profiles. Startup matches the actual connected device's
SDL GUID to its imported profile when there is no existing framework association. Previously
saved framework profile choices take precedence; they can be changed in Controls. Framework
identity hashes are reconstructed on load so those choices survive restarting the game.
The framework supports two bindings per input
and uses its shared stick deadzone/digital-axis threshold; extra legacy bindings and per-axis
threshold/deadzone calibration remain recoverable in the original file, but are not imported.
Review calibration in General after selecting an imported profile.

Pedal defaults are imported from the legacy preferred controller profile. Pedal calibration
then lives separately and applies to the controller assigned to player one, consistent with
the existing player-one analog race hooks. N64 digital bindings remain available in menus.
This does not add new analog gameplay mechanics to players 2-4.

This migration does not enable the mod loader, add a ROM picker, or add an unconnected sound
tab. The existing launcher/ROM selection policy and audio sink are retained.

## Building and checking

RecompFrontend is pinned at `b1a1477c6556aeb7ed45defbfb5924f721efebc1` and
N64ModernRuntime at `cdf5abbd5026fef5c364c676e4667c45e42b6863`. RmlUi is now the frontend's
nested dependency, not a second direct submodule. Use recursive submodule initialization
and the normal build scripts. CMake applies patches 0016/0017/0018 idempotently and refuses
conflicting dependency edits. Existing scheduler/audio/VI and lazy-RDRAM patches still apply;
the newer runtime already includes the former dummy-VI control-register fix. Patch 0018
restores the first-game-display-list call to the port renderer's `enable_instant_present()`.
Launcher dummy workloads keep their existing presentation behavior; gameplay uses RT64's
`PresentEarly` mode, as before the migration. Removing this transition made RT64 detect
60 Hz instead of this game's 30 Hz workload rate: a 120 Hz display then received only two
interpolated frames per workload instead of four. The fix retains display-rate interpolation
and both current library pins.

`lambo_frontend_settings_tests` checks legacy graphics/unknown-key preservation, option
coverage, Apply/Discard, cross-surface fullscreen updates, legacy profile conversion,
pre-attached SDL controllers without added events, preferred-device selection, imported
device mappings, profile save/reload, duplicate added events, four SDL virtual controllers,
reassignment, cross-player isolation and menu-action resolution for an unassigned
controller. The controller-Pak test also verifies all four players' buttons and signed stick
bytes through the actual guest Joybus bridge. The normal
`tools/run_game_scenario.py scenarios/harness-smoke.json` checks the game/replay path.
Native Windows overlay and race smoke checks were run for this migration. Linux, macOS,
Android and physical-controller/haptic validation still need platform/device testing.

`python tools/run_game_scenario.py scenarios/frontend-rt64-smoke.json` runs the native RT64
race and requires `PresentEarly` on the first game display list. The before/after replay
confirmed 30 Hz detection and four interpolated frames at 120 Hz after the repair. This
checks the presentation contract, not subjective motion quality. Controller auto-selection
is a startup policy; hotplug reassignment remains available through Controls > Assign players.

The original standalone exploration remains under `experiments/recompfrontend`; it is not
the game's build or runtime path.

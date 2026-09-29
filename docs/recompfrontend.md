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
in fullscreen. It is the only settings surface: the Windows build has no native menu bar,
so every option below is reached here. Inside it the D-pad or left stick moves focus,
South (A) accepts, West (X) goes back, and the shoulder buttons switch tabs. Menu actions
use the same controller profile the overlay opens with, so a pad can open and navigate the
overlay before it is assigned to a player; the bindings can be changed in Button bindings.
F11 and Alt+Enter still toggle fullscreen, and the window's own close button and Alt+F4
still exit.

Button bindings > Assign players binds devices to N64 ports 1-4. At startup, player one
uses the connected legacy preferred controller, or the first available controller, with
keyboard fallback when no controller is available. Assign controllers explicitly
before multiplayer. Separate keyboard-player profiles can be configured, but players 2-4
start unbound to avoid overlapping keys. Device assignments are session-local; mappings and
controller profile choices persist.

## The quit confirmation

Selecting Quit in the settings overlay while the game is running opens a confirmation.
Back cancels it and the game keeps running; the affirmative Quit stays on its own
confirmation. Back is the mapped menu action, not a fixed button, so the West button by
default and any remapped Back both work.

Before patch 0019 the prompt could not be dismissed with a controller at all. Upstream
builds it from plain elements that do not listen for menu actions, so a Back press never
became a menu action. A accepted because its mapping resolves to Return, which the
frontend's own element handling activates on the focused button. Patch 0019 makes the
prompt's window element answer the Back action through the same cancel path as its Cancel
button.

Back means the same thing on every prompt that uses the shared prompt, because it runs
that prompt's own cancel action. The call sites are unchanged, so this table is read from
`open_choice_prompt` and not introduced by patch 0019.

| Prompt | Confirm | Back / Cancel |
| --- | --- | --- |
| Quit confirmation | `ultramodern::quit()` | No-op. The game keeps running and you return to the settings. |
| Graphics options have unapplied changes | Saves the config, and closes if the tab was closing | Discards the unapplied graphics changes. Graphics is the only tab with `requires_confirmation`; General and Sound are not. |
| Overwrite Mods? | Installs, overwriting existing files | Aborts the installation. |
| Error Installing Mods | Hides the prompt | Hides the prompt. The install was already cancelled when this opened. |
| Unable to start with these mods | Hides the prompt | Hides the prompt. |
| Installing Mods / Please Wait | No buttons | Hides it. It closes itself once the synchronous install returns. |

Note the second row: on the graphics confirmation, Back discards unapplied changes. That
is the same action as its Discard button, but it is the one prompt where an accidental
Back press costs unsaved edits, so it is worth knowing.

Status: **Confirmed** in a played build. The quit confirmation was opened in the
running game, before and after patch 0019, with `tools/drive_input.py`:

| Step | Result |
| --- | --- |
| Unpatched build, Back on the confirmation | Prompt stays open. This is the bug. |
| Patched build, Back on the confirmation | Prompt closes, the game keeps running, the settings page that opened it is showing again. |
| Patched build, Quit on the confirmation | The game exits. |

F15 was used for the Back action. `recompui.h` maps the Back menu action to
`SDLK_F15` / `KI_F15`, and `cont_button_to_key` translates the controller's B
button to that same key, so a real F15 keypress takes the identical path. F16 and
F17 reach nothing: `RmlSDL::ConvertKey` in the pinned RmlUi has no case past
`SDLK_F15`.

The prompt's window element is reached once per press, in the bubble phase, and
the walk cannot leave the prompt's document to reach the settings page, so one
Back press cannot both dismiss the prompt and navigate the page below it. Hiding
the context blurs the document, so a held button repeats into the page underneath,
where Back has no callback.

**Unverified** with a physical gamepad: the button-to-`SDLK_F15` hop was not
played, in the running game or otherwise. `lambo_frontend_settings_tests` covers
it for the default and a remapped binding, and
`tests/test_prompt_back_action.py` checks the patch content.

Reaching the confirmation needs the keyboard. The header's Quit and Close buttons
are laid out past the right edge of the modal, so they cannot be clicked; the
route is F1 to open the overlay, then the Back action to focus the active tab,
then Right through the seven tabs and one more to leave them onto Quit.

## Options and storage

| Tab | Options | Persistence |
| --- | --- | --- |
| General | Rumble strength, stick deadzone, background input, driver-one name, startup launcher preference | Framework `general.json`, Apply/Discard; the name and launcher preference are also saved to `player.json` and `graphics.json` |
| Graphics | Resolution, 1/2/3/4x downsampling, aspect, HUD placement, window mode, original/display/manual presentation rate, MSAA through 8x, HPFB, graphics API, initial window dimensions, texture dump path | Existing `graphics.json`, Apply/Discard |
| Enhancements | Automatic pit-stops (off by default), multiplayer fog/sky matching, full geometry and six circuit switches, draw distance, fog density, camera distance/height/FOV, menu stick sensitivity | Existing `graphics.json`, immediate |
| Cheats | Session-only cheat toggles; see [Cheats](cheats.md) | Not saved; reset on each launch |
| Debug | Developer overlay | Existing `graphics.json`, immediate save, restart for effect |
| Button bindings | N64 button/stick remapping, keyboard/controller profiles, 1-4 player assignment | Framework `controls-framework.json` |
| Driving | Analog/digital throttle and brake (source axis/direction, deadzone, saturation) plus default-off Android gyro steering, steering range/deadzone/inversion, and auto-accelerate | Framework `driving-controls.json`, Apply/Discard |
| Mods | Package installation, enable/disable, ordering, per-mod settings, and texture packs | Runtime `mods.json` and `mod_config/` |

API, the Debug tab's developer overlay, initial dimensions and the texture dump path require
restart. Texture packs are not a Graphics option: the Mods tab installs, enables, disables, and
orders them. The legacy `texture_pack` key still loads ahead of Mods packs and survives a
Graphics save; it is not editable from the UI. Unsupported
MSAA options are disabled using the port renderer's actual device capabilities. Downsampling
only affects Original/Original2x resolution, matching the renderer. Game simulation remains
at its original rate independently of the presentation setting.

Graphics/enhancement persistence stays in the existing port implementation so unknown keys,
per-circuit numeric overrides, portable paths and environment overrides are preserved. The
framework's Config schema has a small downstream external-storage hook; there is no second
graphics/enhancements JSON writer. A fullscreen change made outside the overlay (F11 /
Alt+Enter) and the guest Championship name editor are synchronized back into the shared
settings. An unrelated Apply does not undo a concurrent fullscreen change.

The driver name and startup launcher preference moved from the old Driver page onto General,
which is the page the framework offers them. General therefore carries the same Apply/Discard
footer the Driver page had, and rumble strength, stick deadzone and background input are staged
behind that footer too. The name is a staged text edit: `player.json` is written when Apply
publishes the field rather than on every keystroke, so a half-typed name never reaches a saved
record. A pending name edit is judged on its own, so an unrelated pending slider does not stop a
Championship name save from appearing in the field. General is not external-storage: because the
framework owns `general.json` and serialises every option on the page, that file also carries a
copy of the two values. `player.json` and `graphics.json` remain the port's authoritative copies.

Legacy `controls.json` is left untouched. On first launch its custom controller mappings are
imported as selectable `Imported ...` profiles. Startup matches the actual connected device's
SDL GUID to its imported profile when there is no existing framework association. Previously
saved framework profile choices take precedence; they can be changed in Button bindings. Framework
identity hashes are reconstructed on load so those choices survive restarting the game.
The framework supports two bindings per input
and uses its shared stick deadzone/digital-axis threshold; extra legacy bindings and per-axis
threshold/deadzone calibration remain recoverable in the original file, but are not imported.
Review calibration in General after selecting an imported profile.

The Driving tab holds pedal calibration alongside the gyro/auto-accelerate assists; the
former separate Pedals tab and its `pedals.json` are gone. Pedal defaults are imported from
the legacy preferred controller profile, or from an existing `pedals.json`, which is read
once to seed the merged page. Pedal calibration applies to the controller assigned to player
one, consistent with the existing player-one analog race hooks. N64 digital bindings remain
available in menus. This does not add new analog gameplay mechanics to players 2-4.

The subsequent [mod integration](modding.md) enables the shared mod loader and Mods tab.
The existing launcher/ROM selection policy and audio sink are retained.

## Building and checking

RecompFrontend is pinned at `b1a1477c6556aeb7ed45defbfb5924f721efebc1` and
N64ModernRuntime at `cdf5abbd5026fef5c364c676e4667c45e42b6863`. RmlUi is now the frontend's
nested dependency, not a second direct submodule. Use recursive submodule initialization
and the normal build scripts. CMake applies patches 0016/0017/0018/0019 idempotently and refuses
conflicting dependency edits. Existing scheduler/audio/VI and lazy-RDRAM patches still apply;
the newer runtime already includes the former dummy-VI control-register fix. Patch 0018
restores the first-game-display-list call to the port renderer's `enable_instant_present()`.
Launcher dummy workloads keep their existing presentation behavior; gameplay uses RT64's
`PresentEarly` mode, as before the migration. Removing this transition made RT64 detect
60 Hz instead of this game's 30 Hz workload rate: a 120 Hz display then received only two
interpolated frames per workload instead of four. The fix retains display-rate interpolation
and both current library pins.

`lambo_frontend_settings_tests` checks legacy graphics/unknown-key preservation, option
coverage, the absence of a Graphics texture-pack control, `texture_pack` survival across a
graphics save and the explicit clear that migrates a pack to Mods, Apply/Discard,
cross-surface fullscreen updates, legacy profile conversion,
pre-attached SDL controllers without added events, preferred-device selection, imported
device mappings, profile save/reload, duplicate added events, four SDL virtual controllers,
reassignment, cross-player isolation and menu-action resolution for unassigned and
unresolved controllers, including the default and a remapped Back binding. The
controller-Pak test also verifies all four players' buttons
and signed stick bytes through the actual guest Joybus bridge. The normal
`tools/run_game_scenario.py scenarios/harness-smoke.json` checks the game/replay path.
Native Windows overlay and race smoke checks were run for this migration. Linux, macOS,
Android and physical-controller/haptic validation still need platform/device testing.

`python tools/run_game_scenario.py scenarios/frontend-rt64-smoke.json` runs the native RT64
race and requires `PresentEarly` on the first game display list. The before/after replay
confirmed 30 Hz detection and four interpolated frames at 120 Hz after the repair. This
checks the presentation contract, not subjective motion quality. Controller auto-selection
is a startup policy; hotplug reassignment remains available through Button bindings > Assign players.

The original standalone exploration remains under `experiments/recompfrontend`; it is not
the game's build or runtime path.

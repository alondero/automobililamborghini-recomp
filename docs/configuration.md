# Configuration

The [Cheats tab](cheats.md) offers immediate, session-only toggles. Cheats start
disabled on each launch and are not saved to a configuration file.

The game creates its configuration on first launch. The in-game frontend is
the normal editor. Direct JSON editing is useful for advanced options and
diagnosis.

The hidden `debug_mode` boolean in `general.json` enables the frontend RmlUi
debugger only when explicitly set to `true`; it defaults to `false` and requires
a restart. See [Debugging](debugging.md#frontend-rmlui-debugger).

## Where files live

By default:

- Windows: %LOCALAPPDATA%/LamborghiniRecomp
- Linux: $XDG_CONFIG_HOME/LamborghiniRecomp, or
  ~/.config/LamborghiniRecomp
- portable mode: beside the executable

Portable mode is enabled by --portable, LAMBO_PORTABLE=1, or an empty
portable.txt beside the executable.

The main configuration and save files are:

| File | Owner | Purpose |
| --- | --- | --- |
| graphics.json | port and runtime | Graphics, window, texture, camera, fog, and draw-distance settings. |
| controls-framework.json | RecompFrontend | Current controller profiles and bindings. |
| controls.json | legacy port import | Read as an import source when the framework profile does not exist. It is not the current writer. |
| player.json | port | Player name and player identity. |
| pedals.json | legacy import | Fills pedal values the Driving page has not stored yet, so previously saved settings survive the merge. It is not the current writer. |
| driving-controls.json | frontend/port integration | Pedal bindings, opt-in [steering response](steering.md) settings, plus default-off gyro steering, tuning, and auto-accelerate. |
| mods.json | runtime | Enabled mods and package order. |
| mod_config/ | runtime | Individual mod configuration. |
| general.json | frontend | General settings. Also keeps a copy of the Player Name and Show launcher at startup options. |
| lambo_controller_pak.mpk | port | The normal 32 KiB Controller Pak save image. |

Logs use the state directory, not the Linux config directory:

- Windows: `%LOCALAPPDATA%/LamborghiniRecomp/logs`
- Linux: `$XDG_STATE_HOME/LamborghiniRecomp/logs`, or
  `~/.local/state/LamborghiniRecomp/logs`
- portable mode: `logs/` beside the executable
- `LAMBO_LOG_DIR`: explicit log directory

The LAMBO_GRAPHICS_CONFIG and LAMBO_CONTROLS_CONFIG variables can point at
specific files for testing. LAMBO_LOG_DIR changes the log directory.

## Graphics file

For a weaker PC, open Graphics, select Low hardware under Graphics preset,
then press Apply. It uses 2x original resolution, turns supersampling Off (1x),
disables anti-aliasing, and selects the original presentation rate and standard
colour precision. Resetting supersampling is an extra low-hardware choice: it
replaces a saved X2/X3/X4 choice, and supersampling is ignored while resolution
is Auto. The preset also restores stock track geometry, draw distance, and
multiplayer fog and sky policies. The image will be less detailed and less
smooth. Game speed stays the same.

The preset stages its choices. Discard cancels them all. You can adjust the
renderer options before Apply. Apply saves and replaces four Enhancements
settings: Full track geometry is turned off, Draw distance is set to 1.0,
Match multiplayer fog to single player is turned off, and Show sky in 3-4 player
races is turned off. It also turns off ray-traced shadows, which are a Debug tab
option. Restore the four Enhancements choices in Enhancements, and ray-traced
shadows in Debug. After Apply, the selector returns to Keep current choices.
Window size, controller settings, camera settings and texture packs are kept.
Launch-time environment overrides still take priority.

The standard renderer fields are written by the shared runtime schema:

| Key | Default or role |
| --- | --- |
| res_option | Auto; internal resolution follows the window. On Android it is also kept within a [pixel-rate budget](ANDROID.md#automatic-resolution-budget), with a [2x limit on one Adreno 615 system driver](ANDROID.md#adreno-615-startup-device-loss). |
| wm_option | Windowed. |
| hr_option | Clamp16x9; HUD edge behavior in widescreen. |
| api_option | Auto; the backend is selected at startup. |
| ar_option | Expand; widen the view instead of stretching it. |
| msaa_option | MSAA2X. |
| rr_option | Display; presentation can be smoother while game logic stays at its native rate. |
| rr_manual_value | Used when rr_option is Manual. |
| hpfb_option | Auto; high-precision framebuffer policy. |
| ds_option | X2/X3/X4 supersampling; Off (1x) disables it. Auto resolution ignores this setting. |
| developer_mode | RT64 developer overlay; toggled on the Debug tab. Requires restart. |

The port adds these keys:

| Key | Default | Effect |
| --- | --- | --- |
| window_width, window_height | 1600, 900 | Windowed size. The Graphics tab picks it from common desktop resolutions and the window resizes as soon as you apply; a size edited here directly shows as Custom in the picker and is kept until a preset is chosen. Picked while fullscreen? The new size is used when you leave fullscreen. |
| texture_pack | empty | Compatibility override: one RT64 loose pack or .rtz pack loaded at startup, ahead of Mods packs. Not shown in Settings; manage packs in the Mods tab and clear this key to migrate. |
| texture_dump | empty | Directory for runtime texture dumps. |
| widescreen_fog_match | true | Use the open one-player fog policy for three- and four-player views. |
| widescreen_sky_match | true | Draw the sky panorama in three- and four-player views. |
| race_cars | 6 | Total cars (2-16) in single-player races. Takes effect at the next race or championship; time trials, demos and multiplayer keep their original sizes. |
| automatic_pit_stops | false | Automatically operate the refuelling and tyre-change stick controls during a pit stop. |
| remove_blue_hud_bar | false | Hide the decorative blue race HUD strip. Toggle Remove blue bar from HUD in Enhancements; applies immediately. |
| car_select_info | false | Show the car's name and stats on the car select screen. Toggle Show car details on car select in Enhancements; applies immediately. See [car select info](car-select-info.md). |
| menu_stick_sensitivity | 1.0 | Multiplier on the pre-race menu analog-stick deflection; 1.0 = stock. |
| no_lod | true | Enable port-side removal of some stock distance reductions. |
| no_lod_circuit | [true,true,true,false,false,false] | Per-circuit visibility-list policy. |
| fog_scale | 1.0 | Global fog density multiplier. |
| fog_scale_circuit | six 1.0 values | Per-circuit fog multiplier. |
| draw_distance | 1.5 | Multiplier on the authored segment radius while no_lod is enabled. |
| draw_distance_circuit | six 1.0 values | Per-circuit draw-distance multiplier. |
| camera_distance_scale | 1.0 | Chase-camera distance multiplier. |
| camera_height_scale | 1.0 | Chase-camera height multiplier. |
| camera_fov_add | 0 | Extra field-of-view degrees. |
| show_launcher | false | Show the launcher instead of booting directly into the game. |
| rt_shadows | false | Ray-traced sunlight shadows, pre-alpha. Set on the Debug tab. Expect bugs and lighting issues. Off keeps the original car shadow. |
| rt_shadow_rays | 8 | Shadow quality on the Debug tab: 4, 8 or 16 rays per pixel. |
| rt_shadow_softness | 0.5 | Shadow softness on the Debug tab, in degrees. 0 gives hard shadow edges; the maximum is 5. |

Camera distance, height, and FOV settings apply during player race countdowns
and driving, so the configured framing is already in place before GO.
Intro cinematics, results, and title-screen demos retain authored values.
Pausing a race keeps its configured framing.

Invalid or missing values fall back to defaults. The file is merged and
rewritten with known keys. Keep a backup before hand-editing it.

The loader starts with defaults, reads the selected JSON file, and preserves
unknown keys when it writes the file back. The `LAMBO_GRAPHICS_CONFIG` variable
selects a different JSON file. The environment overrides listed below take
precedence for their individual settings; they are useful for tests and
captures and are not saved back to JSON. `--portable`, `LAMBO_PORTABLE`, and a
`portable.txt` marker choose the directory before the file is selected.

Known bounds are intentionally small and practical: window size is 320..7680
by 240..4320; fog scale is clamped to 0..8; positive draw distance is clamped
to 0.1..100, while zero or a negative value means unlimited; camera distance
and height scales are clamped to 0.2..3; and the added camera FOV is clamped
to -20..60 degrees; menu stick sensitivity is clamped to 1..2.5; shadow rays
snap to 4, 8 or 16 and shadow softness is clamped to 0..5 degrees. The per-circuit
arrays have six entries. Other enum names
and shared-runtime fields should be changed through the frontend unless a
developer is testing a specific JSON value.

## Driving controls file

driving-controls.json is written by the Driving tab. It holds pedal
calibration, gyro steering and auto-accelerate, and the steering response keys
below. Steering settings are global for player one and stored with the Driving
tab, like the pedals: the live race stick does not pass through per-controller
profiles. Out-of-range values are clamped when they are applied. Every default
is the original game. See [Steering response](steering.md).

| Key | Default | Range | Meaning |
| --- | --- | --- | --- |
| steering_direct | false | true/false | Use the game's curve directly, without its slew limit and speed stages. |
| steering_game_deadzone | 4 | 0..20 | Stick units the game ignores at center; 4 is the original value. |

The `LAMBO_STEERING_*` variables in the developer table below override these
values for one run and are not saved.

## Environment overrides

Set drawing and camera overrides before starting the game. Fog/sky match,
no-LOD, fog scale, draw distance, camera distance/height/FOV, and menu stick
sensitivity overrides are captured at configuration load. They keep priority
over saved settings until the next launch. The in-game settings still update
live when their environment override is absent.

These are the stable player-facing variables supported by the current source:

~~~text
LAMBO_GRAPHICS_CONFIG
LAMBO_CONTROLS_CONFIG
LAMBO_LOG_DIR
LAMBO_PORTABLE
LAMBO_LAUNCHER
LAMBO_TEXTURE_PACK
LAMBO_TEXTURE_DUMP
LAMBO_FOG_MATCH_1P
LAMBO_SKY_MATCH_1P
LAMBO_NO_LOD
LAMBO_FOG_SCALE
LAMBO_DRAW_DISTANCE
LAMBO_CAMERA_DISTANCE_SCALE
LAMBO_CAMERA_HEIGHT_SCALE
LAMBO_CAMERA_FOV_ADD
LAMBO_MENU_STICK_SENSITIVITY
LAMBO_RT_SHADOWS
LAMBO_CONTROLLER_PAK_FILE
LAMBO_PLAYER_CONFIG
~~~

Developer-only variables are separate from the player settings contract. The
most useful ones are:

| Variable | Purpose |
| --- | --- |
| `LAMBO_HEADLESS` | Skip the window and use the diagnostic path. |
| `LAMBO_MODERN_INPUT` | Inject a button and stick value for a harness run. |
| `LAMBO_INPUT_PULSE` | Send a timed button pulse for menu navigation. |
| `LAMBO_WARP`, `LAMBO_WARP_MODE`, `LAMBO_WARP_DIFFICULTY` | Start a controlled race. |
| `LAMBO_WARP_REVERSE` | Set a controlled race's direction: 0 forward, 1 reverse. |
| `LAMBO_RACE_CARS` | Override the next eligible race's total field size (2..16). |
| `LAMBO_INPUT_RECORD`, `LAMBO_INPUT_REPLAY` | Record or replay input. |
| `LAMBO_INPUT_START_STATE`, `LAMBO_INPUT_START_DELAY`, `LAMBO_INPUT_EXIT_ON_END` | Control replay start and exit. |
| `LAMBO_TRACK_PATCH` | Load a Track Lab correction package. |
| `LAMBO_ANALOG_THROTTLE`, `LAMBO_ANALOG_BRAKE` | Inject pedal values. |
| `LAMBO_STEERING_DIRECT` | `1` or `0` forces direct steering response on or off. |
| `LAMBO_STEERING_DEADZONE` | Override the game steering deadzone (0..20). |
| `LAMBO_STEERING_TRACE` | Write one CSV row per player-one steering update to this path. |
| `LAMBO_RACE_DL_DUMP`, `LAMBO_MENU_DL_TRACE`, `LAMBO_MENU_DL_DUMP` | Capture display-list diagnostics. |
| `LAMBO_DL_INSPECT`, `LAMBO_DL_RENDER_STATE`, `LAMBO_DL_RENDER_OUT` | Inspect or capture the headless renderer. |
| `LAMBO_PAK_TRACE`, `LAMBO_INPUT_PROBE`, `LAMBO_CAR_TRACE` | Trace guest bridges. |
| `LAMBO_FAKE_GPU` | Reproduce a graphics-driver advisory without changing the device. |
| `LAMBO_CRASH_TEST`, `LAMBO_THREAD_TRACE` | Exercise crash and thread diagnostics. |
| `LAMBO_RT_SHADOW_FAULT=as` | Corrupt one ray-traced shadow caster range to prove the native-shadow fallback. |
| `LAMBO_RT_CAPTURE_KEEP_MSAA` | Keep the configured anti-aliasing during RT swapchain capture. |
| `LAMBO_RT_CAPTURE_KEEP_COLOR` | Keep the configured high-precision colour setting during RT swapchain capture. |
| `LAMBO_RT_SHADOW_DEBUG=<1-4>` | Show one ray-traced shadow term on receivers: 1 transmission, 2 open sky, 3 contact, 4 what remembered scenery changes (red channel with it, green and blue without). |
| `LAMBO_RT_SHADOW_CONTACT`, `LAMBO_RT_SHADOW_CONTACT_DISTANCE` | Contact shadow strength (default 0.55) and reach in renderer units (default 1.6). |
| `LAMBO_RT_SHADOW_SKY_DISTANCE`, `LAMBO_RT_SHADOW_SKY_OPEN` | Reach of the open-sky test (default 30, 0 disables it) and the open-sky fraction that keeps the full sun shadow (default 0.5). |
| `LAMBO_RT_SHADOW_CACHE=<0-2>` | Remembered static scenery: 0 off, 1 on (default), 2 trace only remembered scenery in place of the presented world, to check its coverage. |
| `LAMBO_TEST_RESIZE=<frame>:<W>x<H>` | Resize the window once at an input-replay frame. |

These variables are for developers. They are not a stable player settings
contract and may change with the harness.

## Command-line options

The executable accepts a ROM path as its first non-option argument. Useful
player-facing options are:

~~~text
--portable
--import-save PATH
--controller-pak PATH
~~~

Developer options include --track-patch, --console, --verbose, and
--log-level. Use the same command and configuration in a bug report.

The graphics API takes effect on the next launch. The texture-pack path also
needs a restart. Window size, window mode and enhancement values that the
frontend marks as live can be applied while the game is running. Use
Apply/Discard where the frontend shows those buttons.

## Save formats

The game uses a Controller Pak. The port can import raw 32 KiB .mpk/.pak,
four-port .mpk, Mupen64Plus-Next .srm, and DexDrive .n64 containers. The port
uses controller port one. --import-save leaves the source untouched and backs
up an existing port save before replacing it.

The port does not use a 2 KiB cartridge EEPROM file for this game.

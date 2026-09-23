# Configuration

The game creates its configuration on first launch. The in-game frontend is
the normal editor. Direct JSON editing is useful for advanced options and
diagnosis.

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
| player.json | port | Driver name and player identity. |
| pedals.json | frontend/port integration | Pedal bindings and settings. |
| mods.json | runtime | Enabled mods and package order. |
| mod_config/ | runtime | Individual mod configuration. |
| general.json | frontend | General frontend settings. |
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

The standard renderer fields are written by the shared runtime schema:

| Key | Default or role |
| --- | --- |
| res_option | Auto; internal resolution follows the window. |
| wm_option | Windowed. |
| hr_option | Clamp16x9; HUD edge behavior in widescreen. |
| api_option | Auto; the backend is selected at startup. |
| ar_option | Expand; widen the view instead of stretching it. |
| msaa_option | MSAA2X. |
| rr_option | Display; presentation can be smoother while game logic stays at its native rate. |
| rr_manual_value | Used when rr_option is Manual. |
| hpfb_option | Auto; high-precision framebuffer policy. |
| ds_option | Runtime supersampling option. |
| developer_mode | Runtime developer UI option. |

The port adds these keys:

| Key | Default | Effect |
| --- | --- | --- |
| window_width, window_height | 1600, 900 | Initial window size. |
| texture_pack | empty | One RT64 loose pack or .rtz pack at startup. |
| texture_dump | empty | Directory for runtime texture dumps. |
| widescreen_fog_match | true | Use the open one-player fog policy for three- and four-player views. |
| widescreen_sky_match | true | Draw the sky panorama in three- and four-player views. |
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
to -20..60 degrees. The per-circuit arrays have six entries. Other enum names
and shared-runtime fields should be changed through the frontend unless a
developer is testing a specific JSON value.

## Environment overrides

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
| `LAMBO_INPUT_RECORD`, `LAMBO_INPUT_REPLAY` | Record or replay input. |
| `LAMBO_INPUT_START_STATE`, `LAMBO_INPUT_START_DELAY`, `LAMBO_INPUT_EXIT_ON_END` | Control replay start and exit. |
| `LAMBO_TRACK_PATCH` | Load a Track Lab correction package. |
| `LAMBO_ANALOG_THROTTLE`, `LAMBO_ANALOG_BRAKE` | Inject pedal values. |
| `LAMBO_RACE_DL_DUMP`, `LAMBO_MENU_DL_TRACE`, `LAMBO_MENU_DL_DUMP` | Capture display-list diagnostics. |
| `LAMBO_DL_INSPECT`, `LAMBO_DL_RENDER_STATE`, `LAMBO_DL_RENDER_OUT` | Inspect or capture the headless renderer. |
| `LAMBO_PAK_TRACE`, `LAMBO_INPUT_PROBE`, `LAMBO_CAR_TRACE` | Trace guest bridges. |
| `LAMBO_FAKE_GPU` | Reproduce a graphics-driver advisory without changing the device. |
| `LAMBO_CRASH_TEST`, `LAMBO_THREAD_TRACE` | Exercise crash and thread diagnostics. |

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

The graphics API takes effect on the next launch. Window size and texture-pack
path also need a restart. Enhancement values that the frontend marks as live
can be applied while the game is running. Use Apply/Discard where the frontend
shows those buttons.

## Save formats

The game uses a Controller Pak. The port can import raw 32 KiB .mpk/.pak,
four-port .mpk, Mupen64Plus-Next .srm, and DexDrive .n64 containers. The port
uses controller port one. --import-save leaves the source untouched and backs
up an existing port save before replacing it.

The port does not use a 2 KiB cartridge EEPROM file for this game.

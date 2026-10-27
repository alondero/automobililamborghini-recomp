# Testing

Use the smallest test that can answer the question. A documentation change
does not require a ROM or a graphics device. A change to generated code,
guest memory, input, audio, or rendering usually needs more than a host test.

## Test layers

| Layer | Needs a ROM? | Needs generated output? | What it proves |
| --- | --- | --- | --- |
| Documentation check | No | No | Links, paths, placeholders, and required test documentation are valid. |
| Host unit tests | Usually no | Usually no | Pure config, path, input-state, codec, and policy behavior. |
| ROM-backed build tests | Yes | Yes | The current ROM and generation inputs produce the required symbols and code. |
| Headless scenario | Yes | Yes | Startup, warp, replay, input consumption, movement, swaps, and capture assertions. |
| Interactive renderer check | Yes | Yes | A real window, backend, display lists, texture packs, and visual behavior. |
| Android device check | Imported ROM on device | APK/native output | Launcher import, Vulkan driver, input, saves, and device behavior. |

## Supported commands

Run from the repository root.

~~~powershell
python tools/check_docs.py
python tools/check_branch.py --base origin/main --worktree
~~~

The zero-ROM Python host suite runs with:

~~~powershell
python -m unittest discover tests
~~~

This exercises the Python tests under `tests/` without a ROM, generated game
code, a graphics backend, or a native build.

The branch check compares the merge base with `HEAD`, then checks staged and
unstaged edits when `--worktree` is present. Use `--base REF --head REF` for a
specific review. A clean working tree alone does not check committed changes.
CI uses `--event-file` to select the PR base, the previous push tip, or the
default branch for a newly created or force-pushed branch. A first branch push therefore
checks its changes rather than rechecking every historical file.

## Continuous integration

The Host checks workflow runs on every push and pull request. It installs NumPy,
initializes the pinned RT64, Plume, RT64 shader compiler and RecompFrontend
checkouts, fetches the pinned Android SDL source, checks the complete branch
diff, then runs:

~~~text
python tools/run_host_tests.py --require tests.test_patch_composability --require tests.test_rt_shadow_capture --require tests.test_vulkan_descriptor_pool --require tests.test_vulkan_clear_bounds --require tests.test_vulkan_shader_nonuniform --require tests.test_android_swapchain_format --require tests.test_android_sdl_surface --require tests.test_vulkan_swapchain
~~~

Skipping a test in a required module, or failing to discover the module,
fails the job. ROM-dependent tests may still skip: public pull requests do not
receive the private ROM. To run the SDL tests locally, fetch SDL first:
`python -c "from scripts.build_android import fetch_dependency; fetch_dependency('SDL')"`. Locally,
`--require-all` requires every Python test, including ROM checks, to execute.

Native host checks configure the port without ROM-derived output, build
`lambo_host_tests`, and run CTest. They also compile and run the production mod
menu fixture with Clang and libc++, the standard library used by Android. This
catches incomplete container-element declarations that GCC accepts. It does
not replace an Android cross-build or device check.

Desktop release jobs build `lambo_host_tests`, run CTest, and run the headless
runtime acceptance suite before packaging. These jobs have the private ROM and
fresh generated output. Pull-request host jobs do not compile or play the full
game. Repository branch rules determine whether these checks block merging.

The Linux release job also fetches the [Japanese cartridge](japanese-rom.md)
and runs:

~~~text
python scripts/gen_jp_syms_toml.py --check
python tools/run_host_tests.py --require tests.test_jp_recompile
~~~

The first command prints the ROM hashes it verified. The second fails the job
if any of those tests skip, and prints each skip with its reason. The ROM tests
need both ROMs; the recompile tests also need a
built N64Recomp and gcc. Locally they skip without them. One test needs no ROM
and runs in the host suite on every pull request: it fails when the USA symbol
table or configuration changed after the Japanese tables were generated.

For dependency changes, run `python tools/check_patch_series.py`. It checks
the RT64 replay order against each platform's script and shared CMake calls,
and checks that every patch has a build application site. CI uses
`--apply-script linux` to prepare a clean checkout before its first CMake
configure, without needing a ROM. This command refuses conflicting changes.
The replay matrix lives in `tools/check_patch_series.py`; its platform orders
must match the supported build inputs. The checker also verifies Android's
recognized CMake suffix against the shared application order. Android source
preparation replays that suffix in a temporary index to recognize an already
composed checkout without relying on a later patch marker.

~~~text
python -m unittest tests.test_android_build tests.test_patch_composability tests.test_patch_wiring -v
~~~

The real-patch tests repeat preparation after each shared CMake patch,
including the complete series before and after 0042. They check source and
index preservation with staged, unstaged and untracked edits, and rejection
of conflicting early and late patch edits even when a later marker or older
composed state still matches.
No ROM, Android SDK or device is needed; initialize `lib/rt64` first.

Skip counts depend on the environment. `ShadowCaptureTests` skips its six tests
when NumPy is unavailable. `PatchCompositionTests` skips the platform replay
and Android preparation tests without an initialized RT64 submodule. The HUD
gitlink audit additionally skips in a source archive without the top-level
`.git` metadata; in a Git
checkout, a changed RT64 pin still fails the audit. Use `python -m unittest
discover tests -v` to see each skipped test and its reason.

`python tests/test_hud_blue_bar_rom.py` pins the blue HUD strip hook locations
and the USA ROM instructions they depend on. The instruction check runs when
`Automobili Lamborghini (USA).z64` is present at the repository root and skips
otherwise. It needs no generated output. CTest registers the same check as
`lambo_hud_blue_bar_rom`.

`python tests/test_car_card_rom.py` recomputes the car select card's derived
stats from the USA ROM, checks every card line fits its strip, and pins the
hook and font addresses. The ROM checks skip when the ROM is absent. CTest
registers it as `lambo_car_card_rom`.

`lambo_drive_input` runs `python tests/test_drive_input.py`, which needs no
window, running game or ROM. It pins the driver's physical-key scan codes, checks
the seeded player-one keyboard profile against the `keys[]` table in
`src/ui/lambo_frontend.cpp`, and covers the per-monitor-v2 DPI setup and the
capture path. Failure paths stub `PostMessageW`, so held-input release and
cleanup reporting are exercised without a game. It is registered on every
platform rather than only Windows, because it stubs `sys.platform` and
`ctypes.WinDLL`. See [Automated harness](automation-harness.md#windows-menu-helper)
for what the helper itself does.

For real mod loading, run `tools/check_mod_support.py` with the smoke NRM and
your USA ROM as described in [Modding](modding.md#verification-recorded-for-this-integration).
Its optional `--texture-package PATH` accepts a manifest-free RT64 `.rtz` and
checks actual mod selection plus guest boot. Every case uses a temporary
profile and forces headless mode. This covers container registration, not
rendered texture replacement. `tests/test_check_mod_support.py` checks the
runner's staging, isolation, and rejection of incomplete or skipped load logs
without a ROM.

Add `--check-api` with the rebuilt smoke package to exercise a frontend guest
import and default/persisted per-mod configuration reads. See
[the smoke package contract](modding.md#developer-smoke-package). These cases
run headlessly and do not prove rendered mod UI or native-library execution.

The native `lambo_frontend_settings` fixture works only with the virtual
controllers it attaches. It keeps physical controllers out of its own process
by setting SDL hints before SDL starts, so it passes with a gamepad connected
and needs no environment setup. Game runs are unaffected.

After the submodules are initialized and the ROM is present, the supported
desktop build sequence is:

~~~powershell
.\build.ps1
ctest --test-dir build --output-on-failure
~~~

Build the test fixtures explicitly before running the full CTest suite:

~~~text
cmake --build build --target lambo_host_tests --parallel 2
ctest --test-dir build --output-on-failure --no-tests=error
~~~

The aggregate includes ROM-backed fixtures when generated functions exist.
It does not enable the optional GPU probes.

On Linux:

~~~bash
bash ./build.sh
ctest --test-dir build --output-on-failure
~~~

The scripts apply dependency patches, generate ROM-derived code, configure the
port, and build lamborghini_modern. The exact generated directories are
described in Architecture. If a manual CMake invocation is needed for
diagnosis, record it in the report and also run the script path when possible.

Vendored zstd tests are disabled by default. The port's host tests remain
enabled through `BUILD_TESTING`; zstd's benchmarks and fuzzers are a separate
dependency-maintenance task. To opt in, configure with
`-DZSTD_BUILD_TESTS=ON` and build the dependency test targets before running
them. Existing build directories retain their cached choice: if an older
configuration enabled zstd tests, turn them off once with
`cmake -S . -B build -DZSTD_BUILD_TESTS=OFF`.

The `lambo_frontend_debugger_default` and `lambo_frontend_debugger_enabled`
host tests inject an RmlUi error into the real debugger with a no-render adapter.
They check logger delivery, beacon absence/visibility, inspector toggling,
shutdown, and focus/show/hide on small navigation documents. They need no ROM,
window, or GPU. They do not verify the actual rendered launcher/settings pages.

~~~powershell
cmake --build build --target lambo_frontend_debugger_tests
ctest --test-dir build -R "^lambo_frontend_debugger_" --output-on-failure
~~~

## Focused checks

The CMake project registers tests whose names describe the contract. Useful
groups include:

~~~text
ctest --test-dir build -R "lambo_(portable_paths|logging_policy|config_live_updates)"
ctest --test-dir build -R "lambo_rom_path"
ctest --test-dir build -R "lambo_(controls|input|analog)"
ctest --test-dir build -R "lambo_automatic_pit_stops"
ctest --test-dir build -R "lambo_cheats|lambo_frontend_settings"
ctest --test-dir build -R "lambo_prompt_back_action"
ctest --test-dir build -R "lambo_(native_menu_retired|quit_path)"
ctest --test-dir build -R "lambo_reset"
ctest --test-dir build -R "lambo_menu_stick_scale"
ctest --test-dir build -R "lambo_hud_blue_bar"
ctest --test-dir build -R "lambo_car_card"
ctest --test-dir build -R "lambo_steering_response"
ctest --test-dir build -R "lambo_(controller_pak|startup_state_machine)"
ctest --test-dir build -R "lambo_(no_lod|track_patch|interpolation)"
ctest --test-dir build -R "lambo_rt64|lambo_audio"
ctest --test-dir build -R "lambo_rt_sun_tasks"
ctest --test-dir build -R "lambo_hud_(texture_walk|hash_padding|rt64_contract|savestate)"
ctest --test-dir build -R "lambo_mods_rtz_container"
ctest --test-dir build -R "lambo_mod_menu_refresh"
ctest --test-dir build -R "lambo_mod_runtime_refresh"
ctest --test-dir build -R "lambo_sky_(projection|panorama)|lambo_camera_(projection|runtime)"
~~~

The HUD group covers producer-side display-list walking, RT64's unused-padding
hash behavior, and the reviewed RT64 dependency contract. See the [HUD texture
identity notes](HUD.md#stable-hud-texture-identities-2026-10-23) for fixture
coverage and the bounds that the producer applies.
The save-state case starts separate native processes around the real F7/F8
file I/O, covering four restart cycles for each of 1P/2P, same-process policy
changes, authored fog payloads and legacy/truncated rejection. Build
`lambo_hud_savestate_tests` before running it; the other HUD tests use
`lambo_hud_texture_walk_tests` and `lambo_hud_hash_padding_tests`.

The complete list is the add_test list in CMakeLists.txt; keep this page
updated when a test is added or renamed. Some tests are only registered when
Python, generated functions, Windows, or an RT64 build is available.

`lambo_mod_menu_refresh` compiles the patched frontend's refresh and update
methods with UI and game-state fakes. Its `recomp::mods` fakes use the real
declarations and types from `librecomp/mods.hpp`. It checks the enabled Refresh
button, live and launcher rescanning, empty-list selection, internal list
updates, the selected pack's live and launcher toggle restrictions, and passing
the selected version to the deprecation query.
`lambo_mod_runtime_refresh` uses the real loader and temporary, original `.rtz`
and loose-package fixtures. It checks live discovery, enable/disable callbacks,
preserved selections, order and config pointers, repeated scans, and packages
that must wait for startup validation, including disabled packages present at
startup before the first Refresh. The extractor's Python tests cover braces in
comments and string/character literals. Neither native test renders replacement artwork
or plays the game.

The optional Windows D3D12 sunlight probe executes the shared kernel over
synthetic geometry, including real BLAS/TLAS and GPU readback. Configure with
`-DLAMBO_RT_GPU_TESTS=ON`, build `lambo_rt_shadow_gpu`, then run
`ctest --test-dir build -R lambo_rt_shadow_gpu --output-on-failure`. Exit 77
means skipped hardware. See [groundwork evidence](rt-shadows.md) for toolchain
and limits; this does not test native game receivers or swapchain pixels. The
probe also covers the instance masks (moving-only lamp light, remembered
scenery) and the static layer's rebuild when its snapshot changes.
`lambo_rt_shadow_cache` checks, on the host, which static scenery the shadow
cache remembers, publishes and evicts. `lambo_rt_lap_window` checks that the
lap summary counts each Workload once and keeps a lap's partial tail.

`python tools/check_rt_shadow_lap.py` drives each circuit's recorded
time-trial lap in a real D3D12 window and fails when more than 1% of race
tasks fall back to the native shadow. `--env KEY=VALUE` passes a developer
variable such as `LAMBO_RT_SHADOW_DEBUG=4`, and `--shots-every` saves window
screenshots. It needs a USA ROM build and DXR 1.1 hardware, so it is not part
of CTest.

The config test covers captured launch overrides, their precedence and bounds,
reload, and live settings. The frontend settings test covers the Low hardware
preset's staging, Apply/Discard, later manual adjustments and preservation of
unrelated settings. Neither test measures game frame times.

The optional getter benchmark runs separately from CTest:

~~~powershell
cmake --build build --target lambo_config_benchmark -j 4
./build/lambo_config_benchmark.exe ./build/benchmark-graphics.json
~~~

On Linux, omit `.exe`. Use an isolated graphics file, the same compiler and
optimisation flags, and an idle machine for comparisons. This times 3.8 million
configuration getter calls; it does not predict an FPS increase. See the
[performance audit](performance.md) for the workload and limitations.

`lambo_camera_runtime` links the production camera shims against fixed
non-default settings and synthetic word-swapped guest RAM. It covers sequence
transitions (including countdown to GO and back), race modes, all authored FOV
layouts, pause framing, and restoration of sky, backdrop, and view-cone values.
It does not prove visual framing: compare
intro and attract shots with default and non-default settings in a game build,
then enter a race and repeat the transitions. The measured tuples and current
visual evidence are in [Camera sequences](camera-sequences.md). Companion tests
also cover state changes without any intervening FOV hook.

`lambo_input_driving_assists` checks tilt direction, landscape centering,
held-angle steering, calibration retention after invalid samples, manual
steering priority, race gates, and brake override. The guest runtime test
checks the dedicated assistance hook against synthetic RAM and confirms that
the replay hook alone does not apply assists while a configured recorder still
captures the final assisted pad.
On Android, additionally check both landscape orientations, background/resume,
opening settings while tilted, a phone without sensors, braking, and finishing
a race. These hardware checks are not replaced by host tests.

`lambo_steering_response` checks the steering settings and overrides and both
native steering hooks against synthetic RAM: the authored deadzone is a no-op
over every stick value, other deadzones start the curve exactly at their edge,
and direct mode writes the selected curve entry with the stick's sign.
`scenarios/steering-sweep.json` replays a stick sweep; run the executable with
`LAMBO_STEERING_TRACE` and the `LAMBO_STEERING_*` overrides to compare modes,
as in [Steering response](steering.md#verification).

`lambo_prompt_back_action` checks the settings quit confirmation's controller
contract in the build input: the prompt must answer the Back action, that action
must be the mapped Back rather than a fixed button, and the affirmative quit
must stay on its own confirmation. The prompt itself needs a live render
context, so this is a patch-content check, not a played confirmation.
`lambo_frontend_settings_tests` covers the binding side: that the pressing
controller's default and remapped Back both produce the Back menu action. A
controller playthrough is still the only way to confirm the prompt visually.
The same fixture checks the menu-opening query used for hidden settings
refresh: Escape, both menu bindings, remapping, single-player and multiplayer
profile resolution, unknown devices, and unrelated gameplay inputs. Pass an
isolated data directory followed by `--benchmark` to measure the old and new
refresh triggers instead of running assertions. `lambo_fog_benchmark` is a
separate opt-in target for correction-table lookup cost. These measurements
do not replace a rendered menu or fog check; see [Performance](performance.md).

## End-to-end scenario

After a successful build, run the checked-in headless smoke scenario:

~~~bash
python tools/run_game_scenario.py scenarios/harness-smoke.json
~~~

It uses a developer warp and a recorded input trace. It checks startup,
replay completion, guest input consumption, vehicle movement, frame swaps, and
capture output. Replay reaching end-of-file only proves that input was
consumed; it does not prove that a lap was completed.

The interactive smoke scenario uses the RT64 presenter:

~~~bash
python tools/run_game_scenario.py scenarios/frontend-rt64-smoke.json
python tools/run_game_scenario.py scenarios/sky-turning-smoke.json
python tools/run_game_scenario.py scenarios/rt-sun-provenance.json
python tools/run_game_scenario.py scenarios/menu-to-race-smoke.json
~~~

It needs a display and a working graphics backend. Do not run it in a
headless CI job unless the job supplies a suitable display.

## ROM and generated-output rules

- The local audit input was a 4 MiB North American USA ROM with SHA-256
  `CAB2467684A58BC19C787423D704A961AA497629763367D9FE691172DE58591C`.
  This identifies the test input; it is not a ROM download or a release
  requirement for players.
- The Japanese recompile tests use an 8 MiB *Super Speed Race 64 (Japan)*
  ROM with SHA-256
  `06C9C992014DA5982E747117C690C1590A011C0C6756B0522DF126366BF8B9A1`.
  The port does not run it yet.
- A ROM-backed test must record the ROM identity or SHA-256 in its report.
- A test that uses RecompiledFuncs/ must say whether those files were freshly
  generated.
- A test that uses an RT64 backend must record the operating system and backend.
- A failure caused by missing submodules, missing ROM, missing toolchain, or no
  display is a skipped prerequisite, not a passing test.
- Never add a copyrighted ROM to a fixture or to a CI artifact.

Do not treat a missing ROM, generated output, submodule, display, or device as
a passing test. Record the missing prerequisite in the issue or pull request.

## Test report minimum

Record the commit, OS, compiler, backend, ROM identity/hash when applicable,
exact command, result, logs, generated-file state, and known limitations.
Link the report from an investigation or pull request when the result is
important enough to guide future work.

## Configurable race fields

After the supported USA build, run:

~~~text
ctest --test-dir build -R "lambo_race_field|lambo_config|lambo_frontend_settings"
python tools/check_race_field.py --cars 16 --circuit 1 --reverse 0 --mode 3
python tools/check_field_championship.py artifacts/race-fields/<run>/race.lstate
~~~

The ROM-backed host fixture executes the generated championship standings
initializer for 2-16 racers across six rounds. The game check boots a fresh race,
drives the replay, then checks every car's body, movement and ranking from a v2
snapshot. Use circuits 1-6, reverse 0/1 and modes 1-3 to vary coverage. Captures,
ROM-derived snapshots and logs remain in ignored artifacts/race-fields.
This movement check alone does not prove collision spacing or correct pixels.
The championship check requires a snapshot recorded in championship mode 3. It takes
the sixteen-car snapshot from the previous command,
injects controlled finishing positions, advances the native results and standings
screens, and checks the next round's count, roster and cumulative points while
the next-session setting is two. It does not prove natural race completion.
Add `--render` to inspect those screens in the RT64 window.

## Runtime acceptance suite

After the supported build and the test-fixture build, run:

~~~text
python tools/run_runtime_acceptance.py --headless
python tools/run_runtime_acceptance.py
~~~

Both commands run the native minimap/rank-emitter, reset-safety and Quit-path
fixtures. They boot fresh 2-, 6- and 16-car races. The sixteen-car run uses mode
3; its own snapshot supplies two championship progression checks with player
places 10 and 16. Those checks retain the roster and accumulated points in the
next round. They inject finish positions rather than waiting for a natural
finish. Headless result captures provide evidence for inspection, not a pixel
equivalence assertion.

The second command also needs a display and a working RT64 backend. It runs
`scenarios/launcher-quit.json` and `scenarios/repeated-reset.json`. The opt-in
`LAMBO_UI_ACCEPTANCE` driver clicks the actual RmlUi header elements and shared
confirmation buttons on the presentation thread. It cancels a race reset,
confirms a reset from the race, then confirms another from the live title
screen in the same process. The scenarios require the corresponding guest
transition logs and title completion; reaching a timeout or VI limit cannot
substitute for that evidence. They exercise callbacks and runtime transitions,
not physical-controller navigation or button placement. Reset completion needs
a newer guest title sample after the game thread consumes each request; the
pre-reset title snapshot cannot satisfy a later reset.

Reviewers assess whether the scenarios exercise the behavior requested by the
change, including its transitions and boundary cases. Passing existing
scenarios does not establish coverage for a new requirement.

Use `--exe PATH --build-dir PATH` to select a build explicitly. The suite
writes a JSON report under ignored `artifacts/runtime-acceptance/`, recording
the commit, operating system, compiler, generated-file presence, executable
and ROM hashes, backend diagnostics, completed cases and rendered cases
omitted by `--headless`. Generation freshness must come from the supported
build record; the runner does not infer it from file presence. Each game run
keeps its own isolated
configuration, logs, snapshots and captures. Runtime and visual acceptance
remain separate from the Python runner's tests with fake game processes.

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
git diff --check
~~~

The zero-ROM Python host suite runs with:

~~~powershell
python -m unittest discover tests
~~~

This exercises the Python tests under `tests/` without a ROM, generated game
code, a graphics backend, or a native build.

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

On Linux:

~~~bash
bash ./build.sh
ctest --test-dir build --output-on-failure
~~~

The scripts apply dependency patches, generate ROM-derived code, configure the
port, and build lamborghini_modern. The exact generated directories are
described in Architecture. If a manual CMake invocation is needed for
diagnosis, record it in the report and also run the script path when possible.

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
ctest --test-dir build -R "lambo_menu_stick_scale"
ctest --test-dir build -R "lambo_(controller_pak|startup_state_machine)"
ctest --test-dir build -R "lambo_(no_lod|track_patch|interpolation)"
ctest --test-dir build -R "lambo_rt64|lambo_audio"
ctest --test-dir build -R "lambo_rt_sun_tasks"
ctest --test-dir build -R "lambo_mods_rtz_container"
ctest --test-dir build -R "lambo_sky_(projection|panorama)|lambo_camera_(projection|runtime)"
~~~

The complete list is the add_test list in CMakeLists.txt; keep this page
updated when a test is added or renamed. Some tests are only registered when
Python, generated functions, Windows, or an RT64 build is available.

The optional Windows D3D12 sunlight probe executes the shared kernel over
synthetic geometry, including real BLAS/TLAS and GPU readback. Configure with
`-DLAMBO_RT_GPU_TESTS=ON`, build `lambo_rt_shadow_gpu`, then run
`ctest --test-dir build -R lambo_rt_shadow_gpu --output-on-failure`. Exit 77
means skipped hardware. See [groundwork evidence](rt-shadows.md) for toolchain
and limits; this does not test native game receivers or swapchain pixels.

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

The game-boot check boots GameActivity on the device and fails when the game
process dies during startup. It needs the debug APK with a staged ROM:

~~~powershell
adb shell am instrument -w -e gameboot 1 io.github.alondero.lamborghinirecomp.test/io.github.alondero.lamborghinirecomp.DriverImportInstrumentation
~~~

`lambo_prompt_back_action` checks the settings quit confirmation's controller
contract in the build input: the prompt must answer the Back action, that action
must be the mapped Back rather than a fixed button, and the affirmative quit
must stay on its own confirmation. The prompt itself needs a live render
context, so this is a patch-content check, not a played confirmation.
`lambo_frontend_settings_tests` covers the binding side: that the pressing
controller's default and remapped Back both produce the Back menu action. A
controller playthrough is still the only way to confirm the prompt visually.

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

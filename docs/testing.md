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
ctest --test-dir build -R "lambo_(controller_pak|startup_state_machine)"
ctest --test-dir build -R "lambo_(no_lod|track_patch|interpolation)"
ctest --test-dir build -R "lambo_rt64|lambo_audio"
ctest --test-dir build -R "lambo_mods_rtz_container"
ctest --test-dir build -R "lambo_sky_(projection|panorama)|lambo_camera_projection"
~~~

The complete list is the add_test list in CMakeLists.txt; keep this page
updated when a test is added or renamed. Some tests are only registered when
Python, generated functions, Windows, or an RT64 build is available.

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

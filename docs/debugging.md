# Debugging

Start with a reproducible symptom. Keep the player-facing report short, then
collect developer evidence only when it can distinguish layers.

## First checks

1. Confirm the supported USA ROM and do not replace a save file while the game
   or an emulator has it open.
2. Reproduce once with the default configuration.
3. Record the OS, release or commit, graphics backend, controller type, and
   whether a texture pack or track patch is enabled.
4. Capture the session log and the exact steps.
5. If the failure is deterministic, use the smallest warp or scenario that
   still reproduces it.

## Logs and crash evidence

The normal log level keeps warnings and errors. Use --verbose or
--log-level=debug for a report. --console mirrors the selected output to a
console; it does not by itself enable debug logging.

Logs are stored in the application state directory:

- Windows: %LOCALAPPDATA%/LamborghiniRecomp/logs
- Linux: $XDG_STATE_HOME/LamborghiniRecomp/logs, or
  ~/.local/state/LamborghiniRecomp/logs
- portable mode: a logs directory beside the executable
- LAMBO_LOG_DIR overrides the log directory

Do not paste private machine paths, ROM contents, or credentials into a public
issue. Redact only the path; keep the file name, backend, error text, and
timing.

## Useful runtime controls

The normal settings overlay opens with Esc, F1, or the controller menu button.
For development, the keyboard circuit keys F1 through F6 select a race after
boot. The environment form is:

~~~text
LAMBO_WARP=circuit[:laps[:car[:players]]]
LAMBO_WARP_DIFFICULTY=0|1
~~~

These are developer paths. They are not a player save or a supported custom
race feature.

The harness can run without a visible window:

~~~bash
python tools/run_game_scenario.py scenarios/harness-smoke.json
~~~

Use a copy of the configuration and Controller Pak when narrowing a save or
settings problem. The harness isolates its own state when the scenario asks it
to do so.

## Layered investigation

Use this order when the symptom is unclear:

~~~text
player symptom
  -> log and configuration
  -> host event/input or file boundary
  -> runtime queue/thread boundary
  -> guest address/layout or recompiled function
  -> renderer/audio backend
~~~

Do not infer an owner from a function name. Confirm it with source, a
watchpoint, a display-list capture, a test, or a repeatable log.

## Optional ares comparison

ares is an external N64 emulator, not a repository dependency. It is not
shipped with this project. A live comparison requires a separately installed
ares build, the same legal ROM, and the project's instrumentation workflow.
The current audit worktree did not contain ares, so no live comparison is
claimed here.

When available, compare one narrow fact at a time: a guest-memory write,
controller buffer, VI state, display list, or save transaction. Record the
capture date, emulator version, ROM hash, address/units, and the result in a
dated investigation. Do not copy a conclusion from another port without
repeating the measurement.

## Common failure classes

| Symptom | First boundary to inspect |
| --- | --- |
| Configure fails before compilation | Submodule state, applied patch series, compiler, CMake generator, and the ROM path used by the generator. |
| Missing RecompiledFuncs or audio source | Re-run the script after placing the matching ROM. Do not create an empty generated directory. |
| Black screen or device loss | Backend selection, driver version, RT64 log, and whether the chosen backend is supported on that platform. |
| Controls work in the menu but not in a race | Frontend profile assignment, final input buffer, game state, and player count. |
| Save is not visible in another emulator | Container format, Controller Pak port, file ownership, and whether both programs were open. |
| A track correction changes unrelated geometry | Track patch identity/baseline guard, package contents, and sparse row validation. |
| A visual difference appears after a FOV change | View-cone culling, projection policy, and the matching renderer patch. |

## Reproducible investigation template

Use this shape in docs/investigations/YYYY-MM-DD-name.md:

~~~text
Question:
Commit:
OS/compiler/backend:
ROM identity/hash:
Inputs and configuration:
Observation:
Hypothesis:
Falsification test:
Result:
Remaining uncertainty:
Follow-up owner:
~~~

An investigation may end with "unknown". That is more useful than a confident
claim without a measurement.

# Working notes for coding agents

Read [CONTRIBUTING.md](CONTRIBUTING.md) and the
[documentation map](docs/README.md) before changing the project.

## Project facts

- The supported input is the North American USA ROM.
- The build runs N64Recomp and RSPRecomp, then compiles generated output with
  the hand-written port, N64ModernRuntime, RT64, and RecompFrontend.
- `RecompiledFuncs/` and `src/aspMain.cpp` are generated and ignored. Never
  edit them. Change the checked-in generation inputs or hand-written hooks.
- `build.ps1`, `build.sh`, and `scripts/build_android.py` define the supported
  build sequence. `patches/README.md` lists dependency patches.

## Editing rules

- Verify claims against current source, tests, fixtures, or measurements.
  Mark an unverified claim instead of presenting it as fact.
- Guest memory is emulated N64 RAM with fixed layouts and byte order. Preserve
  address, width, units, owner, timing, and failure behavior at every bridge.
- Keep game-specific behavior in the port. Keep reusable dependency or
  renderer behavior in a reviewable patch and compare it with upstream.
- Player documentation uses short sentences and avoids internal terms unless
  they are needed to solve a player problem.
- AI tools may help draft code and reasoning. The source, tests, and reviewed
  pull request are the project record.

## Verification

Run the smallest relevant test. For documentation changes, run:

~~~text
python tools/check_docs.py
python -m unittest discover tests
git diff --check
~~~

Do not claim a build, game run, visual check, or reference-project playthrough
unless it was actually performed. Do not commit ROM data or generated output.

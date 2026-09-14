# Contributing

The project welcomes AI-assisted work. The source, tests, and reviewed pull
request are the project record. The maintainer decides what is supported and
which trade-offs are acceptable through the normal review process.

## Before changing code

1. Read [the documentation map](docs/README.md).
2. Search the current source, tests, build scripts, and patch inventory.
3. Identify whether the change touches generated output, guest memory, a
   dependency patch, a renderer boundary, or a public user claim.
4. Write down the expected behavior, evidence, and remaining uncertainty.
5. Open an issue when the change is more than a small correction.

Do not use an old issue, PR body, or another port as the only specification.
The current checkout and reproducible evidence are the starting point.

## First-change workflow

For a source change:

1. Make the smallest hand-written change.
2. Add or update a focused host test when the behavior can be isolated.
3. If the change depends on the ROM, record the ROM identity and generated-file
   state.
4. Re-run the relevant CTest group.
5. Run the headless scenario when input, startup, audio, saves, or rendering
   could be affected.
6. Update the stable reference, investigation, or decision page.
7. Report what was not tested.

For a generation change, update the checked-in TOML or script, regenerate
locally, inspect the generated diff, then run the same checks. Never edit
RecompiledFuncs/ or src/aspMain.cpp by hand.

## Generated code and patches

Generated files are:

- RecompiledFuncs/;
- src/aspMain.cpp.

The source inputs are the symbol/config files, dump.toml,
scripts/n64recomp_race.toml, force_stub.txt, and hand-written source hooks.
The build scripts and CMake apply the dependency patches. The patch inventory
is [patches/README.md](patches/README.md).

If a dependency change may be generic, compare it with the current upstream
project and use that project's issue or pull request as the canonical proposal
record. Keep the local purpose and test in `patches/README.md` and this pull
request. If it only exists to handle Lamborghini data, keep it in the port and
explain why. Do not create a private renderer fork.

## Guest and host boundaries

Guest memory is an emulated N64 byte array. A fixed address or packed layout
is fragile port infrastructure. A comment or change that uses it must state:

- address or structure layout;
- width, units, and byte order;
- owning thread and game phase;
- synchronization or timing invariant;
- failure behavior;
- evidence and the intended replacement.

Unknown fields must stay unknown. The desired direction is decompilation,
source-level patches, stable symbols, explicit hooks, and eventually a
versioned code/data mod interface. That is future work, not a reason to hide
the current bridge.

## Comments

Comments should explain purpose, ownership, lifetime, units, invariants,
failure behavior, address/endian assumptions, and why a workaround exists.
Use a local document link or descriptive label for durable context. Do not
leave a raw wave number or issue number as the only explanation.

Keep active experiments in an issue or pull request. Once a result is settled,
put the invariant in the relevant source comment, test, or subsystem page.
Keep source comments short enough to stay beside the invariant they protect.

## Testing and reports

The exact commands are in [docs/testing.md](docs/testing.md). A useful report
includes:

- commit;
- operating system, compiler, and graphics backend;
- ROM identity/hash for ROM-backed work;
- exact command;
- generated-file state;
- logs or captures;
- expected and actual behavior;
- tests run and skipped;
- known limitations;
- documentation impact.

The repository does not require a copyrighted ROM for documentation checks.

## Issues and pull requests

Use the repository templates. A reverse-engineering finding must include the
measurement setup, address/units if relevant, raw or summarized evidence,
hypothesis, falsification step, and remaining uncertainty. A feature proposal
must explain the user problem and the trade-offs it asks the maintainer to
accept.

Do not include private AI-session links, absolute machine paths, ROM bytes, or
unexplained references to another project's issue tracker.

## Legal

Do not commit ROM data, generated ROM-derived output, copyrighted assets, or
private user saves. Code original to this repository is GPLv3-compatible.
Dependencies and local patches retain their own licenses.

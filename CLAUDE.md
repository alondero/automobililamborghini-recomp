# Working notes for coding agents

This file is a compact orientation for automated coding help. Human
contributors should start with [CONTRIBUTING.md](CONTRIBUTING.md) and the
[documentation map](docs/index.md).

## Source of truth

Prefer current source, build scripts, tests, fixtures, and measured captures.
Treat old session notes, issue prose, and AI reasoning as evidence to check.
When evidence is incomplete, say so and show the trade-off to the human
maintainer.

## Repository shape

The build reads the USA ROM, runs N64Recomp and RSPRecomp, then compiles
generated sources with the hand-written port, N64ModernRuntime, RT64, and the
frontend.

RecompiledFuncs/ and src/aspMain.cpp are generated and ignored. Never edit
them. Change the symbol/config inputs or a hand-written hook and regenerate.
The build scripts and [patch inventory](patches/README.md) define dependency
patch application.

## Engineering rules

- Do not invent game behavior to make a test pass.
- Measure a guest-memory or renderer claim before changing architecture.
- Keep guest and host ownership explicit.
- Treat direct guest-memory writes as fragile transitional bridges.
- Keep comments about invariants, units, ownership, lifetime, and failure
  behavior beside the code.
- Keep generic renderer work separate from game-specific display-list policy.
- Prefer a focused regression test and a dated evidence page over a confident
  unsupported claim.

The desired long-term direction is better decompilation, source-level patches,
stable symbols, explicit hooks, and a versioned mod boundary. It is not
complete today.

## Debugging and testing

Use [docs/debugging.md](docs/debugging.md) for logs, captures, and optional
ares comparison. Use [docs/testing.md](docs/testing.md) for host tests,
ROM-backed tests, and the headless scenario.

The normal smoke command is:

~~~text
python tools/run_game_scenario.py scenarios/harness-smoke.json
~~~

It requires a built executable, generated output, and the matching ROM.
Documentation checks do not.

## User-facing changes

Keep player documentation in simple English. Do not make players learn ROM
addresses, RDRAM, RT64, or recompilation to install and use the port. Put
technical detail in the developer and reference pages.

Before claiming support, verify the platform, release path, settings path, save
path, and failure behavior. If a binary was not run, mark that observation
unverified.

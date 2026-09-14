# Decision 0002: guest-memory bridges are transitional

Status: proposed project policy for maintainer review.

Date: 2026-09-14.

## Decision

Direct port-side reads and writes to guest memory are transitional
infrastructure. They may remain where they are the only measured way to bridge
the current generated code, but they are not the target architecture.

Each bridge must document its address or layout, width and units, byte order,
thread owner, timing assumption, failure behavior, and evidence. Unknown
fields stay unknown until measured.

## Intended replacement

The project should move toward:

1. decompiled or source-level game functions where evidence supports them;
2. proper source patches or named recompiled-function hooks;
3. stable symbols and explicit data ownership;
4. versioned interfaces for future code and data mods.

This decision does not authorize runtime changes in the documentation pass.
It describes the direction a future implementation milestone must make
concrete.

## Risk

Removing a bridge without replacing its invariant can regress timing, save
format, input, or rendering. A future replacement needs a regression fixture
before the old bridge is deleted.

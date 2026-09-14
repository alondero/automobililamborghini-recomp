# Decision 0001: human ownership and evidence

Status: proposed project policy for maintainer review.

Date: 2026-09-14.

## Decision

The human project maintainer owns support promises, architecture, upstream
decisions, and release criteria. AI-generated reasoning may suggest code,
tests, or documentation, but it is not authority.

Every uncertain claim must name its evidence and its uncertainty. A proposed
trade-off must be presented to the human owner when it changes runtime
behavior, public support, patch boundaries, or the future mod API.

## Consequences

- Source, fixtures, measurements, and reproducible commands outrank session
  notes.
- Historical issue or PR prose can be preserved as evidence, but it must not
  silently become current behavior.
- A decision page may say “unknown” or “needs approval”.
- Public docs must not claim that a feature was played or tested when it was
  only read about.
- Architecture work should land in small, reviewable milestones with a named
  human owner.

## Open approval

The maintainer still needs to approve the support matrix, the exact
decompilation milestone, and the design of any future code-mod boundary.

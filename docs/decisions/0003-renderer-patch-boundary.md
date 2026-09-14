# Decision 0003: renderer patch boundary

Status: proposed project policy for maintainer review.

Date: 2026-09-14.

## Decision

The project uses a pinned public RT64 checkout plus a local, reviewable patch
series. It should not grow a private renderer fork.

Generic renderer behavior should be compared with upstream and proposed
upstream when it is reusable. Lamborghini-specific display-list, projection,
split-screen, texture-path, and default-policy decisions remain in the port.

## Required record

Every renderer patch needs:

- the upstream revision it applies to;
- the game symptom and smallest reproducer;
- the exact behavior changed;
- a test or capture;
- whether the behavior is generic enough to propose upstream;
- the upstream URL and status, if a proposal exists.

The current inventory is in [the renderer reference](../reference/renderer.md).
No upstream status should be implied by the existence of a local patch.

# Repository history review

Status: historical review of selected issues and pull requests. It is not a
replacement for current source or a claim that every pull request body
describes the current default branch.

Date: 2026-09-14.

## Why this is recorded

Important build, documentation, and research decisions were previously easier
to find in GitHub discussion than in the repository. This page keeps the
useful lessons while separating merged work from open work.

## Historical lessons

- The [roadmap record](https://github.com/alondero/automobililamborghini-recomp/issues/26)
  is a broad historical plan. It warns that behavior inherited from another
  port must be re-verified. It is not the current roadmap.
- The [no-LOD audit](https://github.com/alondero/automobililamborghini-recomp/pull/90)
  and [rumble audit](https://github.com/alondero/automobililamborghini-recomp/pull/107)
  show why a later measurement must be able to revise an earlier headline.
- The [build documentation change](https://github.com/alondero/automobililamborghini-recomp/pull/144)
  exposed that a missing patch in a manual sequence can make a fresh build
  fail. The current patch inventory and build scripts are now the source of
  truth.
- The [texture workflow](https://github.com/alondero/automobililamborghini-recomp/pull/167)
  and [generic texture tooling](https://github.com/alondero/automobililamborghini-recomp/pull/187)
  model a useful report: name the Python checks, say whether a build was run,
  and keep content support separate from the renderer.
- The [track research](https://github.com/alondero/automobililamborghini-recomp/pull/173)
  is valuable because it labels hypotheses and does not turn a research note
  into a playable-track promise.
- The [player-name change](https://github.com/alondero/automobililamborghini-recomp/pull/192),
  [VI boot fix](https://github.com/alondero/automobililamborghini-recomp/pull/203),
  and [interpolation change](https://github.com/alondero/automobililamborghini-recomp/pull/205)
  show three different evidence levels: source/manual checks, a focused
  build-and-boot path, and a deterministic replay. A report must say which
  level it actually reached.

## Work not treated as current

The open [leaderboard documentation change](https://github.com/alondero/automobililamborghini-recomp/pull/171)
and [road-surface texture change](https://github.com/alondero/automobililamborghini-recomp/pull/195)
were not used to describe current features. Their pull request bodies are
useful proposals, not default-branch evidence.

The [merged crash-stack fix](https://github.com/alondero/automobililamborghini-recomp/pull/47)
was inspected as build history. It does not establish a frontend or player
installation contract.

## Rule carried forward

A pull request may explain why a change was attempted. The repository must
still record the resulting invariant, test command, platform, generated-file
state, and remaining uncertainty in a durable local document.

The generation script still contains older measurement labels beside symbol
decisions. Those labels explain why a generated boundary exists, but they are
historical provenance rather than a current roadmap or support promise. New
generation comments should point to this evidence structure instead of adding
another session log.

---
name: release
description: User-invoked skill for cutting a new release of this project. Walks through tagging the merge commit, dispatching the Build & Release workflow, waiting for the binaries, and replacing the workflow's placeholder notes with the rich release-notes body. Deterministic steps live in scripts/ — Claude fills in the variable parts (the notes body, the headline).
disable-model-invocation: true
---

# Release

Cut a new release of **Automobili Lamborghini Recompiled**. The pattern is
intentionally narrow: this project's releases follow a fixed tag-and-build
sequence, and the only variable part is the release-notes body. Scripts in
`scripts/` do everything that's deterministic; Claude fills in the prose.

The expected output is a published GitHub release at
`https://github.com/alondero/automobililamborghini-recomp/releases/tag/<tag>`
with `draft: false, prerelease: false` (matching v0.1.0 / v0.3.0 / v0.4.0 /
v0.4.1 / v0.4.2 — see `references/prior-release-format.md`), the workflow's
two build artifacts attached, and rich release-notes body replacing the
`Automated build from commit ...` placeholder.

## When to invoke

The user says something like "create a release", "cut a v0.4.3", "ship what
we've got", "release 0.5.0", or references this skill by name (`/release`).

## Inputs

- **version** (required) — e.g. `v0.4.3`. Must match `v<MAJOR>.<MINOR>.<PATCH>`.
- **base tag** (optional, default: previous release tag) — for the diff
  ("Closed since v0.4.2"). If unspecified, use the most recent tag reachable
  from HEAD that isn't a release tag.

## Flow

The skill runs five scripts in order. After each, Claude reads the output and
either proceeds or surfaces a diagnostic. The scripts are designed to be
re-runnable; if one fails partway, fix the upstream issue and re-run.

### 1. List changes since the base tag

```
./scripts/list-changes.sh <base-tag>
```

Prints a structured summary of:
- merged PRs since `<base-tag>` (number, title, merge date)
- closed issues since `<base-tag>` (number, title)
- the merge commit at HEAD (where the new tag will land)

Use this to draft the "Closed since <prev-version>" section of the notes.
Read each PR's body via `gh pr view <N>` for the prose context.

### 2. Tag the merge commit and dispatch the workflow

```
./scripts/tag-and-dispatch.sh <version>
```

Does, in order:
1. Find the merge commit at HEAD on `main`
2. `git tag <version> <merge-sha>`
3. `git push origin <version>`
4. `gh workflow run "Build & Release" --ref main \
        -f tag=<version> -f prerelease=false -f draft=false`

The script does NOT take `--prerelease=true --draft=true`. Those flags trigger
the `untagged-<id>` URL trap documented in `references/gotchas.md` — every
prior release (v0.1.0 / v0.3.0 / v0.4.0 / v0.4.1 / v0.4.2) was dispatched
with `--prerelease=false --draft=false`.

Prints the workflow run ID — needed by step 3.

### 3. Wait for the build

```
./scripts/wait-for-build.sh <run-id>
```

`gh run watch <run-id> --exit-status --interval 30` — blocks until the run
finishes. Expected wall time: ~5 min for Linux, ~10-13 min for Windows, ~5 s
for the `release` job. Exits non-zero on build failure.

### 4. Verify the release is properly bound

```
./scripts/verify-release.sh <version>
```

Checks, in order:
1. Git tag exists on origin (`git ls-remote --tags origin <version>`)
2. Release accessible via `gh api .../releases/tags/<version>` (not 404)
3. `html_url` ends with `releases/tag/<version>` (NOT `untagged-<id>`)
4. `isDraft: false`, `isPrerelease: false`
5. Both expected assets uploaded (`lamborghini-recomp-{linux,windows}-x64.zip`)

If any check fails, prints a diagnostic and exits non-zero. Common failures:
- workflow hasn't finished → re-run after a moment
- URL is `untagged-<id>` → the `--draft` trap; see `references/gotchas.md`

### 5. Update the notes

```
./scripts/update-notes.sh <version> <path-to-notes.md>
```

`gh release edit <version> --notes-file <path>` — replaces the workflow's
placeholder notes with the rich body. The notes file should match the
template in `references/release-notes-template.md`.

## Variable parts (handled by Claude)

- **Release-notes body** — drafted from `references/release-notes-template.md`,
  filled in with the PR/issue summary from step 1 and prose from each PR body.
  Output goes in `<drafts>/<version>-release-notes.md` for the user to review,
  then handed to step 5.
- **Headline of the "What's working" section** — pick the single most
  user-visible change since the base tag. For v0.4.2 this was "Intel Xe/Arc
  no longer needs a `graphics.json` edit". For a feature release, it's the
  headline feature.
- **"What's not done yet" follow-ups** — any open issues referenced in the
  PRs that ship as known gaps.

## Reference files

Read these when you need them — don't load them all upfront.

- `references/release-notes-template.md` — the skeleton with all required
  sections and placeholder text.
- `references/gotchas.md` — the four lessons from the v0.4.1 → v0.4.2
  release cycle (the `--draft` URL trap, the merge-commit tagging pattern,
  the actual GitHub flag values vs body copy, the workflow's placeholder
  notes). Read this before doing anything.
- `references/prior-release-format.md` — the v0.1.0 / v0.3.0 / v0.4.0 /
  v0.4.1 / v0.4.2 release bodies side-by-side, so you can match the
  voice and structure for the new notes.
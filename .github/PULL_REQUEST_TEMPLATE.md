<!--
Replace this header with a 1–3 sentence summary of what the change does.
-->

## Why?

Closes # <!-- required — issue this PR addresses, or "none — small cleanup" -->

## How was it verified?

- [ ] Built clean with `build.sh` (Linux) / `build.ps1` (Windows) — no manual cmake incantation
- [ ] Booted smoke-tested: attract → title → menu → race
- [ ] If visual: screenshot or short clip attached / linked below
- [ ] If config/schema: defaults preserved when fields absent in `graphics.json`
- [ ] If new dep patch: mirrored in both `build.sh` AND `build.ps1` AND CI (`build-release.yml`)
- [ ] If new hook into recompiled code: also mirrored in `scripts/gen_syms_toml.py` (the `PATCH_BLOCKS`-stale trap)
- [ ] No comments that explain *what* the code does (only *why* — see `CLAUDE.md`)
- [ ] No "🤖 Generated with Claude Code" footer in commits or PR description

## Screenshots / video

<!-- For visual changes only. Drag into the PR or link to a gist. -->

## Risk / rollback

One sentence: what could break, and how to revert (commit hash, `git revert`, flag to disable, etc.).
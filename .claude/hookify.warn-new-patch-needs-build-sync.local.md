---
name: warn-new-patch-needs-build-sync
enabled: true
event: file
action: warn
conditions:
  - field: file_path
    operator: regex_match
    pattern: patches[/\\][^/\\]+\.patch$
---

⚠️ **Patch file written/edited in `patches/`**

A file under `patches/*.patch` was just created or modified. Per the project's
discipline (see `CLAUDE.md` + memory note about PATCH_BLOCKS-stale-again),
every patch MUST be wired into all three of:

1. **`build.ps1`** — the `$patches` array (Windows path, lines ~118-123).
2. **`build.sh`**  — the `PATCHES` bash array (Linux path, ~line 96).
3. **`.github/workflows/build-release.yml`** — both `build-linux` and
   `build-windows` jobs have their own `git apply` step (Linux only applies
   runtime+rt64 patches; Windows also applies the MinGW/D3D12 fixes).

Before declaring this work done:

- For each build file, grep for the new patch's filename (e.g. the
  `000X-foo.patch` basename) and confirm it appears in the right context.
- If this patch only applies on one OS (e.g. MinGW-only or D3D12-only),
  make sure it isn't listed on the other OS — patches are gated per-OS.
- If you just re-edited an existing patch that's already wired up, ignore
  this reminder.

Skip this check only if you wrote a temporary patch under a path that does
NOT start with `patches/` (this rule matches `patches/.*\.patch` only).
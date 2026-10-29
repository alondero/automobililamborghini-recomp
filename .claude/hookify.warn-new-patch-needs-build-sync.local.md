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

After changing a dependency patch, list it in `patches/series.txt` (append new
patches to their dependency's series; add platforms only if it is
platform-specific), explain it in `patches/README.md`, and run
`python tools/apply_patches.py`.

Then replay every platform's series with
`python -m unittest tests.test_apply_patches tests.test_patch_composability -v`
(needs initialized submodules). Do not add a patch list anywhere else: CMake,
the build scripts and the Android script all read `series.txt`.

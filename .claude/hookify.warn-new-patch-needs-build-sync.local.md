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

After changing a dependency patch, read the application matrix in
`patches/README.md` and run `python tools/check_patch_series.py`.

Windows, Linux and Android script prefixes live in `build.ps1`, `build.sh`
and `scripts/build_android.py`. Shared patches live in `CMakeLists.txt` and
`cmake/Frontend.cmake`. Release workflows call those scripts.

For an RT64 series change, update the replay matrix in
`tools/check_patch_series.py` and run `python -m unittest
tests.test_patch_composability -v` with `lib/rt64` initialized.

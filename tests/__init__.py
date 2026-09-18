# Make `tests` an importable package so cross-module helpers (e.g.
# `from tests.test_track_lab import make_rdram`) resolve when running
# scripts via `python -m tests.<name>`.

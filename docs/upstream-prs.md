# Upstream status

Status: current repository record, checked 2026-09-14.

This page records proposals that have actually been made. A local patch is not
an upstream pull request. At this audit no upstream URL was recorded for the
project-specific patch series below.

| Area | Local source | Upstream URL | Status |
| --- | --- | --- | --- |
| Runtime scheduler, audio, and VI | patches/0001 | none recorded | Human decision needed: split generic runtime fixes from game policy. |
| Save-state thread relinking | patches/0007 | none recorded | Local patch; no proposal recorded. |
| Lazy RDRAM commitment | patches/0012 | none recorded | Local build dependency; compare with current runtime before proposing. |
| RT64 interpolation matching | patches/0006 | none recorded | Local patch; needs a reusable reproducer. |
| RT64 backdrop and split-screen behavior | patches/0008, 0009, 0011 | none recorded | Mixed generic and game-specific behavior; do not label upstream-supported. |
| RT64 Intel backend selection | patches/0010 | none recorded | Local driver workaround; needs device/driver evidence. |
| MinGW and Plume compatibility | patches/0004, 0005 | none recorded | Candidate portability fixes; no proposal recorded. |
| Android dependency integration | patches/0013 through 0015 | none recorded | Local build/device path; no proposal recorded. |
| Host config, frontend, presentation | patches/0016 through 0018 | none recorded | Cross-project integration; maintainer decision needed. |

When an upstream proposal exists, replace “none recorded” with the public URL,
target revision, date, author, review status, and the behavior covered by its
tests. Do not paste private discussion links here.

---
name: Reverse-engineering finding
about: Record a measured ROM, runtime, or renderer finding
title: "[RE] "
labels: "research"
assignees: ""
---

Use this template when you have measured the original game, an emulator, or
the port and need to preserve a finding before making a code change. It is for
questions such as input layout, guest-memory ownership, display-list behavior,
or ROM data formats.

Do not use it for an ordinary player bug or feature request. Once a finding is
confirmed, put the lasting rule in the relevant source comment, test, or
subsystem documentation and link the issue or pull request. Leave the issue
open only while the measurement or follow-up is still needed.

This template records evidence. A hypothesis is useful, but it is not a fact
until the measurement or source check supports it.

## Question

What behavior or ownership question were you trying to answer?

## Setup

- Commit:
- ROM region and SHA-256:
- Emulator or hardware:
- Emulator version:
- Operating system and tool versions:
- Build/backend, if the port was involved:
- Input, save, or capture fixture:

## Observation

Give the exact address, width, units, byte order, thread, frame/state, or
renderer command when relevant. Attach a small log, trace, screenshot, or
sanitized capture when possible.

## Hypothesis

What do you think the observation means? Mark this as a hypothesis.

## Falsification

What test could prove this wrong? What did that test show?

## Result

- Confirmed, inferred, experimental, historical, or unverified:
- Source/config/test change, if any:
- Regression command:
- Remaining uncertainty:
- Follow-up owner:

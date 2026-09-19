# Input recordings

Files here are versioned, run-length encoded effective N64 controller traces for
the automated game harness. See [the harness guide](../docs/automation-harness.md)
for the format, recording command, and replay limitations.

`harness-smoke.jsonl` is a synthetic mechanism test, not a driven lap. Name real
fixtures by circuit, mode, car, and purpose so their required starting conditions
remain obvious, for example `circuit-1-time-trial-car-0.jsonl`.

`harness-one-frame-a.jsonl` is the phase-boundary regression: its sole A-button
frame must be verified in guest RAM and pass through one complete dispatcher
update before EOF can stop the process.

`sky-turning-smoke.jsonl` accelerates and steers left and right to exercise sky
scrolling. Run `scenarios/sky-turning-smoke.json` with RT64. Completion verifies
input and rendering progress; the projection and panorama host tests provide
the motion/coverage assertions. This trace is not a completed lap.

`menu-to-race-smoke.jsonl` starts in menu state 6 and confirms the default
single-race selections using Start/A pulses, without a warp. Its RT64 scenario
regresses menu assets being replaced while a previous graphics task still uses
them. It is a transition/progress check, not a driven lap or sky-motion oracle.

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

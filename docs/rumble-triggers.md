# Rumble trigger audit (issue #102, map #101)

**Verdict: the ROM has a complete, emitted, reachable rumble system — the
`src/libultra_stubs.c` FAITHFULNESS NOTE claim ("detects the Rumble Pak but never drives the
motor") is FALSE.** The old verification enumerated `__osContRamWrite`-style call sites, but the
motor path never goes through the pak-write primitive at runtime: `osMotorInit` **pre-builds**
raw ON/OFF joybus frames once, and start/stop **replay** those frames through the raw SI submit
function — which in the port is the native bridge `func_8007F780`.

Naming: recomp names are runtime VRAM + 0xC00 (e.g. name `func_8006A910` = runtime
`0x80069D10`). Both are given below. Evidence method: read the emitted bodies in
`RecompiledFuncs/`, then a **whole-ROM `jal`-opcode scan** (`.z64`, mapping vram = ROM offset +
`0x7FFFF400`, anchored on the `rumble_set` prologue byte signature) to catch callers hiding in
force-stubbed/truncated code. Every scan count matched the emitted call sites exactly — nothing
rumble-related hides in stubbed code.

## The chain (all emitted real, all reachable)

| Role | Recomp name | Runtime VRAM | Notes |
|---|---|---|---|
| Gameplay intensity writers | `func_8001EC1C`, `func_8005AC84`, `func_800657CC` | `0x8001E01C`, `0x8005A084`, `0x80064BCC` | write per-channel intensity word `0x800A3A40[ch]` (`func_8001EC1C` writes constant `0x32`; the others copy computed halfwords `0x80098830` / `0x80098730`). Each has emitted callers (`func_800657CC` is called from inside `func_800030F8`). |
| Forwarder (main state machine) | `func_800030F8` | `0x800024F8` | per-tick: `rumble_set(ch, D_800A3A40[ch])`, caches last-sent in `0x800A3A30[ch]`; two other sites send intensity **5** = the stop sentinel. `func_80008ECC` has a 4th site. |
| **Trigger API** `rumble_set(ch, intensity)` | `func_8006A8B4` | `0x80069CB4` | if enabled flag `0x80110F08[ch]`: clamp to `0x50`, store request in `0x80110F28[ch]`. 5 ROM-wide jals = 5 emitted sites (`BootLoadInitialAssets`, `func_800030F8` ×3, `func_80008ECC`). |
| **Per-frame engine** | `func_8006A910` | `0x80069D10` | PWM engine: request ≥ `0x47` → motor hard ON; < 6 → stop + `osMotorInit` re-init; else duty-cycle via accumulator `0x80110F38[ch] += intensity³ >> 9 + 4`, motor toggled as it wraps `0x100`. **Analog rumble by pulse-width modulation.** Motor-on state in `0x80110F18[ch]`. 3 ROM-wide jals, all inside `BootLoadInitialAssets`'s per-frame region (between frame-counter increments at `0x800A0000-0x7E20`, gated on halfword `-0x7D8E` == 0) — that function contains the running main loop, so the engine ticks every frame. |
| Motor start / stop wrappers | `func_8006A7A0` / `func_8006A82C` | `0x80069BA0` / `0x80069C2C` | stamp cmd byte 3 at `0x8011C680`, submit the per-channel **pre-built** frame via `func_8007F780(1, frame)`, wait on the SI queue, re-check status via `D_8011C6D0`, verify error bits. (`func_8006A870`/`0x80069C70` = second stop wrapper, **0 jals ROM-wide — dead code**.) |
| Frame pre-builder | `func_8007ADE4` (called by `osMotorInit`) | `0x8007A1E4`-area helper at `0x8007ADE4`-name | `osMotorInit` (`func_8007AF60` / `0x8007A360`) probes bank `0x80` @ block `0x400`, requires read-back byte31 == `0x80`, then builds 32-byte `0x01`/`0x00` payloads at `0x8011C930`/`0x8011C910` and stages per-channel write frames for **block `0x600` = address `0xC000`** at `0x8011C810 + ch*0x40` (ON) and `0x8011C710 + ch*0x40` (OFF). |
| Raw SI submit | `func_8007F780` | `0x8007EB80` | **NOT recompiled** — RACE-ignored, supplied natively by the port bridge in `src/libultra_stubs.c`, which passes the game's `a1` buffer to `lambo_joybus_answer`. The port's `0xC000` → `lambo_pak_set_rumble()` → SDL branch is the intended receiver. |
| Boot detection | `BootLoadInitialAssets` + pak scan `func_80069710` | — | pak scan calls `osMotorInit` and writes the present flag `0x80110F08[ch]` (set at funcs_8.c:8601 from the init result; cleared on failure paths). This is the launch-time detection the user observes on hardware. |

## RDRAM variable map

- `0x800A3A40[ch]` (word ×4) — requested rumble intensity, the *gameplay* trigger surface; `0x800A3A30[ch]` last-sent cache.
- `0x80110F08[ch]` — rumble pak present/enabled; `0x80110F28[ch]` — engine request; `0x80110F18[ch]` — motor-on state; `0x80110F38[ch]` — PWM accumulator.
- `0x8011C710/0x8011C810 + ch*0x40` — pre-built OFF/ON joybus frames (block `0x600` ⇒ addr `0xC000`); payload patterns at `0x8011C910` (0x00×32) / `0x8011C930` (0x01×32).
- `0x800986D8` (halfword) — channel/player index used by the forwarder; `0x80098830`, `0x80098730` — computed intensity halfwords feeding two of the writers.
- Intensity semantics: `5` = stop sentinel (engine: <6 ⇒ stop+reinit), `0x32` = typical event value, clamp `0x50`, ≥`0x47` ⇒ solid ON.

## Verdicts on the three audit holes

1. **Force-stubbed/truncated callers** — CLOSED, negative: whole-ROM jal counts for engine (3), `rumble_set` (5), start (2), stop (3), `osMotorInit` (2) each match the emitted call sites one-for-one. No rumble caller hides in stubbed code.
2. **Raw SI frames bypassing the pak-write primitive** — CONFIRMED as the actual mechanism: motor writes are pre-staged frames replayed via `func_8007F780`, never a runtime `__osContRamWrite(0xC000)`. This is why the old enumeration found nothing yet concluded wrongly.
3. **Prompt / detection flow** — located: detection = pak scan `func_80069710` → `osMotorInit` → `0x80110F08[ch]`. UI-side readers of the present flag (candidate "switch to Rumble Pak?" prompt gates, for ticket #104 to probe live): `func_800400EC`, `func_8004EC98`, `func_8004F254`, `func_80050274`.

## Implications for the rest of map #101

- The port's receiving plumbing (bridge → `lambo_joybus_answer` → `0xC000` → SDL) already parses the game-passed buffer, so rumble **may already work or be one small defect away** — nobody has empirically watched for the motor frame in the port. Sharp probes now available: breakpoint/trace `rumble_set` (`0x80069CB4`) and watch `0x80110F28`/`0x800A3A40` writes; `LAMBO_PAK_TRACE=1` shows any `0xC000` WRITE frames reaching the HLE.
- For ares (#103): watchpoint `0x800A3A40` and the frame templates `0x8011C810` read, or break at `0x80069BA0` (start wrapper) — much sharper than scanning PIF traffic.
- `src/libultra_stubs.c`'s FAITHFULNESS NOTE (~line 253) and issue #98's "ROM doesn't drive the motor" out-of-scope rationale are wrong and must be rewritten when #106 lands.

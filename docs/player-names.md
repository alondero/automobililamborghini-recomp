# Persisted player names and records

Issue #170 had two independent causes: restoration ran only when the name editor
opened, and `player.json` values such as `Adam` failed the uppercase-only validator.
A no-edit run therefore left the ROM's player-one buffer as `TITUS`. Loading now
normalizes ASCII lowercase to the ROM keyboard's uppercase alphabet, and record
writers restore the saved identity without requiring a visit to the editor.

The records header at `0x800A4160` is not the player's name. Do not overwrite it or
pre-populate leaderboard rows. Both ROM record writers copy from
`0x800A4160 + 0x6C6 + player_index * 13`, with zero-based player indices. Player
one's source is `0x800A4826`, the same buffer used by the name editor (whose driver
selector at `0x800CE6A6` is instead one-based).

| Writer | Runtime entry / generated symbol | Restoration boundary |
| --- | --- | --- |
| Lap completion | `0x80029628` / `func_8002A228` | `0x80029C48`, after eligibility and player ownership have been resolved; player index is `s16[sp+0x2E]` |
| Five-entry leaderboard | `0x8003F5F0` / `func_800401F0` | Entry, with the signed zero-based player index in `a0` |

Lap completion has four name destinations relative to the records base: `0x72`
(race), `0x17A` (mirrored race), `0x516` (Time Trial), and `0x61E` (mirrored Time
Trial), plus the ROM's class/track offsets. The leaderboard has normal (`0x402`)
and mirrored (`0x470`) tables. Each table row is 14 bytes; the ROM copies the
13-byte name and preserves its own ranking and row-shifting behavior.

`lambo_player_records_tests` extracts and compiles both complete generated ROM
functions, retaining the production hooks. It exercises no-edit persisted names,
both modes/directions, guest ownership, record shifting, confirmed edits and name
validation. The extraction output is generated locally from the user's ROM and
is never committed. Regenerate `RecompiledFuncs` before running this test after
hook changes:

```sh
cmake --build build --target lambo_player_records_tests
ctest --test-dir build -R '^lambo_player_records$' --output-on-failure
```

This deterministic test verifies the record-writing code, not driving, rendering,
Controller Pak serialization, or complete gameplay traversal.

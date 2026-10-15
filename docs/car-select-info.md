# Car select info

Status: implemented for the USA ROM; checked in RT64 on the one-player car
select screen. Acceleration figures are port measurements, not reference-emulator
measurements.

Enable **Show car details on car select** in **Enhancements**. It is off by
default and is saved as `car_select_info` in `graphics.json`. Changes apply
immediately.

The car preview then shows the car's name at the top and three lines below it:

- power, torque and rev limit;
- top speed and number of gears;
- the 0-60 MPH or 0-100 KM/H time, with the difficulty it was measured on.

Speeds follow the game's **SPEEDS IN** option. The time follows the chosen
difficulty, because Expert accelerates harder than Novice. Brakes and steering
are not shown: the game applies the same brake and steering rules to every car,
so there is no per-car figure to show.

## What the numbers mean

The ROM has no car names. Names come from the preview models; each of the eight
physics categories has three colour variants. See
[car identities](CAR_DIFFERENCES.md#car-identities).

| Category | Car | Power | Torque | Rev limit | Gears | Top speed | 0-100 km/h Novice / Expert | 0-60 mph Novice / Expert |
| ---: | --- | ---: | ---: | ---: | ---: | --- | --- | --- |
| 0 | Lamborghini Diablo | 514 hp | 590 N m | 7000 | 5 | 323 km/h, 200 mph | 3.6 / 3.3 s | 3.4 / 3.2 s |
| 4 | Lamborghini Countach | 571 hp | 610 N m | 7200 | 5 | 308 km/h, 191 mph | 2.6 / 2.2 s | 2.5 / 2.1 s |
| 1 | Porsche 959 | 485 hp | 555 N m | 6700 | 6 | 324 km/h, 201 mph | 3.0 / 2.4 s | 2.8 / 2.2 s |
| 2 | Ferrari F50 | 531 hp | 485 N m | 8700 | 6 | 324 km/h, 201 mph | 3.2 / 2.8 s | 3.1 / 2.6 s |
| 3 | McLaren F1 | 550 hp | 590 N m | 7600 | 6 | 331 km/h, 205 mph | 2.8 / 2.3 s | 2.7 / 2.2 s |
| 5 | Bugatti EB110 | 558 hp | 605 N m | 8200 | 6 | 326 km/h, 202 mph | 2.7 / 2.5 s | 2.6 / 2.4 s |
| 6 | Dodge Viper | 527 hp | 730 N m | 6000 | 5 | 312 km/h, 193 mph | 2.7 / 2.4 s | 2.6 / 2.3 s |
| 7 | Ferrari Testarossa | 497 hp | 590 N m | 6500 | 5 | 317 km/h, 196 mph | 3.2 / 2.8 s | 3.1 / 2.7 s |

Rows are in physics-category order down the engine table, grouped as the
menu shows them; the table is not a ranking.

- **Torque** is the peak of the car's engine curve between idle and the rev
  limit. The engine updater uses the sample at engine speed shifted right by
  6, multiplied by throttle, as its driving torque (runtime
  `0x80025ED8-0x80025EF4`). The card reads the samples as newton-metres. That
  unit is an interpretation: it matches published figures for the identified
  cars, such as 605 against the EB110's 611 N m and 485 against the F50's
  471 N m.
- **Power** is the highest engine speed times torque over the same range,
  converted with 745.7 W per horsepower.
- **Rev limit** and **gears** are the engine row's `+4` and `+8` halfwords.
- **Top speed** is the speed at the rev limit in top gear. The engine speed in
  top gear is about 13.77 times km/h times the row's ratio multiplier and the
  gear ratio. That factor is the median from the Diablo's recorded top-gear
  samples (13.74-13.79). The Diablo's recorded lap peaked at 323 km/h on
  6996 RPM and was still accelerating, which matches its calculated limit.
  Top speeds of the other cars are calculated, not observed. The figure is the
  speedometer value: km/h is truncated, and MPH is the game's own
  `trunc(km/h x 0.621)`.
- **Acceleration** is measured in the port with the probe below. Times count
  race updates from launch; 30 updates are one second on the race clock.
  0-60 MPH ends at 97 km/h, the first speed the speedometer shows as 60 MPH.
  Times are rounded to the nearest tenth.

`tests/test_car_card_rom.py` recomputes power, torque, rev limit, gears and top
speed from the ROM and compares them with the table in
`src/lambo_car_card.cpp`. It also checks that every card line fits its strip
using the font's own widths.

## Measurement

The retained [acceleration evidence](evidence/car-select-card-usa.json) uses the
same probe and schema as the earlier campaign in
[car differences](CAR_DIFFERENCES.md#measurements-with-the-actual-model-cursor-2026-09-05):
Circuit 1 time trial, automatic gearbox, A held, zero stick, one model per
category. It was collected on 2026-10-16 from revision `f355970`: two runs
per model and difficulty, 32 runs in all, with no excluded attempts. Both runs
of each case gave identical times. Run it once per difficulty into separate
directories:

~~~powershell
python tools/emu_instrumentation/probe_car_models.py --out scratch/card-novice --difficulty 0 --vis 1000 --reps 2
python tools/emu_instrumentation/probe_car_models.py --out scratch/card-expert --difficulty 1 --vis 1000 --reps 2
~~~

## Guest boundary

All reads and writes happen on the game thread in one hook,
`lambo_car_card_draw`, at runtime `0x80002480` in `func_800028D0`. Both
dispatcher paths join there just before the frame's `G_RDPFULLSYNC` and
`G_ENDDL`, so the card draws over the finished screen.

The hook does nothing unless the option is on, game state `0x800CE6AC` is 6
(menu) and menu screen `0x80098562` is 13 (CAR). It reads the selecting player
at `0x800CE6A6` (1-4), that player's model cursor at
`0x800CE7E8 + (player - 1) * 2` (0-23; the shared vehicle-layout address), the category from model descriptor `0x8013D490 + model*24`,
difficulty `0x800CE7A4` and speed units `0x800CE802`. Any value out of range
skips the card. These are all signed halfwords.

The hook appends commands at the display-list cursor `0x800A39CC` and writes no
other guest memory. The current task pointer at `0x800A2BFC` must be one of
the two double-buffered tasks, `0x800BF240` or `0x800C6C90`. The cursor must be
8-byte aligned and inside that task's display-list arena, which runs from
task + `0x1C0` to the scheduler fields at task + `0x79C0`
([sky panorama](sky-panorama.md)). The card is skipped unless it and the game's
own `G_RDPFULLSYNC` and `G_ENDDL` fit before task + `0x79C0`.

Commands copy the game's own menu drawing:

- Two strips use the CAR panel's two-cycle fill: render mode `0x0C184240`,
  primitive-colour combiner and colour `0x181512C4`. The car's shadow already
  leaves two-cycle mode set; the hook sets it again for clarity.
- Text uses the blue chrome font drawn by `func_8004CCB4` (runtime
  `0x8004C0B4`), which the race HUD and menu code call. A memory dump of the
  car screen shows its glyphs and tables resident there. The hook reads the font's character map at
  `0x8015DA16` and advances at `0x8015DA8A`, and loads each 16x16 RGBA32 glyph
  from `0x8013F820` with that routine's load commands. Only the rectangle is
  scaled: 10-pixel glyphs for the name and 8-pixel glyphs for the stats.
  Texture packs that replace this font also change the card.

The yellow button font has only letters and the digits 1-6, so it cannot show
the stats.

## Tests

- `tests/test_car_card.cpp` (`lambo_car_card`) checks the card text, the
  rounding of times, every gate and the display-list overrun guard against
  RDRAM-backed fixtures.
- `tests/test_car_card_rom.py` (`lambo_car_card_rom`) pins the hook location,
  the instructions it relies on, the font table addresses, the derived stats
  and line widths. The ROM checks skip when the ROM is absent.

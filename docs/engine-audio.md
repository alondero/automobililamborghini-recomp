# Engine sound replacement

Status: experimental procedural engine replacement for the USA cartridge.
The DSP and guest bridge have automated tests. Listening quality is subjective;
these are authored engine sounds built from each car's layout, not recordings.

## Playing and comparing

The replacement is enabled by default. Each car has its own engine layout:

| Car | Layout modelled |
| --- | --- |
| Lamborghini Diablo, Countach | 60-degree V12 |
| Ferrari F50 | 65-degree V12 |
| McLaren F1 | 60-degree V12 |
| Bugatti EB110 | 60-degree V12, turbocharged |
| Porsche 959 | Flat-six, twin-turbocharged |
| Dodge Viper | 90-degree V10 |
| Ferrari Testarossa | 180-degree V12 (flat-twelve) |

The sound follows the car's engine speed, accelerator, gear changes, volume
and stereo pan. Lifting off the accelerator gives a quieter, hollow overrun
with occasional pops; the turbocharged cars vent boost when you lift.
Music, tyres, impacts and menu effects retain their cartridge sounds.
No sound pack or additional download is required.

For an original-audio comparison, set `LAMBO_ENGINE_AUDIO=0` before launching.
Remove the variable, or set it to `1`, to enable the replacement again. This is
a launch-time switch; restart the game after changing it.

```powershell
$env:LAMBO_ENGINE_AUDIO = '0'
./build/lamborghini_modern.exe
```

The game's effects-volume control also scales the replacement. There is no
separate engine volume slider in the port's settings.

## Replacement boundary

The final AI buffer contains music and effects together. Filtering that buffer
cannot selectively remove an engine. Instead, generation hooks observe the
cartridge sound-player API after it accepts an operation, and zero only the
engine's original volume before the library posts its volume event. Original
voice allocation, pitch, pan, playback-status queries and stopping still run.
This avoids changing game logic that tests whether a loop is playing.

The host synthesizes the replacement into a copy of the stereo PCM after
unswizzling its channels and before SDL's persistent sample-rate conversion.
It never edits the original AI buffer. With no engines active, the PCM is
unchanged. Addition saturates to signed 16-bit range rather than wrapping.

The checked-in implementation is:

- [DSP and publication](../src/lambo_engine_audio.cpp): engine profiles, four
  independent engine voices, control snapshots and mixing.
- [Guest bridge](../src/lambo_engine_audio_runtime.cpp): accepted sound events,
  vehicle sampling, selective muting and effects-volume scaling.
- [Generation inputs](../scripts/gen_syms_toml.py): reproducible hook placement.
- [Audio sink](../src/lambo_audio.cpp): mixing before resampling.

Do not edit `RecompiledFuncs/`. Run the supported build to regenerate it after
changing hook placement. The cartridge file is never patched on disk.

## USA layout and evidence

Addresses below are runtime addresses; the historical `func_` names are
0xC00 higher. They were checked against generated instruction listings from
USA ROM SHA-1 `78b36b48040426478011caef5e11884af2e80375`.

| Operation | Function | Hook before | Meaning |
| --- | --- | --- | --- |
| Immediate play | `func_80077550` | `0x80076950` | Called by the accepted `func_80066F84` start, with its stack frame intact. |
| Volume event | `func_800774F0` | `0x800768F0` | Mute only the engine volume register before the event is posted. |
| Delayed start | `func_800670BC` | `0x80066538` | Same acceptance guards; delay is signed microseconds at `sp+0x2C`. |
| Update | `func_800671F8` | `0x80066668` | ID exists and voice status is playing. |
| Stop | `func_800673A0` | `0x80066804` | Voice is playing and will receive stop. |
| Unload effects | `func_80066EB4` | `0x800662B4` | Clear host targets before sound-bank teardown. |

Engine IDs are `0x3E..0x41`. The vehicle update at `0x8002683C..0x80026964`
selects a player engine ID from the vehicle's channel and sends pitch
`0.1 + rpm * 1.5 / (8000 - idle)`. The engine speed is the working copy at
`0x800983A4`, loaded from vehicle `+0x9E` at `0x80025990`; the idle value is
the car's engine-table row (`0x8013E0F0 + category * 52`); the 0.1 constant is
the double at `0x8008DC58`. Its volume is the constant 18000, so the game's
sound parameters carry no accelerator information. The race stop routine
`func_80009008` iterates those four IDs separately from the next four loops.

At the observation sites, `sp` identifies the game wrapper's 0x18-byte frame;
the low-level hooks run before allocating their own frame. The sound ID
is signed 32-bit at `sp+0x18`. Start reads float pitch at `sp+0x1C`, signed
16-bit volume at `sp+0x22`, and byte pan at `sp+0x27`. Update reads byte flags
at `sp+0x1F`, float pitch at `sp+0x20`, signed volume at `sp+0x26`, and pan at
`sp+0x2B`. Flag bits 1, 2 and 4 select pitch, volume and pan independently;
unselected sentinel values must not change host targets. Pan is 0..127.

### Vehicle sampling

The wrappers save `$ra` at `sp+0x14`, but recompiled `jal` calls do not
maintain the guest `$ra`; a race trace read `0x00000000` and `0xFFFFFFFF`
there. The bridge recognises each call site by its arguments instead:

| Signature | Call site | Vehicle read |
| --- | --- | --- |
| Update, flags exactly pitch+volume, volume 18000, pan `0xFF` | Player engine update, `jal 0x80026960` in `func_8002622C` | Index `0x80098398`; its channel must equal voice + 1. |
| ID `0x3F`: immediate start, or update with all three flags | Nearby-car start/update, `jal 0x80002A10`/`0x80002B00` in `func_800030F8` | Index `0x8009869C`; its channel must be an AI channel (5..16). |
| Delayed start | Race start, `jal 0x8000764C` (the only delayed-play caller) | No vehicle yet; the player's model cursor `0x800CE7E8 + voice * 2` selects descriptor `0x8013D490 + model * 24`, whose first halfword is the category. |

Vehicle records are in the relocated pool at `0x80910000`, stride `0x10C`
([race fields](race-fields.md)). The bridge reads category `+0x12`, engine
speed `+0x9E` and throttle demand `+0xAA`. It normalizes throttle by the
channel's limit at `0x80902044 + channel * 0x84`, as the
[analog throttle](analog-throttle.md) bridge does. An index outside 0..63, an
unexpected channel or a category above 7 marks the sample unavailable. The
synth then derives engine speed from the pitch formula, assumes a driven
engine and keeps the last known car layout. Other callers, such as the pause
volume fade at `0x800030E0`, do not change the vehicle sample.

The nearby-car path uses `0x3F`, the second player's engine ID; it shares that
voice just as it does in the cartridge. Its pan and volume come from the
distance to the player.

The race initialization path at `0x8000762C..0x80007650` schedules player
engine `player + 0x3E` with pitch 0.25, volume 18000 and a 500000 microsecond
delay. The host counts that delay in audio samples; an accepted playing-state
update clears any remainder. This preserves the initial half-second silence
without leaving the cartridge engine audible.

The low-level immediate-play function has one cartridge caller:
`func_80066F84`. The volume setter has three: that same wrapper, delayed start
and update. Their shared caller-frame layout is used before the low-level
prologue runs. Volume muting changes `a1`, retaining the already scaled volume
on the stack for the immediate-play observer. Keeping `func_80066F84` itself
unprotected preserves the bundled weather mod's entry hook. Changes to these
callers, or to the argument signatures in the vehicle-sampling table, require
rechecking this stack contract.

Effects volume is signed 16-bit at `0x800A2BE2`. The cartridge multiplies
the supplied volume by that percentage and the double constant 0.01 at
`0x8008E938` (immediate start), `0x8008E940` (delayed start), or
`0x8008E948` (update). Delayed start and update apply that scale before
normalizing by 32767.
The immediate-play observer instead reads the already scaled stack value,
so it does not apply effects volume a second time. Despite older guest
declarations naming the float argument `volume`, it is the playback pitch ratio.

All reads use `MEM_*` on the game thread with N64Recomp's word-swizzled byte
order. Vehicle sampling only reads guest memory. A mutex publishes a complete
host control snapshot. The audio thread does not access guest RAM, and
releases the control mutex before synthesis or SDL calls. DSP state belongs to
the audio thread and resets when the audio device generation changes. Invalid
host controls are clamped; other sound IDs are ignored. The original-audio
switch bypasses both muting and host mixing.

## Sound design and tuning

The sound is generated from original code under the repository's license.
There are no extracted cartridge samples or third-party recordings to ship.
The structure follows the physically informed approach of separating a
firing-pulse source from fixed exhaust resonances (Baldan et al., "Physically
informed car engine sound synthesis for virtual and augmented environments",
IEEE SIVE 2015); no code is taken from that work.

Each voice runs this chain:

1. **Crank and firing.** Crank angle advances `6 * rpm` degrees per second.
   Each profile lists its cylinders' firing angles in the 720-degree cycle and
   the exhaust bank each one feeds. A firing schedules an exhaust pulse with
   sub-sample timing.
2. **Combustion.** Pulse amplitude follows fuelling: full on throttle, an idle
   floor near idle speed, and a weak overrun charge when lifted. Combustion
   varies from cycle to cycle, more at light load. Each cylinder has a fixed
   charge, header-delay and pulse-width signature. Because that signature
   repeats once per cycle, it adds energy at every half order of engine speed:
   the rumble under the firing note. Gas leaving the valve carries filtered
   turbulence noise.
3. **Exhaust.** Each bank has a waveguide pipe: a fixed-length delay with an
   inverting, damped open-end reflection. The collector feeds a silencer with
   two fixed resonances and a low-pass whose cutoff opens with load.
   These resonances are in hertz, not engine orders, so they stay put while
   the revs sweep through them.
4. **Induction and turbo.** An airbox resonance and throttle roar scale with
   load and revs. Turbo profiles build boost with lag, damp the exhaust, add
   spool hiss and release a blow-off burst on a sharp lift.
5. **Radiation.** A 60 Hz first-order high-pass removes pulse DC. A load- and
   rev-dependent soft saturation adds rasp under hard acceleration.
   Afterfire pops occur only within 1.8 s of lifting from above 45% of the
   limiter.

Engine speed passes through two cascaded 15 ms smoothing stages to turn the
game's 30 Hz updates into a continuous sweep, plus a small random wander
(about 0.3%). Throttle smooths over 40 ms; gain and pan over 8 ms.
Coefficients that depend on load refresh every 32 samples of each voice's own
clock, so buffer sizes cannot change the output.

Profiles are indexed by vehicle category
([car identities](CAR_DIFFERENCES.md#car-identities)). Idle and limiter
speeds come from the cartridge engine table. Firing intervals follow the
road cars: even 60 degrees for 60- and 180-degree V12s, alternating 65/55 for
the F50's 65-degree V12, alternating 90/54 for the Viper's 90-degree V10 on
shared crankpins, and 120 degrees for the flat-six. The V12 values follow
from the bank angle. The Viper's 90/54 split is reported consistently by owner
and tuning sources but was not checked against a factory manual. Pipe
lengths, resonances, pulse widths and levels are authored acoustic tuning,
not measurements of the real cars.

Tune a car in its `profiles` row in `src/lambo_engine_audio.cpp`. Keep each
pipe's resonances, `(k + 0.5) / pipe_ms`, away from the firing frequency at
mid revs, or that car's note washes out. The tests enforce this at 4000 rpm.

To audition every car with the same sequence (idle, blip, four full-throttle
gears with ratio-based shift drops, lift-off overrun, part-throttle cruise):

```text
cmake --build build --target lambo_engine_audio_tests
mkdir build/engine-audition
build/lambo_engine_audio_tests build/engine-audition
```

On Windows append `.exe` to the test executable. It writes
`engine-<category>-<car>.wav` files at the in-game level, so turn the volume
up. The files are generated from the same DSP as gameplay and stay in the
ignored build directory. This is a preview; it does not replace an in-game
listening check with music and effects.

## Verification

```text
cmake --build build --target lambo_engine_audio_tests lambo_engine_audio_runtime_tests lambo_audio_locking_tests lambo_audio_recovery_tests
ctest --test-dir build -R "engine_audio|audio_" --output-on-failure
python tools/run_game_scenario.py scenarios/engine-audio-smoke.json
```

DSP tests run every profile at 32000 Hz and the mixing contract at
22050/32000/48000 Hz. They cover unchanged inactive PCM, buffer-size
independence, stereo pan, stop fade, invalid controls, pitch-only fallback
and the cartridge pitch inversion. Acoustic checks measure tonality (energy in
the 20 strongest spectral bins; the PR #438 synth measured 0.90), low-frequency
energy near the limiter, a clear firing-rate pitch, sub-firing-rate rumble,
louder and brighter full throttle than overrun, mix level, the V10's
half-order lump, and the 959's blow-off. Bridge tests cover swizzled stack
fields, selective muting, master volume, partial updates, call-site vehicle
sampling and its fallbacks, the race-start car layout, unrelated effects,
delayed start timing, stop/reset and original-audio bypass.

The checked-in engine scenario enables control tracing. Set
`LAMBO_ENGINE_AUDIO_TRACE=1` and enable debug logging for a manual run to
inspect accepted engine starts and every 120th update per voice, including
the recognised call site, sampled engine speed, throttle, car and
pitch-derived engine speed.
Race testing should include idle, acceleration, shifting, braking,
pause/resume, leaving a race and split-screen. Audio-device recovery tests
check the sink independently of perceived sound quality. Native DSP state is
not part of developer save states; fresh races are the reference for sound
comparisons. Japanese cartridge hooks are not supported by this implementation.

### Session measurements (Windows, 15 November 2026)

The MinGW GCC build completed and all 102 CTest cases passed. The Python
suite passed 427 tests with 14 platform-dependent skips; documentation and
branch whitespace checks passed. Measured with the DSP tests' method at
4000 rpm and full throttle, tonality was 0.37-0.63 across the eight profiles,
against 0.90 for the PR #438 synth.

Headless 600-frame replays passed for a one-player Diablo, a one-player Viper
and a two-player Diablo race. Traces showed:

- Player updates recognised in every sample; vehicle `+0x9E` equalled the
  pitch-derived engine speed each time, and throttle followed the replayed
  input between 0 and 1.
- The race start selected the Viper profile from the model cursor before the
  first update.
- The nearby-car voice sampled AI vehicles (a 959, then a Testarossa) with
  throttle 0.86-1.00 while racing; its engine speed matched each car's own
  idle-based pitch formula.
- In two-player races `0x3F` was recognised as player two's engine.

These runs establish control transport and execution. They are not a
listening verdict; the per-car audition files were produced for that.
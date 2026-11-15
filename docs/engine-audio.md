# Engine sound replacement

Status: experimental procedural engine replacement for the USA cartridge.
The DSP and guest bridge have automated tests. Listening quality is subjective;
this is an authored sports-car sound, not a recording of a particular model.

## Playing and comparing

The replacement is enabled by default. It follows the game's engine pitch,
volume and stereo pan, including the pitch drops produced by gear changes.
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

- [DSP and publication](../src/lambo_engine_audio.cpp): four independent engine
  voices, control snapshots, sample-rate-aware smoothing and mixing.
- [Guest bridge](../src/lambo_engine_audio_runtime.cpp): accepted sound events,
  selective muting and effects-volume scaling.
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
selects a player engine ID and sends a pitch derived from engine state. The
race stop routine `func_80009008` iterates those four IDs separately from the
next four loops. The nearby-car path also uses `0x3F`; it shares that voice
just as it does in the cartridge.

At the observation sites, `sp` identifies the game wrapper's 0x18-byte frame;
the low-level hooks run before allocating their own frame. The sound ID
is signed 32-bit at `sp+0x18`. Start reads float pitch at `sp+0x1C`, signed
16-bit volume at `sp+0x22`, and byte pan at `sp+0x27`. Update reads byte flags
at `sp+0x1F`, float pitch at `sp+0x20`, signed volume at `sp+0x26`, and pan at
`sp+0x2B`. Flag bits 1, 2 and 4 select pitch, volume and pan independently;
unselected sentinel values must not change host targets. Pan is 0..127.

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
callers require rechecking that stack contract.

Effects volume is signed 16-bit at `0x800A2BE2`. The cartridge multiplies
the supplied volume by that percentage and the double constant 0.01 at
`0x8008E938` (immediate start), `0x8008E940` (delayed start), or
`0x8008E948` (update). Delayed start and update apply that scale before
normalizing by 32767.
The immediate-play observer instead reads the already scaled stack value,
so it does not apply effects volume a second time. Despite older guest
declarations naming the float argument `volume`, it is the playback pitch ratio.

All reads use `MEM_*` on the game thread with N64Recomp's word-swizzled byte
order. A mutex publishes a complete host control snapshot. The audio thread
does not access guest RAM, and releases the control mutex before synthesis or
SDL calls. DSP state belongs to the audio thread and resets when the audio
device generation changes. Invalid host controls are clamped; other sound IDs
are ignored. The original-audio switch bypasses both muting and host mixing.

## Sound design and tuning

The sound is generated from original code under the repository's license.
There are no extracted cartridge samples or third-party recordings to ship.

Each voice models twelve firing events per four-stroke cycle, with cylinder
variation, a polynomial band-limited exhaust pressure pulse and reflected
rarefaction, lower engine orders, filtered intake noise and DC removal. The authored mapping is
`rpm = 410 + 4900 * pitch`; it is an acoustic tuning curve, not a claim that
the cartridge stores physical RPM in its sound parameter. Pitch is clamped to
0.1..2.0. Phase continues across buffers and shifts. Pitch changes slew over
18 ms and gain/pan over 8 ms to suppress clicks and stepped control noise.

Tune the pitch-to-RPM mapping, pressure-pulse shape, filter cutoff, body
harmonics and output gain in `Synth::mix`. The current brightness estimate
uses voice gain; it is not a separately measured throttle/load signal. All
cars currently share this twelve-cylinder character. Model-specific V8/V12
profiles and recorded multi-RPM layers are possible future work, not current
features. The sample-rate ceiling and filtering do not make this a physical
exhaust simulation.

To audition an isolated 14-second idle/acceleration/shift/lift-off sequence:

```text
cmake --build build --target lambo_engine_audio_tests
build/lambo_engine_audio_tests build/engine-audition.wav
```

On Windows append `.exe` to the test executable. The WAV is generated from
the same DSP as gameplay and stays in the ignored build directory. This is a
preview; it does not replace an in-game listening check with music and effects.

## Verification

```text
cmake --build build --target lambo_engine_audio_tests lambo_engine_audio_runtime_tests lambo_audio_locking_tests lambo_audio_recovery_tests
ctest --test-dir build -R "engine_audio|audio_" --output-on-failure
python tools/run_game_scenario.py scenarios/engine-audio-smoke.json
```

DSP tests cover unchanged inactive PCM, buffer-size independence, stereo pan,
RPM-dependent spectral peaks, stop fade, and 22050/32000/48000 Hz input rates.
Bridge tests cover swizzled stack fields, selective muting, master volume,
partial updates, unrelated effects, delayed start timing, stop/reset and original-audio bypass.

The checked-in engine scenario enables control tracing. Set
`LAMBO_ENGINE_AUDIO_TRACE=1` and enable debug logging for a manual run to inspect accepted
engine starts and occasional parameter updates. Race testing should include
idle, acceleration, shifting, braking, pause/resume, leaving a race and
split-screen. Audio-device recovery tests check the sink independently of
perceived sound quality. Native DSP state is not part of developer save states;
fresh races are the reference for sound comparisons. Japanese cartridge hooks
are not supported by this implementation.

### Session measurements (Windows, 15 November 2026)

The MinGW GCC 15.2 build completed with regenerated USA functions. Five focused
CTest cases passed. The 600-frame headless replay passed, and a D3D12/device
run opened stereo SDL audio at 48000 Hz with the guest producing 22050 Hz.
The race trace observed delayed player start `0x3E` at pitch 0.25 and gain
0.549, an immediate nearby-car start `0x3F`, and changing pitch/pan during the
race. A time-trial trace ranged from approximately 0.34 to 1.58 pitch and
showed later drops. A four-player replay started IDs `0x3E..0x41` and passed.
These establish execution and control transport, not a human listening verdict.
The audition WAV and logs are local build artifacts, not repository assets.

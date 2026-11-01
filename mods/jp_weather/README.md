# JP weather package (experimental)

This development package applies measured JP race-slot selection, fog settings,
AI speed parameters, wet-weather countdown operation and low-speed maneuver
throttle cap to the USA game. Lights, grass traction and perspective-distance
changes remain separate work. It is not full JP parity.
See [the investigation](../../docs/jp-weather.md).

The package hooks the unprotected final audio call in the race loader. It
matches the loader phase and argument tuple, after the selected circuit,
championship round and fog fields have been initialized. It does not replace
the protected AI or loader routines. The base bridge changes only two register
results inside the existing AI acceleration branch; no registered mod retains
the stock cap of 100. Generated base calls do not preserve
guest return addresses, so the hook does not use a return-address guard.

## Settings

`Weather` defaults to **US default**, which performs no gameplay writes on a
fresh run. Dry, Wet and Night enable the package. Single races and time trials
use that selection. Championship races use twelve independent round settings,
all initially Dry; these are race slots, not circuit IDs. Changes apply when
the next race loads. The mod settings persist through the standard Mods tab.

Time challenge stays Dry. Single races with three or four players stay Dry.
The JP loader has no equivalent player-count gate in its championship branch.
Invalid selections leave US behavior in place. Disable or unload code mods by
restarting the game, as required by the mod manager.

The USA game has two difficulty tiers; JP has four. This weather package does
not transplant JP difficulty tiers. Wet changes the USA speed parameters from
`290/240` to `220/180` and applies JP's arithmetic right shift to the existing
countdowns: stock USA `40/25` becomes `20/12`. Dry and Night preserve the USA
countdowns. Selecting US default for a later race restores the captured AI
table values; the stock loader initializes that race's normal fog.

Wet sets the AI maneuver cap to 60, while all other modes use 100. This applies
only to the native speed-below-60 branch after its signed halfword addition of
10. It does not globally clamp throttle or change the speed-at-least-60
deceleration branch. Registration stores a pointer to the mod's cap word, so
the bridge reads the current value on each call and never caches a host cap.

Developer savestates exclude the mod heap. Restoring across races with different
weather is unsupported; the base bridge does not extend that snapshot format.

## Build

Supply a MIPS-capable Clang, LLD and the pinned RecompModTool. No ROM is needed
to build this original package source:

~~~text
python scripts/build_jp_weather_mod.py --clang clang --linker ld.lld --mod-tool PATH_TO_RECOMP_MOD_TOOL
~~~

On Windows, Clang builds that omit the MIPS backend cannot build the package.
The builder can use Clang in WSL while keeping the linker and packager native:

~~~text
python scripts/build_jp_weather_mod.py --wsl-distro Ubuntu --clang /usr/bin/clang --linker PATH_TO_LD_LLD_EXE --mod-tool PATH_TO_RECOMP_MOD_TOOL_EXE
~~~

Package version 0.2.0 requires a port built with the
`lambo_register_ai_maneuver_cap_v1` export introduced by this change. Older
0.10.3 builds lack it and reject the package before gameplay.
Install `build/jp-weather/lambo_jp_weather.nrm` through the Mods tab and restart.
The builder accepts `--output` for an alternate build directory. It does not
install tools or modify the base port.

## Validation

~~~text
python -m unittest discover -s tests -p "test_jp_weather*.py"
python -m unittest discover -s tests -p test_build_jp_weather_mod.py
python tools/check_jp_weather_mod.py --exe PATH_TO_PORT --package build/jp-weather/lambo_jp_weather.nrm --rom "Automobili Lamborghini (USA).z64"
~~~

The loader checker creates isolated profiles and retains logs and car traces.
It checks absent/default/dry/wet/night, multiplayer restriction, time challenge
and championship configuration. A successful loader check is not proof of AI
trajectory parity. The bridge preserves the existing port relocation fixes;
do not force-replace the protected AI routine.

The [retained loader run](../../docs/evidence/jp-weather-mod-loader.json) records
the executable and package identities for all eight cases.
Host tests exercise exact memory writes, restoration and repeated races; ROM
contract tests check the hook target, loader arguments and data addresses.

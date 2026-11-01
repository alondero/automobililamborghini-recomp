# JP weather package (experimental)

This development package applies measured JP race-slot selection, fog settings,
AI speed parameters and the wet-weather countdown operation to the USA game.
It does not yet reproduce the instruction-encoded wet throttle cap, lights,
grass traction or perspective-distance changes. It is not full JP parity.
See [the investigation](../../docs/jp-weather.md).

The package hooks the unprotected final audio call in the race loader. It
matches the loader phase and argument tuple, after the selected circuit,
championship round and fog fields have been initialized. It does not replace
the protected AI or loader routines. Generated base calls do not preserve
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
trajectory parity. Do not force-replace protected functions to implement the
missing throttle branch: that would remove existing port relocation fixes.

The [retained loader run](../../docs/evidence/jp-weather-mod-loader.json) passed
all eight cases with the package hash recorded there. It used an existing local
port executable older than the current source checkout, not a fresh full build.
Host tests exercise exact memory writes, restoration and repeated races; ROM
contract tests check the hook target, loader arguments and data addresses.

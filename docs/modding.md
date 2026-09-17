# Modding

The project has useful replacement features, but it does not have a general
code-mod system. This distinction is important.

## Supported today

### Texture packs

RT64 can load one loose texture directory or one .rtz pack through
graphics.json or LAMBO_TEXTURE_PACK. The port also exposes runtime texture
dumping for pack authors. See [Texture packs](TEXTURES.md).

Texture packs replace renderer resources. They do not change game code,
collision, AI, physics, menus, or save formats.

### Track Lab corrections

Track Lab can apply a guarded .altrk package containing sparse visibility
corrections to a stock circuit. It checks the ROM/runtime context and applies
the package as a unit. This is experimental.

It does not currently import arbitrary geometry, collision, navigation, AI,
new tracks, or new race rules. The research notes contain hypotheses that have
not become a stable mod contract.

## Not supported

- arbitrary native code mods;
- a general mod manager or load-order system;
- arbitrary custom playable tracks;
- user-defined collision, navigation, or AI data;
- a promise that a texture pack works on every backend or platform;
- replacing the matching ROM with another regional release.

Do not call texture packs or Track Lab a full modding system in player-facing
documentation.

## Intended direction

The safer long-term route is:

~~~text
better decompilation and source patches
        -> stable symbols and explicit hook contracts
        -> versioned game data/code mod boundary
        -> documented mod packages and compatibility checks
~~~

The current port-side guest-memory bridges are a temporary constraint on that
route. A real code-mod API should use named, versioned interfaces and reject
incompatible builds. It should not ask authors to guess raw guest addresses.
This is a proposed direction that requires maintainer approval and design work.

## Standards used for comparison

The official Banjo: Recompiled mod template shows one practical pattern for
native function patches and a manifest. Shipwright documents user-facing
assets and mod workflows. SpaghettiKart documents package metadata,
dependencies, and load order. These are reference points, not promises that
this project already provides the same system.

The [reference project comparison](peer-projects.md) records the official
patterns that informed these limits and which hands-on checks remain
unverified.

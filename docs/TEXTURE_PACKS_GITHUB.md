# GitHub storage and distribution for texture packs

**Status:** primary-source research snapshot, 2026-09-04

This note covers how established N64 projects store, build, and distribute
replacement textures, with a recommendation for an Automobili Lamborghini
RT64 pack. It supplements the port-specific [texture-pack workflow](./TEXTURES.md);
it does not change the runtime format or claim that a texture pack is part of
the game code.

## Recommendation

Keep replacement artwork in a separate companion repository (for example,
`automobili-lamborghini-textures`) and keep this port repository asset-free.
That repository should:

* commit the editable source images (normally PNG), `rt64.json`, a small
  pack-manifest/README, provenance and credits, and the scripts/tool versions
  needed to rebuild the pack;
* target the supported ROM revision explicitly (`USA` today), and record the
  RT64 configuration/hash versions and minimum port version in metadata;
* generate DDS, the RT64 low-mipmap cache, and `.rtz` in CI from a tagged
  commit; and
* attach the end-user `.rtz` files to a GitHub Release, with SHA-256 checksums
  and separate `hd`/`4k` variants if both are offered.

Do not commit the original ROM, ROM-derived raw dumps, or generated release
archives to the port's normal history. Do not ship PNG as the end-user RT64
format. If editable source files such as layered art or project files become
large, use Git LFS for those source files selectively; LFS is a version-control
aid, not a replacement for release packaging or a way to make copyrighted ROM
content distributable.

This split keeps the code checkout small, lets artists review source and
metadata, and gives players one immutable, tested binary pack. It also follows
the existing port policy: the [project README says no game assets are shipped](../README.md#legal),
and [CONTRIBUTING.md requires contributions to be free of upstream ROM bytes or assets](../CONTRIBUTING.md#legal).

## What the primary sources show

### RT64: source images plus a versioned hash manifest

The RT64 project's own [texture-pack specification](https://github.com/rt64/rt64/blob/main/TEXTURE-PACKS.md)
defines a pack as image files plus an `rt64.json` database. The document makes
the source/distribution distinction explicit:

* PNG is supported for convenience while developing a pack, but the document
  says not to ship PNG to end users because runtime decompression increases
  load time and memory use.
* DDS is the first-class shipping format, including BC7 compression and
  mipmaps. RT64 tries DDS before PNG when both exist.
* `rt64.json` maps one or more hashes to a path and can set per-texture
  `stream`, `preload`, or `stall` behavior and `half`/`none` texture shifting.
* The example manifest carries `configurationVersion: 3` and `hashVersion: 5`.
  The hash is based on RT64's XXH3 hash of N64 TMEM contents; Rice hashes are
  supported for compatibility with older packs but should not be invented by
  hand.
* RT64's `texture_packer` creates a distributable `.rtz`, includes the JSON and
  low-mipmap cache, and uses zstd by default. If DDS and PNG for one texture
  are both present, only DDS is put into the `.rtz`. The low-mipmap cache must
  be regenerated whenever DDS changes.

The same project publishes the required command-line tools as a tagged
[Texture pack tools release](https://github.com/rt64/rt64/releases/tag/texture_pack_tools),
which is a useful build input to pin in CI. A loose directory is still useful
for authoring/debugging; RT64's in-game replacement editor requires a
directory rather than an `.rtz` file.

### SM64 Reloaded: source tree, sidecar metadata, and release variants

[SM64 Reloaded's README](https://raw.githubusercontent.com/GhostlyDark/SM64-Reloaded/master/README.md)
describes source PNG textures as the input used to generate HTS caches and
keeps platform conversion scripts in the repository. Its [pack tree](https://github.com/GhostlyDark/SM64-Reloaded/tree/master/SUPER%20MARIO%2064)
contains the game texture hierarchy and sidecar files including
[`rt64.json`](https://github.com/GhostlyDark/SM64-Reloaded/blob/master/SUPER%20MARIO%2064/rt64.json),
[`sm64.tdb`](https://github.com/GhostlyDark/SM64-Reloaded/blob/master/SUPER%20MARIO%2064/sm64.tdb),
and [`mod.json`](https://github.com/GhostlyDark/SM64-Reloaded/blob/master/SUPER%20MARIO%2064/mod.json).

The checked-in `rt64.json` is a concrete example of the RT64 contract: it
records `configurationVersion` 3, `hashVersion` 5, `autoPath`, defaults, and
entries containing Rice and RT64 hashes. `mod.json` adds pack identity,
`game_id`, a human version (`2.6.0`), and `minimum_recomp_version`. The README
also documents a `manifest.json` regeneration step when the Ghostship input
archives change, showing why a pack should identify the consumer/build it was
generated for rather than relying only on a filename.

The [v2.6.0 GitHub release](https://github.com/GhostlyDark/SM64-Reloaded/releases/tag/v2.6.0)
is a particularly useful distribution example. It links an external project
download page as well as GitHub Releases, and attaches separately named
platform/format artifacts: Dolphin DDS and PNG archives, GLideN64 HTS and PNG
archives, and Ghostship O2R and PNG archives. The listed assets include
SHA-256 values and sizes (for example, a 916 MB GLideN64 HTS 4K archive, a
1.23 GB GLideN64 PNG 4K archive, and smaller HD variants). In other words,
the repository is the editable source/metadata home, while releases are the
stable, consumer-specific binaries; the external site is a fallback/mirror,
not a mutable branch URL.

### Fanfreluche's SM64 pack: a small source-only repository

[Fanfreluche's README](https://raw.githubusercontent.com/Fanfreluche/SUPERMARIO64-hires-texture-pack/master/README.md)
instructs users to copy the `hires_texture` directory into an emulator's
texture path. The [committed texture tree](https://github.com/Fanfreluche/SUPERMARIO64-hires-texture-pack/tree/master/hires_texture/SUPER%20MARIO%2064)
contains hash-named PNG files rather than an opaque cache, and the README
records separate EU and USA hash values for some glyphs. This is a useful
small-pack model: source files can be directly inspectable, but region is
still part of texture identity. The repository also includes an explicit
[MIT license](https://github.com/Fanfreluche/SUPERMARIO64-hires-texture-pack/blob/master/LICENSE),
which is the level of per-pack licensing/credit information the Automobili
Lamborghini companion repository should provide for original contributor art.

### Mupen64Plus-Next/GLideN64: compiled caches are compatibility-sensitive

The official libretro [Mupen64Plus-Next documentation](https://github.com/libretro/docs/blob/master/docs/library/mupen64plus.md#high-resolution-textures)
documents two workflows: download precompiled `.htc` caches, or place
uncompressed Rice textures in the named game directory and let the emulator
compile the cache. It says that precompiled packs work only when the texture
related core settings match those used at compilation; enabling extra alpha
and enhanced-storage options produces `.hts`. This is evidence for recording
the target backend, cache/format, and relevant settings in release metadata,
and for treating `.htc`/`.hts` as generated consumer artifacts rather than
the canonical authoring source.

## GitHub storage and release mechanics

GitHub's own [large-file guidance](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github)
warns at 50 MiB, blocks regular Git files over 100 MiB, recommends keeping a
repository ideally below 1 GB (and strongly below 5 GB), and says to use Git
LFS or Releases for large files. Its [release documentation](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)
says releases are tied to Git tags and are intended for deployable binaries;
each release asset must be under 2 GiB, with up to 1,000 assets and no total
release-size or bandwidth limit.

Use those mechanisms as follows:

| Material | Normal home | Reason |
| --- | --- | --- |
| Editable PNG, `rt64.json`, pack manifest, scripts, credits | Companion repository Git history | Reviewable, diffable, reproducible source and metadata. |
| Large layered source (`.blend`, `.psd`, etc.) | Companion repository with selective Git LFS patterns | LFS stores a pointer in Git and the object separately; collaborators still get versioned source without inflating ordinary Git history. See [GitHub's Git LFS documentation](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-git-large-file-storage). |
| Generated DDS, low-mipmap cache, `.rtz` | Tagged GitHub Release asset | End users need a tested binary; it should not be re-downloaded by every source checkout or duplicated through every commit. |
| Pack larger than one 2 GiB release asset | Split by resolution/region or use a first-party external host linked from the release | Keep each GitHub asset below the documented limit and publish checksums/mirrors. SM64 Reloaded demonstrates the release-plus-external-download pattern. |
| Original ROM, `.z64`, ROM-derived raw dumps, or unlicensed extracted assets | Never publish | The port and comparable N64 projects use bring-your-own-ROM/asset extraction. |

Git LFS is not free object storage in the abstract: [GitHub documents that it
keeps a pointer in the repository and the actual object elsewhere](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-git-large-file-storage).
It is appropriate when the *editable source* itself is a large binary that
must be versioned. It is unnecessary for ordinary PNG sources and does not
make a ROM or a ROM-derived texture legally redistributable. For player-facing
packs, Release assets provide a simpler download path and immutable tag
context.

## Compatibility and manifest requirements

At minimum, every Automobili Lamborghini release should expose these fields
in a human-readable `pack.json` (or equivalent sidecar) in addition to the
required `rt64.json`:

```json
{
  "pack_id": "automobili-lamborghini-rt64",
  "pack_version": "0.1.0",
  "game_id": "automobili_lamborghini",
  "region": "USA",
  "base_rom_sha1": "<hash only; never the ROM>",
  "min_port_version": "<first compatible port release>",
  "rt64_configuration_version": 3,
  "rt64_hash_version": 5,
  "format": "rtz",
  "variant": "hd",
  "source_commit": "<tag or commit>",
  "license": "<pack artwork license>",
  "rights_confirmed": false,
  "credits": "CREDITS.md"
}
```

The exact field names are a recommendation, not a runtime requirement today.
The important compatibility keys have precedent:

* RT64's manifest carries configuration and hash versions and can map both
  RT64 and legacy Rice hashes ([RT64 specification](https://github.com/rt64/rt64/blob/main/TEXTURE-PACKS.md#configuration-file)).
* SM64 Reloaded's [`mod.json`](https://raw.githubusercontent.com/GhostlyDark/SM64-Reloaded/master/SUPER%20MARIO%2064/mod.json)
  carries `game_id`, pack `version`, and `minimum_recomp_version`.
* Region/revision must not be assumed from the pack name: the Fanfreluche
  README lists different USA/EU hashes, and the port currently supports only
  the USA release ([port README](../README.md#legal)).
* The Ship of Harkinian project uses a [supported-ROM hash list and explicit
  asset extraction](https://github.com/HarbourMasters/Shipwright/blob/develop/README.md#quick-start),
  and its [versioning document](https://github.com/HarbourMasters/Shipwright/blob/develop/docs/VERSIONING.md)
  says a major `x` version requires a new `oot.o2r`. This is a good precedent
  for making a pack's required port/asset-generation version explicit.
* Mupen64Plus-Next's documentation requires matching texture settings for
  precompiled caches. Record the equivalent RT64 choices (hash version,
  DDS/PNG, default shift, stream/preload policy, and pack tool version) in
  the release notes or manifest.

Keep `rt64.json` authoritative for texture identity and paths. Use
`pack.json` for game/region/port/reproducibility information that RT64 does
not own. If the pack eventually supports another ROM revision, publish a
separate variant or a clearly versioned manifest; do not silently mix hashes
from incompatible revisions.

The seed validator also requires `rights_confirmed: true` and rejects pending
rights language in `CREDITS.md` for a public build. This is an explicit release
check, not a claim that the current candidate artwork is licensed.

## Licensing and ROM separation

The legal separation is a project boundary, not just a `.gitignore` trick:

* [sm64ex's README](https://raw.githubusercontent.com/sm64pc/sm64ex/master/README.md)
  explicitly says not to upload copyrighted assets, provides a clean command
  to remove ROM-originated content, and describes external texture loading.
* [Mario Kart 64 decompilation's README](https://raw.githubusercontent.com/n64decomp/mk64/master/README.md)
  says the repository contains no assets and requires extraction from a prior
  copy of the game; it also publishes supported ROM revisions and SHA-1
  checksums. A hash/checksum identifies a required input without distributing
  that input.
* [Shipwright's README](https://raw.githubusercontent.com/HarbourMasters/Shipwright/develop/README.md)
  says it includes no copyrighted assets, requires a legally acquired ROM,
  extracts those assets into `.o2r`, and loads custom assets from `.otr` mods.
  That project also links its releases separately from the user-supplied ROM.

For this project, keep the companion pack's license and credits scoped to
original replacement artwork and scripts. Add a `CREDITS.md`/`NOTICE` that
names every contributor and any reused texture, states its license or
permission, and identifies the target ROM revision by hash only. Do not copy
the original `.z64`, RT64 raw texture dumps, Rice/RDRAM captures, or other
ROM-derived material into either repository or a release. The pack can be
optional and layered over the original artwork; users supply the game input
separately.

## Suggested companion-repository layout

```text
automobili-lamborghini-textures/
  README.md                 # install and supported port/release
  LICENSE                   # original pack artwork/scripts
  CREDITS.md                # contributors and reused assets
  NOTICE.md                 # ROM/asset separation and provenance
  pack.json                 # game/region/version/tool compatibility
  rt64.json                 # RT64 hash -> path/operation/shift manifest
  textures/                 # editable PNG sources, grouped by scene/category
  previews/                 # optional screenshots/contact sheets
  tools/                    # dump decoding, conversion, packaging scripts
  .github/workflows/        # deterministic DDS + .rtz release build
```

Generated DDS files, low-mip cache, `.rtz`, CI scratch dumps, and temporary
decode output should be ignored in normal branches. A release workflow should
build from a tag, pin the RT64 texture-pack tools and CPU DDS encoder, run
manifest/coverage checks, generate the low-mip cache after the final DDS
conversion, package the `.rtz`, and publish SHA-256 values beside each asset.

The first Automobili Lamborghini release can be one USA RT64 `.rtz` plus a
source archive. If the pack grows beyond one release asset, split by
resolution or provide a stable external mirror while retaining the manifest,
tag, checksums, installation instructions, and compatibility warning on the
GitHub release page.

## Sources

All sources below are first-party project repositories/docs or official GitHub
documentation:

* [RT64 texture-pack specification](https://github.com/rt64/rt64/blob/main/TEXTURE-PACKS.md)
  and [RT64 texture-pack tools release](https://github.com/rt64/rt64/releases/tag/texture_pack_tools)
* [SM64 Reloaded README](https://raw.githubusercontent.com/GhostlyDark/SM64-Reloaded/master/README.md),
  [pack tree](https://github.com/GhostlyDark/SM64-Reloaded/tree/master/SUPER%20MARIO%2064),
  [`rt64.json`](https://raw.githubusercontent.com/GhostlyDark/SM64-Reloaded/master/SUPER%20MARIO%2064/rt64.json),
  [`mod.json`](https://raw.githubusercontent.com/GhostlyDark/SM64-Reloaded/master/SUPER%20MARIO%2064/mod.json),
  and [v2.6.0 release](https://github.com/GhostlyDark/SM64-Reloaded/releases/tag/v2.6.0)
* [Fanfreluche SM64 pack README](https://raw.githubusercontent.com/Fanfreluche/SUPERMARIO64-hires-texture-pack/master/README.md),
  [texture tree](https://github.com/Fanfreluche/SUPERMARIO64-hires-texture-pack/tree/master/hires_texture/SUPER%20MARIO%2064),
  and [MIT license](https://github.com/Fanfreluche/SUPERMARIO64-hires-texture-pack/blob/master/LICENSE)
* [Mupen64Plus-Next high-resolution texture documentation](https://github.com/libretro/docs/blob/master/docs/library/mupen64plus.md#high-resolution-textures)
* [sm64ex README](https://raw.githubusercontent.com/sm64pc/sm64ex/master/README.md),
  [Mario Kart 64 README](https://raw.githubusercontent.com/n64decomp/mk64/master/README.md),
  and [Shipwright README](https://raw.githubusercontent.com/HarbourMasters/Shipwright/develop/README.md)
* [Shipwright versioning](https://raw.githubusercontent.com/HarbourMasters/Shipwright/develop/docs/VERSIONING.md)
* Official [GitHub large-file guidance](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github),
  [Git LFS](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-git-large-file-storage),
  and [releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)

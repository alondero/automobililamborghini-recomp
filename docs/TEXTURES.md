# Texture extraction & replacement (issue #9 foundation, 2026-07-06)

Reference for anyone (human or LLM) adding replacement/HD textures — the first target
being the game's hard-to-read text. Everything here was verified end-to-end on this port:
a headless dump of the demo race, an offline decode, and a replacement texture visibly
applied in-game (the speedometer's MPH digits and the title "COPYRIGHT 1997 TITUS" line
turned solid magenta once their font-atlas texture was replaced).

## What RT64 already gives us (and what the port adds)

RT64 (`lib/rt64`) ships the **entire** texture-replacement system — hashing, a dump mode,
a pack database, DDS/PNG loading, `.rtz` packaging tools. The port previously wired **none**
of it (`src/rt64_renderer.cpp` was "adapted from Zelda64Recomp minus the texture-pack
plumbing"). Issue #9's foundation is just to *expose* it:

| Capability | Provided by | Where |
|---|---|---|
| Texture identity = XXH3-64 of the used TMEM bytes + TLUT + tile params (**version 5**) | RT64 | `common/rt64_tmem_hasher.h` |
| Dump every uploaded texture (raw TMEM/RDRAM + tile JSON) | RT64 | `hle/rt64_rdp_tmem.cpp` `dumpTexture()`, gated on `state->dumpingTexturesDirectory` |
| Pack manifest `rt64.json` (hash → file, per-texture stream/preload + shift) | RT64 | `common/rt64_replacement_database.*` |
| Load a pack (loose directory **or** `.rtz`) | RT64 | `TextureCache::loadReplacementDirectory(ReplacementDirectory)` |
| **Config-driven dump + startup pack auto-load** | **port** | `src/rt64_renderer.cpp` (after `app->setup()`), `src/lambo_config.cpp` |
| Dump → viewable PNG decode | **port** | `tools/decode_dump.py` |
| Generate `rt64.json` from replacement files | **port** | `tools/make_pack.py` |
| Build `.rtz` (+ low-mip cache) | RT64 | `build/rt64/src/tools/texture_packer/texture_packer.exe` |

Texture identity is the **TMEM content hash**, not an RDRAM address — so a replacement keyed
by hash survives the asset moving in memory, but the *same on-screen text drawn at a
different scale/palette is a different hash* (see the coverage gotcha below).

## Config keys (graphics.json)

Extra string/bool keys alongside the standard `GraphicsConfig` fields, each overridable
by an env var so a headless capture never has to touch the user's `graphics.json`:

| Key | Type | Default | Env override | Effect |
|---|---|---|---|---|
| `texture_pack` | string | `""` | `LAMBO_TEXTURE_PACK` | Directory or `.rtz` auto-loaded at startup. |
| `texture_dump` | string | `""` | `LAMBO_TEXTURE_DUMP` | Directory RT64 writes every used texture to. |
| `widescreen_fog_match` | bool | `true` | `LAMBO_FOG_MATCH_1P=1/0` | Issue #83: widen the dense 3P/4P split-screen fog to the 1P window/colour (rewrite of `G_MW_FOG` + `gDPSetFogColor` on the shared RDRAM DL, gated `players >= 3`). 1P/2P stay faithful. |

Both are independent of `developer_mode`, so an end-user pack loads **without** the F1
developer overlay. On success the log prints `[rt64] texture pack loaded: …` /
`[rt64] texture dump enabled -> …`.

## End-to-end workflow

### 1. Dump

```bash
# headless: no dev overlay needed
LAMBO_TEXTURE_DUMP=/path/to/dump  ./build/lamborghini_modern
```

Coverage is **runtime-driven**: a texture is only dumped once the game uploads it to TMEM.
Exercise every screen whose text you care about — attract intro, PRESS START title,
menus (RACE / NAME / records), and the in-race HUD. The dev warp menu (`LAMBO_WARP`,
F1–F6) reaches race screens quickly. Missed screen ⇒ missing texture in the pack.

Each unique texture writes `<hash>.v5.tmem`, `<hash>.v5.tile.json` (fmt/siz/dims/tlut),
plus `.rice.rdram` / `.rice.palette.rdram` for CI textures. These are **raw data, not
images**.

> The in-repo `tools/drive_input.py` drives the window headlessly (PostMessage + PrintWindow)
> and is how the captures for this doc were produced. It matches SDL's `SDL_app` window
> class, so an editor/browser with the repo open in a tab won't be captured by mistake.

### 2. Decode to viewable PNGs

```bash
python tools/decode_dump.py /path/to/dump          # writes <dump>/png/*.png + index.html
```

Open `index.html` (a contact sheet) to eyeball the whole dump. **Decode fidelity:**
- **RGBA16 / RGBA32 / IA / I** — accurate. The "automobili Lamborghini" wordmark tiles
  decoded pixel-clean.
- **CI4 / CI8** (palettized — *the format most fonts use*) — accurate as of issue #50.
  They used to come out "sheared and miscoloured", but the cause was the **TLUT byte order**,
  not the texel decode: `.rice.palette.rdram` is raw RDRAM, which RT64 stores byte-swapped
  within each 32-bit word (logical byte `A` at physical `A^3`). Reading the 16-bit palette
  entries without that swap mapped every index to a garbled RGBA5551 value, and because the
  noise still carried the image's index structure it *looked* like a diagonal shear. The tool
  now reads the palette through the `^3` swap by default (`--pal-no-swap` is a calibration
  knob). Texels still decode from `.tmem`, which was already correct. Verified end-to-end: the
  `aec01187` 512×8 font atlas reads as legible characters and the sky/cloud CI4 tiles show
  clean blue/gold. RT64's live F1 inspector still gives a GPU-decoded cross-check.

### 3. Identify the text

The HUD/menu font atlases are **CI4/CI8 banner-shaped** textures (e.g. the small HUD font is
`aec01187…`, a 512×8 CI4 strip of glyphs). Filter the dump for `fmt2` (CI) textures that are
wide-and-short. There are **several** font atlases — replacing the small-HUD one changed the
speedo digits and the title copyright line but **not** "LAP/TIME/RANK/GET READY", which use a
different atlas. Expect to find and replace each.

### 4. Author replacements

- Name each file by the RT64 hash: `<16-hex-hash>.png` (or `.dds`). That hash is exactly the
  dump filename prefix.
- **PNG** loads directly and is fine for iteration. **DDS** (BC7 + mipmaps, e.g. via Texconv
  / Compressonator's *CPU* encoder) is what you ship — never ship PNG.
- Keep `shift: half` for modern-tool exports.
- Coverage gotcha restated: a CI font re-hashes when its palette changes (highlighted vs.
  normal menu item), so the same glyphs can need replacing under several hashes.

### 5. Build the manifest + pack

```bash
python tools/make_pack.py /path/to/pack            # writes rt64.json from the <hash>.png/.dds files
```

`rt64.json` is **required** — RT64 does not auto-scan for hash-named files, and its own
`texture_hasher` only *upgrades* an existing manifest, it won't create one. `make_pack.py`
fills that gap.

Point the port at the directory to test:

```bash
LAMBO_TEXTURE_PACK=/path/to/pack  ./build/lamborghini_modern
```

To ship, zip to a `.rtz` (loads identically):

```bash
build/rt64/src/tools/texture_packer/texture_packer.exe /path/to/pack --create-low-mip-cache
build/rt64/src/tools/texture_packer/texture_packer.exe /path/to/pack --create-pack
```

(The low-mip cache is only meaningful for DDS mipmaps; with PNG it is empty, which is fine.)

## The F1 developer overlay (optional)

With `developer_mode: true`, RT64's overlay opens on **F1** (Inspector) / **F4** (Replacements),
giving a live per-draw-call texture view, "Start dumping textures", and interactive replace.
⚠️ **Key clash:** the dev warp menu also uses F1–F6 (`src/main.cpp` polls those scancodes
directly), so both fire at once. The config-driven dump/pack above avoids the overlay
entirely, which is why it is the recommended path here; if you need the live inspector,
expect the warp keys to also trigger.

## Files

- `src/rt64_renderer.cpp` — startup wiring (dump dir + `loadReplacementDirectory`).
- `src/lambo_config.{h,cpp}` — `texture_pack` / `texture_dump` / `widescreen_fog_match` keys + env overrides.
- `src/lambo_fog_widescreen.cpp` — issue #83 3P/4P fog-widening DL rewrite.
- `tools/decode_dump.py` — dump → PNG (+ contact sheet).
- `tools/make_pack.py` — replacement files → `rt64.json`.

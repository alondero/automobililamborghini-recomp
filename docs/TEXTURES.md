# Texture packs

This is the end-to-end guide for creating native RT64 replacement packs for the port.
Texture-pack support is content-agnostic: an HD-art pack, a readable-text pack, or a small
one-texture experiment all use the same runtime facility, and the replacement artwork can
live in a separate repository. Everything below was verified with a config-driven texture dump,
an offline decode, a loose replacement directory, and a packaged `.rtz` loaded in-game.

This guide covers the generic RT64 dump, authoring, manifest, and loading contract.

## What RT64 already gives us (and what the port adds)

RT64 (`lib/rt64`) ships the texture-replacement system: hashing, a dump mode, a pack
database, DDS/PNG loading, and `.rtz` packaging tools. The port exposes those facilities
through startup configuration and supplies the dump-decoding and manifest-generation tools:

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

Texture identity is the **TMEM content hash**, not an RDRAM address. A replacement keyed by
hash survives the source asset moving in memory, but a different palette or tile state can
produce another hash for artwork that otherwise looks identical.

## Config keys (graphics.json)

Two string keys alongside the standard `GraphicsConfig` fields, each overridable by an
environment variable so a capture or test run never has to modify `graphics.json`:

| Key | Type | Default | Env override | Effect |
|---|---|---|---|---|
| `texture_pack` | string | `""` | `LAMBO_TEXTURE_PACK` | Directory or `.rtz` auto-loaded at startup. |
| `texture_dump` | string | `""` | `LAMBO_TEXTURE_DUMP` | Directory RT64 writes every used texture to. |

Both are independent of `developer_mode`, so an end-user pack loads **without** the F1
developer overlay. On success the log prints `[rt64] texture pack loaded: …` /
`[rt64] texture dump enabled -> …`.

## End-to-end workflow

### 1. Dump

```bash
# config-driven: no developer overlay needed (do not set LAMBO_HEADLESS=1)
LAMBO_TEXTURE_DUMP=/path/to/dump  ./build/lamborghini_modern
```

Coverage is **runtime-driven**: a texture is only dumped once the game uploads it to TMEM.
Exercise every screen and scene the pack should cover: attract/title, menus, every relevant
HUD state, vehicles, rivals, and each track. The dev warp menu (`LAMBO_WARP`, F1–F6) reaches
race screens quickly. A scene not visited can leave its textures out of the dump.

Each unique texture writes `<hash>.v5.tmem`, `<hash>.v5.tile.json` (fmt/siz/dims/tlut),
plus `.rice.rdram` / `.rice.palette.rdram` for CI textures. These are **raw data, not
images**.

> The in-repo `tools/drive_input.py` drives the window automatically (PostMessage + PrintWindow)
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

### 3. Choose textures to replace

Browse the generated contact sheet and copy only the PNGs you want to change into a separate
pack directory. Keep each 16-hex hash as the filename: it is the identity RT64 uses at runtime.
Some artwork appears under several hashes because its palette or tile state changes, so verify
all variants in-game. Fonts are commonly CI4/CI8 strips, while HUD art, logos, vehicles, and
track textures may use unrelated formats and dimensions.

### 4. Author replacements

- Name each file by the RT64 hash: `<16-hex-hash>.png` (or `.dds`). That hash is exactly the
  dump filename prefix.
- **PNG** loads directly and is fine for iteration. **DDS** (BC7 + mipmaps, e.g. via Texconv
  / Compressonator's *CPU* encoder) is what you ship — never ship PNG.
- Grid-aligned integer upscales use `--shift none` (the generator default). Modern-tool
  exports that bake a half-texel origin offset can opt into
  `python tools/make_pack.py /path/to/pack --shift half`; test atlas and tiled textures
  carefully because the wrong shift produces sampling offsets or neighbouring-texel bleed.
- Paletted textures re-hash when their palette changes (for example, highlighted versus normal
  menu art), so visually identical pixels can require replacements under several hashes.

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
- `src/lambo_config.{h,cpp}` — `texture_pack` / `texture_dump` keys + env overrides.
- `tools/decode_dump.py` — dump → PNG (+ contact sheet).
- `tools/make_pack.py` — replacement files → `rt64.json`.

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

Two string keys, both empty (off) by default, each overridable by an env var so a headless
capture never has to touch the user's `graphics.json`:

| Key | Env override | Effect |
|---|---|---|
| `texture_pack` | `LAMBO_TEXTURE_PACK` | Directory or `.rtz` auto-loaded at startup. |
| `texture_dump` | `LAMBO_TEXTURE_DUMP` | Directory RT64 writes every used texture to. |

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
- **Shift depends on how you authored the pixels.** A modern-tool export that bakes a
  half-texel origin offset wants `shift: half`. But an *upscale* of the decoded dump
  (`tools/upscale_font.py`, an integer-scaled copy of the N64 texel grid) is grid-aligned
  and wants **`shift: none`** — `half` over-shifts it by half a source texel, which in-game
  reads as each glyph doubling into its neighbour's cell (verified on the 512×8 HUD font:
  `shift: half` renders "ONE PLAYER" as "ONE PLAYER:"). So `make_pack.py --shift none` for
  the upscale pipeline; `--shift half` (the default) for offset exports.
- Coverage gotcha restated: a CI font re-hashes when its palette changes (highlighted vs.
  normal menu item), so the same glyphs can need replacing under several hashes. The main
  text font ships as **three** 512×8 CI4 hashes — `2cc2b764…`/`aec01187…` (white) and
  `7c1ef5cc…` (gold menu-highlight) — one typeface, three palettes.

### Upscaling the decoded font (algorithmic, letterform-preserving)

`tools/upscale_font.py` turns a decoded `<hash>.png` into an N×-larger replacement that
de-blocks the staircase edges while keeping the original 1997 letterforms (it does not
re-draw from a modern font). It bleeds the glyph colour into the transparent region before
resampling, then upscales colour and alpha independently so a palettized font's undefined
transparent RGB can't fringe the glyph edges. `--edge crisp` keeps edges tight on very short
(≤8px) glyphs; `--edge soft` is the faithful default.

```bash
python tools/upscale_font.py <dump>/png/<hash>.png <pack>/<hash>.png --scale 8 --edge crisp
python tools/make_pack.py <pack> --shift none      # grid-aligned upscale -> shift none
```

### Re-rendering the font for *readable* text (issue #52, experimental pack)

Upscaling only smooths the 8px source; it can't add detail, so the text stays soft. To make
text genuinely **legible at native/4K output**, `tools/render_font.py` re-draws each glyph
from a real vector font at high resolution (HD packs work because RT64 remaps the game's UVs
onto a higher-res replacement — `lib/rt64/src/render/rt64_texture_cache.cpp`).

The hard part is that the three small-font atlases are packed **differently** and need
different handling — the tool embeds a per-atlas profile keyed by hash:

- **White** (`aec01187`, `2cc2b764`) — proportionally packed, glyphs at irregular positions,
  and **italic** in the original → pass a bold *italic/oblique* face via `--ttf-italic`.
- **Gold** (`7c1ef5cc`) — cleanly separated on a regular pitch, **upright** → `--ttf`.

Key rules (all learned the hard way): render every glyph at a **uniform cap-height + stroke and
position it — never stretch to fill the box** (stretching fattens `I`, thins `M`); paint onto a
**transparent base** (any upscaled base leaves original-glyph slivers between glyphs); bottom-
align punctuation; pack with **`--shift none`**. A naive fixed-8px-cell mapping or upright
glyphs in the italic atlas **jumbles** the text.

```bash
for h in aec011878342c59d 2cc2b76457edc973 7c1ef5cc1ab579eb; do
  python tools/render_font.py <dump>/png/$h.png <pack>/$h.png \
      --ttf <Bold.ttf> --ttf-italic <BoldOblique.ttf>
done
python tools/make_pack.py <pack> --shift none
```

Use an **open-licensed** bold face (e.g. Liberation Sans Bold / Bold-Italic, or DejaVu Sans
Bold / Bold-Oblique) so the shipped pack embeds no proprietary font. **This is an opt-in,
experimental pack — texture packs are never on by default** (`texture_pack` is empty unless the
user sets it / `LAMBO_TEXTURE_PACK`). The larger blue-chrome HUD/menu font (LAP/TIME/RANK,
PAUSED, the name grid) is **not an atlas** — it is ~40 per-glyph 16×16 textures (see the
xBRZ section below), so a vector re-render would need a per-glyph pipeline; the xBRZ
upscale covers it meanwhile.

### xBRZ-upscaling everything else: HUD art, wordmarks, signs (issue #52 follow-up)

Text re-rendering covers the small fonts, but the rest of the 2D art — the title
wordmark, the Lamborghini bull, the speedometer dial, the gold countdown letters,
trackside signs — is still tiny pixel art that reads blocky against natively-rendered
polygons. `tools/upscale_texture.py` batch-runs the **xBRZ** pixel-art scaler (vendored
under `tools/xbrz/`, GPLv3 like this repo; the CLI auto-builds with g++ on first run)
over a whole decoded dump:

```bash
python tools/decode_dump.py <dump>
python tools/upscale_texture.py <dump> <pack> --scale 6
python tools/make_pack.py <pack> --shift none     # grid-aligned -> shift none, as ever
```

Verified end-to-end at 1600×900: title bull/wordmark/PRESS START, the race HUD
(LAP/TIME/RANK, the whole speedo dial, gear pill, mp/h readout) and the pak-message
text all render visibly smoother with original letterforms intact.

What the tool does beyond calling xBRZ, and why:

- **Edge padding follows each tile's clamp bits** (`cms`/`cmt` from the dump's
  tile.json: 0=wrap, 1=mirror, 2/3=clamp). A tiling texture scaled in isolation gets
  seams at every repeat because xBRZ treats the bitmap edge as a hard boundary; the
  tool pads 3 texels per side in the tile's own addressing mode, scales, then crops.
- **Transparent texels get RGB bled from opaque neighbours** before scaling (same
  reason as `upscale_font.py` — palettized decodes leave junk RGB under alpha=0).
- **CI textures whose decode hit the palette-fallback (magenta) are skipped loudly** —
  a handful of dumped CI palettes are shorter than the indices used, and shipping the
  fallback would replace a good texture with corruption. ~30 of ~620 skip this way;
  they stay at their original resolution in-game.

Trade-off to know about: xBRZ turns the banding in smooth photographic gradients
(skies, clouds) into crisp painterly regions — most textures look strictly better, the
sky becomes a matter of taste. Use `--only`/`--exclude` (hash-prefix filters) to carve
the pack if wanted, e.g. `--exclude` the three small-font atlases when layering the
`render_font.py` output on top (or simply run `render_font.py` into the pack dir
afterwards — later writes win since the manifest is per-hash).

Dump-coverage discoveries that took real chasing (don't re-derive):

- **The big blue-chrome HUD font (LAP/TIME/RANK, PAUSED, name grid) is NOT an atlas** —
  it ships as ~40 individual 16×16 RGBA textures, one per glyph, plus 128×16 strips
  for the ordinal suffixes ("st/nd/rd/th"). They only enter TMEM once a real race HUD
  draws, so a menu-only dump misses them entirely.
- The gold on-track message letters (GET READY / countdown ONE-TWO-THREE etc.) are
  individual 24×24 RGBA tiles.
- The speedo dial is assembled from parts: 32×32 rim/arc quadrants, a 32×13 gear
  pill, a 96×21 red-glass mph window, 32×8 "mp/h" labels, 16×7 red shift-light arrows.
- The title wordmark ("automobili Lamborghini") is ~20 64×32 RGBA16 tiles.

So: to build a complete pack, dump through **attract → title → menus → an actual
race with rivals** (gear changes, countdown, at least one lap for all HUD states).
Union multiple dumps by running the tool over each into the same pack directory.

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

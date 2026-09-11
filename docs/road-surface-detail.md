# Baked road surface detail

This optional RT64 pack adds asphalt grain, small pits and baked relief highlights
to the main driving surface used on circuit 1. Its editable source is maintained
in the companion texture repository at `textures/296a811299243783.png`. It replaces v5 hash
`296a811299243783`, a 128x16 atlas containing the road and lane markings. The
replacement retains their layout and exports at 768x96, the companion pack's 6x
scale. It uses BC7, ten mip levels, streaming, a low-mip cache and `shift: none`.

This is authored **albedo detail**, not dynamic normal mapping. Lighting does not
respond to a per-pixel normal map. It is a small content experiment related to
#191, not an implementation of that issue's normal-map or HUD-filtering requests.
Other textures and circuits are outside its verified scope.

## Build and enable

From the port root, with the companion textures checkout and the two pack tools:

```powershell
python tools/build_road_bump_pack.py `
  --companion F:/src/automobili-lamborghini-textures `
  --texconv /path/to/texconv.exe `
  --rt64-packer /path/to/texture_packer.exe `
  --output artifacts/road-detail
```

The output directory must be new or empty. The builder reads the companion source
checkout and does not modify it.
The builder prints the resulting `.rtz` path. To enable it for ordinary launches,
add `--install /path/to/graphics.json`. This preserves other settings and refuses
to replace an already configured texture pack. Restart the game afterward.
The usual Windows config is `%LOCALAPPDATA%/LamborghiniRecomp/graphics.json`;
for portable mode, pass its active `graphics.json` instead.
Clear `texture_pack` to disable the effect.

For a single launch, set `LAMBO_TEXTURE_PACK` to the printed archive path before
starting the game. Merely building the executable does **not** enable this pack.
See `docs/TEXTURES.md` for the loading contract. This one-texture pack replaces
the configured pack; it does not merge with a full HD pack.

## Evidence and limitations

The target was established with a controlled replacement in the live renderer.
The earlier handoff's `b5bc57ba524dce3e` affects an apron beside the road; replacing
it did not change the main driving surface. A hash appearing in a texture dump
and a successful pack-loading log do not establish a visible effect by themselves.

The before/after comparison in `docs/images/road-detail/` uses actual RT64
captures. The stock surface is compared with this pack; the same circuit,
vehicle, camera and graphics settings are used. Highlights identify the asphalt
region, not a change to lighting or geometry.

Build and runtime verification are recorded in the PR. The standard headless
harness checks gameplay through the software renderer and cannot verify RT64
replacements. Separate RT64 runs check the pack and replay. Validation hardware
is an RTX 3080; Intel HD 620 performance has not been measured.

## Authoring provenance

The source is maintained in the companion texture repository. New artwork was authored with the
built-in imagegen tool on 2026-09-09. The source is stored with normalized UV
coordinates on a 3:1 canvas; Texconv restores the 8:1 runtime atlas. The pack
retains the companion metadata's unresolved release-license status.

Authoring prompt: preserve the left white edge, central white dash and right
yellow edge in their normalized locations; preserve dark/light lane regions;
replace blurry angular asphalt with fine aggregate, pits and restrained baked
highlights; no new cracks, paint or wet glare; seamless longitudinal tiling.
The final edit removed generation padding and stretched that same road content
to fill the master canvas before the runtime aspect-ratio conversion.

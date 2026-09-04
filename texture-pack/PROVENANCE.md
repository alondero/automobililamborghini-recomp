# Pack provenance

This source set was generated on 2026-09-03 from six runtime-driven RT64 v5
dumps of the supported USA revision. Coverage exercised circuits 1-6, car
selectors 0-5, menus, HUD states, vehicles, and scenery. The current set has
569 replacements.

## Processing

* 551 replacements use BC7 DDS with full mipmaps and asynchronous streaming.
* 18 alpha-critical HUD/font and sky-seam replacements use lossless,
  single-level RGBA8 DDS and preload at startup. Their hashes are listed in
  `texture-policy.json`.
* Cars, HUD geometry, and layout-sensitive scenery use geometry-safe xBRZ
  upscaling with tile-aware padding.
* Road (`b5bc57ba524dce3e`), limestone wall (`9a54e91197135650`), and dusk sky
  (`3751e36042f86cc8`) use authored enhancement masters. The car-atlas authored
  candidate was rejected because it changed the UV layout.

The raw TMEM/RDRAM captures and the original ROM remain local authoring inputs;
they are not part of this repository or any release. Keep the detailed prompt
and review notes alongside the source repository if the contributors approve
publishing them.

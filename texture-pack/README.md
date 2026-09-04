# Automobili Lamborghini RT64 texture pack

This directory is the seed for the separate `automobili-lamborghini-textures`
repository. It is kept in the port repository only so the pack format, build
steps, and release contract stay discoverable; the port repository deliberately
does not commit replacement artwork.

## Source and generated files

Put editable, hash-named PNG sources in `textures/` in the companion repository.
Each file must be named `<16-hex-RT64-hash>.png`; the hash is the RT64 v5 TMEM
identity written by the port's dump tooling. Do not put `.z64`, `.tmem`,
`.rdram`, palette dumps, or other ROM-derived captures in this repository.

`rt64.json` is generated from the final replacement set, committed at the
companion repository root, and included in the runtime archive. `pack.json`
describes the game revision, pack version, port compatibility, and artwork
license. `texture-policy.json` records the small set
of protected textures as one entry per hash, including lossless format,
single-level mip policy, and preload behavior (the speedometer, font, and sky
seam family); all other textures stream as BC7 DDS with mipmaps.

The optional local seed command copies the currently generated PNG sources into
an external companion-repository directory. Requiring the destination keeps
ROM-derived artwork out of the port checkout:

```powershell
pwsh ./texture-pack/tools/import-current-pack.ps1 `
  -DestinationDirectory C:/src/automobili-lamborghini-textures/textures
```

To create a complete standalone checkout (including the source import and a
root `rt64.json`) in one step, run this from the port checkout:

```powershell
pwsh ./texture-pack/tools/bootstrap-companion.ps1 `
  -DestinationDirectory C:/src/automobili-lamborghini-textures `
  -InitializeGit
```

The destination must be outside the port checkout. In that companion checkout,
`textures/` and `rt64.json` are normal tracked source; the root repository's
`.gitignore` keeps any local staging directory used for testing out of the port
history.

## Local build

The pack builder requires:

* Python 3.9 or newer;
* Microsoft's `texconv.exe` (DirectXTex) for PNG -> DDS conversion; and
* RT64's `texture_packer.exe` from the pinned texture-pack tools release.

From the companion repository:

```powershell
python ./tools/build_pack.py `
  --texconv C:/tools/texconv.exe `
  --rt64-packer C:/tools/texture_packer.exe
```

The output is written under `dist/`:

* `automobili-lamborghini-rt64-hd-v0.1.0.rtz` - end-user archive;
* `SHA256SUMS` - checksums for the archive and release metadata;
* `pack.json`, `CREDITS.md`, `NOTICE.md`, `PROVENANCE.md`, and an optional
  `LICENSE` - release-sidecar metadata; and
* `loose/` - generated DDS files and manifest for local testing.

Before a release, test the loose directory with the port using
`LAMBO_TEXTURE_PACK`, then test the `.rtz` itself. The pack is optional and
does not contain a ROM; players must supply their own USA copy and configure
the port's `texture_pack` setting.

## GitHub Release workflow

Set `pack_version` (for example `0.1.0`) and push the matching tag (`v0.1.0`)
in the companion repository. The workflow in
`.github/workflows/release.yml` validates the metadata, downloads pinned RT64
and DirectXTex tools, converts the source images, regenerates the low-mipmap
cache, creates the `.rtz`, computes SHA-256, and opens a **draft** GitHub
Release. Review the compatibility and licensing notes before publishing it.

For a private test build without a tag, open the repository's **Actions** tab,
select **Build and release texture pack**, and choose **Run workflow**. The
generated `.rtz` and metadata are uploaded as an artifact for 14 days; tag
runs additionally create a draft release. Both triggers use the release
validation gate, so the artwork license, completed credits, and
`rights_confirmed: true` are required before either build can succeed.

Keep generated DDS, low-mipmap caches, `.rtz`, and CI scratch output out of
normal Git history. Use Git LFS only for genuinely large editable source files
such as layered project files; it is not a distribution channel.

## Before publishing

1. Fill the artwork license and every contributor in `CREDITS.md`, remove its
   pending-rights note, and set `rights_confirmed` to `true` in `pack.json`.
2. Confirm `base_rom_sha1` in `pack.json` identifies the supported USA revision
   without distributing the ROM.
3. Exercise every menu, HUD state, vehicle, and circuit represented by the pack.
4. Confirm the release notes state the minimum compatible port version,
   RT64 hash/configuration versions, variant, and installation path.

The broader rationale and examples are in the port's
[GitHub texture-pack research](https://github.com/alondero/automobililamborghini-recomp/blob/main/docs/TEXTURE_PACKS_GITHUB.md);
the runtime dump/authoring contract is in its
[texture workflow](https://github.com/alondero/automobililamborghini-recomp/blob/main/docs/TEXTURES.md).

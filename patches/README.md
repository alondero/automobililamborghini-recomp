# Dependency patch inventory

These patches are part of the build input. They are applied to pinned public
submodules by the build scripts or by CMake. They are not a private fork.

The patch files are the source of truth for exact hunks. The descriptions below
explain why each patch exists and which changes need comparison with upstream.

A patch file is itself a diff, so a blank context line inside a hunk is a line
holding one space — exactly what `git diff` emits. Never strip that space:
after a CRLF checkout (standard on Windows CI) a stripped blank line becomes
a lone carriage return and `git apply` rejects the whole patch as corrupt.
`.gitattributes` forces LF checkouts for `*.patch` and exempts their
significant single-space lines from `git diff --check`;
`tests/test_patch_line_endings.py` enforces both properties.

| Patch | Dependency | Purpose and current status |
| --- | --- | --- |
| 0001 | N64ModernRuntime | Scheduler dispatch, audio/VI runtime behavior, and port runtime integration. Required on desktop and Android. Separate generic runtime behavior from game policy before proposing a fix. |
| 0004 | Plume inside RT64 | MinGW/D3D12 COM ABI struct-return compatibility. Windows-only. Compare with current Plume before proposing a generic fix. |
| 0005 | RT64 | MinGW compiler compatibility in the texture hasher. Windows-only. Compare with current RT64 before proposing a generic fix. |
| 0006 | RT64 | Frame-interpolation transform matching used by this port. All desktop/Android paths that use the patch series. Needs a generic reproducer before proposing a fix. |
| 0007 | N64ModernRuntime | Save-state thread-context relinking used by the port's developer save-state tool. Not a player quick-save guarantee. |
| 0009 | RT64 | Widescreen split-screen viewport origin used by the Lamborghini HUD path. Project-specific unless another game needs the same API. |
| 0010 | RT64 | Intel automatic backend-selection workaround. Keep tied to a reproducible device and driver case. |
| 0011 | RT64 | Explicit backdrop tag, world-matched horizontal projection and independent vertical coverage. The port extends panorama tiles; see [sky motion evidence](../docs/sky-panorama.md). |
| 0012 | N64ModernRuntime | Lazy RDRAM commitment and the source file required by this CMake build. Required on desktop and Android. |
| 0013 | RT64 | Android cross-build support for host shader/compiler inputs. Android-only local build integration. |
| 0014 | Plume inside RT64 | Android SDL/Vulkan window integration. Android-only. |
| 0015 | SDL dependency | Android USB receiver registration compatibility. Android-only. |
| 0016 | N64ModernRuntime | Host-owned configuration storage for the frontend integration. Applied by CMake; project integration. |
| 0017 | RecompFrontend | Host event handling and Lamborghini frontend integration, including controller menu actions resolved from the pressing device instead of only player one. Applied by CMake; the player-one-only lookup still exists on upstream `main`, so this hunk is an upstream proposal candidate. |
| 0018 | N64ModernRuntime | Game presentation behavior used by the launcher and normal game path. Applied by CMake; project integration. |

## Application matrix

| Build path | Applies |
| --- | --- |
| Linux script | 0001, 0007, 0012, 0006, 0009, 0010, 0011, then 0016 through 0018 in CMake. |
| Windows script | The Linux set plus 0005 and 0004, then 0016 through 0018 in CMake. |
| Android script | 0001, 0007, 0012, 0006, 0009, 0010, 0011, 0013, 0014, and 0015, then 0016 through 0018 in CMake. |

If a patch no longer applies to its pinned submodule, stop and update the
patch or pin as a deliberate change. Do not reset a developer's unrelated
submodule work to make the build pass.

## Renderer boundary

RT64 supplies the general N64 renderer, texture replacement, and standard
F3DEX or extended-GBI handling. The port owns Lamborghini-specific display-list
hooks, projection policy, split-screen policy, texture paths, and defaults.

The project does not maintain a private RT64 fork. The local renderer-related
exceptions are visible in this patch list: interpolation matching (0006),
backdrop behavior (0011), split-screen viewport origin (0009), backend selection
policy (0010), and platform compatibility (0004,
0005, 0013-0015). `src/stub_renderer.cpp` is a separate port-owned diagnostic
renderer, not an RT64 feature.

The current boundary is imperfect where a patch combines reusable runtime
behavior with Lamborghini policy. That is a maintenance risk, not proof that
the patch violates an upstream contract. Separate those concerns before
replacing or proposing the patch.

Do not call a local patch upstream-supported without checking the pinned source
and the current upstream project. If a change may be reusable, make the
upstream project's issue or pull request the canonical proposal record. Keep
the local purpose, test, and limitation here and in the pull request.

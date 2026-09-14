# Dependency patch inventory

These patches are part of the build input. They are applied to pinned public
submodules by the build scripts or by CMake. They are not a private fork.

The patch files are the source of truth for exact hunks. The descriptions below
explain why each patch exists and what still needs a human upstream decision.

| Patch | Dependency | Purpose and current status |
| --- | --- | --- |
| 0001 | N64ModernRuntime | Scheduler dispatch, audio/VI runtime behavior, and port runtime integration. Required on desktop and Android. Mixed runtime and game needs; audit before upstreaming. |
| 0004 | Plume inside RT64 | MinGW/D3D12 COM ABI struct-return compatibility. Windows-only. Candidate portability fix; no upstream proposal recorded. |
| 0005 | RT64 | MinGW compiler compatibility in the texture hasher. Windows-only. Candidate portability fix; no upstream proposal recorded. |
| 0006 | RT64 | Frame-interpolation transform matching used by this port. All desktop/Android paths that use the patch series. Needs a generic reproducer before upstreaming. |
| 0007 | N64ModernRuntime | Save-state thread-context relinking used by the port's developer save-state tool. Not a player quick-save guarantee. |
| 0008 | RT64 | Project-specific skybox/backdrop behavior. Its general renderer shape may be reusable, but the Lamborghini policy belongs in the port. |
| 0009 | RT64 | Widescreen split-screen viewport origin used by the Lamborghini HUD path. Project-specific until a generic API is agreed. |
| 0010 | RT64 | Intel automatic backend-selection workaround. Keep tied to a reproducible driver case. |
| 0011 | RT64 | FOV-independent backdrop tag and projection behavior. Requires renderer-boundary review before upstreaming. |
| 0012 | N64ModernRuntime | Lazy RDRAM commitment and the source file required by this CMake build. Required on desktop and Android. |
| 0013 | RT64 | Android cross-build support for host shader/compiler inputs. Android-only. |
| 0014 | Plume inside RT64 | Android SDL/Vulkan window integration. Android-only. |
| 0015 | SDL dependency | Android USB receiver registration compatibility. Android-only. |
| 0016 | N64ModernRuntime | Host-owned configuration storage for the frontend integration. Applied by CMake. |
| 0017 | RecompFrontend | Host event handling and Lamborghini frontend integration. Applied by CMake. |
| 0018 | N64ModernRuntime | Game presentation behavior used by the launcher and normal game path. Applied by CMake. |

## Application matrix

| Build path | Applies |
| --- | --- |
| Linux script | 0001, 0007, 0012, 0006, 0008, 0009, 0010, 0011, then 0016 through 0018 in CMake. |
| Windows script | The Linux set plus 0005 and 0004, then 0016 through 0018 in CMake. |
| Android script | 0001, 0007, 0012, 0006, 0008, 0009, 0010, 0011, 0013, 0014, and 0015, then 0016 through 0018 in CMake. |

If a patch no longer applies to its pinned submodule, stop and update the
patch or pin as a deliberate change. Do not reset a developer's unrelated
submodule work to make the build pass.

See [renderer boundary](../docs/reference/renderer.md) and
[upstream status](../docs/upstream-prs.md) for the capability audit and
proposal record.

# Named gameplay extension seams

Status: **Proposed design**, for [#394](https://github.com/alondero/automobililamborghini-recomp/issues/394).
The inventory below is confirmed against this checkout. New declarations and
tests below are proposals, not available exports or claims of ROM parity.
Implementation follows maintainer review; this change adds no runtime prototype.

## Context and decision

The issue predates the [experimental v1 game interface](mod-api.md). That
interface already supplies named queries, lifecycle events, racer extensions
and utility stores. Keep it, the logging export and stock-function hooks as
the compatibility baseline. Add small, independently versioned capabilities
when a concrete consumer needs them; do not replace the package loader.

A supported gameplay seam is a named operation or event whose meaning,
ownership, timing, layout and failures are documented independently of a ROM
address. A capability identifies an implemented version of such a contract
for the selected cartridge. A regional adapter resolves that contract to
the cartridge's implementation. A symbol name alone is not a capability.

Guest source owns gameplay state and rules. The port owns host settings,
capability discovery and translation between the guest ABI and host services.
Mods own their private extension data. New interfaces expose values, copied
snapshots or opaque handles, not writable views of retail records. Existing
body-address and data-symbol interfaces keep their documented USA limits;
this proposal does not retrospectively make them layout-independent.

## Current inventory

| Path | Registration and current contract | Compatibility limit |
| --- | --- | --- |
| Stock function entry/return hooks and replacements | Packages use `lamborghini.syms.toml`, the pinned RecompModTool and runtime. [Modding](modding.md#code-mod-contract) describes authoring and rejection. `scripts/gen_mod_patch_guards.py` derives protected functions from generation inputs. | USA function identities and signatures; injected hooks, instruction patches and stubs cannot be safely regenerated from retail bytes. Ordinary conflicting replacements fail; force patches explicitly assume responsibility for removed port behavior. |
| Named base exports | `src/lambo_mods.cpp::register_content` registers `lambo_log_v1`, `lambo_register_ai_maneuver_cap_v1`, `lambo_register_rain_v1`, and calls game/storage registration. Logging validates and synchronously copies a bounded guest string. | Logging remains the minimal execution oracle. AI/rain registrations retain validated guest u32 pointers, use last-successful-registration ownership and reset on fresh boot; these are narrow existing exceptions, not a general memory interface. See [weather](jp-weather.md) and [rain](rain.md). |
| Game exports/events | `src/lambo_mod_game.cpp::register_game_api` registers `lambo_mod_api_version`, game-value and racer lookups, extension registration/lookup, and nine events listed in [mod-api.md](mod-api.md#events). Owner-aware exports receive the importing mod identity from the runtime. | API version currently returns 1, not a per-feature or per-ROM capability set. Game data is USA-specific. Racer handles and extension storage have explicit reset/removal lifetimes. |
| Utility stores | `src/lambo_mod_storage.cpp::register_storage_api` registers create/put/insert/get/erase/destroy for private maps, sets and slots. | Mod ownership, limits and failure sentinels are already specified in [mod-api.md](mod-api.md#utility-stores); preserve them. |
| Runtime/frontend exports | Runtime package configuration and native-library imports, plus frontend `recompui_*` exports, use the pinned dependencies; see [modding](modding.md#code-mod-contract). | Native-library `recomp_api_version` and package `minimum_recomp_version` are separate from gameplay contract versions. |
| Guest source patches | `scripts/build_guest_patches.py` compiles pit, vehicle-selection, menu-input and vehicle-update C with MIPS o32, links `guest/patches.ld` and `guest/symbols.ld`, then invokes N64Recomp with retained relocations. `src/register_overlays.cpp` calls `lambo_register_guest_patches` in `src/lambo_guest_patches.cpp`; it registers real compiled sections followed by protected legacy sections via `register_patches`. | Real patch bytes/relocations allow hook regeneration while replacement-conflict checks remain. Original entry identities preserve indirect calls. Generated output stays build-local. |
| Private manual patch symbols | `register_manual_patch_symbols` binds `lambo_get_automatic_pit_stops`, `lambo_get_extra_vehicles`, `lambo_get_menu_stick_sensitivity`, `lambo_vehicle_input`, and `lambo_vehicle_observe` to `0x8F000000..0x8F000010` in four-byte steps. `guest/symbols.ld` supplies the corresponding linker bindings. | These are synthetic call identities, not memory cells or public mod exports. Their host `recomp_context` adapters are implementation details. Do not import them by numeric address. |

The [guest bridge inventory](guest-bridge-inventory.md) covers the wider port
bridges. This inventory covers extension entry paths, not every internal hook.

## Proposed discovery and compatibility

Keep `lambo_mod_api_version()` and all v1 meanings unchanged. Add one bootstrap
export to the base `*` namespace in a future release:

```c
/* Proposed guest declarations; not present in mod_api.h today. */
enum LamboCapabilityV1 { LAMBO_CAP_PIT_ASSIST_SETTING = 1 };
int32_t lambo_query_capability_v1(uint32_t capability, uint32_t major);
```

Numeric capability IDs are permanent identifiers declared with descriptive
names in the public guest header; never recycle them. Initially reserve only
ID 1 for the concrete pit query below. Add IDs for existing interface families
only after auditing their per-region behavior; a blanket v1-supported flag
would hide the USA layout dependencies.

The result is `1` for supported, `0` for unknown capability, `-1` for a known
capability with unsupported major version (including zero), and `-2` for a
known version unavailable for the selected ROM. Check in that order. This is
an availability query, not an enable toggle: assistance being off still means
the capability is supported. Results are fixed from game-thread init until
the next boot. Re-query on a new boot; never reuse a result across cartridges.

The first release implementing discovery must document its actual minimum
port version. Packages importing discovery or the pit query must declare at
least that release in `minimum_recomp_version`; do not invent a version now.
An old host cannot be made compatible by calling a function it cannot bind.
Existing packages keep their existing minimum-version and import behavior.

All exports introduced in that release must remain bindable on every ROM
accepted by that host, using unsupported-result adapters where necessary.
Thus an optional feature can query availability after import binding without
failing because its known export is absent on JP. This does not provide weak
imports for arbitrary future functions: packages using those functions need
their introducing release too. Bind exports before loading mods; select and
freeze capability availability before `recomp_on_init`.

An optional consumer disables only its unsupported feature and logs once via
`lambo_log_v1`, including capability name, requested major and returned reason.
A consumer that cannot function without it keeps its handlers inert and logs
the incompatibility at init. Static package hooks/callbacks are bound before
init; their bodies must default to doing nothing until discovery succeeds.
Do not use this fallback for a static replacement that cannot preserve stock
behavior when unavailable: that package needs a load-time compatibility gate
before installation of the replacement. This is a consumer obligation,
not a new loader-enforced dependency or a promise to prevent all gameplay.
Existing loader failures (minimum version, missing import, patch conflict)
continue through the Mods error UI. Capability requirements in manifests and
new UI error plumbing are deferred until a mandatory consumer needs them.

## Concrete seam: read the pit-assistance setting

The useful first consumer is a mod displaying whether the port's pit assistance
is active. It needs a setting value, not pit progress, controller buffers or a
replacement pit handler. Proposed public export:

```c
int32_t lambo_get_pit_assist_setting_v1(void);
/* 0 = disabled, 1 = enabled, -1 = unavailable for this ROM. */
```

| Contract aspect | Proposed v1 rule |
| --- | --- |
| Ownership | Host configuration owns the atomic boolean. Guest pit source owns fuel, pressure, tyre progress and completion. Querying changes neither owner nor state. |
| Thread and phase | Synchronous on the guest game thread, from `recomp_on_init` onward, including menus and races. Never call from render, UI, audio or native worker threads. A wrong-thread/pre-init call is outside the contract, not a promised detectable error. |
| Timing | Sample the current atomic value once per call. A UI change can be visible at the next call; two calls are not a transaction or a frame-consistent snapshot. No allocation, I/O, callback dispatch or retained pointer. |
| Argument/result layout | No arguments. Signed 32-bit scalar result in MIPS o32 `v0`; host adapter sign-extends negative values into the runtime GPR. The mod sees the guest C declaration, not `recomp_context`. No structure packing, float ABI or byte-order conversion is required for this scalar. |
| Versioning | `_v1` fixes semantics and result values. A changed meaning, timing guarantee or layout requires a new name/major; retain v1 while supported packages use it. Discovery major 1 refers to this precise contract, not the application release or native-module ABI. |
| Failure | Unsupported regional adapter returns `-1`, even if the UI setting is enabled. Do not conflate unavailable with disabled. No guest memory access or mutation on failure. |
| Lifetime/reset | No private gameplay state. Developer RAM restore does not rewind the host setting; subsequent calls sample current configuration. Fresh boot reselects capabilities. |

The existing private `lambo_get_automatic_pit_stops()` remains unchanged. It
already returns only the atomic setting through `ctx->r2`, with no guest memory
access ([adapter](../src/lambo_automatic_pit_stops.cpp)). The future public
wrapper would use the same configuration getter after checking regional
availability; it would not rename or redirect private linker symbols.

Conceptual consumer code, not a buildable package in this change:

```c
/* Called by the mod's existing recomp_on_init callback. */
int32_t support = lambo_query_capability_v1(LAMBO_CAP_PIT_ASSIST_SETTING, 1);
pit_setting_available = support == 1; /* Defaults to false before init. */
if (!pit_setting_available) {
    if (support == 0) lambo_log_v1("pit-assist setting v1: unknown capability");
    else if (support == -1) lambo_log_v1("pit-assist setting v1: unsupported major");
    else lambo_log_v1("pit-assist setting v1: unavailable for selected ROM");
}

/* Called on the game thread when updating the indicator. */
if (pit_setting_available) {
    int32_t setting = lambo_get_pit_assist_setting_v1();
    if (setting >= 0) update_indicator(setting == 1);
}
```

New data seams should follow the same discipline: scalar values first; larger
results use caller-owned, word-aligned guest output buffers with explicit u32
size/version fields, fixed-width fields, units and lifetimes. Validate the
whole range and layout before writing, use guest byte-order helpers, retain no
pointer and leave output unchanged on failure. Each seam must define its own
layout and errors before implementation; this note introduces no generic
record-read/write operation. Existing v1 pointer contracts are unchanged.

Use existing events for notifications. Any new event needs an exact semantic
phase, argument layout, delivery/order rules and restore behavior. Follow the
existing game-thread dispatch with an isolated callback register context;
never expose interrupted registers as event arguments. For the setting query
no new event, mutation callback or callback arbitration is needed.

## ROM variants and responsibility for #338

[#338](https://github.com/alondero/automobililamborghini-recomp/issues/338)
owns ROM selection, regional symbol/address/layout resolution and behavioral
parity. This design consumes that selection; it adds no parallel address map
and makes no new JP support claim. USA is the supported input today; JP remains
experimental and is not linked as a playable image in this checkout.

For a future dual-ROM host, expose pit capability v1 only when that ROM has
the verified guest pit implementation connected to the setting. The boolean's
existence alone is insufficient. Until then a JP adapter returns unavailable
without reading USA tables. The same named scalar result must mean the same
thing on both cartridges; different layouts stay inside the regional adapter.
If equivalent semantics cannot be demonstrated, keep it unavailable or design
a separate capability, rather than silently approximating USA behavior.

The current private pit patch uses USA bindings and relocated race records;
see [automatic pit-stops](automatic-pit-stops.md). Stock symbol hooks, raw body
addresses and the six direct data symbols stay cartridge/layout-specific.
Only validated named operations gain cross-ROM guarantees. Package game ID,
host version and native-module ABI do not establish ROM compatibility.

## Compatibility test outline and implementation gate

These are required follow-up tests, not results from this documentation change:

1. Host adapter tests: off/on returns exactly 0/1, live toggle is observed on
   the next call, no guest bytes change, unavailable returns signed -1 and
   never touches USA memory. Discovery covers supported, unknown ID, unknown
   major, wrong ROM and reboot selection; enabled/off does not alter support.
2. MIPS NRM fixture: import both proposed exports from `*`, query in init and
   read on a game-thread event. Assert sign extension, repeated reads and
   optional-feature fallback. A too-old host rejects the package before init
   through the minimum-version gate. A future host with a supported JP boot
   but no pit adapter must bind imports and report unavailable.
3. Baseline regressions: retain logging, stock hook entry/return, protected
   replacement rejection and compiled guest helper regeneration via
   [mod support checks](../tools/check_mod_support.py) and
   [guest-source checks](../tools/check_guest_source_mods.py). Preserve the
   current [game interface tests](../tests/test_mod_game.cpp).
4. Guest semantics: retain the independent ROM parity and multi-update pit
   tests documented in [automatic pit-stops](automatic-pit-stops.md#verification),
   including live disable, both slots and invalid slots. A public wrapper test
   alone is not proof of service behavior or visual correctness.
5. Regional acceptance: #338 supplies per-ROM identity and matching adapter
   fixtures, verifies no cross-region accesses, and runs equivalent gameplay
   scenarios before advertising the capability. A mocked unavailable JP case
   does not establish working Japanese gameplay.

The next implementation should contain only the discovery export, pit query,
guest declarations/import stubs, a small consumer fixture and these checks.
The existing private query already demonstrates the ownership seam; adding a
second executable prototype before agreeing the public contract would create
an accidental compatibility promise. No runtime change is needed to finish
this design issue. Asset replacement under
[#20](https://github.com/alondero/automobililamborghini-recomp/issues/20), a general
gameplay mutation interface and a loader rewrite remain outside this design.

## Rejected approaches

- **Raw addresses as the interface:** a valid pointer says nothing about owner,
  width, phase, units or region. Arbitrary writes bypass invariants and cannot
  survive relocation. Existing escape hatches retain their narrow limits.
- **Generated registers/stack offsets:** register allocation, recompilation and
  source replacement can change them without changing gameplay. `recomp_context`
  is transport machinery for the pinned runtime, not a gameplay data schema.
- **Rename every stock function and call it stable:** a label does not define
  timing, lifetime or cross-ROM semantics. Hooks remain useful at their current
  compatibility level; publish supported seams individually.
- **One ever-increasing API version:** it cannot distinguish a missing regional
  implementation from a supported feature that is disabled. Keep existing v1
  and add per-capability discovery rather than changing its meaning.
- **General memory accessor, event bus or mod-loader replacement:** none is
  needed for a read-only setting consumer. They increase ownership and ordering
  obligations without a measured use case.
- **Expose the private synthetic call addresses:** those enable patch linking
  and runtime hook regeneration. Freezing them as author-facing imports would
  couple mods to build plumbing instead of the named guest declaration.

// Persistent user-facing graphics configuration (#1/#2 enhancement wave).
//
// Same model as Zelda64Recomp: ultramodern owns the GraphicsConfig struct and the
// renderer reacts to set_graphics_config(); the PORT owns persistence. We persist a
// graphics.json in a per-user config directory using the NLOHMANN_JSON_SERIALIZE_ENUM
// mappings ultramodern ships in ultramodern/config.hpp, so the on-disk vocabulary
// ("Expand", "Display", "MSAA4X", ...) matches the peer ports.
#ifndef LAMBO_CONFIG_H
#define LAMBO_CONFIG_H

#include <filesystem>
#include <string>

#include "ultramodern/config.hpp"

namespace lambo {
namespace config {

// Per-user persistent config directory (created on demand):
//   Windows: %LOCALAPPDATA%\LamborghiniRecomp
//   else:    $XDG_CONFIG_HOME/LamborghiniRecomp (or ~/.config/LamborghiniRecomp)
std::filesystem::path app_config_dir();

// Absolute path of the live graphics.json (honours the LAMBO_GRAPHICS_CONFIG
// override), for user-facing messages that tell people what file to edit.
std::filesystem::path graphics_config_path();

// The enhancement-oriented defaults this port ships with (widescreen Expand,
// display-rate interpolated rendering, window-scaled internal resolution).
ultramodern::renderer::GraphicsConfig default_graphics_config();

// Load graphics.json (falling back to defaults for missing/invalid keys), apply it
// via ultramodern::renderer::set_graphics_config, and write the merged file back so
// users always have a complete, editable file on disk. Returns the applied config.
ultramodern::renderer::GraphicsConfig load_and_apply_graphics();

// Persist the given config (full overwrite of graphics.json).
void save_graphics(const ultramodern::renderer::GraphicsConfig& cfg);

// Persist a runtime window-mode change (F11) by re-reading the on-disk file and
// updating only wm_option, so concurrent hand-edits to other keys survive. A file
// that fails to parse is left untouched.
void update_saved_window_mode(ultramodern::renderer::WindowMode wm);

// Requested window size for windowed mode (from graphics.json; defaults 1600x900,
// chosen 16:9 so AspectRatio::Expand actually widens on first launch).
struct WindowSize { int width; int height; };
WindowSize window_size();

// RT64 texture-replacement paths (issue #9). Both are extra graphics.json string keys
// (empty = feature off), overridable by env var for headless capture/testing:
//   texture_pack  / LAMBO_TEXTURE_PACK  -- directory or .rtz to auto-load at startup.
//   texture_dump  / LAMBO_TEXTURE_DUMP  -- directory RT64 writes every used texture to
//                                          (raw TMEM/RDRAM dumps; decode with
//                                          tools/decode_dump.py). Enables headless dump
//                                          without the F1 developer overlay.
std::string texture_pack_path();
std::string texture_dump_dir();

// Widen the dense 3P/4P split-screen fog to the 1P window/colour (issue #83).
// graphics.json key "widescreen_fog_match" (default true), overridable by
// LAMBO_FOG_MATCH_1P=1/0. The rewrite still self-gates on player count >= 3.
bool widescreen_fog_match();

// Draw the sky panorama in 3P/4P split screen like 1P/2P (issue #84).
// graphics.json key "widescreen_sky_match" (default true), overridable by
// LAMBO_SKY_MATCH_1P=1/0. Only flips a branch that 1P/2P already take.
bool widescreen_sky_match();

// Remove the ROM's per-mode LOD reductions (issues #87/#91): emit each track
// segment's scenery layer in 2P-4P races like 1P does. graphics.json key
// "no_lod" (default true), overridable by LAMBO_NO_LOD=1/0.
bool no_lod();

// Per-circuit refinement of no_lod: the full-track walk (PVS synth) is what fixes
// the cross-track distance pop-in (PR #122), but on the pro tracks it surfaces
// back-of-buildings / cross-track geometry the track authors relied on the
// authored PVS rows to hide. Ship-safe default: pro tracks (3-5) ship with the
// PVS synth OFF (N64-style authoring); basic tracks (0-2) ship with it ON (modern
// pop-in fix). The radius cull and the 2P+ scenery sub-DL stay gated on the
// global no_lod() so distance culling and 2P scenery work on all 6 tracks.
// graphics.json key "no_lod_circuit" (array of 6 bools, default [true, true, true,
// false, false, false]).
bool no_lod_circuit(int circuit);

// Fog density multiplier applied to every fog moveword in the frame DL: 1.0 =
// authored fog, 0.0 = no fog, values in between thin the fog uniformly without
// moving its onset distance. graphics.json keys "fog_scale" (global, default 1.0)
// and "fog_scale_circuit" (array of 6 per-circuit multipliers, default all 1.0,
// multiplied with the global -- lets a hazy city track be cleared individually).
// LAMBO_FOG_SCALE=<float> overrides the whole computation for capture/testing.
double fog_scale(int circuit);

// Draw-distance multiplier applied (while no_lod is on) to the authored per-circuit
// segment-cull radii the scene builder tests visibility-list entries against.
// 1.0 = the N64 radii, larger = see further, 0 (or negative) = unlimited: the whole
// track subject only to the per-frame view-cone tests. Unlimited shows geometry the
// track authors never meant to be visible (distant segments floating with nothing
// in between), hence the finite default. graphics.json keys "draw_distance"
// (global, default 1.5) and "draw_distance_circuit" (array of 6 per-circuit
// multipliers, default all 1.0, multiplied with the global).
// LAMBO_DRAW_DISTANCE=<float> overrides the whole computation for capture/testing.
double draw_distance(int circuit);

} // namespace config
} // namespace lambo

#endif

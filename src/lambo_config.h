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

#include "ultramodern/config.hpp"

namespace lambo {
namespace config {

// Per-user persistent config directory (created on demand):
//   Windows: %LOCALAPPDATA%\LamborghiniRecomp
//   else:    $XDG_CONFIG_HOME/LamborghiniRecomp (or ~/.config/LamborghiniRecomp)
std::filesystem::path app_config_dir();

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

} // namespace config
} // namespace lambo

#endif

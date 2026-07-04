// Persistent graphics configuration (see lambo_config.h).
//
// Schema and behaviour mirror Zelda64Recomp's src/game/config.cpp graphics.json
// (same key names, same per-key fall-back-to-default on missing/corrupt values,
// same "portable.txt beside the exe -> use the working directory" escape hatch),
// minus the RmlUi menu: this port's UI is the JSON file itself plus hotkeys.
#include "lambo_config.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "json/json.hpp"

namespace {

constexpr const char* kAppFolderName = "LamborghiniRecomp";
constexpr const char* kGraphicsFile = "graphics.json";

// Window-size keys live in the same graphics.json (extra keys alongside the
// GraphicsConfig fields); 16:9 default so AspectRatio::Expand widens on first run.
constexpr int kDefaultWindowWidth = 1600;
constexpr int kDefaultWindowHeight = 900;

lambo::config::WindowSize g_window_size{kDefaultWindowWidth, kDefaultWindowHeight};

// Read a key into `out`, keeping the existing (default) value when the key is
// missing or has the wrong type. NLOHMANN_JSON_SERIALIZE_ENUM maps an unknown
// string to the FIRST enumerator of the mapping, which for every GraphicsConfig
// enum is a valid conservative option, so no extra validation is needed.
template <typename T>
void from_or_default(const nlohmann::json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it == j.end()) return;
    try {
        out = it->get<T>();
    } catch (const nlohmann::json::exception&) {
        // keep default
    }
}

nlohmann::json to_json(const ultramodern::renderer::GraphicsConfig& c) {
    return nlohmann::json{
        {"res_option", c.res_option},
        {"wm_option", c.wm_option},
        {"hr_option", c.hr_option},
        {"api_option", c.api_option},
        {"ar_option", c.ar_option},
        {"msaa_option", c.msaa_option},
        {"rr_option", c.rr_option},
        {"hpfb_option", c.hpfb_option},
        {"rr_manual_value", c.rr_manual_value},
        {"ds_option", c.ds_option},
        {"developer_mode", c.developer_mode},
        {"window_width", g_window_size.width},
        {"window_height", g_window_size.height},
    };
}

void from_json(const nlohmann::json& j, ultramodern::renderer::GraphicsConfig& c) {
    from_or_default(j, "res_option", c.res_option);
    from_or_default(j, "wm_option", c.wm_option);
    from_or_default(j, "hr_option", c.hr_option);
    from_or_default(j, "api_option", c.api_option);
    from_or_default(j, "ar_option", c.ar_option);
    from_or_default(j, "msaa_option", c.msaa_option);
    from_or_default(j, "rr_option", c.rr_option);
    from_or_default(j, "hpfb_option", c.hpfb_option);
    from_or_default(j, "rr_manual_value", c.rr_manual_value);
    from_or_default(j, "ds_option", c.ds_option);
    from_or_default(j, "developer_mode", c.developer_mode);
    from_or_default(j, "window_width", g_window_size.width);
    from_or_default(j, "window_height", g_window_size.height);
    if (g_window_size.width < 320 || g_window_size.height < 240) {
        g_window_size = {kDefaultWindowWidth, kDefaultWindowHeight};
    }
}

std::filesystem::path graphics_json_path() {
    // Test/harness override: point at (or isolate to) an explicit file.
    if (const char* p = std::getenv("LAMBO_GRAPHICS_CONFIG")) {
        return std::filesystem::path{p};
    }
    return lambo::config::app_config_dir() / kGraphicsFile;
}

} // anonymous namespace

namespace lambo {
namespace config {

std::filesystem::path app_config_dir() {
    // Portable mode: a portable.txt in the working directory keeps everything local.
    std::error_code ec;
    if (std::filesystem::exists("portable.txt", ec)) {
        return std::filesystem::current_path();
    }
#if defined(_WIN32)
    if (const char* localappdata = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path{localappdata} / kAppFolderName;
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        return std::filesystem::path{xdg} / kAppFolderName;
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path{home} / ".config" / kAppFolderName;
    }
#endif
    return std::filesystem::current_path();
}

ultramodern::renderer::GraphicsConfig default_graphics_config() {
    // Zelda64Recomp's shipped defaults (config.cpp:26-35), which are the
    // enhancement goals of issue wave 1: window-scaled internal resolution,
    // widescreen Expand with the HUD clamped to 16:9, and RT64 frame
    // interpolation up to the display refresh rate (game logic stays at its
    // native 30Hz tick either way -- ultramodern's VI clock is fixed).
    ultramodern::renderer::GraphicsConfig cfg{};
    cfg.res_option = ultramodern::renderer::Resolution::Auto;
    cfg.wm_option = ultramodern::renderer::WindowMode::Windowed;
    cfg.hr_option = ultramodern::renderer::HUDRatioMode::Clamp16x9;
    cfg.api_option = ultramodern::renderer::GraphicsApi::Auto;
    cfg.ar_option = ultramodern::renderer::AspectRatio::Expand;
    cfg.msaa_option = ultramodern::renderer::Antialiasing::MSAA2X;
    cfg.rr_option = ultramodern::renderer::RefreshRate::Display;
    cfg.hpfb_option = ultramodern::renderer::HighPrecisionFramebuffer::Auto;
    cfg.rr_manual_value = 60;
    cfg.ds_option = 1;
    cfg.developer_mode = false;
    return cfg;
}

ultramodern::renderer::GraphicsConfig load_and_apply_graphics() {
    ultramodern::renderer::GraphicsConfig cfg = default_graphics_config();
    const std::filesystem::path path = graphics_json_path();

    std::ifstream in{path};
    if (in.good()) {
        try {
            nlohmann::json j;
            in >> j;
            from_json(j, cfg);
        } catch (const nlohmann::json::exception& e) {
            std::fprintf(stderr, "[config] %s unparseable (%s); using defaults\n",
                         path.string().c_str(), e.what());
        }
    }
    in.close();

    ultramodern::renderer::set_graphics_config(cfg);
    // Write the merged config back so the on-disk file is always complete and
    // editable (new keys appear with their defaults after an upgrade).
    save_graphics(cfg);
    std::fprintf(stderr, "[config] graphics config: %s\n", path.string().c_str());
    return cfg;
}

void save_graphics(const ultramodern::renderer::GraphicsConfig& cfg) {
    const std::filesystem::path path = graphics_json_path();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out{path};
    if (!out.good()) {
        std::fprintf(stderr, "[config] cannot write %s\n", path.string().c_str());
        return;
    }
    out << to_json(cfg).dump(4) << "\n";
}

WindowSize window_size() {
    return g_window_size;
}

} // namespace config
} // namespace lambo

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "json/json.hpp"
#include "lambo_config.h"

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream in(path);
    nlohmann::json result;
    in >> result;
    return result;
}

} // namespace

// lambo_config.cpp normally calls into the runtime. This focused test only needs
// to verify the port-owned snapshot and persistent representation.
namespace ultramodern::renderer {
int graphics_config_apply_count = 0;
GraphicsConfig last_live_graphics{};
void set_graphics_config(const GraphicsConfig& cfg) {
    ++graphics_config_apply_count;
    last_live_graphics = cfg;
}
}

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() /
                     ("lambo-config-test-" + std::to_string(unique));
    const auto path = dir / "graphics.json";
#if defined(_WIN32)
    _putenv_s("LAMBO_GRAPHICS_CONFIG", path.string().c_str());
#else
    setenv("LAMBO_GRAPHICS_CONFIG", path.string().c_str(), 1);
#endif

    auto cfg = lambo::config::load_and_apply_graphics();
    expect(std::filesystem::exists(path), "first load creates graphics.json");
    expect(cfg.ar_option == ultramodern::renderer::AspectRatio::Expand,
           "enhancement-oriented aspect default is preserved");

    // Runtime updates must merge over hand edits rather than reconstructing the
    // whole document from the fixed C++ schema.
    auto hand_edited = read_json(path);
    hand_edited["custom_renderer_tweak"] = "keep me";
    hand_edited["texture_pack"] = "manual-texture-pack";
    {
        std::ofstream output(path);
        output << hand_edited.dump(4) << '\n';
    }

    cfg.msaa_option = ultramodern::renderer::Antialiasing::MSAA4X;
    cfg.rr_option = ultramodern::renderer::RefreshRate::Manual;
    cfg.rr_manual_value = 120;
    lambo::config::apply_graphics(cfg);
    lambo::config::flush_pending_graphics_updates();
    expect(lambo::config::current_graphics().msaa_option ==
               ultramodern::renderer::Antialiasing::MSAA4X,
           "graphics menu changes update the live snapshot");
    expect(read_json(path).at("custom_renderer_tweak") == "keep me" &&
               read_json(path).at("texture_pack") == "manual-texture-pack",
           "graphics menu changes preserve unrelated hand edits");

    const int live_apply_count_before_api_change = ultramodern::renderer::graphics_config_apply_count;
    cfg.api_option = ultramodern::renderer::GraphicsApi::Vulkan;
    lambo::config::apply_graphics(cfg, false);
    lambo::config::flush_pending_graphics_updates();
    expect(ultramodern::renderer::graphics_config_apply_count == live_apply_count_before_api_change,
           "restart-only graphics API changes do not reconfigure the live renderer");
    expect(read_json(path).at("api_option") == "Vulkan",
           "restart-only graphics API changes persist");

    cfg.hpfb_option = ultramodern::renderer::HighPrecisionFramebuffer::On;
    lambo::config::apply_graphics(cfg);
    expect(ultramodern::renderer::last_live_graphics.api_option ==
               ultramodern::renderer::GraphicsApi::Auto,
           "later live updates retain the renderer's startup API");

    lambo::config::set_widescreen_fog_match(false);
    lambo::config::set_widescreen_sky_match(false);
    lambo::config::set_no_lod(false);
    lambo::config::set_no_lod_circuit(4, true);
    lambo::config::set_global_fog_scale(1.5);
    lambo::config::set_global_draw_distance(2.0);
    lambo::config::set_camera_distance_scale(0.65);
    lambo::config::set_camera_height_scale(0.5);
    lambo::config::set_camera_fov_add(10.0);
    expect(!lambo::config::widescreen_fog_match(), "fog toggle updates live");
    expect(!lambo::config::widescreen_sky_match(), "sky toggle updates live");
    expect(!lambo::config::no_lod(), "LOD toggle updates live");
    expect(lambo::config::no_lod_circuit(4), "per-circuit visibility updates live");
    expect(lambo::config::global_fog_scale() == 1.5, "fog scale updates live");
    expect(lambo::config::global_draw_distance() == 2.0, "draw distance updates live");
    expect(lambo::config::camera_distance_scale() == 0.65, "camera distance scale updates live");
    expect(lambo::config::camera_height_scale() == 0.5, "camera height scale updates live");
    expect(lambo::config::camera_fov_add() == 10.0, "camera FOV delta updates live");

    lambo::config::update_saved_window_mode(ultramodern::renderer::WindowMode::Fullscreen);
    lambo::config::flush_pending_graphics_updates();
    const auto json = read_json(path);
    expect(json.at("wm_option") == "Fullscreen", "fullscreen selection persists");
    expect(json.at("msaa_option") == "MSAA4X", "graphics selection persists");
    expect(json.at("rr_option") == "Manual" && json.at("rr_manual_value") == 120,
           "manual frame rate persists");
    expect(json.at("widescreen_fog_match") == false, "fog toggle persists");
    expect(json.at("widescreen_sky_match") == false, "sky toggle persists");
    expect(json.at("no_lod") == false, "LOD toggle persists");
    expect(json.at("no_lod_circuit").at(4) == true, "per-circuit visibility persists");
    expect(json.at("fog_scale") == 1.5, "fog scale persists");
    expect(json.at("draw_distance") == 2.0, "draw distance persists");
    expect(json.at("camera_distance_scale") == 0.65, "camera distance scale persists");
    expect(json.at("camera_height_scale") == 0.5, "camera height scale persists");
    expect(json.at("camera_fov_add") == 10.0, "camera FOV delta persists");

    // Out-of-range values are clamped, not rejected: the ROM-authored defaults
    // (900 / 200 / +0) must round-trip exactly so stock presentation is stable.
    lambo::config::set_camera_distance_scale(99.0);
    lambo::config::set_camera_height_scale(-5.0);
    lambo::config::set_camera_fov_add(500.0);
    expect(lambo::config::camera_distance_scale() == 3.0, "camera distance scale clamps high");
    expect(lambo::config::camera_height_scale() == 0.2, "camera height scale clamps low");
    expect(lambo::config::camera_fov_add() == 60.0, "camera FOV delta clamps high");

    lambo::config::set_show_launcher(true);
    lambo::config::flush_pending_graphics_updates();
    expect(lambo::config::show_launcher() == true, "show_launcher toggle updates snapshot");
    expect(read_json(path).at("show_launcher") == true, "show_launcher persists to json");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return failures == 0 ? 0 : 1;
}



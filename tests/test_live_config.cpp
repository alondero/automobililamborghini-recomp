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
void set_graphics_config(const GraphicsConfig&) {}
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

    cfg.msaa_option = ultramodern::renderer::Antialiasing::MSAA4X;
    cfg.rr_option = ultramodern::renderer::RefreshRate::Manual;
    cfg.rr_manual_value = 120;
    lambo::config::apply_graphics(cfg);
    expect(lambo::config::current_graphics().msaa_option ==
               ultramodern::renderer::Antialiasing::MSAA4X,
           "graphics menu changes update the live snapshot");

    lambo::config::set_widescreen_fog_match(false);
    lambo::config::set_widescreen_sky_match(false);
    lambo::config::set_no_lod(false);
    lambo::config::set_no_lod_circuit(4, true);
    lambo::config::set_global_fog_scale(1.5);
    lambo::config::set_global_draw_distance(2.0);
    expect(!lambo::config::widescreen_fog_match(), "fog toggle updates live");
    expect(!lambo::config::widescreen_sky_match(), "sky toggle updates live");
    expect(!lambo::config::no_lod(), "LOD toggle updates live");
    expect(lambo::config::no_lod_circuit(4), "per-circuit visibility updates live");
    expect(lambo::config::global_fog_scale() == 1.5, "fog scale updates live");
    expect(lambo::config::global_draw_distance() == 2.0, "draw distance updates live");

    lambo::config::update_saved_window_mode(ultramodern::renderer::WindowMode::Fullscreen);
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

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return failures == 0 ? 0 : 1;
}

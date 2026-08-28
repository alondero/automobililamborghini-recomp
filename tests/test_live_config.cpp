#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "json/json.hpp"
#include "lambo_config.h"
#include "lambo_player_name.h"
#include "recomp.h"

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
    const auto player_path = dir / "player.json";
#if defined(_WIN32)
    _putenv_s("LAMBO_GRAPHICS_CONFIG", path.string().c_str());
    _putenv_s("LAMBO_PLAYER_CONFIG", player_path.string().c_str());
#else
    setenv("LAMBO_GRAPHICS_CONFIG", path.string().c_str(), 1);
    setenv("LAMBO_PLAYER_CONFIG", player_path.string().c_str(), 1);
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
    expect(lambo::config::show_launcher() == true, "show_launcher toggle updates snapshot");
    expect(read_json(path).at("show_launcher") == true, "show_launcher persists to json");

    lambo::config::set_texture_upscaler("xbrz");
    expect(lambo::config::texture_upscaler() == "xbrz", "xBRZ upscaler selection updates live");
    expect(read_json(path).at("texture_upscaler") == "xbrz", "xBRZ upscaler persists");
    lambo::config::set_texture_upscaler("scalefx");
    expect(lambo::config::texture_upscaler() == "scalefx", "ScaleFX upscaler selection updates live");
    expect(read_json(path).at("texture_upscaler") == "scalefx", "ScaleFX upscaler persists");
    lambo::config::set_texture_upscaler("bogus");
    expect(lambo::config::texture_upscaler() == "off", "unknown upscaler mode falls back to off");

    std::vector<uint8_t> memory(0x800000);
    uint8_t* rdram = memory.data();
    const gpr driver_index = (gpr)(int32_t)0x800CE6A6u;
    const gpr player_one_name = (gpr)(int32_t)0x800A4826u;
    MEM_H(0, driver_index) = 1;
    const std::string edited = "CHAMP";
    for (int i = 0; i < 13; ++i) {
        MEM_B(i, player_one_name) = i < (int)edited.size() ? edited[(size_t)i] : 0;
    }
    lambo_player_name_save(rdram);
    expect(read_json(player_path).at("name") == edited, "confirmed ROM name persists");

    for (int i = 0; i < 13; ++i) MEM_B(i, player_one_name) = 0;
    lambo_player_name_seed(rdram);
    std::string seeded;
    for (int i = 0; i < 13 && MEM_BU(i, player_one_name) != 0; ++i) {
        seeded.push_back((char)MEM_BU(i, player_one_name));
    }
    expect(seeded == edited, "next run seeds the persisted name into the ROM buffer");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return failures == 0 ? 0 : 1;
}



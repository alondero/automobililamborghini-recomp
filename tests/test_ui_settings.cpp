#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "json/json.hpp"
#include "lambo_config.h"
#include "ui/lambo_ui_settings.h"

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void set_environment(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream input(path);
    nlohmann::json result;
    input >> result;
    return result;
}

} // namespace

namespace ultramodern::renderer {
void set_graphics_config(const GraphicsConfig&) {}
}

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("lambo-ui-settings-test-" + std::to_string(unique));
    const auto config_path = directory / "graphics.json";
    set_environment("LAMBO_GRAPHICS_CONFIG", config_path.string().c_str());
    set_environment("LAMBO_FOG_MATCH_1P", nullptr);
    set_environment("LAMBO_SKY_MATCH_1P", nullptr);
    set_environment("LAMBO_NO_LOD", nullptr);

    lambo::config::load_and_apply_graphics();

    constexpr std::array binding_names{
        "res:next", "ss:next", "aspect:next", "hud:next", "rate:next",
        "msaa:next", "hpfb:next", "api:next",
        "fog:toggle", "sky:toggle", "lod:toggle", "upscale:next",
        "circuit:1", "circuit:2", "circuit:3", "circuit:4", "circuit:5", "circuit:6",
        "distance:next", "fogdensity:next",
    };
    for (const char* name : binding_names) {
        expect(lambo::ui::setting_action_from_name(name).has_value(),
               "every documented setting binding parses");
    }
    expect(!lambo::ui::setting_action_from_name("unknown:setting").has_value(),
           "unknown setting bindings are rejected");

    const auto apply = [](const char* name) {
        const auto action = lambo::ui::setting_action_from_name(name);
        return action.has_value() && lambo::ui::apply_setting_action(*action);
    };

    auto custom_refresh = lambo::config::current_graphics();
    custom_refresh.rr_option = ultramodern::renderer::RefreshRate::Manual;
    custom_refresh.rr_manual_value = 75;
    lambo::config::apply_graphics(custom_refresh);
    expect(apply("rate:next") && lambo::config::current_graphics().rr_manual_value == 90,
           "refresh cycle advances a custom manual value to the next preset");
    lambo::config::apply_graphics(lambo::config::default_graphics_config());

    expect(apply("res:next") && apply("ss:next") && apply("aspect:next") &&
           apply("hud:next") && apply("rate:next") && apply("msaa:next") &&
           apply("hpfb:next") && apply("api:next"),
           "graphics cycle bindings apply through the typed settings seam");
    const auto graphics = lambo::config::current_graphics();
    using namespace ultramodern::renderer;
    expect(graphics.res_option == Resolution::Original, "resolution cycle updates config");
    expect(graphics.ds_option == 2, "supersampling cycle updates config");
    expect(graphics.ar_option == AspectRatio::Original, "aspect cycle updates config");
    expect(graphics.hr_option == HUDRatioMode::Full, "HUD cycle updates config");
    expect(graphics.rr_option == RefreshRate::Manual && graphics.rr_manual_value == 30,
           "refresh cycle updates config");
    expect(graphics.msaa_option == Antialiasing::MSAA4X, "MSAA cycle updates config");
    expect(graphics.hpfb_option == HighPrecisionFramebuffer::On,
           "framebuffer precision cycle updates config");
#if defined(_WIN32)
    expect(graphics.api_option == GraphicsApi::D3D12, "graphics API cycle updates config");
#elif defined(__APPLE__)
    expect(graphics.api_option == GraphicsApi::Metal, "graphics API cycle updates config");
#else
    expect(graphics.api_option == GraphicsApi::Vulkan, "graphics API cycle updates config");
#endif

    expect(apply("fog:toggle") && apply("sky:toggle") && apply("lod:toggle") &&
           apply("circuit:6") && apply("distance:next") && apply("fogdensity:next") &&
           apply("upscale:next"),
           "enhancement bindings apply through the typed settings seam");
    expect(!lambo::config::widescreen_fog_match(), "fog match toggles live");
    expect(!lambo::config::widescreen_sky_match(), "sky match toggles live");
    expect(!lambo::config::no_lod(), "LOD removal toggles live");
    expect(lambo::config::no_lod_circuit(5), "per-circuit visibility toggles live");
    expect(lambo::config::global_draw_distance() == 2.0, "draw-distance cycle applies live");
    expect(lambo::config::global_fog_scale() == 1.5, "fog-density cycle applies live");
    // The initial mode cycles from off to scalefx. A second cycle reaches xbrz.
    lambo::config::set_texture_upscaler("off");
    expect(apply("upscale:next"), "upscaler cycle 1: off -> scalefx");
    expect(lambo::config::texture_upscaler() == "scalefx", "upscaler state after cycle 1");
    expect(apply("upscale:next"), "upscaler cycle 2: scalefx -> xbrz");
    expect(lambo::config::texture_upscaler() == "xbrz", "upscaler state after cycle 2");
    expect(apply("upscale:next"), "upscaler cycle 3: xbrz -> off (wrap)");
    expect(lambo::config::texture_upscaler() == "off", "upscaler state after cycle 3 (wraps to off)");

    const auto snapshot = lambo::ui::settings_snapshot();
    expect(snapshot.resolution == "Original", "settings snapshot presents resolution");
    expect(snapshot.refresh_rate == "30 Hz", "settings snapshot presents refresh rate");
    expect(snapshot.msaa == "4x", "settings snapshot presents MSAA");
    expect(snapshot.circuit_visibility[5] == "Enabled",
           "settings snapshot presents every circuit");
    expect(snapshot.draw_distance == "2x", "settings snapshot presents draw distance");
    expect(snapshot.texture_upscaler == "Off", "settings snapshot presents texture upscaler mode (wrapped back to off)");
    expect(snapshot.fog_density == "150%", "settings snapshot presents fog density");

    using SnapshotStringMember = std::string lambo::ui::SettingsSnapshot::*;
    const auto expect_cycle_wraps = [&](const char* binding, int steps, const std::string& initial,
                                        SnapshotStringMember member,
                                        const char* message) {
        for (int step = 0; step < steps; ++step) expect(apply(binding), message);
        expect(lambo::ui::settings_snapshot().*member == initial, message);
    };
    expect_cycle_wraps("res:next", 3, snapshot.resolution,
                       &lambo::ui::SettingsSnapshot::resolution, "resolution cycle wraps");
    expect_cycle_wraps("ss:next", 4, snapshot.supersampling,
                       &lambo::ui::SettingsSnapshot::supersampling, "supersampling cycle wraps");
    expect_cycle_wraps("aspect:next", 2, snapshot.aspect_ratio,
                       &lambo::ui::SettingsSnapshot::aspect_ratio, "aspect cycle wraps");
    expect_cycle_wraps("hud:next", 3, snapshot.hud_layout,
                       &lambo::ui::SettingsSnapshot::hud_layout, "HUD cycle wraps");
    expect_cycle_wraps("rate:next", 9, snapshot.refresh_rate,
                       &lambo::ui::SettingsSnapshot::refresh_rate, "refresh cycle wraps");
    expect_cycle_wraps("msaa:next", 4, snapshot.msaa,
                       &lambo::ui::SettingsSnapshot::msaa, "MSAA cycle wraps");
    expect_cycle_wraps("hpfb:next", 3, snapshot.framebuffer_precision,
                       &lambo::ui::SettingsSnapshot::framebuffer_precision,
                       "framebuffer precision cycle wraps");
#if defined(_WIN32)
    constexpr int api_cycle_size = 3;
#else
    constexpr int api_cycle_size = 2;
#endif
    expect_cycle_wraps("api:next", api_cycle_size, snapshot.graphics_api,
                       &lambo::ui::SettingsSnapshot::graphics_api, "graphics API cycle wraps");
    expect_cycle_wraps("distance:next", 5, snapshot.draw_distance,
                       &lambo::ui::SettingsSnapshot::draw_distance, "draw-distance cycle wraps");
    expect_cycle_wraps("fogdensity:next", 6, snapshot.fog_density,
                       &lambo::ui::SettingsSnapshot::fog_density, "fog-density cycle wraps");

    const auto persisted = read_json(config_path);
#if defined(_WIN32)
    expect(persisted.at("api_option") == "D3D12", "graphics API persists");
#elif defined(__APPLE__)
    expect(persisted.at("api_option") == "Metal", "graphics API persists");
#else
    expect(persisted.at("api_option") == "Vulkan", "graphics API persists");
#endif
    expect(persisted.at("rr_manual_value") == 30, "manual refresh rate persists");
    expect(persisted.at("no_lod_circuit").at(5) == true,
           "per-circuit visibility persists");
    expect(persisted.at("draw_distance") == 2.0, "draw distance persists");
    expect(persisted.at("fog_scale") == 1.5, "fog density persists");

    lambo::config::load_and_apply_graphics();
#if defined(_WIN32)
    expect(lambo::config::current_graphics().api_option == GraphicsApi::D3D12,
#elif defined(__APPLE__)
    expect(lambo::config::current_graphics().api_option == GraphicsApi::Metal,
#else
    expect(lambo::config::current_graphics().api_option == GraphicsApi::Vulkan,
#endif
           "settings survive reload through the config source of truth");

    std::error_code error;
    std::filesystem::remove_all(directory, error);
    return failures == 0 ? 0 : 1;
}

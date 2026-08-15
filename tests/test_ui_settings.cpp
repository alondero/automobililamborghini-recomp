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
        "res:auto", "res:original", "res:2x",
        "ss:1", "ss:2", "ss:3", "ss:4",
        "aspect:original", "aspect:expand",
        "hud:original", "hud:16x9", "hud:full",
        "rate:original", "rate:display", "rate:30", "rate:60", "rate:90",
        "rate:120", "rate:144", "rate:165", "rate:240",
        "msaa:off", "msaa:2", "msaa:4", "msaa:8",
        "hpfb:auto", "hpfb:on", "hpfb:off",
        "api:auto", "api:d3d12", "api:vulkan",
        "fog:toggle", "sky:toggle", "lod:toggle",
        "circuit:1", "circuit:2", "circuit:3", "circuit:4", "circuit:5", "circuit:6",
        "distance:1", "distance:1.5", "distance:2", "distance:3", "distance:unlimited",
        "fogdensity:off", "fogdensity:0.5", "fogdensity:0.75", "fogdensity:1",
        "fogdensity:1.5", "fogdensity:2",
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

    expect(apply("res:2x") && apply("ss:4") && apply("aspect:original") &&
           apply("hud:full") && apply("rate:144") && apply("msaa:8") &&
           apply("hpfb:on") && apply("api:vulkan"),
           "graphics bindings apply through the typed settings seam");
    const auto graphics = lambo::config::current_graphics();
    using namespace ultramodern::renderer;
    expect(graphics.res_option == Resolution::Original2x, "resolution binding updates config");
    expect(graphics.ds_option == 4, "supersampling binding updates config");
    expect(graphics.ar_option == AspectRatio::Original, "aspect binding updates config");
    expect(graphics.hr_option == HUDRatioMode::Full, "HUD binding updates config");
    expect(graphics.rr_option == RefreshRate::Manual && graphics.rr_manual_value == 144,
           "manual refresh binding updates config");
    expect(graphics.msaa_option == Antialiasing::MSAA8X, "MSAA binding updates config");
    expect(graphics.hpfb_option == HighPrecisionFramebuffer::On,
           "framebuffer precision binding updates config");
    expect(graphics.api_option == GraphicsApi::Vulkan, "graphics API binding updates config");

    expect(apply("fog:toggle") && apply("sky:toggle") && apply("lod:toggle") &&
           apply("circuit:6") && apply("distance:3") && apply("fogdensity:0.75"),
           "enhancement bindings apply through the typed settings seam");
    expect(!lambo::config::widescreen_fog_match(), "fog match toggles live");
    expect(!lambo::config::widescreen_sky_match(), "sky match toggles live");
    expect(!lambo::config::no_lod(), "LOD removal toggles live");
    expect(lambo::config::no_lod_circuit(5), "per-circuit visibility toggles live");
    expect(lambo::config::global_draw_distance() == 3.0, "3x draw distance applies live");
    expect(lambo::config::global_fog_scale() == 0.75, "75% fog density applies live");

    const std::string graphics_summary = lambo::ui::graphics_summary_html();
    expect(graphics_summary.find("Resolution: Original 2x") != std::string::npos,
           "graphics summary presents resolution");
    expect(graphics_summary.find("Refresh rate: 144 Hz") != std::string::npos,
           "graphics summary presents manual refresh rate");
    expect(graphics_summary.find("MSAA: 8x") != std::string::npos,
           "graphics summary presents MSAA");
    expect(graphics_summary.find("restart required") != std::string::npos,
           "graphics API is marked restart required");

    const std::string enhancements_summary = lambo::ui::enhancements_summary_html();
    expect(enhancements_summary.find("6:on") != std::string::npos,
           "enhancement summary presents every circuit");
    expect(enhancements_summary.find("Draw distance: 3x") != std::string::npos,
           "enhancement summary presents draw distance");
    expect(enhancements_summary.find("Fog density: 75%") != std::string::npos,
           "enhancement summary presents fog density");

    const auto persisted = read_json(config_path);
    expect(persisted.at("api_option") == "Vulkan", "graphics API persists");
    expect(persisted.at("rr_manual_value") == 144, "manual refresh rate persists");
    expect(persisted.at("no_lod_circuit").at(5) == true,
           "per-circuit visibility persists");
    expect(persisted.at("draw_distance") == 3.0, "draw distance persists");
    expect(persisted.at("fog_scale") == 0.75, "fog density persists");

    lambo::config::load_and_apply_graphics();
    expect(lambo::config::current_graphics().api_option == GraphicsApi::Vulkan,
           "settings survive reload through the config source of truth");

    std::error_code error;
    std::filesystem::remove_all(directory, error);
    return failures == 0 ? 0 : 1;
}

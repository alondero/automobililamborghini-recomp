#include "lambo_ui_settings.h"
#include "lambo_ui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <string>
#include <utility>

#include "lambo_config.h"

namespace {

using lambo::ui::SettingAction;

constexpr auto setting_bindings = std::to_array<std::pair<std::string_view, SettingAction>>({
    {"res:next", SettingAction::ResolutionNext},
    {"ss:next", SettingAction::SupersamplingNext},
    {"aspect:next", SettingAction::AspectNext},
    {"hud:next", SettingAction::HudNext},
    {"rate:next", SettingAction::RefreshNext},
    {"msaa:next", SettingAction::MsaaNext},
    {"hpfb:next", SettingAction::HpfbNext},
    {"api:next", SettingAction::ApiNext},
    {"fog:toggle", SettingAction::FogMatchToggle},
    {"sky:toggle", SettingAction::SkyMatchToggle},
    {"lod:toggle", SettingAction::NoLodToggle},
    {"upscale:next", SettingAction::UpscalerNext},
    {"circuit:1", SettingAction::Circuit1Toggle},
    {"circuit:2", SettingAction::Circuit2Toggle},
    {"circuit:3", SettingAction::Circuit3Toggle},
    {"circuit:4", SettingAction::Circuit4Toggle},
    {"circuit:5", SettingAction::Circuit5Toggle},
    {"circuit:6", SettingAction::Circuit6Toggle},
    {"distance:next", SettingAction::DrawDistanceNext},
    {"fogdensity:next", SettingAction::FogDensityNext},
});

template <typename T, size_t Size>
T next_value(T current, const std::array<T, Size>& values) {
    const auto position = std::find(values.begin(), values.end(), current);
    if (position == values.end() || std::next(position) == values.end()) return values.front();
    return *std::next(position);
}

template <size_t Size>
double next_number(double current, const std::array<double, Size>& values) {
    const auto position = std::find_if(values.begin(), values.end(), [current](double value) {
        return std::abs(value - current) < 0.001;
    });
    if (position == values.end() || std::next(position) == values.end()) return values.front();
    return *std::next(position);
}

const char* resolution_name(ultramodern::renderer::Resolution value) {
    using ultramodern::renderer::Resolution;
    switch (value) {
        case Resolution::Auto: return "Automatic";
        case Resolution::Original2x: return "Original 2x";
        case Resolution::Original: return "Original";
        default: return "Unknown";
    }
}

const char* aspect_name(ultramodern::renderer::AspectRatio value) {
    using ultramodern::renderer::AspectRatio;
    switch (value) {
        case AspectRatio::Original: return "Original 4:3";
        case AspectRatio::Expand: return "Expand";
        case AspectRatio::Manual: return "Manual";
        default: return "Unknown";
    }
}

const char* hud_name(ultramodern::renderer::HUDRatioMode value) {
    using ultramodern::renderer::HUDRatioMode;
    switch (value) {
        case HUDRatioMode::Original: return "Original 4:3";
        case HUDRatioMode::Clamp16x9: return "Clamp to 16:9";
        case HUDRatioMode::Full: return "Full width";
        default: return "Unknown";
    }
}

std::string refresh_name(const ultramodern::renderer::GraphicsConfig& cfg) {
    using ultramodern::renderer::RefreshRate;
    if (cfg.rr_option == RefreshRate::Original) return "Original";
    if (cfg.rr_option == RefreshRate::Display) return "Display";
    if (cfg.rr_option == RefreshRate::Manual) return std::to_string(cfg.rr_manual_value) + " Hz";
    return "Unknown";
}

const char* msaa_name(ultramodern::renderer::Antialiasing value) {
    using ultramodern::renderer::Antialiasing;
    switch (value) {
        case Antialiasing::None: return "Off";
        case Antialiasing::MSAA2X: return "2x";
        case Antialiasing::MSAA4X: return "4x";
        case Antialiasing::MSAA8X: return "8x";
        default: return "Unknown";
    }
}

const char* hpfb_name(ultramodern::renderer::HighPrecisionFramebuffer value) {
    using ultramodern::renderer::HighPrecisionFramebuffer;
    switch (value) {
        case HighPrecisionFramebuffer::Auto: return "Automatic";
        case HighPrecisionFramebuffer::On: return "On";
        case HighPrecisionFramebuffer::Off: return "Off";
        default: return "Unknown";
    }
}

const char* api_name(ultramodern::renderer::GraphicsApi value) {
    using ultramodern::renderer::GraphicsApi;
    switch (value) {
        case GraphicsApi::Auto: return "Automatic";
        case GraphicsApi::D3D12: return "Direct3D 12";
        case GraphicsApi::Vulkan: return "Vulkan";
        case GraphicsApi::Metal: return "Metal";
        default: return "Unknown";
    }
}

std::string multiplier_name(double value) {
    if (value <= 0.0) return "Unlimited";
    const int tenths = static_cast<int>(std::lround(value * 10.0));
    if (tenths % 10 == 0) return std::to_string(tenths / 10) + "x";
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "x";
}

std::string upscale_pretty_name(lambo::config::TextureUpscalerMode mode) {
    if (mode == lambo::config::TextureUpscalerMode::Xbrz) return "xBRZ (4x)";
    return "Off";
}

} // namespace

namespace lambo::ui {

// The cycle action now uses the typed `lambo::TextureUpscalerMode` enum and
// calls `lambo::config::set_texture_upscaler_mode`, which itself fires the
// registered callback (`RT64Context::on_texture_upscaler_changed`) to
// push the change into the running renderer and flush the upscaled texture
// cache so the toggle is visible immediately. No render-side hook is
// needed from this translation unit.

std::optional<SettingAction> setting_action_from_name(std::string_view name) {
    for (const auto& [binding, action] : setting_bindings) {
        if (binding == name) return action;
    }
    return std::nullopt;
}

bool apply_setting_action(SettingAction action) {
    using namespace ultramodern::renderer;
    auto cfg = lambo::config::current_graphics();

    switch (action) {
        case SettingAction::ResolutionNext:
            cfg.res_option = next_value(cfg.res_option,
                std::array{Resolution::Auto, Resolution::Original, Resolution::Original2x}); break;
        case SettingAction::SupersamplingNext:
            cfg.ds_option = cfg.ds_option < 1 || cfg.ds_option >= 4 ? 1 : cfg.ds_option + 1; break;
        case SettingAction::AspectNext:
            cfg.ar_option = next_value(cfg.ar_option, std::array{AspectRatio::Expand, AspectRatio::Original}); break;
        case SettingAction::HudNext:
            cfg.hr_option = next_value(cfg.hr_option,
                std::array{HUDRatioMode::Clamp16x9, HUDRatioMode::Full, HUDRatioMode::Original}); break;
        case SettingAction::RefreshNext:
            if (cfg.rr_option == RefreshRate::Original) cfg.rr_option = RefreshRate::Display;
            else if (cfg.rr_option == RefreshRate::Display) { cfg.rr_option = RefreshRate::Manual; cfg.rr_manual_value = 30; }
            else {
                constexpr std::array rates{30, 60, 90, 120, 144, 165, 240};
                const auto position = std::find(rates.begin(), rates.end(), cfg.rr_manual_value);
                if (position == rates.end()) {
                    const auto next_rate = std::upper_bound(rates.begin(), rates.end(), cfg.rr_manual_value);
                    if (next_rate == rates.end()) cfg.rr_option = RefreshRate::Original;
                    else cfg.rr_manual_value = *next_rate;
                }
                else if (std::next(position) == rates.end()) cfg.rr_option = RefreshRate::Original;
                else cfg.rr_manual_value = *std::next(position);
            }
            break;
        case SettingAction::MsaaNext:
            cfg.msaa_option = next_value(cfg.msaa_option, std::array{
                Antialiasing::None, Antialiasing::MSAA2X, Antialiasing::MSAA4X, Antialiasing::MSAA8X}); break;
        case SettingAction::HpfbNext:
            cfg.hpfb_option = next_value(cfg.hpfb_option, std::array{
                HighPrecisionFramebuffer::Auto, HighPrecisionFramebuffer::On, HighPrecisionFramebuffer::Off}); break;
        case SettingAction::ApiNext:
#if defined(_WIN32)
            cfg.api_option = next_value(cfg.api_option,
                std::array{GraphicsApi::Auto, GraphicsApi::D3D12, GraphicsApi::Vulkan});
#elif defined(__APPLE__)
            cfg.api_option = next_value(cfg.api_option,
                std::array{GraphicsApi::Auto, GraphicsApi::Metal});
#else
            cfg.api_option = next_value(cfg.api_option,
                std::array{GraphicsApi::Auto, GraphicsApi::Vulkan});
#endif
            break;
        case SettingAction::FogMatchToggle:
            lambo::config::set_widescreen_fog_match(!lambo::config::widescreen_fog_match()); return true;
        case SettingAction::SkyMatchToggle:
            lambo::config::set_widescreen_sky_match(!lambo::config::widescreen_sky_match()); return true;
        case SettingAction::NoLodToggle:
            lambo::config::set_no_lod(!lambo::config::no_lod()); return true;
        case SettingAction::Circuit1Toggle: case SettingAction::Circuit2Toggle:
        case SettingAction::Circuit3Toggle: case SettingAction::Circuit4Toggle:
        case SettingAction::Circuit5Toggle: case SettingAction::Circuit6Toggle: {
            const int circuit = static_cast<int>(action) - static_cast<int>(SettingAction::Circuit1Toggle);
            lambo::config::set_no_lod_circuit(circuit, !lambo::config::no_lod_circuit(circuit));
            return true;
        }
        case SettingAction::DrawDistanceNext:
            lambo::config::set_global_draw_distance(next_number(
                lambo::config::global_draw_distance(), std::array{1.0, 1.5, 2.0, 3.0, 0.0})); return true;
        case SettingAction::FogDensityNext:
            lambo::config::set_global_fog_scale(next_number(
                lambo::config::global_fog_scale(), std::array{0.0, 0.5, 0.75, 1.0, 1.5, 2.0})); return true;
        case SettingAction::UpscalerNext: {
            // Cycle through the typed mode enum. set_texture_upscaler_mode
            // both persists the value and fires the registered callback so
            // the live RT64 context picks up the change immediately.
            using Mode = lambo::config::TextureUpscalerMode;
            constexpr std::array<Mode, 2> modes{ Mode::Off, Mode::Xbrz };
            const Mode current = lambo::config::texture_upscaler_mode();
            auto position = std::find(modes.begin(), modes.end(), current);
            const Mode next = (position == modes.end() || std::next(position) == modes.end())
                ? modes.front()
                : *std::next(position);
            lambo::config::set_texture_upscaler_mode(next);
            return true;
        }
    }

    lambo::config::apply_graphics(cfg);
    return true;
}

SettingsSnapshot settings_snapshot() {
    const auto cfg = lambo::config::current_graphics();
    SettingsSnapshot result{
        resolution_name(cfg.res_option),
        std::to_string(cfg.ds_option) + "x",
        aspect_name(cfg.ar_option),
        hud_name(cfg.hr_option),
        refresh_name(cfg),
        msaa_name(cfg.msaa_option),
        hpfb_name(cfg.hpfb_option),
        api_name(cfg.api_option),
        lambo::config::widescreen_fog_match() ? "Enabled" : "Disabled",
        lambo::config::widescreen_sky_match() ? "Enabled" : "Disabled",
        lambo::config::no_lod() ? "Enabled" : "Disabled",
        upscale_pretty_name(lambo::config::texture_upscaler_mode()),
        multiplier_name(lambo::config::global_draw_distance()),
        {},
        {},
    };
    const int fog_percent = static_cast<int>(std::lround(lambo::config::global_fog_scale() * 100.0));
    result.fog_density = fog_percent == 0 ? "Off" : std::to_string(fog_percent) + "%";
    for (int circuit = 0; circuit < 6; ++circuit) {
        result.circuit_visibility[circuit] = lambo::config::no_lod_circuit(circuit) ? "Enabled" : "Disabled";
    }
    return result;
}

} // namespace lambo::ui

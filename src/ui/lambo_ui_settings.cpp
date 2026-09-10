#include "lambo_ui_settings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <string>
#include <utility>

#include "lambo_config.h"

namespace {

using lambo::ui::SettingAction;
using lambo::ui::SettingDirection;

struct SettingBinding {
    std::string_view name;
    SettingAction action;
    SettingDirection direction;
};

constexpr auto setting_bindings = std::to_array<SettingBinding>({
    {"res:next", SettingAction::ResolutionNext, SettingDirection::Next},
    {"res:prev", SettingAction::ResolutionNext, SettingDirection::Previous},
    {"ss:next", SettingAction::SupersamplingNext, SettingDirection::Next},
    {"ss:prev", SettingAction::SupersamplingNext, SettingDirection::Previous},
    {"aspect:next", SettingAction::AspectNext, SettingDirection::Next},
    {"aspect:prev", SettingAction::AspectNext, SettingDirection::Previous},
    {"hud:next", SettingAction::HudNext, SettingDirection::Next},
    {"hud:prev", SettingAction::HudNext, SettingDirection::Previous},
    {"rate:next", SettingAction::RefreshNext, SettingDirection::Next},
    {"rate:prev", SettingAction::RefreshNext, SettingDirection::Previous},
    {"msaa:next", SettingAction::MsaaNext, SettingDirection::Next},
    {"msaa:prev", SettingAction::MsaaNext, SettingDirection::Previous},
    {"hpfb:next", SettingAction::HpfbNext, SettingDirection::Next},
    {"hpfb:prev", SettingAction::HpfbNext, SettingDirection::Previous},
    {"api:next", SettingAction::ApiNext, SettingDirection::Next},
    {"api:prev", SettingAction::ApiNext, SettingDirection::Previous},
    {"fog:toggle", SettingAction::FogMatchToggle, SettingDirection::Next},
    {"sky:toggle", SettingAction::SkyMatchToggle, SettingDirection::Next},
    {"lod:toggle", SettingAction::NoLodToggle, SettingDirection::Next},
    {"circuit:1", SettingAction::Circuit1Toggle, SettingDirection::Next},
    {"circuit:2", SettingAction::Circuit2Toggle, SettingDirection::Next},
    {"circuit:3", SettingAction::Circuit3Toggle, SettingDirection::Next},
    {"circuit:4", SettingAction::Circuit4Toggle, SettingDirection::Next},
    {"circuit:5", SettingAction::Circuit5Toggle, SettingDirection::Next},
    {"circuit:6", SettingAction::Circuit6Toggle, SettingDirection::Next},
    {"distance:next", SettingAction::DrawDistanceNext, SettingDirection::Next},
    {"distance:prev", SettingAction::DrawDistanceNext, SettingDirection::Previous},
    {"fogdensity:next", SettingAction::FogDensityNext, SettingDirection::Next},
    {"fogdensity:prev", SettingAction::FogDensityNext, SettingDirection::Previous},
    {"camdist:next", SettingAction::CameraDistanceNext, SettingDirection::Next},
    {"camdist:prev", SettingAction::CameraDistanceNext, SettingDirection::Previous},
    {"camheight:next", SettingAction::CameraHeightNext, SettingDirection::Next},
    {"camheight:prev", SettingAction::CameraHeightNext, SettingDirection::Previous},
    {"fov:next", SettingAction::FovNext, SettingDirection::Next},
    {"fov:prev", SettingAction::FovNext, SettingDirection::Previous},
});

constexpr auto resolution_options = std::array{
    ultramodern::renderer::Resolution::Auto,
    ultramodern::renderer::Resolution::Original,
    ultramodern::renderer::Resolution::Original2x,
};
constexpr auto supersampling_options = std::array{1, 2, 3, 4};
constexpr auto aspect_options = std::array{
    ultramodern::renderer::AspectRatio::Expand,
    ultramodern::renderer::AspectRatio::Original,
};
constexpr auto hud_options = std::array{
    ultramodern::renderer::HUDRatioMode::Clamp16x9,
    ultramodern::renderer::HUDRatioMode::Full,
    ultramodern::renderer::HUDRatioMode::Original,
};
constexpr auto refresh_rates = std::array{30, 60, 90, 120, 144, 165, 240};
constexpr auto msaa_options = std::array{
    ultramodern::renderer::Antialiasing::None,
    ultramodern::renderer::Antialiasing::MSAA2X,
    ultramodern::renderer::Antialiasing::MSAA4X,
    ultramodern::renderer::Antialiasing::MSAA8X,
};
constexpr auto hpfb_options = std::array{
    ultramodern::renderer::HighPrecisionFramebuffer::Auto,
    ultramodern::renderer::HighPrecisionFramebuffer::On,
    ultramodern::renderer::HighPrecisionFramebuffer::Off,
};
#if defined(_WIN32)
constexpr auto api_options = std::array{
    ultramodern::renderer::GraphicsApi::Auto,
    ultramodern::renderer::GraphicsApi::D3D12,
    ultramodern::renderer::GraphicsApi::Vulkan,
};
#elif defined(__APPLE__)
constexpr auto api_options = std::array{
    ultramodern::renderer::GraphicsApi::Auto,
    ultramodern::renderer::GraphicsApi::Metal,
};
#else
constexpr auto api_options = std::array{
    ultramodern::renderer::GraphicsApi::Auto,
    ultramodern::renderer::GraphicsApi::Vulkan,
};
#endif
constexpr auto draw_distance_options = std::array{1.0, 1.5, 2.0, 3.0, 0.0};
constexpr auto fog_density_options = std::array{0.0, 0.5, 0.75, 1.0, 1.5, 2.0};
constexpr auto camera_distance_options = std::array{1.0, 0.8, 0.65, 0.5};
constexpr auto camera_height_options = std::array{1.0, 0.66, 0.4};
constexpr auto fov_options = std::array{0.0, 5.0, 10.0, 15.0, 20.0};

template <typename T, size_t Size>
T step_value(T current, const std::array<T, Size>& values, bool forward) {
    const auto position = std::find(values.begin(), values.end(), current);
    if (position == values.end()) return forward ? values.front() : values.back();
    if (forward) {
        const auto next = std::next(position);
        return next == values.end() ? values.front() : *next;
    }
    return position == values.begin() ? values.back() : *std::prev(position);
}

template <size_t Size>
double step_number(double current, const std::array<double, Size>& values, bool forward) {
    const auto position = std::find_if(values.begin(), values.end(), [current](double value) {
        return std::abs(value - current) < 0.001;
    });
    if (position == values.end()) return forward ? values.front() : values.back();
    if (forward) {
        const auto next = std::next(position);
        return next == values.end() ? values.front() : *next;
    }
    return position == values.begin() ? values.back() : *std::prev(position);
}

// Index of the value within an authored option list, used only for the visual
// position track. Exact matches are the normal case; interpolate otherwise.
template <size_t Size>
int index_of(double current, const std::array<double, Size>& values) {
    for (size_t i = 0; i < values.size(); ++i) {
        if (std::abs(values[i] - current) < 0.001) return static_cast<int>(i);
    }
    size_t i = 0;
    while (i < values.size() && values[i] < current) ++i;
    return static_cast<int>(i > 0 ? i - 1 : 0);
}

template <typename T, size_t Size>
int option_index(T current, const std::array<T, Size>& values) {
    const auto position = std::find(values.begin(), values.end(), current);
    return position == values.end() ? 0 : static_cast<int>(position - values.begin());
}

std::string track(int index, int count) {
    std::string html;
    for (int step = 0; step < count; ++step) {
        html += step == index ? "<span class=\"track-step on\"></span>"
                              : "<span class=\"track-step\"></span>";
    }
    return html;
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

// Camera scales use two-decimal authored steps (0.65/0.66), so the tenths-only
// multiplier_name would blur them. "Original" keeps the stock value obvious.
std::string camera_scale_name(double value) {
    if (std::abs(value - 1.0) < 0.0001) return "Original";
    int hundredths = static_cast<int>(std::lround(value * 100.0));
    std::string text = std::to_string(hundredths / 100) + ".";
    const int cents = hundredths % 100;
    text += static_cast<char>('0' + cents / 10);
    text += static_cast<char>('0' + cents % 10);
    while (text.back() == '0') text.pop_back();
    if (text.back() == '.') text.pop_back();
    return text + "x";
}

std::string fov_add_name(double value) {
    const int degrees = static_cast<int>(std::lround(value));
    if (degrees == 0) return "Original";
    if (degrees < 0) return std::to_string(degrees) + " deg";
    return "+" + std::to_string(degrees) + " deg";
}

int refresh_index(const ultramodern::renderer::GraphicsConfig& cfg) {
    using ultramodern::renderer::RefreshRate;
    if (cfg.rr_option == RefreshRate::Original) return 0;
    if (cfg.rr_option == RefreshRate::Display) return 1;
    if (cfg.rr_option == RefreshRate::Manual) {
        int position = 0;
        while (position < static_cast<int>(refresh_rates.size()) &&
               refresh_rates[position] < cfg.rr_manual_value) {
            ++position;
        }
        position = std::clamp(position - 1, 0, static_cast<int>(refresh_rates.size()) - 1);
        return 2 + position;
    }
    return 0;
}

} // namespace

namespace lambo::ui {

std::optional<SettingRequest> setting_request_from_name(std::string_view name) {
    for (const auto& binding : setting_bindings) {
        if (binding.name == name) return SettingRequest{binding.action, binding.direction};
    }
    constexpr std::string_view toggle_suffix = ":toggle";
    if (name.size() > toggle_suffix.size() &&
        name.substr(name.size() - toggle_suffix.size()) == toggle_suffix) {
        const auto base_name = name.substr(0, name.size() - toggle_suffix.size());
        for (const auto& binding : setting_bindings) {
            if (binding.name == base_name) return SettingRequest{binding.action, binding.direction};
        }
    }
    return std::nullopt;
}

bool apply_setting_request(const SettingRequest& request) {
    using namespace ultramodern::renderer;
    auto cfg = lambo::config::current_graphics();
    const bool forward = request.direction == SettingDirection::Next;
    bool apply_live = true;

    switch (request.action) {
        case SettingAction::ResolutionNext:
            cfg.res_option = step_value(cfg.res_option, resolution_options, forward); break;
        case SettingAction::SupersamplingNext:
            cfg.ds_option = step_value(cfg.ds_option, supersampling_options, forward); break;
        case SettingAction::AspectNext:
            cfg.ar_option = step_value(cfg.ar_option, aspect_options, forward); break;
        case SettingAction::HudNext:
            cfg.hr_option = step_value(cfg.hr_option, hud_options, forward); break;
        case SettingAction::RefreshNext: {
            if (cfg.rr_option == RefreshRate::Original) {
                if (forward) {
                    cfg.rr_option = RefreshRate::Display;
                } else {
                    cfg.rr_option = RefreshRate::Manual;
                    cfg.rr_manual_value = refresh_rates.back();
                }
            } else if (cfg.rr_option == RefreshRate::Display) {
                if (forward) {
                    cfg.rr_option = RefreshRate::Manual;
                    cfg.rr_manual_value = 30;
                } else {
                    cfg.rr_option = RefreshRate::Original;
                }
            } else {
                const auto position = std::find(refresh_rates.begin(), refresh_rates.end(), cfg.rr_manual_value);
                if (position == refresh_rates.end()) {
                    if (forward) {
                        const auto next_rate = std::upper_bound(
                            refresh_rates.begin(), refresh_rates.end(), cfg.rr_manual_value);
                        if (next_rate == refresh_rates.end()) cfg.rr_option = RefreshRate::Original;
                        else cfg.rr_manual_value = *next_rate;
                    } else {
                        const auto lower = std::lower_bound(
                            refresh_rates.begin(), refresh_rates.end(), cfg.rr_manual_value);
                        if (lower == refresh_rates.begin()) cfg.rr_option = RefreshRate::Display;
                        else cfg.rr_manual_value = *std::prev(lower);
                    }
                } else if (forward) {
                    if (std::next(position) == refresh_rates.end()) cfg.rr_option = RefreshRate::Original;
                    else cfg.rr_manual_value = *std::next(position);
                } else {
                    if (position == refresh_rates.begin()) cfg.rr_option = RefreshRate::Display;
                    else cfg.rr_manual_value = *std::prev(position);
                }
            }
            break;
        }
        case SettingAction::MsaaNext:
            cfg.msaa_option = step_value(cfg.msaa_option, msaa_options, forward); break;
        case SettingAction::HpfbNext:
            cfg.hpfb_option = step_value(cfg.hpfb_option, hpfb_options, forward); break;
        case SettingAction::ApiNext:
            cfg.api_option = step_value(cfg.api_option, api_options, forward);
            apply_live = false;
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
            const int circuit = static_cast<int>(request.action) - static_cast<int>(SettingAction::Circuit1Toggle);
            lambo::config::set_no_lod_circuit(circuit, !lambo::config::no_lod_circuit(circuit));
            return true;
        }
        case SettingAction::DrawDistanceNext:
            lambo::config::set_global_draw_distance(step_number(
                lambo::config::global_draw_distance(), draw_distance_options, forward)); return true;
        case SettingAction::FogDensityNext:
            lambo::config::set_global_fog_scale(step_number(
                lambo::config::global_fog_scale(), fog_density_options, forward)); return true;
        case SettingAction::CameraDistanceNext:
            lambo::config::set_camera_distance_scale(step_number(
                lambo::config::camera_distance_scale(), camera_distance_options, forward)); return true;
        case SettingAction::CameraHeightNext:
            lambo::config::set_camera_height_scale(step_number(
                lambo::config::camera_height_scale(), camera_height_options, forward)); return true;
        case SettingAction::FovNext:
            lambo::config::set_camera_fov_add(step_number(
                lambo::config::camera_fov_add(), fov_options, forward)); return true;
    }

    lambo::config::apply_graphics(cfg, apply_live);
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
        lambo::config::widescreen_fog_match(),
        lambo::config::widescreen_sky_match(),
        lambo::config::no_lod(),
        multiplier_name(lambo::config::global_draw_distance()),
        {},
        camera_scale_name(lambo::config::camera_distance_scale()),
        camera_scale_name(lambo::config::camera_height_scale()),
        fov_add_name(lambo::config::camera_fov_add()),
    };
    const int fog_percent = static_cast<int>(std::lround(lambo::config::global_fog_scale() * 100.0));
    result.fog_density = fog_percent == 0 ? "Off" : std::to_string(fog_percent) + "%";
    for (int circuit = 0; circuit < 6; ++circuit) {
        result.circuit_visibility[circuit] = lambo::config::no_lod_circuit(circuit);
    }
    result.resolution_track = track(option_index(cfg.res_option, resolution_options), resolution_options.size());
    result.supersampling_track = track(option_index(cfg.ds_option, supersampling_options), supersampling_options.size());
    result.aspect_track = track(option_index(cfg.ar_option, aspect_options), aspect_options.size());
    result.hud_track = track(option_index(cfg.hr_option, hud_options), hud_options.size());
    result.refresh_track = track(refresh_index(cfg), 2 + refresh_rates.size());
    result.msaa_track = track(option_index(cfg.msaa_option, msaa_options), msaa_options.size());
    result.framebuffer_precision_track = track(option_index(cfg.hpfb_option, hpfb_options), hpfb_options.size());
    result.graphics_api_track = track(option_index(cfg.api_option, api_options), api_options.size());
    result.draw_distance_track = track(index_of(
        lambo::config::global_draw_distance(), draw_distance_options), draw_distance_options.size());
    result.fog_density_track = track(index_of(
        lambo::config::global_fog_scale(), fog_density_options), fog_density_options.size());
    result.camera_distance_track = track(index_of(
        lambo::config::camera_distance_scale(), camera_distance_options), camera_distance_options.size());
    result.camera_height_track = track(index_of(
        lambo::config::camera_height_scale(), camera_height_options), camera_height_options.size());
    result.camera_fov_track = track(index_of(
        lambo::config::camera_fov_add(), fov_options), fov_options.size());
    return result;
}

} // namespace lambo::ui

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

int resolution_index(ultramodern::renderer::Resolution value) {
    using ultramodern::renderer::Resolution;
    switch (value) {
        case Resolution::Auto: return 0;
        case Resolution::Original: return 1;
        case Resolution::Original2x: return 2;
        default: return 0;
    }
}

int aspect_index(ultramodern::renderer::AspectRatio value) {
    using ultramodern::renderer::AspectRatio;
    switch (value) {
        case AspectRatio::Expand: return 0;
        case AspectRatio::Original: return 1;
        default: return 0;
    }
}

int hud_index(ultramodern::renderer::HUDRatioMode value) {
    using ultramodern::renderer::HUDRatioMode;
    switch (value) {
        case HUDRatioMode::Clamp16x9: return 0;
        case HUDRatioMode::Full: return 1;
        case HUDRatioMode::Original: return 2;
        default: return 0;
    }
}

int msaa_index(ultramodern::renderer::Antialiasing value) {
    using ultramodern::renderer::Antialiasing;
    switch (value) {
        case Antialiasing::None: return 0;
        case Antialiasing::MSAA2X: return 1;
        case Antialiasing::MSAA4X: return 2;
        case Antialiasing::MSAA8X: return 3;
        default: return 0;
    }
}

int hpfb_index(ultramodern::renderer::HighPrecisionFramebuffer value) {
    using ultramodern::renderer::HighPrecisionFramebuffer;
    switch (value) {
        case HighPrecisionFramebuffer::Auto: return 0;
        case HighPrecisionFramebuffer::On: return 1;
        case HighPrecisionFramebuffer::Off: return 2;
        default: return 0;
    }
}

int api_index(ultramodern::renderer::GraphicsApi value) {
    using ultramodern::renderer::GraphicsApi;
    if (value == GraphicsApi::Auto) return 0;
#if defined(_WIN32)
    if (value == GraphicsApi::D3D12) return 1;
    if (value == GraphicsApi::Vulkan) return 2;
#elif defined(__APPLE__)
    if (value == GraphicsApi::Metal) return 1;
#else
    if (value == GraphicsApi::Vulkan) return 1;
#endif
    return 0;
}

int api_count() {
#if defined(_WIN32)
    return 3;
#else
    return 2;
#endif
}

int refresh_index(const ultramodern::renderer::GraphicsConfig& cfg) {
    using ultramodern::renderer::RefreshRate;
    if (cfg.rr_option == RefreshRate::Original) return 0;
    if (cfg.rr_option == RefreshRate::Display) return 1;
    if (cfg.rr_option == RefreshRate::Manual) {
        constexpr std::array rates{30, 60, 90, 120, 144, 165, 240};
        int position = 0;
        while (position < static_cast<int>(rates.size()) && rates[position] < cfg.rr_manual_value) {
            ++position;
        }
        position = std::clamp(position - 1, 0, static_cast<int>(rates.size()) - 1);
        return 2 + position;
    }
    return 0;
}

} // namespace

namespace lambo::ui {

std::optional<SettingAction> setting_action_from_name(std::string_view name) {
    for (const auto& binding : setting_bindings) {
        if (binding.name == name) return binding.action;
    }
    return std::nullopt;
}

std::optional<SettingRequest> setting_request_from_name(std::string_view name) {
    for (const auto& binding : setting_bindings) {
        if (binding.name == name) return SettingRequest{binding.action, binding.direction};
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
            cfg.res_option = step_value(cfg.res_option,
                std::array{Resolution::Auto, Resolution::Original, Resolution::Original2x}, forward); break;
        case SettingAction::SupersamplingNext:
            if (forward) cfg.ds_option = cfg.ds_option < 1 || cfg.ds_option >= 4 ? 1 : cfg.ds_option + 1;
            else cfg.ds_option = cfg.ds_option <= 1 || cfg.ds_option > 4 ? 4 : cfg.ds_option - 1;
            break;
        case SettingAction::AspectNext:
            cfg.ar_option = step_value(cfg.ar_option,
                std::array{AspectRatio::Expand, AspectRatio::Original}, forward); break;
        case SettingAction::HudNext:
            cfg.hr_option = step_value(cfg.hr_option,
                std::array{HUDRatioMode::Clamp16x9, HUDRatioMode::Full, HUDRatioMode::Original}, forward); break;
        case SettingAction::RefreshNext: {
            constexpr std::array rates{30, 60, 90, 120, 144, 165, 240};
            if (cfg.rr_option == RefreshRate::Original) {
                if (forward) {
                    cfg.rr_option = RefreshRate::Display;
                } else {
                    cfg.rr_option = RefreshRate::Manual;
                    cfg.rr_manual_value = rates.back();
                }
            } else if (cfg.rr_option == RefreshRate::Display) {
                if (forward) {
                    cfg.rr_option = RefreshRate::Manual;
                    cfg.rr_manual_value = 30;
                } else {
                    cfg.rr_option = RefreshRate::Original;
                }
            } else {
                const auto position = std::find(rates.begin(), rates.end(), cfg.rr_manual_value);
                if (position == rates.end()) {
                    if (forward) {
                        const auto next_rate = std::upper_bound(rates.begin(), rates.end(), cfg.rr_manual_value);
                        if (next_rate == rates.end()) cfg.rr_option = RefreshRate::Original;
                        else cfg.rr_manual_value = *next_rate;
                    } else {
                        const auto lower = std::lower_bound(rates.begin(), rates.end(), cfg.rr_manual_value);
                        if (lower == rates.begin()) cfg.rr_option = RefreshRate::Display;
                        else cfg.rr_manual_value = *std::prev(lower);
                    }
                } else if (forward) {
                    if (std::next(position) == rates.end()) cfg.rr_option = RefreshRate::Original;
                    else cfg.rr_manual_value = *std::next(position);
                } else {
                    if (position == rates.begin()) cfg.rr_option = RefreshRate::Display;
                    else cfg.rr_manual_value = *std::prev(position);
                }
            }
            break;
        }
        case SettingAction::MsaaNext:
            cfg.msaa_option = step_value(cfg.msaa_option, std::array{
                Antialiasing::None, Antialiasing::MSAA2X, Antialiasing::MSAA4X, Antialiasing::MSAA8X}, forward); break;
        case SettingAction::HpfbNext:
            cfg.hpfb_option = step_value(cfg.hpfb_option, std::array{
                HighPrecisionFramebuffer::Auto, HighPrecisionFramebuffer::On, HighPrecisionFramebuffer::Off}, forward); break;
        case SettingAction::ApiNext:
#if defined(_WIN32)
            cfg.api_option = step_value(cfg.api_option,
                std::array{GraphicsApi::Auto, GraphicsApi::D3D12, GraphicsApi::Vulkan}, forward);
#elif defined(__APPLE__)
            cfg.api_option = step_value(cfg.api_option,
                std::array{GraphicsApi::Auto, GraphicsApi::Metal}, forward);
#else
            cfg.api_option = step_value(cfg.api_option,
                std::array{GraphicsApi::Auto, GraphicsApi::Vulkan}, forward);
#endif
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
                lambo::config::global_draw_distance(), std::array{1.0, 1.5, 2.0, 3.0, 0.0}, forward)); return true;
        case SettingAction::FogDensityNext:
            lambo::config::set_global_fog_scale(step_number(
                lambo::config::global_fog_scale(), std::array{0.0, 0.5, 0.75, 1.0, 1.5, 2.0}, forward)); return true;
        case SettingAction::CameraDistanceNext:
            lambo::config::set_camera_distance_scale(step_number(
                lambo::config::camera_distance_scale(), std::array{1.0, 0.8, 0.65, 0.5}, forward)); return true;
        case SettingAction::CameraHeightNext:
            lambo::config::set_camera_height_scale(step_number(
                lambo::config::camera_height_scale(), std::array{1.0, 0.66, 0.4}, forward)); return true;
        case SettingAction::FovNext:
            lambo::config::set_camera_fov_add(step_number(
                lambo::config::camera_fov_add(), std::array{0.0, 5.0, 10.0, 15.0, 20.0}, forward)); return true;
    }

    lambo::config::apply_graphics(cfg, apply_live);
    return true;
}

bool apply_setting_action(SettingAction action) {
    return apply_setting_request(SettingRequest{action, SettingDirection::Next});
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
        multiplier_name(lambo::config::global_draw_distance()),
        {},
        camera_scale_name(lambo::config::camera_distance_scale()),
        camera_scale_name(lambo::config::camera_height_scale()),
        fov_add_name(lambo::config::camera_fov_add()),
    };
    const int fog_percent = static_cast<int>(std::lround(lambo::config::global_fog_scale() * 100.0));
    result.fog_density = fog_percent == 0 ? "Off" : std::to_string(fog_percent) + "%";
    for (int circuit = 0; circuit < 6; ++circuit) {
        result.circuit_visibility[circuit] = lambo::config::no_lod_circuit(circuit) ? "Enabled" : "Disabled";
    }
    result.resolution_track = track(resolution_index(cfg.res_option), 3);
    result.supersampling_track = track(std::clamp(static_cast<int>(cfg.ds_option) - 1, 0, 3), 4);
    result.aspect_track = track(aspect_index(cfg.ar_option), 2);
    result.hud_track = track(hud_index(cfg.hr_option), 3);
    result.refresh_track = track(refresh_index(cfg), 9);
    result.msaa_track = track(msaa_index(cfg.msaa_option), 4);
    result.framebuffer_precision_track = track(hpfb_index(cfg.hpfb_option), 3);
    result.graphics_api_track = track(api_index(cfg.api_option), api_count());
    result.draw_distance_track = track(index_of(
        lambo::config::global_draw_distance(), std::array{1.0, 1.5, 2.0, 3.0, 0.0}), 5);
    result.fog_density_track = track(index_of(
        lambo::config::global_fog_scale(), std::array{0.0, 0.5, 0.75, 1.0, 1.5, 2.0}), 6);
    result.camera_distance_track = track(index_of(
        lambo::config::camera_distance_scale(), std::array{1.0, 0.8, 0.65, 0.5}), 4);
    result.camera_height_track = track(index_of(
        lambo::config::camera_height_scale(), std::array{1.0, 0.66, 0.4}), 3);
    result.camera_fov_track = track(index_of(
        lambo::config::camera_fov_add(), std::array{0.0, 5.0, 10.0, 15.0, 20.0}), 5);
    return result;
}

} // namespace lambo::ui

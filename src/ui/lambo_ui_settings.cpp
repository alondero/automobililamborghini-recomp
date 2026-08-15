#include "lambo_ui_settings.h"

#include <array>
#include <cmath>
#include <string>
#include <utility>

#include "lambo_config.h"

namespace {

using lambo::ui::SettingAction;

constexpr auto setting_bindings = std::to_array<std::pair<std::string_view, SettingAction>>({
    {"res:auto", SettingAction::ResolutionAuto},
    {"res:original", SettingAction::ResolutionOriginal},
    {"res:2x", SettingAction::ResolutionOriginal2x},
    {"ss:1", SettingAction::Supersampling1x},
    {"ss:2", SettingAction::Supersampling2x},
    {"ss:3", SettingAction::Supersampling3x},
    {"ss:4", SettingAction::Supersampling4x},
    {"aspect:original", SettingAction::AspectOriginal},
    {"aspect:expand", SettingAction::AspectExpand},
    {"hud:original", SettingAction::HudOriginal},
    {"hud:16x9", SettingAction::HudClamp16x9},
    {"hud:full", SettingAction::HudFull},
    {"rate:original", SettingAction::RefreshOriginal},
    {"rate:display", SettingAction::RefreshDisplay},
    {"rate:30", SettingAction::Refresh30},
    {"rate:60", SettingAction::Refresh60},
    {"rate:90", SettingAction::Refresh90},
    {"rate:120", SettingAction::Refresh120},
    {"rate:144", SettingAction::Refresh144},
    {"rate:165", SettingAction::Refresh165},
    {"rate:240", SettingAction::Refresh240},
    {"msaa:off", SettingAction::MsaaOff},
    {"msaa:2", SettingAction::Msaa2x},
    {"msaa:4", SettingAction::Msaa4x},
    {"msaa:8", SettingAction::Msaa8x},
    {"hpfb:auto", SettingAction::HpfbAuto},
    {"hpfb:on", SettingAction::HpfbOn},
    {"hpfb:off", SettingAction::HpfbOff},
    {"api:auto", SettingAction::ApiAuto},
    {"api:d3d12", SettingAction::ApiD3D12},
    {"api:vulkan", SettingAction::ApiVulkan},
    {"fog:toggle", SettingAction::FogMatchToggle},
    {"sky:toggle", SettingAction::SkyMatchToggle},
    {"lod:toggle", SettingAction::NoLodToggle},
    {"circuit:1", SettingAction::Circuit1Toggle},
    {"circuit:2", SettingAction::Circuit2Toggle},
    {"circuit:3", SettingAction::Circuit3Toggle},
    {"circuit:4", SettingAction::Circuit4Toggle},
    {"circuit:5", SettingAction::Circuit5Toggle},
    {"circuit:6", SettingAction::Circuit6Toggle},
    {"distance:1", SettingAction::DrawDistance1x},
    {"distance:1.5", SettingAction::DrawDistance1_5x},
    {"distance:2", SettingAction::DrawDistance2x},
    {"distance:3", SettingAction::DrawDistance3x},
    {"distance:unlimited", SettingAction::DrawDistanceUnlimited},
    {"fogdensity:off", SettingAction::FogDensityOff},
    {"fogdensity:0.5", SettingAction::FogDensity50},
    {"fogdensity:0.75", SettingAction::FogDensity75},
    {"fogdensity:1", SettingAction::FogDensity100},
    {"fogdensity:1.5", SettingAction::FogDensity150},
    {"fogdensity:2", SettingAction::FogDensity200},
});

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

} // namespace

namespace lambo::ui {

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
        case SettingAction::ResolutionAuto: cfg.res_option = Resolution::Auto; break;
        case SettingAction::ResolutionOriginal: cfg.res_option = Resolution::Original; break;
        case SettingAction::ResolutionOriginal2x: cfg.res_option = Resolution::Original2x; break;
        case SettingAction::Supersampling1x: cfg.ds_option = 1; break;
        case SettingAction::Supersampling2x: cfg.ds_option = 2; break;
        case SettingAction::Supersampling3x: cfg.ds_option = 3; break;
        case SettingAction::Supersampling4x: cfg.ds_option = 4; break;
        case SettingAction::AspectOriginal: cfg.ar_option = AspectRatio::Original; break;
        case SettingAction::AspectExpand: cfg.ar_option = AspectRatio::Expand; break;
        case SettingAction::HudOriginal: cfg.hr_option = HUDRatioMode::Original; break;
        case SettingAction::HudClamp16x9: cfg.hr_option = HUDRatioMode::Clamp16x9; break;
        case SettingAction::HudFull: cfg.hr_option = HUDRatioMode::Full; break;
        case SettingAction::RefreshOriginal: cfg.rr_option = RefreshRate::Original; break;
        case SettingAction::RefreshDisplay: cfg.rr_option = RefreshRate::Display; break;
        case SettingAction::Refresh30: cfg.rr_option = RefreshRate::Manual; cfg.rr_manual_value = 30; break;
        case SettingAction::Refresh60: cfg.rr_option = RefreshRate::Manual; cfg.rr_manual_value = 60; break;
        case SettingAction::Refresh90: cfg.rr_option = RefreshRate::Manual; cfg.rr_manual_value = 90; break;
        case SettingAction::Refresh120: cfg.rr_option = RefreshRate::Manual; cfg.rr_manual_value = 120; break;
        case SettingAction::Refresh144: cfg.rr_option = RefreshRate::Manual; cfg.rr_manual_value = 144; break;
        case SettingAction::Refresh165: cfg.rr_option = RefreshRate::Manual; cfg.rr_manual_value = 165; break;
        case SettingAction::Refresh240: cfg.rr_option = RefreshRate::Manual; cfg.rr_manual_value = 240; break;
        case SettingAction::MsaaOff: cfg.msaa_option = Antialiasing::None; break;
        case SettingAction::Msaa2x: cfg.msaa_option = Antialiasing::MSAA2X; break;
        case SettingAction::Msaa4x: cfg.msaa_option = Antialiasing::MSAA4X; break;
        case SettingAction::Msaa8x: cfg.msaa_option = Antialiasing::MSAA8X; break;
        case SettingAction::HpfbAuto: cfg.hpfb_option = HighPrecisionFramebuffer::Auto; break;
        case SettingAction::HpfbOn: cfg.hpfb_option = HighPrecisionFramebuffer::On; break;
        case SettingAction::HpfbOff: cfg.hpfb_option = HighPrecisionFramebuffer::Off; break;
        case SettingAction::ApiAuto: cfg.api_option = GraphicsApi::Auto; break;
        case SettingAction::ApiD3D12: cfg.api_option = GraphicsApi::D3D12; break;
        case SettingAction::ApiVulkan: cfg.api_option = GraphicsApi::Vulkan; break;
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
        case SettingAction::DrawDistance1x: lambo::config::set_global_draw_distance(1.0); return true;
        case SettingAction::DrawDistance1_5x: lambo::config::set_global_draw_distance(1.5); return true;
        case SettingAction::DrawDistance2x: lambo::config::set_global_draw_distance(2.0); return true;
        case SettingAction::DrawDistance3x: lambo::config::set_global_draw_distance(3.0); return true;
        case SettingAction::DrawDistanceUnlimited: lambo::config::set_global_draw_distance(0.0); return true;
        case SettingAction::FogDensityOff: lambo::config::set_global_fog_scale(0.0); return true;
        case SettingAction::FogDensity50: lambo::config::set_global_fog_scale(0.5); return true;
        case SettingAction::FogDensity75: lambo::config::set_global_fog_scale(0.75); return true;
        case SettingAction::FogDensity100: lambo::config::set_global_fog_scale(1.0); return true;
        case SettingAction::FogDensity150: lambo::config::set_global_fog_scale(1.5); return true;
        case SettingAction::FogDensity200: lambo::config::set_global_fog_scale(2.0); return true;
    }

    lambo::config::apply_graphics(cfg);
    return true;
}

std::string graphics_summary_html() {
    const auto cfg = lambo::config::current_graphics();
    return std::string("Resolution: ") + resolution_name(cfg.res_option) +
           "<br/>Supersampling: " + std::to_string(cfg.ds_option) + "x" +
           "<br/>Aspect ratio: " + aspect_name(cfg.ar_option) +
           "<br/>HUD: " + hud_name(cfg.hr_option) +
           "<br/>Refresh rate: " + refresh_name(cfg) +
           "<br/>MSAA: " + msaa_name(cfg.msaa_option) +
           "<br/>Framebuffer precision: " + hpfb_name(cfg.hpfb_option) +
           "<br/>Graphics API: " + api_name(cfg.api_option) + " (restart required)";
}

std::string enhancements_summary_html() {
    std::string circuits;
    for (int circuit = 0; circuit < 6; ++circuit) {
        if (!circuits.empty()) circuits += ", ";
        circuits += std::to_string(circuit + 1) + ":" +
                    (lambo::config::no_lod_circuit(circuit) ? "on" : "off");
    }
    const int fog_percent = static_cast<int>(std::lround(lambo::config::global_fog_scale() * 100.0));
    return std::string("Widescreen fog: ") + (lambo::config::widescreen_fog_match() ? "matched" : "original") +
           "<br/>Widescreen sky: " + (lambo::config::widescreen_sky_match() ? "matched" : "original") +
           "<br/>N64 LOD: " + (lambo::config::no_lod() ? "removed" : "original") +
           "<br/>Full-track visibility: " + circuits +
           "<br/>Draw distance: " + multiplier_name(lambo::config::global_draw_distance()) +
           "<br/>Fog density: " + (fog_percent == 0 ? std::string("Off") : std::to_string(fog_percent) + "%");
}

} // namespace lambo::ui

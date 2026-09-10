#ifndef LAMBO_UI_SETTINGS_H
#define LAMBO_UI_SETTINGS_H

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace lambo::ui {

enum class SettingAction {
    ResolutionNext,
    SupersamplingNext,
    AspectNext,
    HudNext,
    RefreshNext,
    MsaaNext,
    HpfbNext,
    ApiNext,
    FogMatchToggle,
    SkyMatchToggle,
    NoLodToggle,
    Circuit1Toggle,
    Circuit2Toggle,
    Circuit3Toggle,
    Circuit4Toggle,
    Circuit5Toggle,
    Circuit6Toggle,
    DrawDistanceNext,
    FogDensityNext,
    CameraDistanceNext,
    CameraHeightNext,
    FovNext,
};

enum class SettingDirection {
    Next,
    Previous,
};

struct SettingRequest {
    SettingAction action;
    SettingDirection direction;
};

struct SettingsSnapshot {
    std::string resolution;
    std::string supersampling;
    std::string aspect_ratio;
    std::string hud_layout;
    std::string refresh_rate;
    std::string msaa;
    std::string framebuffer_precision;
    std::string graphics_api;
    bool widescreen_fog_enabled;
    bool widescreen_sky_enabled;
    bool lod_removal_enabled;
    std::string draw_distance;
    std::string fog_density;
    std::string camera_distance;
    std::string camera_height;
    std::string camera_fov;
    std::array<bool, 6> circuit_visibility;
    // Position indicators let the focused row show where its value sits in its cycle.
    std::string resolution_track;
    std::string supersampling_track;
    std::string aspect_track;
    std::string hud_track;
    std::string refresh_track;
    std::string msaa_track;
    std::string framebuffer_precision_track;
    std::string graphics_api_track;
    std::string draw_distance_track;
    std::string fog_density_track;
    std::string camera_distance_track;
    std::string camera_height_track;
    std::string camera_fov_track;
};

std::optional<SettingRequest> setting_request_from_name(std::string_view name);
bool apply_setting_request(const SettingRequest& request);

SettingsSnapshot settings_snapshot();

} // namespace lambo::ui

#endif

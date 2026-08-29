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
    UpscalerNext,
    Circuit1Toggle,
    Circuit2Toggle,
    Circuit3Toggle,
    Circuit4Toggle,
    Circuit5Toggle,
    Circuit6Toggle,
    DrawDistanceNext,
    FogDensityNext,
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
    std::string widescreen_fog;
    std::string widescreen_sky;
    std::string lod_removal;
    std::string texture_upscaler;
    std::string draw_distance;
    std::string fog_density;
    std::array<std::string, 6> circuit_visibility;
};

std::optional<SettingAction> setting_action_from_name(std::string_view name);
bool apply_setting_action(SettingAction action);
SettingsSnapshot settings_snapshot();

} // namespace lambo::ui

#endif

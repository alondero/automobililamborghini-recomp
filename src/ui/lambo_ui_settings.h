#ifndef LAMBO_UI_SETTINGS_H
#define LAMBO_UI_SETTINGS_H

#include <optional>
#include <string>
#include <string_view>

namespace lambo::ui {

enum class SettingAction {
    ResolutionAuto,
    ResolutionOriginal,
    ResolutionOriginal2x,
    Supersampling1x,
    Supersampling2x,
    Supersampling3x,
    Supersampling4x,
    AspectOriginal,
    AspectExpand,
    HudOriginal,
    HudClamp16x9,
    HudFull,
    RefreshOriginal,
    RefreshDisplay,
    Refresh30,
    Refresh60,
    Refresh90,
    Refresh120,
    Refresh144,
    Refresh165,
    Refresh240,
    MsaaOff,
    Msaa2x,
    Msaa4x,
    Msaa8x,
    HpfbAuto,
    HpfbOn,
    HpfbOff,
    ApiAuto,
    ApiD3D12,
    ApiVulkan,
    FogMatchToggle,
    SkyMatchToggle,
    NoLodToggle,
    Circuit1Toggle,
    Circuit2Toggle,
    Circuit3Toggle,
    Circuit4Toggle,
    Circuit5Toggle,
    Circuit6Toggle,
    DrawDistance1x,
    DrawDistance1_5x,
    DrawDistance2x,
    DrawDistance3x,
    DrawDistanceUnlimited,
    FogDensityOff,
    FogDensity50,
    FogDensity75,
    FogDensity100,
    FogDensity150,
    FogDensity200,
};

std::optional<SettingAction> setting_action_from_name(std::string_view name);
bool apply_setting_action(SettingAction action);
std::string graphics_summary_html();
std::string enhancements_summary_html();

} // namespace lambo::ui

#endif

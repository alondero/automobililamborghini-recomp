#ifndef LAMBO_LAUNCHER_MENU_H
#define LAMBO_LAUNCHER_MENU_H

#include <array>
#include <functional>

namespace recompui { class LauncherMenu; }

namespace lambo::ui {

// Menu rows on the Paddock-split launcher: a brand rail carries
// identity and system state, numbered rows carry the actions. The row model and action policy
// compile without the RmlUi stack so host tests cover the native buttons.
enum class PaddockTarget { Play, Settings, Mods, Controls, Quit };

struct PaddockRow {
    const char* number;
    const char* title;
    const char* description;
    PaddockTarget target;
};

// Ordered row model for the Paddock menu. Kept inline in the header so the
// host test and the native builder share one table that cannot drift.
inline constexpr std::array<PaddockRow, 5> paddock_rows() {
    return {{
        {"01", "Start Engine", "Jump straight into the game", PaddockTarget::Play},
        {"02", "Settings", "Tune graphics and driving", PaddockTarget::Settings},
        {"03", "Mods & texture packs", "Install, enable and order mods and texture packs", PaddockTarget::Mods},
        {"04", "Controls & players", "Remap inputs and assign up to four players", PaddockTarget::Controls},
        {"05", "Quit", "Exit the game", PaddockTarget::Quit},
    }};
}

enum class LauncherSettingsPage { Current, Mods, Controls };

// Host effects are supplied by the frontend adapter. Keep routing and the
// successful-Play gate here so tests exercise the same policy as the buttons.
struct LauncherActions {
    std::function<bool()> request_play;
    std::function<void()> hide_launcher;
    std::function<void(LauncherSettingsPage)> open_settings;
    std::function<void()> request_quit;
};

inline void activate_launcher(PaddockTarget target, const LauncherActions& actions) {
    switch (target) {
    case PaddockTarget::Play:
        if (actions.request_play()) actions.hide_launcher();
        break;
    case PaddockTarget::Settings:
        actions.open_settings(LauncherSettingsPage::Current);
        break;
    case PaddockTarget::Mods:
        actions.open_settings(LauncherSettingsPage::Mods);
        break;
    case PaddockTarget::Controls:
        actions.open_settings(LauncherSettingsPage::Controls);
        break;
    case PaddockTarget::Quit:
        actions.request_quit();
        break;
    }
}

// Builds the paddock split: a brand rail beside numbered menu
// rows. It replaces the default launcher title and content; play/quit policy
// stays with the caller.
void build_paddock_launcher(recompui::LauncherMenu* menu, const LauncherActions& actions);

} // namespace lambo::ui

#endif

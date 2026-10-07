#ifndef LAMBO_LAUNCHER_MENU_H
#define LAMBO_LAUNCHER_MENU_H

#include <array>
#include <functional>

namespace recompui { class LauncherMenu; }

namespace lambo::ui {

// Menu rows on the Paddock-split launcher (prototype D): a brand rail carries
// identity and system state, numbered rows carry the actions. The native
// builder maps each target to its action; the model only names the
// destination so the host test compiles without the RmlUi stack.
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
        {"02", "Settings", "Tune graphics, sound and driving", PaddockTarget::Settings},
        {"03", "Mods & texture packs", "Install, enable and order mods and texture packs", PaddockTarget::Mods},
        {"04", "Controls & players", "Remap inputs and assign up to four players", PaddockTarget::Controls},
        {"05", "Quit", "Close the launcher", PaddockTarget::Quit},
    }};
}

struct LauncherActions {
    std::function<void(PaddockTarget)> activate;
};

// Builds prototype-D ("Paddock split"): a brand rail beside numbered menu
// rows. It replaces the default launcher title and content; play/quit policy
// stays with the caller.
void build_paddock_launcher(recompui::LauncherMenu* menu, const LauncherActions& actions);

} // namespace lambo::ui

#endif

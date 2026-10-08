#include "ui/lambo_launcher_menu.h"

#include <cstdio>
#include <string_view>
#include <vector>

// The Paddock row model is shared with the native launcher builder, so pin
// its numbers, titles, descriptions and destinations here: a drift would
// silently reorder the launcher menu.
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    };

    const auto rows = lambo::ui::paddock_rows();
    check(rows.size() == 5, "paddock menu has five rows");

    const char* numbers[] = {"01", "02", "03", "04", "05"};
    const char* titles[] = {"Start Engine", "Settings", "Mods & texture packs",
                            "Controls & players", "Quit"};
    const lambo::ui::PaddockTarget targets[] = {
        lambo::ui::PaddockTarget::Play,
        lambo::ui::PaddockTarget::Settings,
        lambo::ui::PaddockTarget::Mods,
        lambo::ui::PaddockTarget::Controls,
        lambo::ui::PaddockTarget::Quit,
    };
    for (size_t i = 0; i < rows.size() && i < 5; ++i) {
        check(std::string_view(rows[i].number) == numbers[i], "row number and order");
        check(std::string_view(rows[i].title) == titles[i], "row title and order");
        check(rows[i].title[0] != '\0', "row title is non-empty");
        check(rows[i].description[0] != '\0', "row description is non-empty");
        check(rows[i].target == targets[i], "row destination and order");
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        for (size_t j = i + 1; j < rows.size(); ++j) {
            check(rows[i].target != rows[j].target, "row destinations are distinct");
        }
    }

    using namespace lambo::ui;
    enum class Effect { Play, Hide, Settings, Mods, Controls, Quit };
    std::vector<Effect> effects;
    bool play_accepted = false;
    LauncherActions actions;
    actions.request_play = [&] { effects.push_back(Effect::Play); return play_accepted; };
    actions.hide_launcher = [&] { effects.push_back(Effect::Hide); };
    actions.open_settings = [&](LauncherSettingsPage page) {
        switch (page) {
        case LauncherSettingsPage::Current: effects.push_back(Effect::Settings); break;
        case LauncherSettingsPage::Mods: effects.push_back(Effect::Mods); break;
        case LauncherSettingsPage::Controls: effects.push_back(Effect::Controls); break;
        }
    };
    actions.request_quit = [&] { effects.push_back(Effect::Quit); };

    activate_launcher(PaddockTarget::Play, actions);
    check(effects == std::vector{Effect::Play}, "rejected Play keeps the launcher visible");
    effects.clear();
    play_accepted = true;
    activate_launcher(PaddockTarget::Play, actions);
    check(effects == std::vector{Effect::Play, Effect::Hide},
          "accepted Play hides only after requesting startup");
    effects.clear();
    activate_launcher(PaddockTarget::Settings, actions);
    activate_launcher(PaddockTarget::Mods, actions);
    activate_launcher(PaddockTarget::Controls, actions);
    activate_launcher(PaddockTarget::Quit, actions);
    check(effects == std::vector{Effect::Settings, Effect::Mods, Effect::Controls, Effect::Quit},
          "each destination invokes only its host action, in order");
    check(std::string_view(rows[1].description).find("sound") == std::string_view::npos,
          "Settings must not promise unavailable sound settings");
    check(std::string_view(rows[4].description) == "Exit the game",
          "Quit describes application exit");

    if (failures == 0) std::puts("Launcher menu: all cases passed");
    return failures == 0 ? 0 : 1;
}

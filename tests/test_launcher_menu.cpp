#include "ui/lambo_launcher_menu.h"

#include <cstdio>
#include <string_view>

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

    if (failures == 0) std::puts("Launcher menu: all cases passed");
    return failures == 0 ? 0 : 1;
}

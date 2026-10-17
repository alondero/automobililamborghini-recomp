// Exercise the production ModMenu refresh/update methods without a renderer.
#include "librecomp/mods.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool running = false;
int scans = 0, releases = 0, fetches = 0, deprecation_queries = 0;
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
}

namespace ultramodern {
bool is_game_started() { return running; }
}

namespace recomp::mods {
void scan_mods() { ++scans; }
std::vector<ModDetails> get_all_mod_details(const std::string&) { ++fetches; return {}; }
bool is_mod_auto_enabled(const std::string&) { return false; }
bool is_mod_deprecated(const std::string&, const recomp::Version& version) {
    require(version.major == 7, "deprecation query lost the selected mod's version");
    ++deprecation_queries;
    return false;
}
}

namespace recompui {
enum class EventType { Update };
struct Event { EventType type; };
void release_image(const std::string&) { ++releases; }
struct Button {
    bool enabled = false;
    void set_enabled(bool value) { enabled = value; }
};
struct Panel {
    Button toggle;
    Button* get_enable_toggle() { return &toggle; }
};
struct ModMenu {
    Button install, refresh;
    Panel panel;
    Button *install_mods_button = &install, *refresh_button = &refresh;
    Panel* mod_details_panel = &panel;
    std::vector<recomp::mods::ModDetails> mod_details;
    std::vector<std::string> loaded_thumbnails = {"thumbnail"};
    std::string game_mod_id = "lamborghini";
    bool mods_dirty = false, mod_scan_queued = false;
    int active_mod_index = -1, rebuilds = 0;
    void refresh_mods(bool);
    void process_event(const Event&);
    void create_mod_list() { ++rebuilds; }
};

#include "mod_menu_refresh_methods.inl"
}

int main() {
    using namespace recompui;
    ModMenu menu;
    running = true;
    menu.process_event({EventType::Update});
    require(menu.refresh.enabled, "Refresh is disabled during gameplay");
    require(!menu.install.enabled, "Installing mods during gameplay must stay disabled");
    menu.refresh_mods(true);
    require(scans == 1 && releases == 1 && fetches == 1 && menu.rebuilds == 1,
            "Refresh during gameplay must discover packages and rebuild the list");

    running = false;
    menu.process_event({EventType::Update});
    require(menu.refresh.enabled && menu.install.enabled,
            "Launcher controls must recover after a failed game start");
    menu.active_mod_index = 0;
    menu.refresh_mods(true);
    require(scans == 2 && fetches == 2 && releases == 2 && menu.rebuilds == 2,
            "Launcher refresh must rescan packages and rebuild the list");
    require(menu.active_mod_index == -1,
            "Removing the last mod must clear its selection before rebuilding");
    menu.process_event({EventType::Update}); // Must not index the now empty list.

    running = true;
    menu.mods_dirty = true;
    menu.mod_scan_queued = false;
    menu.process_event({EventType::Update});
    require(scans == 2 && fetches == 3 && menu.rebuilds == 3,
            "Internal updates during gameplay must still rebuild without rescanning");
    require(!menu.mods_dirty && !menu.mod_scan_queued, "Queued update flags must be cleared");

    recomp::mods::ModDetails selected{};
    selected.mod_id = "texture";
    selected.version = recomp::Version{7, 0, 0};
    selected.runtime_toggleable = true;
    menu.mod_details.push_back(selected);
    menu.active_mod_index = 0;
    menu.process_event({EventType::Update});
    require(menu.panel.toggle.enabled && deprecation_queries == 1,
            "selected texture pack must allow toggling during play");
    menu.mod_details[0].runtime_toggleable = false;
    menu.process_event({EventType::Update});
    require(!menu.panel.toggle.enabled, "startup-only pack must stay locked during play");
    running = false;
    menu.process_event({EventType::Update});
    require(menu.panel.toggle.enabled, "launcher must allow selecting startup-only packs");
    std::cout << "Mod menu refresh checks passed\n";
}

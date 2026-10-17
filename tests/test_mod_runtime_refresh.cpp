#include "librecomp/mods.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>

// The host normally supplies this through recomp::start. No game is run here.
extern recomp::Version project_version;

namespace {
std::map<std::string, int> enables, disables;
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
}

int main(int argc, char** argv) {
    require(argc == 2, "expected an isolated fixture directory");
    const std::filesystem::path root = argv[1];
    const auto mods = root / "mods";
    project_version = recomp::Version{1, 0, 0};
    recomp::register_config_path(root);
    recomp::mods::ModContext context;
    context.register_game("lamborghini");
    context.set_mods_config_path(root / "mods.json");
    context.set_mod_config_directory(root / "mod_config");
    const auto textures = context.register_content_type({
        .content_filename = "rt64.json",
        .allow_runtime_toggle = true,
        .on_enabled = [](auto&, const auto& mod) { ++enables[mod.manifest.mod_id]; },
        .on_disabled = [](auto&, const auto& mod) { ++disables[mod.manifest.mod_id]; },
        .on_reordered = nullptr,
    });
    require(context.register_container_type("rtz", {textures}, false), "RTZ registration failed");
    require(context.scan_mod_folder(mods).empty(), "initial scan failed");
    context.load_mods_config();
    context.enable_mod("base", true, false); // An unsaved choice must survive refresh.
    context.enable_mod("startup_dependent", true, false);
    require(context.is_mod_enabled("startup_dependent"), "launcher must allow selecting a dependency pack");
    context.enable_mod("startup_dependent", false, false);
    auto* base_config = context.get_mod_config("base");
    const auto base_order = context.get_mod_order_index("base");
    recomp::GameEntry game{};
    game.mod_game_id = "lamborghini";
    uint32_t ram_used = 0;
    require(context.load_mods(game, "", nullptr, 0, ram_used).empty(), "texture-only startup failed");
    require(enables["base"] == 1, "initial pack callback was not called");

    // Packages present but disabled at startup must have the same live-toggle
    // restrictions as packages discovered later, before anyone presses Refresh.
    for (const auto* id : {"startup_dependent", "startup_future", "startup_mode", "startup_native", "startup_code"}) {
        const auto details = context.get_details_for_mod(id);
        require(details.has_value() && !details->runtime_toggleable,
                "startup-only package is runtime-toggleable before Refresh");
        context.enable_mod(id, true, false);
        require(!context.is_mod_enabled(id) && enables[id] == 0,
                "startup-only package was enabled live before Refresh");
    }

    for (const auto& entry : std::filesystem::directory_iterator(root / "incoming")) {
        std::filesystem::copy(entry.path(), mods / entry.path().filename(),
                              std::filesystem::copy_options::recursive);
    }
    // Force growth beyond vector capacity; existing Config pointers must survive.
    for (int i = 0; i < 64; ++i) {
        std::filesystem::copy_file(root / "incoming/fresh.rtz", mods / ("extra" + std::to_string(i) + ".rtz"));
    }
    require(context.scan_mod_folder(mods).empty(), "live scan failed");
    context.load_mods_config(); // This is also called by the public scan_mods wrapper.
    require(context.get_details_for_mod("fresh").has_value(), "live refresh did not discover the RTZ");
    require(context.is_mod_enabled("base") && !context.is_mod_enabled("off"), "refresh lost current choices");
    require(context.get_mod_config("base") == base_config, "refresh invalidated an existing Config pointer");
    require(context.get_mod_order_index("base") == base_order, "refresh reordered an existing mod");
    require(enables["base"] == 1 && disables["base"] == 0, "refresh reloaded an active pack");
    require(!context.is_mod_enabled("fresh"), "live refresh auto-enabled a newly discovered pack");
    context.enable_mod("fresh", true, false);
    context.enable_mod("fresh", false, false);
    require(enables["fresh"] == 1 && disables["fresh"] == 1, "new pack cannot be toggled live");
    for (const auto* id : {"future", "dependent", "mode", "native", "code"}) {
        const auto details = context.get_details_for_mod(id);
        require(details.has_value() && !details->runtime_toggleable, "startup-only package allowed a live toggle");
        context.enable_mod(id, true, false);
        require(!context.is_mod_enabled(id) && enables[id] == 0, "startup-only package was enabled live");
    }
    const auto count = context.num_opened_mods();
    require(context.scan_mod_folder(mods).empty() && context.num_opened_mods() == count,
            "repeated refresh duplicated packages");
    context.enable_mod("fresh", true, false);
    context.set_mod_index("lamborghini", "fresh", 0);
    require(context.get_mod_order_index("fresh") == 0, "new pack cannot be reordered");
    // Removing a package on disk must keep the active handle until restart.
    // Use a loose package: Windows keeps open archive files locked on disk.
    std::filesystem::remove_all(mods / "base");
    require(context.scan_mod_folder(mods).empty() && context.is_mod_enabled("base"),
            "live refresh discarded a removed but active package");
    context.enable_mod("base", false, false);
    require(disables["base"] == 1, "removed active package lost its disable callback");
    context.enable_mod("fresh", false, false);
    require(disables["fresh"] == 2, "refresh lost the new pack's disable callback");
    context.unload_mods();
    require(context.scan_mod_folder(mods).empty() && !context.get_details_for_mod("base"),
            "full scan after unload did not remove a missing package");
    std::cout << "Runtime live mod discovery checks passed\n";
}

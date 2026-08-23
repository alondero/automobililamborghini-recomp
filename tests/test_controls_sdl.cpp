#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include <SDL.h>

#include "controls/lambo_controls_sdl.h"
#include "ultramodern/config.hpp"

namespace ultramodern::renderer {
void set_graphics_config(const GraphicsConfig&) {}
}

int main() {
    const auto path = std::filesystem::temp_directory_path() /
        ("lambo-controls-sdl-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
#ifdef _WIN32
    _putenv_s("LAMBO_CONTROLS_CONFIG", path.string().c_str());
#else
    setenv("LAMBO_CONTROLS_CONFIG", path.string().c_str(), 1);
#endif
    lambo::controls::SdlAdapter adapter;
    adapter.open_existing();
    adapter.process_commands();
    const auto state = adapter.sample();
    const auto snapshot = lambo::controls::ui_snapshot();
    bool ok = state == lambo::controls::EvaluatedState{} &&
              !snapshot.selected_instance.has_value() && snapshot.controllers.empty();
    if (!ok) std::cerr << "FAIL: missing-controller adapter state is not neutral\n";
    adapter.shutdown();
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return ok ? 0 : 1;
}

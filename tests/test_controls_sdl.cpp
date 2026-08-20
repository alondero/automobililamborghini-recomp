#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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
    bool ok = SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0;
    if (!ok) std::cerr << "FAIL: SDL game-controller initialization failed: " << SDL_GetError() << '\n';

    {
        lambo::controls::SdlAdapter adapter;
        adapter.process_commands();
        const auto state = adapter.sample();
        const auto snapshot = lambo::controls::ui_snapshot();
        const bool neutral = state == lambo::controls::EvaluatedState{} &&
                             !snapshot.selected_instance.has_value() && snapshot.controllers.empty();
        if (!neutral) std::cerr << "FAIL: missing-controller adapter state is not neutral\n";
        ok = ok && neutral;
        adapter.shutdown();
    }

    SDL_VirtualJoystickDesc description{};
    description.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    description.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    description.naxes = SDL_CONTROLLER_AXIS_MAX;
    description.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    description.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1u;
    description.button_mask = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1u;
    description.name = "Lambo analog disconnect test controller";
    const int device_index = SDL_JoystickAttachVirtualEx(&description);
    if (device_index < 0 || !SDL_IsGameController(device_index)) {
        std::cerr << "FAIL: virtual game controller could not be attached: " << SDL_GetError() << '\n';
        ok = false;
    } else {
        char guid[33]{};
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device_index), guid, sizeof(guid));
        std::ofstream config(path, std::ios::trunc);
        config << "{\"version\":1,\"preferred_controller_guid\":\"" << guid
               << "\",\"profiles\":{\"" << guid
               << "\":{\"throttle\":{\"mode\":\"analog\",\"source\":null,"
                  "\"deadzone\":0.05,\"saturation\":1.0}}}}";
        config.close();

        const SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID(device_index);
        lambo::controls::SdlAdapter adapter;
        adapter.device_added(device_index);
        const auto connected = adapter.sample();
        const bool analog_connected = connected.throttle_mode == lambo::controls::ThrottleMode::Analog;
        if (!analog_connected) std::cerr << "FAIL: virtual controller did not load its analog profile\n";
        ok = ok && analog_connected;

        SDL_JoystickDetachVirtual(device_index);
        adapter.device_removed(instance);
        const auto disconnected = adapter.sample();
        const bool neutral_disconnect =
            disconnected.throttle_mode == lambo::controls::ThrottleMode::Analog &&
            disconnected.throttle == 0.0f;
        if (!neutral_disconnect) {
            std::cerr << "FAIL: analog controller disconnect did not retain mode and publish neutral\n";
        }
        ok = ok && neutral_disconnect;
        adapter.shutdown();
    }
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return ok ? 0 : 1;
}

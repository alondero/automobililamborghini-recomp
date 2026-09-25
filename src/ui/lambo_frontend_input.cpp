#include "lambo_frontend_input.h"
#include "lambo_config.h"
#include "lambo_driving_assists.h"
#include "lambo_input_gate.h"
#include "recompinput/profiles.h"
#include "recompinput/input_events.h"
#include "recompui/config.h"
#include <mutex>
#include <SDL.h>

namespace lambo::ui {
namespace {
std::mutex pedal_mutex;
lambo::controls::Profile pedal_profile = lambo::controls::default_profile();
lambo::driving::Settings driving_settings;
lambo::driving::TiltSteering tilt;
SDL_Sensor* phone_gyro = nullptr;
SDL_Sensor* phone_accel = nullptr;
bool sensors_initialized = false;
Uint32 sensor_retry = 0, sensor_sample_time = 0;
Uint32 gyro_event_time = 0, accel_event_time = 0;
bool have_gyro_event = false, have_accel_event = false;
bool motion_valid = false;

void close_phone_sensors() {
    if (phone_gyro) SDL_SensorClose(phone_gyro);
    if (phone_accel) SDL_SensorClose(phone_accel);
    phone_gyro = phone_accel = nullptr;
    if (sensors_initialized) SDL_QuitSubSystem(SDL_INIT_SENSOR);
    sensors_initialized = false;
    sensor_sample_time = 0;
    have_gyro_event = have_accel_event = false;
    motion_valid = false;
    tilt.reset();
}

void read_driving_settings() {
    auto& page = recompui::config::get_config("driving-controls");
    driving_settings.gyro = std::get<bool>(page.get_option_value("gyro"));
    driving_settings.auto_accelerate = std::get<bool>(page.get_option_value("auto_accelerate"));
    driving_settings.invert = std::get<bool>(page.get_option_value("gyro_invert"));
    driving_settings.full_lock_degrees = float(std::get<double>(page.get_option_value("gyro_range")));
    driving_settings.deadzone_degrees = float(std::get<double>(page.get_option_value("gyro_deadzone")));
    // Settings and sensor sampling both run under frontend_mutex().
    tilt.reset();
    motion_valid = false;
}
using namespace recompinput;
using recomp::config::ConfigOptionEnumOption;
constexpr GameInput targets[] = {GameInput::A, GameInput::B, GameInput::Z, GameInput::START,
    GameInput::L, GameInput::R, GameInput::DPAD_UP, GameInput::DPAD_DOWN, GameInput::DPAD_LEFT,
    GameInput::DPAD_RIGHT, GameInput::C_UP, GameInput::C_DOWN, GameInput::C_LEFT, GameInput::C_RIGHT};

void read_pedals() {
    auto& page = recompui::config::get_config("pedals");
    std::lock_guard lock(pedal_mutex);
    auto apply = [&page](const std::string& prefix, auto& pedal) {
        pedal.mode = static_cast<decltype(pedal.mode)>(std::get<uint32_t>(page.get_option_value(prefix + "mode")));
        const uint32_t axis = std::get<uint32_t>(page.get_option_value(prefix + "axis"));
        if (axis == 0) pedal.source.reset();
        else {
            pedal.source.emplace();
            pedal.source->axis = static_cast<lambo::controls::LogicalAxis>(axis - 1);
            pedal.source->direction = std::get<bool>(page.get_option_value(prefix + "negative")) ?
                lambo::controls::AxisDirection::Negative : lambo::controls::AxisDirection::Positive;
        }
        pedal.deadzone = float(std::get<double>(page.get_option_value(prefix + "deadzone")));
        pedal.saturation = float(std::get<double>(page.get_option_value(prefix + "saturation")));
    };
    apply("throttle_", pedal_profile.throttle);
    apply("brake_", pedal_profile.brake);
}
}

void create_frontend_driving_settings() {
    auto& page = recompui::config::create_config_tab("Controls", "driving-controls", true);
    page.add_bool_option("gyro", "Gyro steering", "Player one only, during races. Tilt your Android phone like a steering wheel. Requires a phone gyroscope and accelerometer; keep the screen upright. Hold it comfortably when starting or resuming a race to center steering. Button mappings are in Button bindings.", false);
    page.add_bool_option("auto_accelerate", "Auto-accelerate", "Accelerate automatically during races. Hold the brake to stop automatic acceleration. Player one only; off in menus and while paused.", false);
    page.add_number_option("gyro_range", "Tilt for full steering (degrees)", "Smaller angles make steering more sensitive.", 15, 90, 5, 0, false, 35);
    page.add_number_option("gyro_deadzone", "Gyro deadzone (degrees)", "Ignore small movements around the center.", 0, 10, 1, 0, false, 2);
    page.add_bool_option("gyro_invert", "Invert gyro steering", "Reverse the steering direction.", false);
    page.set_load_callback(read_driving_settings);
    page.set_save_callback(read_driving_settings);
}

void driving_sensor_event(const SDL_Event& event) {
    if (event.type != SDL_SENSORUPDATE) return;
    // Android's SDL2 backend supplies zero hardware timestamps. Event freshness
    // uses SDL's millisecond clock instead, including unsigned wraparound.
    if (phone_gyro && event.sensor.which == SDL_SensorGetInstanceID(phone_gyro)) {
        gyro_event_time = event.sensor.timestamp;
        have_gyro_event = true;
    }
    if (phone_accel && event.sensor.which == SDL_SensorGetInstanceID(phone_accel)) {
        accel_event_time = event.sensor.timestamp;
        have_accel_event = true;
    }
}

void sample_frontend_driving_assists() {
    const bool active = lambo::driving::racing() && !lambo::input_gate::guest_input_suppressed() &&
                        SDL_GetKeyboardFocus() != nullptr;
    lambo::driving::Demand demand{};
    demand.auto_accelerate = active && driving_settings.auto_accelerate;
    const Uint32 now = SDL_GetTicks();
    if (!active || !driving_settings.gyro) {
        close_phone_sensors();
        sensor_retry = now - 1000u;
    } else {
        if (!phone_gyro && now - sensor_retry >= 1000u) {
            sensor_retry = now;
            sensors_initialized = SDL_InitSubSystem(SDL_INIT_SENSOR) == 0;
            if (sensors_initialized) {
                for (int i = 0; i < SDL_NumSensors(); ++i) {
                    const auto type = SDL_SensorGetDeviceType(i);
                    if (!phone_gyro && type == SDL_SENSOR_GYRO) phone_gyro = SDL_SensorOpen(i);
                    if (!phone_accel && type == SDL_SENSOR_ACCEL) phone_accel = SDL_SensorOpen(i);
                }
            }
            if (!phone_gyro || !phone_accel) close_phone_sensors();
            sensor_sample_time = now;
        }
        if (phone_gyro && phone_accel) {
            float gyro[3]{}, accel[3]{};
            const float dt = float(now - sensor_sample_time) / 1000.0f;
            if (dt > 0) {
                sensor_sample_time = now;
                const bool fresh = have_gyro_event && have_accel_event &&
                    now - gyro_event_time < 250u && now - accel_event_time < 250u;
                if (fresh && SDL_SensorGetData(phone_gyro, gyro, 3) == 0 &&
                    SDL_SensorGetData(phone_accel, accel, 3) == 0) {
                    motion_valid = tilt.sample(gyro[2], accel[0], accel[1], dt);
                } else {
                    tilt.reset();
                    motion_valid = false;
                }
            }
            demand.gyro_valid = motion_valid;
            demand.steering = tilt.steering(driving_settings);
        }
    }
    lambo::driving::publish(demand);
}

void create_frontend_pedal_settings() {
    const auto legacy = lambo::controls::load_config();
    pedal_profile = lambo::controls::profile_for_guid(legacy.config, legacy.config.preferred_controller_guid);
    auto& page = recompui::config::create_config_tab("Pedals", "pedals", true);
    auto add = [&page](const std::string& prefix, const std::string& label, const auto& pedal) {
        page.add_enum_option(prefix + "mode", label + " mode", "Analog pedals affect player one during races; digital N64 bindings remain available in menus.",
            {{0, "Digital"}, {1, "Analog"}}, uint32_t(pedal.mode));
        std::vector<ConfigOptionEnumOption> axes{{0, "None"}};
        const char* names[] = {"LX", "LY", "RX", "RY", "LT", "RT"};
        for (int axis = 0; axis < 6; ++axis) axes.emplace_back(axis + 1, names[axis]);
        uint32_t selected = pedal.source ? 1 + uint32_t(pedal.source->axis) : 0;
        page.add_enum_option(prefix + "axis", label + " source", "Uses the controller assigned to player one in Button bindings.", axes, selected);
        page.add_bool_option(prefix + "negative", label + " negative half-axis", "Use the negative direction of a stick axis. Leave off for triggers.",
            pedal.source && pedal.source->direction == lambo::controls::AxisDirection::Negative);
        page.add_number_option(prefix + "deadzone", label + " deadzone", "Input below this level is ignored.", 0, .95, .01, 2, false, pedal.deadzone);
        page.add_number_option(prefix + "saturation", label + " saturation", "Input at this level reaches full demand.", .05, 1, .01, 2, false, pedal.saturation);
    };
    add("throttle_", "Throttle", pedal_profile.throttle);
    add("brake_", "Brake", pedal_profile.brake);
    page.set_load_callback(read_pedals);
    page.set_save_callback(read_pedals);
}

void configure_frontend_input_defaults() {
    using namespace lambo::controls;
    const auto defaults = default_profile();
    for (size_t target = 0; target < digital_target_count; ++target) {
        std::vector<InputField> fields;
        for (const auto& binding : defaults.digital[target]) {
            if (const auto* button = std::get_if<ButtonSource>(&binding))
                fields.push_back(InputField::controller_digital(SDL_GameControllerButton(button->button)));
            else if (const auto* axis = std::get_if<AxisHalfSource>(&binding))
                fields.push_back(InputField::controller_analog(SDL_GameControllerAxis(axis->axis), axis->direction == AxisDirection::Positive));
        }
        set_default_mapping_for_controller(targets[target], fields);
    }
}

void initialize_frontend_controllers() {
    const auto legacy = lambo::controls::load_config();
    SDL_GameController* selected = nullptr;
    // Share the framework hotplug path so sensors and profile registration agree.
    for (int device = 0; device < SDL_NumJoysticks(); ++device) {
        if (!SDL_IsGameController(device)) continue;
        char guid[33]{};
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device), guid, sizeof(guid));
        // Match the legacy SDL GUID against the actual device. Never replace a
        // profile choice already saved by the framework.
        if (auto* probe = SDL_GameControllerOpen(device)) {
            const auto identity = profiles::get_guid_from_sdl_controller(probe);
            const int imported = profiles::get_input_profile_by_key(std::string("legacy-") + guid);
            if (imported >= 0 && profiles::get_controller_by_guid(identity) < 0)
                profiles::add_controller(identity, imported);
            SDL_GameControllerClose(probe);
        }
        SDL_Event added{};
        added.type = SDL_CONTROLLERDEVICEADDED;
        added.cdevice.which = device;
        handle_event(added);
        auto* controller = get_controller_from_joystick_id(SDL_JoystickGetDeviceInstanceID(device));
        if (!controller) continue;
        if (!selected || legacy.config.preferred_controller_guid == guid) selected = controller;
    }
    playerassignment::start();
    if (selected) playerassignment::add_controller_player(selected);
    else playerassignment::add_keyboard_player();
    playerassignment::commit_player_assignment();
}

lambo::controls::EvaluatedState sample_frontend_pedals() {
    if (!players::get_player_is_assigned(0)) return {};
    auto* controller = players::get_player(0).controller;
    if (!controller || !SDL_GameControllerGetAttached(controller)) return {};
    lambo::controls::RawState raw{};
    for (int axis = 0; axis < 6; ++axis) raw.axes[axis] = SDL_GameControllerGetAxis(controller, SDL_GameControllerAxis(axis));
    std::lock_guard lock(pedal_mutex);
    return lambo::controls::evaluate(pedal_profile, raw);
}

void import_frontend_profiles() {
    using namespace lambo::controls;
    const auto legacy = load_config();
    if (legacy.status != LoadStatus::Loaded) return;
    for (const auto& [guid, profile] : legacy.config.profiles) {
        const int index = profiles::add_input_profile("legacy-" + guid, "Imported " + guid.substr(0, 8), InputDevice::Controller, true);
        profiles::reset_profile_bindings(index, InputDevice::Controller);
        for (size_t target = 0; target < digital_target_count; ++target) {
            profiles::clear_input_binding(index, targets[target]);
            const auto& bindings = profile.digital[target];
            for (size_t i = 0; i < std::min(bindings.size(), num_bindings_per_input); ++i) {
                InputField field{};
                if (const auto* button = std::get_if<ButtonSource>(&bindings[i]))
                    field = InputField::controller_digital(SDL_GameControllerButton(button->button));
                else if (const auto* axis = std::get_if<AxisHalfSource>(&bindings[i]))
                    field = InputField::controller_analog(SDL_GameControllerAxis(axis->axis), axis->direction == AxisDirection::Positive);
                profiles::set_input_binding(index, targets[target], i, field);
            }
        }
        for (size_t axis = 0; axis < 2; ++axis) {
            const GameInput pos = axis == 0 ? GameInput::X_AXIS_POS : GameInput::Y_AXIS_POS;
            const GameInput neg = axis == 0 ? GameInput::X_AXIS_NEG : GameInput::Y_AXIS_NEG;
            profiles::clear_input_binding(index, pos);
            profiles::clear_input_binding(index, neg);
            if (profile.analog[axis]) {
                const auto& source = *profile.analog[axis];
                const bool positive = !source.invert;
                profiles::set_input_binding(index, pos, 0, InputField::controller_analog(SDL_GameControllerAxis(source.axis), positive));
                profiles::set_input_binding(index, neg, 0, InputField::controller_analog(SDL_GameControllerAxis(source.axis), !positive));
            }
        }
    }
    profiles::save_controls_config(lambo::config::app_config_dir() / "controls-framework.json");
}
}

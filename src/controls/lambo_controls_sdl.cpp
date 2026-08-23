#include "lambo_controls_sdl.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include <SDL.h>

#include "lambo_log.h"

namespace {

using namespace lambo::controls;

std::optional<LogicalButton> logical_button(std::uint8_t button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A: return LogicalButton::A;
        case SDL_CONTROLLER_BUTTON_B: return LogicalButton::B;
        case SDL_CONTROLLER_BUTTON_X: return LogicalButton::X;
        case SDL_CONTROLLER_BUTTON_Y: return LogicalButton::Y;
        case SDL_CONTROLLER_BUTTON_BACK: return LogicalButton::Back;
        case SDL_CONTROLLER_BUTTON_GUIDE: return LogicalButton::Guide;
        case SDL_CONTROLLER_BUTTON_START: return LogicalButton::Start;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK: return LogicalButton::LeftStick;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return LogicalButton::RightStick;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return LogicalButton::LeftShoulder;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return LogicalButton::RightShoulder;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return LogicalButton::DpadUp;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return LogicalButton::DpadDown;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return LogicalButton::DpadLeft;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return LogicalButton::DpadRight;
        default: return std::nullopt;
    }
}

std::optional<LogicalAxis> logical_axis(std::uint8_t axis) {
    switch (axis) {
        case SDL_CONTROLLER_AXIS_LEFTX: return LogicalAxis::LeftX;
        case SDL_CONTROLLER_AXIS_LEFTY: return LogicalAxis::LeftY;
        case SDL_CONTROLLER_AXIS_RIGHTX: return LogicalAxis::RightX;
        case SDL_CONTROLLER_AXIS_RIGHTY: return LogicalAxis::RightY;
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT: return LogicalAxis::TriggerLeft;
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return LogicalAxis::TriggerRight;
        default: return std::nullopt;
    }
}

ControllerLayout controller_layout(SDL_GameController* controller) {
    switch (SDL_GameControllerGetType(controller)) {
        case SDL_CONTROLLER_TYPE_XBOX360:
        case SDL_CONTROLLER_TYPE_XBOXONE: return ControllerLayout::Xbox;
        case SDL_CONTROLLER_TYPE_PS3:
        case SDL_CONTROLLER_TYPE_PS4:
        case SDL_CONTROLLER_TYPE_PS5: return ControllerLayout::PlayStation;
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: return ControllerLayout::Nintendo;
        default: return ControllerLayout::Generic;
    }
}

std::string controller_guid(SDL_GameController* controller) {
    char text[33]{};
    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), text, sizeof(text));
    const auto canonical = canonicalize_guid(text);
    return canonical.value_or(std::string{});
}

} // namespace

namespace lambo::controls {

struct SdlAdapter::Impl {
    struct Device {
        SDL_GameController* handle{};
        std::int32_t instance{};
        std::string guid;
        std::string name;
        ControllerLayout layout{ControllerLayout::Generic};
    };

    ControlsConfig config;
    std::vector<std::string> warnings;
    std::vector<Device> devices;
    std::optional<std::int32_t> selected;
    Capture capture;
    RawState raw;
    EvaluatedState evaluated;
    std::uint64_t config_revision{1};
    std::uint64_t sample_revision{};
    bool saved{};
    std::string status;
    bool rumble_on{};
    ThrottleMode disconnected_throttle_mode{ThrottleMode::Digital};
    Profile fallback_profile{default_profile()};

    Device* selected_device() {
        if (!selected) return nullptr;
        auto found = std::find_if(devices.begin(), devices.end(),
            [&](const Device& device) { return device.instance == *selected; });
        return found == devices.end() ? nullptr : &*found;
    }
    const Device* selected_device() const {
        return const_cast<Impl*>(this)->selected_device();
    }
    Profile& selected_profile() {
        Device* device = selected_device();
        return device ? profile_for_guid(config, device->guid) : fallback_profile;
    }
    void stop_rumble() {
        if (Device* device = selected_device(); device && device->handle) {
            SDL_GameControllerRumble(device->handle, 0, 0, 0);
        }
        rumble_on = false;
    }
    void choose(std::optional<std::int32_t> instance, bool persist) {
        if (selected == instance) return;
        stop_rumble();
        capture.cancel();
        selected = instance;
        if (Device* device = selected_device()) {
            profile_for_guid(config, device->guid);
            if (persist) {
                config.preferred_controller_guid = device->guid;
                save();
            }
        }
        ++config_revision;
    }
    void choose_startup() {
        if (selected_device()) return;
        auto preferred = std::find_if(devices.begin(), devices.end(), [&](const Device& device) {
            return !config.preferred_controller_guid.empty() && device.guid == config.preferred_controller_guid;
        });
        choose(preferred != devices.end() ? std::optional{preferred->instance} :
               devices.empty() ? std::nullopt : std::optional{devices.front().instance}, false);
    }
    void save() {
        const SaveResult result = save_config(config);
        saved = result.saved;
        status = result.saved ? "Saved" : "Save failed: " + result.error + " (" + result.path.string() + ")";
    }
    void mutation_finished() {
        ++config_revision;
        save();
    }
    void publish() {
        UiSnapshot snapshot{};
        snapshot.selected_instance = selected;
        if (const Device* selected_ptr = selected_device()) snapshot.selected_guid = selected_ptr->guid;
        snapshot.profile = selected_device() ? profile_for_guid(config, snapshot.selected_guid) : default_profile();
        snapshot.raw = raw;
        snapshot.evaluated = evaluated;
        snapshot.capture_phase = capture.phase();
        snapshot.capture_target = capture.target();
        snapshot.config_revision = config_revision;
        snapshot.sample_revision = sample_revision;
        snapshot.read_only = config.read_only;
        snapshot.saved = saved;
        snapshot.config_path = controls_config_path();
        snapshot.status = status;
        snapshot.warnings = warnings;
        bool duplicate = false;
        for (std::size_t target = 0; target < digital_target_count && !duplicate; ++target) {
            for (const auto& source : snapshot.profile.digital[target]) {
                if (has_cross_target_duplicate(snapshot.profile, static_cast<Target>(target), source)) {
                    duplicate = true;
                    break;
                }
            }
        }
        duplicate = duplicate || (snapshot.profile.analog[0] && snapshot.profile.analog[1] &&
            snapshot.profile.analog[0]->axis == snapshot.profile.analog[1]->axis);
        if (snapshot.profile.throttle.source) {
            duplicate = duplicate || std::any_of(snapshot.profile.analog.begin(),
                snapshot.profile.analog.end(), [&](const std::optional<AnalogSource>& analog) {
                    return analog && snapshot.profile.throttle.source->axis == analog->axis;
                });
        }
        if (duplicate) snapshot.warnings.emplace_back(
            "One or more controller sources are intentionally assigned to multiple N64 targets.");
        for (const auto& device : devices) {
            snapshot.controllers.push_back({device.instance, device.guid, device.name, device.layout,
                                            selected && device.instance == *selected});
        }
        publish_ui_snapshot(std::move(snapshot));
    }
};

SdlAdapter::SdlAdapter() : impl_(std::make_unique<Impl>()) {
    LoadResult loaded = load_config();
    impl_->config = std::move(loaded.config);
    impl_->warnings = std::move(loaded.warnings);
    switch (loaded.status) {
        case LoadStatus::Missing: impl_->status = "Defaults (not saved yet)"; break;
        case LoadStatus::Loaded: impl_->status = "Loaded"; impl_->saved = true; break;
        case LoadStatus::Malformed: impl_->status = "Read-only: malformed controls.json"; break;
        case LoadStatus::InvalidVersion: impl_->status = "Read-only: invalid controls.json version"; break;
        case LoadStatus::FutureVersion: impl_->status = "Read-only: controls.json is from a newer version"; break;
    }
    impl_->publish();
}

SdlAdapter::~SdlAdapter() { shutdown(); }

void SdlAdapter::open_existing() {
    for (int index = 0; index < SDL_NumJoysticks(); ++index) device_added(index);
    auto preferred = std::find_if(impl_->devices.begin(), impl_->devices.end(), [&](const Impl::Device& device) {
        return !impl_->config.preferred_controller_guid.empty() &&
               device.guid == impl_->config.preferred_controller_guid;
    });
    if (preferred != impl_->devices.end()) impl_->choose(preferred->instance, false);
    else impl_->choose_startup();
    impl_->publish();
}

void SdlAdapter::device_added(int joystick_index) {
    if (!SDL_IsGameController(joystick_index)) return;
    SDL_GameController* handle = SDL_GameControllerOpen(joystick_index);
    if (!handle) return;
    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(handle);
    const std::int32_t instance = SDL_JoystickInstanceID(joystick);
    if (std::any_of(impl_->devices.begin(), impl_->devices.end(),
                    [&](const Impl::Device& device) { return device.instance == instance; })) {
        SDL_GameControllerClose(handle);
        return;
    }
    const char* name = SDL_GameControllerName(handle);
    impl_->devices.push_back({handle, instance, controller_guid(handle),
                              name ? name : "Unknown controller", controller_layout(handle)});
    LAMBO_LOG("input", "controller connected: %s (%s, instance %d)\n",
              impl_->devices.back().name.c_str(), impl_->devices.back().guid.c_str(), int(instance));
    impl_->choose_startup();
    ++impl_->config_revision;
    impl_->publish();
}

void SdlAdapter::device_removed(std::int32_t instance) {
    auto found = std::find_if(impl_->devices.begin(), impl_->devices.end(),
        [&](const Impl::Device& device) { return device.instance == instance; });
    if (found == impl_->devices.end()) return;
    const bool was_selected = impl_->selected && *impl_->selected == instance;
    if (was_selected) {
        impl_->disconnected_throttle_mode = impl_->selected_profile().throttle.mode;
        impl_->stop_rumble();
        impl_->capture.cancel();
        impl_->selected.reset();
    }
    SDL_GameControllerClose(found->handle);
    impl_->devices.erase(found);
    if (was_selected && !impl_->devices.empty()) impl_->choose(impl_->devices.front().instance, false);
    ++impl_->config_revision;
    impl_->publish();
}

bool SdlAdapter::handle_capture_event(const SDL_Event& event) {
    if (!impl_->capture.active()) return false;
    if (impl_->capture.phase() == CapturePhase::Conflict) {
        const bool keyboard_cancel = event.type == SDL_KEYDOWN && !event.key.repeat &&
                                     event.key.keysym.sym == SDLK_ESCAPE;
        const bool controller_cancel = event.type == SDL_CONTROLLERBUTTONDOWN &&
            (event.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
             event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK);
        if (keyboard_cancel || controller_cancel) {
            impl_->capture.reject_conflict(); ++impl_->config_revision; impl_->publish();
            return true;
        }
        return false;
    }
    if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym == SDLK_ESCAPE) {
        impl_->capture.cancel(); ++impl_->config_revision; impl_->publish(); return true;
    }
    if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
        const CapturePhase before = impl_->capture.phase();
        if (const auto button = logical_button(event.cbutton.button)) {
            impl_->capture.button_event(event.cbutton.which, *button,
                event.type == SDL_CONTROLLERBUTTONDOWN);
        }
        if (impl_->capture.phase() != before) ++impl_->config_revision;
        impl_->publish();
        return true;
    }
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        const CapturePhase before = impl_->capture.phase();
        if (const auto axis = logical_axis(event.caxis.axis)) {
            impl_->capture.axis_event(event.caxis.which, *axis, event.caxis.value);
        }
        if (impl_->capture.phase() != before) ++impl_->config_revision;
        impl_->publish();
        return true;
    }
    return event.type == SDL_KEYUP;
}

void SdlAdapter::process_commands() {
    for (const Command& command : take_commands()) {
        if (command.kind == CommandKind::CaptureCancel) { impl_->capture.cancel(); ++impl_->config_revision; continue; }
        if (command.kind == CommandKind::Select) {
            const bool exists = std::any_of(impl_->devices.begin(), impl_->devices.end(),
                [&](const Impl::Device& device) { return device.instance == command.instance; });
            if (exists) impl_->choose(command.instance, true);
            continue;
        }
        if (!impl_->selected_device() || impl_->config.read_only) continue;
        Profile& profile = impl_->selected_profile();
        const std::size_t target = static_cast<std::size_t>(command.target);
        bool changed = false;
        switch (command.kind) {
            case CommandKind::Add:
                impl_->capture.begin(command.target, *impl_->selected); ++impl_->config_revision; break;
            case CommandKind::Remove:
                if (target < digital_target_count && command.binding_index < profile.digital[target].size()) {
                    profile.digital[target].erase(profile.digital[target].begin() + command.binding_index); changed = true;
                } break;
            case CommandKind::Threshold:
                if (target < digital_target_count && command.binding_index < profile.digital[target].size()) {
                    if (auto* half = std::get_if<AxisHalfSource>(&profile.digital[target][command.binding_index])) {
                        half->threshold = static_cast<std::int16_t>(std::clamp(command.value, 0, 32767)); changed = true;
                    }
                } break;
            case CommandKind::Deadzone:
                if (command.target == Target::Throttle) {
                    profile.throttle.deadzone = std::clamp(command.value / 1000.0f, 0.0f, 0.5f);
                    profile.throttle.saturation = std::max(
                        profile.throttle.saturation, profile.throttle.deadzone);
                    changed = true;
                } else if (target >= digital_target_count && target < digital_target_count + analog_target_count) {
                    if (!profile.analog[target - digital_target_count]) break;
                    profile.analog[target - digital_target_count]->deadzone =
                        static_cast<std::int16_t>(std::clamp(command.value, 0, 32767)); changed = true;
                } break;
            case CommandKind::Invert:
                if (command.target == Target::Throttle && profile.throttle.source) {
                    profile.throttle.source->direction =
                        profile.throttle.source->direction == AxisDirection::Positive
                            ? AxisDirection::Negative : AxisDirection::Positive;
                    changed = true;
                } else if (target >= digital_target_count && target < digital_target_count + analog_target_count) {
                    auto& source = profile.analog[target - digital_target_count];
                    if (source) { source->invert = !source->invert; changed = true; }
                } break;
            case CommandKind::Saturation:
                if (command.target == Target::Throttle) {
                    profile.throttle.saturation = std::clamp(
                        command.value / 1000.0f, profile.throttle.deadzone, 1.0f);
                    changed = true;
                } break;
            case CommandKind::ThrottleMode:
                if (command.target == Target::Throttle) {
                    profile.throttle.mode = command.value != 0
                        ? ThrottleMode::Analog : ThrottleMode::Digital;
                    changed = true;
                } break;
            case CommandKind::ClearThrottleSource:
                if (command.target == Target::Throttle && profile.throttle.source) {
                    profile.throttle.source.reset();
                    changed = true;
                } break;
            case CommandKind::ResetTarget: {
                const Profile defaults = default_profile();
                if (target < digital_target_count) profile.digital[target] = defaults.digital[target];
                else if (command.target == Target::Throttle) profile.throttle = defaults.throttle;
                else if (target < digital_target_count + analog_target_count)
                    profile.analog[target - digital_target_count] = defaults.analog[target - digital_target_count];
                changed = true; break;
            }
            case CommandKind::ResetProfile: profile = default_profile(); changed = true; break;
            case CommandKind::ConflictAccept:
                changed = impl_->capture.accept_conflict(profile) == CaptureResult::Committed; break;
            case CommandKind::ConflictMove:
                changed = impl_->capture.move_conflict(profile) == CaptureResult::Committed; break;
            case CommandKind::Select: case CommandKind::CaptureCancel: break;
        }
        if (changed) impl_->mutation_finished();
    }
    impl_->publish();
}

EvaluatedState SdlAdapter::sample() {
    impl_->raw = {};
    Impl::Device* device = impl_->selected_device();
    if (device &&
        SDL_GameControllerGetAttached(device->handle)) {
        for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; ++button) {
            if (const auto logical = logical_button(static_cast<std::uint8_t>(button))) {
                impl_->raw.buttons[static_cast<std::size_t>(*logical)] =
                    SDL_GameControllerGetButton(device->handle, static_cast<SDL_GameControllerButton>(button)) != 0;
            }
        }
        for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; ++axis) {
            if (const auto logical = logical_axis(static_cast<std::uint8_t>(axis))) {
                impl_->raw.axes[static_cast<std::size_t>(*logical)] =
                    SDL_GameControllerGetAxis(device->handle, static_cast<SDL_GameControllerAxis>(axis));
            }
        }
        const CapturePhase before = impl_->capture.phase();
        const CaptureResult result = impl_->capture.sample(impl_->raw, impl_->selected_profile());
        if (result == CaptureResult::Committed) impl_->mutation_finished();
        else if (result == CaptureResult::Noop || result == CaptureResult::Conflict) ++impl_->config_revision;
        else if (impl_->capture.phase() != before) ++impl_->config_revision;
        impl_->evaluated = evaluate(impl_->selected_profile(), impl_->raw);
        impl_->disconnected_throttle_mode = impl_->evaluated.throttle_mode;
    } else {
        impl_->evaluated = {};
        if (device) {
            impl_->disconnected_throttle_mode = impl_->selected_profile().throttle.mode;
        }
        // Preserve the selected profile's mode while publishing a neutral sample.
        // Analog disconnect must write 0 now, rather than re-enable the ROM's ramp-down.
        impl_->evaluated.throttle_mode = impl_->disconnected_throttle_mode;
    }
    ++impl_->sample_revision;
    if (impl_->capture.phase() != CapturePhase::Idle ||
        (impl_->selected && impl_->config_revision != 0)) {
        impl_->publish();
    }
    return impl_->evaluated;
}

void SdlAdapter::apply_rumble(bool on) {
    Impl::Device* device = impl_->selected_device();
    if (!device || !SDL_GameControllerGetAttached(device->handle)) { impl_->rumble_on = false; return; }
    if (on) SDL_GameControllerRumble(device->handle, 0xFFFF, 0xFFFF, 150);
    else if (impl_->rumble_on) SDL_GameControllerRumble(device->handle, 0, 0, 0);
    impl_->rumble_on = on;
}

bool SdlAdapter::selected_back_pressed(const SDL_Event& event) const {
    if (event.type != SDL_CONTROLLERBUTTONDOWN || event.cbutton.state != SDL_PRESSED ||
        !impl_->selected || event.cbutton.which != *impl_->selected) {
        return false;
    }
    const auto btn = event.cbutton.button;
    return btn == SDL_CONTROLLER_BUTTON_BACK ||
           btn == SDL_CONTROLLER_BUTTON_START ||
           btn == SDL_CONTROLLER_BUTTON_GUIDE;
}

void SdlAdapter::cancel_capture() { impl_->capture.cancel(); ++impl_->config_revision; impl_->publish(); }

void SdlAdapter::shutdown() {
    if (!impl_) return;
    impl_->capture.cancel();
    impl_->stop_rumble();
    for (auto& device : impl_->devices) SDL_GameControllerClose(device.handle);
    impl_->devices.clear();
    impl_->selected.reset();
    impl_->publish();
}

} // namespace lambo::controls

#include "lambo_ui_input.h"

#include <cstdlib>

namespace lambo::ui {

namespace {

constexpr auto initial_repeat_delay = std::chrono::milliseconds(500);
constexpr auto repeat_interval = std::chrono::milliseconds(50);
constexpr int axis_dead_zone = 9000;

size_t index_of(NavigationKey key) {
    return static_cast<size_t>(key);
}

bool repeatable(NavigationKey key) {
    return key == NavigationKey::Up || key == NavigationKey::Down ||
           key == NavigationKey::Left || key == NavigationKey::Right;
}

NavigationKey axis_key(NavigationAxis axis, int value) {
    if (!navigation_axis_active(value)) return NavigationKey::None;
    if (axis == NavigationAxis::Horizontal) {
        return value > 0 ? NavigationKey::Right : NavigationKey::Left;
    }
    return value > 0 ? NavigationKey::Down : NavigationKey::Up;
}

} // namespace

bool navigation_axis_active(int value) {
    return std::abs(value) >= axis_dead_zone;
}

std::vector<NavigationEvent> ControllerNavigation::button_down(NavigationKey key, TimePoint now) {
    if (key == NavigationKey::None || key == NavigationKey::Count) return {};
    const size_t index = index_of(key);
    if (button_held_[index]) return {};
    button_held_[index] = true;
    return press_source(key, now);
}

std::vector<NavigationEvent> ControllerNavigation::button_up(NavigationKey key, TimePoint now) {
    if (key == NavigationKey::None || key == NavigationKey::Count) return {};
    const size_t index = index_of(key);
    if (!button_held_[index]) return {};
    button_held_[index] = false;
    return release_source(key, now);
}

std::vector<NavigationEvent> ControllerNavigation::axis_motion(NavigationAxis axis, int value,
                                                               TimePoint now) {
    const size_t axis_index = static_cast<size_t>(axis);
    const NavigationKey previous = axis_keys_[axis_index];
    const NavigationKey next = axis_key(axis, value);
    if (previous == next) return {};

    std::vector<NavigationEvent> events;
    if (previous != NavigationKey::None) {
        auto released = release_source(previous, now);
        events.insert(events.end(), released.begin(), released.end());
    }
    axis_keys_[axis_index] = next;
    if (next != NavigationKey::None) {
        auto pressed = press_source(next, now);
        events.insert(events.end(), pressed.begin(), pressed.end());
    }
    return events;
}

std::vector<NavigationEvent> ControllerNavigation::update(TimePoint now) {
    if (repeating_key_ == NavigationKey::None || now < next_repeat_) return {};
    next_repeat_ = now + repeat_interval;
    return {{NavigationEventType::Press, repeating_key_}};
}

void ControllerNavigation::reset() {
    button_held_.fill(false);
    source_counts_.fill(0);
    axis_keys_.fill(NavigationKey::None);
    repeating_key_ = NavigationKey::None;
    next_repeat_ = {};
}

std::vector<NavigationEvent> ControllerNavigation::press_source(NavigationKey key, TimePoint now) {
    const size_t index = index_of(key);
    if (source_counts_[index]++ != 0) return {};
    if (repeatable(key)) {
        repeating_key_ = key;
        next_repeat_ = now + initial_repeat_delay;
    }
    return {{NavigationEventType::Press, key}};
}

std::vector<NavigationEvent> ControllerNavigation::release_source(NavigationKey key, TimePoint now) {
    const size_t index = index_of(key);
    if (source_counts_[index] == 0 || --source_counts_[index] != 0) return {};
    if (repeating_key_ == key) {
        repeating_key_ = NavigationKey::None;
        for (NavigationKey candidate : {NavigationKey::Up, NavigationKey::Down,
                                        NavigationKey::Left, NavigationKey::Right}) {
            if (source_counts_[index_of(candidate)] != 0) {
                repeating_key_ = candidate;
                next_repeat_ = now + initial_repeat_delay;
                break;
            }
        }
    }
    return {{NavigationEventType::Release, key}};
}

} // namespace lambo::ui

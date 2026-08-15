#ifndef LAMBO_UI_INPUT_H
#define LAMBO_UI_INPUT_H

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lambo::ui {

enum class NavigationKey : uint8_t {
    None,
    Activate,
    Back,
    Up,
    Down,
    Left,
    Right,
    Count,
};

enum class NavigationEventType : uint8_t {
    Press,
    Release,
};

struct NavigationEvent {
    NavigationEventType type;
    NavigationKey key;

    bool operator==(const NavigationEvent&) const = default;
};

enum class NavigationAxis : uint8_t {
    Horizontal,
    Vertical,
};

bool navigation_axis_active(int value);

class ControllerNavigation {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    std::vector<NavigationEvent> button_down(NavigationKey key, TimePoint now);
    std::vector<NavigationEvent> button_up(NavigationKey key, TimePoint now);
    std::vector<NavigationEvent> axis_motion(NavigationAxis axis, int value, TimePoint now);
    std::vector<NavigationEvent> update(TimePoint now);
    void reset();

private:
    static constexpr size_t key_count = static_cast<size_t>(NavigationKey::Count);

    std::vector<NavigationEvent> press_source(NavigationKey key, TimePoint now);
    std::vector<NavigationEvent> release_source(NavigationKey key, TimePoint now);

    std::array<bool, key_count> button_held_{};
    std::array<uint8_t, key_count> source_counts_{};
    std::array<NavigationKey, 2> axis_keys_{NavigationKey::None, NavigationKey::None};
    NavigationKey repeating_key_ = NavigationKey::None;
    TimePoint next_repeat_{};
};

} // namespace lambo::ui

#endif

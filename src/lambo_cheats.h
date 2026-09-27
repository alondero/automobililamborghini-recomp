#pragma once

#include <array>
#include <cstddef>

namespace lambo::cheats {
enum class Cheat { ExtraVehicles, InfiniteTime, FreezeLapTimers, Count };
struct Description {
    Cheat cheat;
    const char* id;
    const char* name;
    const char* description;
};
inline constexpr std::array<Description, static_cast<std::size_t>(Cheat::Count)> catalog{{
    {Cheat::ExtraVehicles, "extra_vehicles", "Extra vehicles",
     "Select any vehicle on your next left/right selection. Saved unlocks are unchanged. An already selected vehicle stays selected when disabled. Resets when the application closes."},
    {Cheat::InfiniteTime, "infinite_time", "Infinite time",
     "Freeze the remaining countdown from the next timer tick. Disable to resume. Does not restart an expired race. Resets when the application closes."},
    {Cheat::FreezeLapTimers, "freeze_lap_timers", "Freeze lap timers",
     "Freeze lap and time-trial clocks for all players from the next timer tick. Disable to resume. Race results can retain cheated times. Resets when the application closes."},
}};

// UI threads publish flags; game-thread hooks consume them. Session-only,
// initially off: neither a settings file nor a guest save owns these flags.
bool enabled(Cheat cheat);
void set_enabled(Cheat cheat, bool enabled);
}

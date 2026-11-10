#include "lambo_cheats.h"
#include "lambo_settings_revision.h"
#include <atomic>

namespace lambo::cheats {
namespace {
std::array<std::atomic_bool, static_cast<std::size_t>(Cheat::Count)> flags{};
}
bool enabled(Cheat cheat) {
    const auto index = static_cast<std::size_t>(cheat);
    return index < flags.size() && flags[index].load(std::memory_order_relaxed);
}
void set_enabled(Cheat cheat, bool value) {
    const auto index = static_cast<std::size_t>(cheat);
    if (index < flags.size() && flags[index].exchange(value, std::memory_order_relaxed) != value)
        lambo::settings::changed();
}
}

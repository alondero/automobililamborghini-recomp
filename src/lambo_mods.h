#ifndef LAMBO_MODS_H
#define LAMBO_MODS_H

#include <filesystem>
#include <optional>
#include <vector>

namespace lambo::mods {
inline constexpr char game_id[] = "lamborghini";
void register_content();
// Called by the runtime's failed-load callback, after content callbacks stop.
void discard_failed_load();
// Consumed only by the renderer thread. Paths are ordered lowest priority first.
std::optional<std::vector<std::filesystem::path>> take_texture_pack_update();
}

#endif

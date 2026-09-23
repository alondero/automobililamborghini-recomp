#ifndef LAMBO_MODS_H
#define LAMBO_MODS_H

#include <filesystem>
#include <optional>
#include <vector>

namespace lambo::mods {
inline constexpr char game_id[] = "lamborghini";
// Container extension for RT64 texture packs, without the leading dot. The
// runtime rejects extensions containing a dot and prepends the dot itself
// (N64ModernRuntime librecomp/src/mods.cpp, register_container_type).
inline constexpr char texture_pack_container_extension[] = "rtz";
void register_content();
// Called by the runtime's failed-load callback, after content callbacks stop.
void discard_failed_load();
// Consumed only by the renderer thread. Paths are ordered lowest priority first.
std::optional<std::vector<std::filesystem::path>> take_texture_pack_update();
}

#endif

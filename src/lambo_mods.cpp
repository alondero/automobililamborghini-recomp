#include "lambo_mods.h"
#include "lambo_log.h"
#include "librecomp/mods.hpp"
#include "librecomp/overlays.hpp"
#include <algorithm>
#include <mutex>
#include <unordered_set>

extern "C" void lambo_log_v1(uint8_t*, recomp_context*);

namespace lambo::mods {
namespace {
// Runtime serializes content callbacks under its mod-context lock. Publish a
// path snapshot so RT64 never calls back into that lock from its render thread.
std::unordered_set<std::string> enabled_packs;
std::mutex pending_mutex;
std::optional<std::vector<std::filesystem::path>> pending_paths;

void publish(recomp::mods::ModContext& context) {
    std::vector<std::string> ids(enabled_packs.begin(), enabled_packs.end());
    std::sort(ids.begin(), ids.end(), [&](const auto& a, const auto& b) {
        return context.get_mod_order_index(a) > context.get_mod_order_index(b);
    });
    std::vector<std::filesystem::path> paths;
    for (const auto& id : ids) paths.push_back(context.get_mod_filename(id));
    std::lock_guard lock(pending_mutex);
    pending_paths = std::move(paths);
}
}

void register_content() {
    recomp::overlays::register_base_export("lambo_log_v1", lambo_log_v1);
    const auto textures = recomp::mods::register_mod_content_type({
        .content_filename = "rt64.json",
        .allow_runtime_toggle = true,
        .on_enabled = [](recomp::mods::ModContext& context, const recomp::mods::ModHandle& mod) {
            enabled_packs.insert(mod.manifest.mod_id);
            publish(context);
        },
        .on_disabled = [](recomp::mods::ModContext& context, const recomp::mods::ModHandle& mod) {
            enabled_packs.erase(mod.manifest.mod_id);
            publish(context);
        },
        .on_reordered = publish,
    });
    if (!recomp::mods::register_mod_container_type(
            texture_pack_container_extension, {textures}, false)) {
        LAMBO_LOG_ERROR("mods", "failed to register .rtz texture-pack container type\n");
    }
}

std::optional<std::vector<std::filesystem::path>> take_texture_pack_update() {
    std::lock_guard lock(pending_mutex);
    auto result = std::move(pending_paths);
    pending_paths.reset();
    return result;
}

void discard_failed_load() {
    enabled_packs.clear();
    std::lock_guard lock(pending_mutex);
    pending_paths = std::vector<std::filesystem::path>{};
}
}

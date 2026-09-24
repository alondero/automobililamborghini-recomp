// Test-only transcription of the N64ModernRuntime mod API surface used by
// src/lambo_mods.cpp, copied from lib/N64ModernRuntime @ cdf5abbd
// (librecomp/include/librecomp/mods.hpp, librecomp/src/recomp.cpp).
// Only this test target includes this directory; production builds use the
// real submodule headers. If the runtime changes these signatures, the full
// build (not this test) reports the drift.
#ifndef LAMBO_TEST_FAKE_LIBRECOMP_MODS_HPP
#define LAMBO_TEST_FAKE_LIBRECOMP_MODS_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct recomp_context;

namespace recomp::mods {

class ModContext;
class ModHandle;

using content_enabled_callback = void(ModContext&, const ModHandle&);
using content_disabled_callback = void(ModContext&, const ModHandle&);
using content_reordered_callback = void(ModContext&);

struct ModContentType {
    std::string content_filename;
    bool allow_runtime_toggle;
    content_enabled_callback* on_enabled;
    content_disabled_callback* on_disabled;
    content_reordered_callback* on_reordered;
};

struct ModContentTypeId {
    size_t value;
    bool operator==(const ModContentTypeId& rhs) const = default;
};

// Only what src/lambo_mods.cpp touches: the manifest id of a toggled mod.
struct ModManifest {
    std::string mod_id;
};

struct ModHandle {
    ModManifest manifest;
};

// Test-controlled stand-ins for ModContext queries used by publish().
namespace stub {
extern size_t context_order_index;
extern std::filesystem::path context_filename;
}

class ModContext {
public:
    size_t get_mod_order_index(const std::string&) const { return stub::context_order_index; }
    std::filesystem::path get_mod_filename(const std::string&) const { return stub::context_filename; }
};

ModContentTypeId register_mod_content_type(const ModContentType& type);
bool register_mod_container_type(const std::string& extension,
                                 const std::vector<ModContentTypeId>& content_types,
                                 bool requires_manifest);

} // namespace recomp::mods

#endif

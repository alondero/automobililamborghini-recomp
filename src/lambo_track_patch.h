#ifndef LAMBO_TRACK_PATCH_H
#define LAMBO_TRACK_PATCH_H

#include <cstdint>
#include <filesystem>

namespace lambo::track_patch {

// load_package performs all file I/O, allocation, and portable-format
// validation. A failed load leaves the previously active package unchanged;
// callers that want no package can explicitly call disable().
enum class LoadResult {
    Loaded,
    IoError,
    InvalidFormat,
    IncompatibleRom,
};

// apply_to_active_track is suitable for a post-track-load or post-savestate
// hook: it performs no allocation or file I/O. Every rejection is read-only;
// writes occur only after the complete live PVS has passed its guards.
enum class ApplyResult {
    Disabled,
    WrongCircuit,
    BadContext,
    BaseMismatch,
    AlreadyApplied,
    Applied,
};

LoadResult load_package(const std::filesystem::path& path);
void disable();
ApplyResult apply_to_active_track(uint8_t* rdram) noexcept;
uint64_t active_package_id() noexcept;
const char* apply_result_name(ApplyResult result) noexcept;

// Diagnostic for the most recent load_package call. The returned pointer names
// a static message and remains valid until the next load/disable call.
const char* last_error() noexcept;

} // namespace lambo::track_patch

extern "C" {

void lambo_track_patch_on_track_loaded(uint8_t* rdram);
void lambo_track_patch_on_savestate_loaded(uint8_t* rdram);
uint64_t lambo_track_patch_active_package_id(void);

}

#endif // LAMBO_TRACK_PATCH_H

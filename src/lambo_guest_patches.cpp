#include "librecomp/overlays.hpp"
#include "librecomp/addresses.hpp"
#include "lambo_guest_calls.h"
#include <iterator>
#include <vector>

#define ARRLEN(x) (sizeof(x) / sizeof((x)[0]))
#include "recomp_overlays.inl"
#include "guest_patch_data.inc"

static_assert(recomp::patch_rdram_start == (int32_t)0x80801000u);
static_assert(sizeof(guest_patch_data) < 0x80900000u - 0x80801000u);

void lambo_register_guest_patches(const std::vector<SectionTableEntry>& protected_sections) {
    // Real patch sections come first: the runtime can regenerate mod hooks
    // from their MIPS bytes and relocations. Legacy injected patches remain
    // protected by the zero-sized entries supplied by register_overlays.
    static std::vector<SectionTableEntry> sections;
    sections.assign(std::begin(section_table), std::end(section_table));
    sections.insert(sections.end(), protected_sections.begin(), protected_sections.end());
    recomp::overlays::register_patches(reinterpret_cast<const char*>(guest_patch_data),
        sizeof(guest_patch_data), sections.data(), sections.size());
    static const ManualPatchSymbol manual_calls[] = {
        {0x8f000000, lambo_get_automatic_pit_stops},
        {0x8f000004, lambo_get_extra_vehicles},
        {0x8f000008, lambo_get_menu_stick_sensitivity},
        {0x8f00000c, lambo_vehicle_input},
        {0x8f000010, lambo_vehicle_observe}, {0, nullptr}};
    recomp::overlays::register_manual_patch_symbols(manual_calls);
}

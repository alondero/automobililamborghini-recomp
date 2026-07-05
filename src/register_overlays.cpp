// This TU is the only place that includes the generated recomp_overlays.inl
// (it defines section_table[] / overlay_sections_by_index[] as file-local
// statics, so they can't be referenced from anywhere else).
//
// Issue #13: also feed each section's FuncEntry pointers to the crash module
// before the librecomp registration, so the native-PC -> N64-vram map is
// ready by the time the game thread starts running recompiled functions.

#include "recomp_overlays.inl"

#include "librecomp/overlays.hpp"
#include "librecomp/sections.h"

#ifndef ARRLEN
#define ARRLEN(x) (sizeof(x) / sizeof((x)[0]))
#endif

extern "C" void lambo_crash_register_code_ptrs(
    uint32_t ram_addr, uint32_t size, FuncEntry* funcs, size_t num_funcs);

void register_overlays() {
    for (size_t i = 0; i < ARRLEN(section_table); i++) {
        const SectionTableEntry& sec = section_table[i];
        lambo_crash_register_code_ptrs(sec.ram_addr, sec.size, sec.funcs, sec.num_funcs);
    }

    recomp::overlays::overlay_section_table_data_t sections {
        .code_sections = section_table,
        .num_code_sections = ARRLEN(section_table),
        .total_num_sections = num_sections,
    };

    recomp::overlays::overlays_by_index_t overlays {
        .table = overlay_sections_by_index,
        .len = ARRLEN(overlay_sections_by_index),
    };

    recomp::overlays::register_overlays(sections, overlays);
}

// Wire the recompiled image's overlay/section tables into librecomp's function
// lookup. The generated recomp_overlays.inl defines `section_table[]`,
// `num_sections`, and `overlay_sections_by_index[]` (all file-local statics), so
// this TU is the one place that includes it. Modeled on drmario64's
// src/main/register_overlays.cpp, adapted to our single linear .text section
// (per-overlay sections are epic #54 phase 3).

#include "recomp_overlays.inl"

#include "librecomp/overlays.hpp"
#include "librecomp/sections.h"

#ifndef ARRLEN
#define ARRLEN(x) (sizeof(x) / sizeof((x)[0]))
#endif

// Native crash backtrace (issue #13 / A14): we also feed the same section
// table (ram_addr + FuncEntry pointers) to the crash module so it can map
// native PCs from backtrace(3) / CaptureStackBackTrace back to the N64
// vram the recompiled function represents. The function lives in
// src/lambo_crash.cpp and is declared extern "C" here to keep that
// header free of recompiled-side types.
extern "C" void lambo_crash_register_code_ptrs(
    uint32_t ram_addr, uint32_t size, FuncEntry* funcs, size_t num_funcs);

void register_overlays() {
    // Native -> vram map first (consumed by the crash module's
    // capture-time backtrace translation). The crash module is idempotent
    // if it's called before register_overlays() at startup -- sections
    // added later will be merged into the existing map.
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

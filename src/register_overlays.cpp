// Wire the recompiled image's overlay/section tables into librecomp's function
// lookup. The generated recomp_overlays.inl defines `section_table[]`,
// `num_sections`, and `overlay_sections_by_index[]` (all file-local statics), so
// this TU is the one place that includes it. Modeled on drmario64's
// src/main/register_overlays.cpp, adapted to our single linear .text section
// (per-overlay sections are epic #54 phase 3).

#include "recomp_overlays.inl"

#include "librecomp/overlays.hpp"

#ifndef ARRLEN
#define ARRLEN(x) (sizeof(x) / sizeof((x)[0]))
#endif

void register_overlays() {
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

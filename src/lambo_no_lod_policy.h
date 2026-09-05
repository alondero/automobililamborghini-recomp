#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace lambo::no_lod {

using SegmentRenderable = bool (*)(int segment, const void* context);

bool has_synthesized_extra_exclusions(int circuit, int camera_segment);
bool should_exclude_synthesized_extra(int circuit, int camera_segment, int segment);

int build_synthesized_row(int circuit, int segment_count, int camera_segment,
                          std::span<const int16_t> authored,
                          SegmentRenderable is_renderable, const void* renderable_context,
                          std::span<int16_t> output);

}  // namespace lambo::no_lod

#include "lambo_no_lod_policy.h"

namespace {

struct SynthesizedExtraExclusion {
    int circuit;
    int camera_segment;
    int segment;
};

// These are measured exceptions to the optional full-track PVS synthesis. Keep
// the table in this implementation file so the public seam stays about row
// construction rather than ROM asset data.
constexpr SynthesizedExtraExclusion kSynthesizedExtraExclusions[] = {
    // Circuit 6 (index 5), segment 55 is occluded from camera segment 2 by the
    // canyon wall identified in the 2026-09-03 frame-DL bisection.
    {5, 2, 55},
};

constexpr int kMaxSegments = 256;
constexpr std::size_t kAuthoredRowCapacity = 10;

}  // namespace

namespace lambo::no_lod {

bool has_synthesized_extra_exclusions(int circuit, int camera_segment) {
    for (const SynthesizedExtraExclusion& exclusion : kSynthesizedExtraExclusions) {
        if (exclusion.circuit == circuit && exclusion.camera_segment == camera_segment) {
            return true;
        }
    }
    return false;
}

bool should_exclude_synthesized_extra(int circuit, int camera_segment, int segment) {
    for (const SynthesizedExtraExclusion& exclusion : kSynthesizedExtraExclusions) {
        if (exclusion.circuit == circuit &&
            exclusion.camera_segment == camera_segment &&
            exclusion.segment == segment) {
            return true;
        }
    }
    return false;
}

int build_synthesized_row(int circuit, int segment_count, int camera_segment,
                          std::span<const int16_t> authored,
                          SegmentRenderable is_renderable, const void* renderable_context,
                          std::span<int16_t> output) {
    if (segment_count < 2 || segment_count > kMaxSegments ||
        camera_segment < 0 || camera_segment >= segment_count ||
        authored.size() != kAuthoredRowCapacity ||
        output.size() < static_cast<std::size_t>(segment_count) ||
        is_renderable == nullptr) {
        return -1;
    }

    bool listed[kMaxSegments] = {};
    int out = 0;
    listed[camera_segment] = true;
    for (int16_t segment : authored) {
        if (segment < 0 || segment >= segment_count || listed[segment]) continue;
        listed[segment] = true;
        output[out++] = segment;
    }

    const bool has_exclusions = has_synthesized_extra_exclusions(circuit, camera_segment);
    for (int segment = 0; segment < segment_count; ++segment) {
        if (listed[segment] || !is_renderable(segment, renderable_context)) continue;
        if (has_exclusions && should_exclude_synthesized_extra(circuit, camera_segment, segment)) {
            continue;
        }
        output[out++] = static_cast<int16_t>(segment);
    }
    return out;
}

}  // namespace lambo::no_lod

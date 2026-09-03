#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <span>

#include "lambo_no_lod_policy.h"

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct Renderability {
    std::array<bool, 256> values{};
};

bool renderable_from_table(int segment, const void* context) {
    const auto& table = *static_cast<const Renderability*>(context);
    return table.values[segment];
}

int build_row(int circuit, int segment_count, int camera_segment,
              std::span<const int16_t> authored, const Renderability& renderability,
              std::span<int16_t> output) {
    return lambo::no_lod::build_synthesized_row(
        circuit, segment_count, camera_segment, authored,
        &renderable_from_table, &renderability, output);
}

bool contains(std::span<const int16_t> row, int16_t segment) {
    for (int16_t value : row) {
        if (value == segment) return true;
    }
    return false;
}

bool has_duplicate(std::span<const int16_t> row) {
    for (std::size_t i = 0; i < row.size(); ++i) {
        for (std::size_t j = i + 1; j < row.size(); ++j) {
            if (row[i] == row[j]) return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    using lambo::no_lod::has_synthesized_extra_exclusions;
    using lambo::no_lod::should_exclude_synthesized_extra;

    expect(has_synthesized_extra_exclusions(5, 2),
           "circuit 6 camera segment 2 has a measured synthesized-extra exception");
    expect(!has_synthesized_extra_exclusions(5, 3),
           "other circuit 6 cameras can short-circuit the policy scan");
    expect(!has_synthesized_extra_exclusions(4, 2),
           "circuits without exceptions can short-circuit the policy scan");
    expect(should_exclude_synthesized_extra(5, 2, 55),
           "the measured circuit 6 camera-2 segment-55 exception is active");
    expect(!should_exclude_synthesized_extra(5, 3, 55),
           "an unverified camera segment must not inherit the exception");
    expect(!should_exclude_synthesized_extra(4, 2, 55),
           "an adjacent circuit must not inherit the exception");
    expect(!should_exclude_synthesized_extra(5, 2, 54),
           "an adjacent segment must not inherit the exception");

    Renderability renderability;
    renderability.values.fill(true);
    renderability.values[6] = false;
    std::array<int16_t, 57> output{};
    const std::array<int16_t, 10> authored = {3, 4, 3, -1, 1, 0, -1, 11, -1, -1};
    int count = build_row(5, 57, 2, authored, renderability, output);
    expect(count > 0, "a valid row produces entries");
    expect(!contains(std::span<const int16_t>(output.data(), count), 55),
           "segment 55 is withheld when it is only a synthesized extra");
    expect(!contains(std::span<const int16_t>(output.data(), count), 6),
           "non-renderable segment records are skipped");
    expect(contains(std::span<const int16_t>(output.data(), count), 3),
           "authored entries are preserved");
    expect(!has_duplicate(std::span<const int16_t>(output.data(), count)),
           "duplicate authored entries are emitted only once");

    count = build_row(5, 57, 3, authored, renderability, output);
    expect(contains(std::span<const int16_t>(output.data(), count), 55),
           "an unverified camera retains segment 55 as a synthesized extra");

    const std::array<int16_t, 10> authored_segment_55 = {55, 3, 4, 5, -1, 1, 0, 11, -1, -1};
    count = build_row(5, 57, 2, authored_segment_55, renderability, output);
    expect(count > 0 && output[0] == 55,
           "authored membership takes precedence over synthesized-extra exclusions");

    const std::array<int16_t, 10> valid_authored = {3, 4, 5, -1, 1, 0, -1, 11, -1, -1};
    const std::array<int16_t, 9> short_authored = {3, 4, 5, -1, 1, 0, -1, 11, -1};
    std::array<int16_t, 1> undersized_output{};
    expect(build_row(5, 1, 0, valid_authored, renderability, output) == -1,
           "segment counts below two are rejected");
    expect(build_row(5, 257, 0, valid_authored, renderability, output) == -1,
           "segment counts above the row limit are rejected");
    expect(build_row(5, 57, -1, valid_authored, renderability, output) == -1,
           "negative camera segments are rejected");
    expect(build_row(5, 57, 57, valid_authored, renderability, output) == -1,
           "camera segments outside the row are rejected");
    expect(build_row(5, 57, 2, short_authored, renderability, output) == -1,
           "malformed authored rows are rejected");
    expect(build_row(5, 57, 2, valid_authored, renderability, undersized_output) == -1,
           "undersized output buffers are rejected");
    expect(lambo::no_lod::build_synthesized_row(
               5, 57, 2, std::span<const int16_t>(valid_authored), nullptr,
               nullptr, std::span<int16_t>(output)) == -1,
           "a missing renderability adapter is rejected");
}

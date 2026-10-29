// Opt-in CPU measurement. No game data, renderer or frame-time claim.
// Compares rebuilding a synthesized visibility row with the per-segment part of
// a safe cache check: re-reading the authored row and every record's
// renderability, then comparing them with the previous walk's.
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "lambo_no_lod_policy.h"

namespace {

// Segment counts per circuit index, measured in races; see docs/no_lod_audit.md §14.
constexpr std::array<int, 6> kSegmentCounts = {25, 29, 30, 75, 55, 67};
constexpr unsigned kRows = 1000000;
constexpr int kMaxSegments = 256;
constexpr int kRecordWords = 64 / 4;
constexpr int kRoadPointerWord = 0x4 / 4;

struct Track {
    std::array<uint32_t, kMaxSegments * kRecordWords> records{};
};

bool renderable(int segment, const void* context) {
    const auto& track = *static_cast<const Track*>(context);
    const uint32_t pointer = track.records[segment * kRecordWords + kRoadPointerWord];
    return pointer >= 0x80000000u && pointer < 0x80800000u;
}

struct CacheKey {
    int circuit, segment_count, camera_segment;
    std::array<int16_t, 10> authored;
    std::array<uint8_t, kMaxSegments> renderable;
};

CacheKey read_key(int circuit, int segment_count, int camera_segment,
                  const std::array<int16_t, 10>& authored, const Track& track) {
    CacheKey key{};
    key.circuit = circuit;
    key.segment_count = segment_count;
    key.camera_segment = camera_segment;
    key.authored = authored;
    for (int segment = 0; segment < segment_count; ++segment) {
        key.renderable[segment] = renderable(segment, &track) ? 1 : 0;
    }
    return key;
}

}  // namespace

int main() {
    for (int circuit = 0; circuit < 6; ++circuit) {
        const int segment_count = kSegmentCounts[circuit];
        Track track;
        for (int segment = 0; segment < segment_count; ++segment) {
            track.records[segment * kRecordWords + kRoadPointerWord] = 0x80200000u + segment * 0x100u;
        }
        const std::array<int16_t, 10> authored = {3, 4, 5, -1, 1, 0, -1, 11, -1, -1};
        std::array<int16_t, kMaxSegments> row{};
        constexpr int camera = 2;

        long long checksum = 0;
        auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < kRows; ++i) {
            checksum += lambo::no_lod::build_synthesized_row(
                circuit, segment_count, camera, authored, &renderable, &track, row);
            checksum += row[i % 8];
        }
        const double rebuild_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        const CacheKey cached = read_key(circuit, segment_count, camera, authored, track);
        long long hits = 0;
        start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < kRows; ++i) {
            const CacheKey key = read_key(circuit, segment_count, camera, authored, track);
            hits += std::memcmp(&key, &cached, sizeof(key)) == 0;
        }
        const double validate_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        std::cout << "circuit_index=" << circuit << " segments=" << segment_count
                  << " rows=" << kRows << " rebuild_ms=" << rebuild_ms
                  << " validate_ms=" << validate_ms << " hits=" << hits
                  << " checksum=" << checksum << '\n';
    }
}

// Synthetic data only. The Python driver injects both versions of RT64's
// complete load helpers so the oracle remains the pinned implementation.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "contrib/xxHash/xxhash.h"
#include "common/rt64_load_types.h"
#include "shared/rt64_f3d_defines.h"
#include "common/rt64_tmem_hasher.h"

constexpr uint32_t RDP_TMEM_BYTES = 4096;
constexpr uint32_t RDP_TMEM_MASK8 = 4095;
constexpr uint32_t RDP_TMEM_MASK16 = 2047;
namespace baseline {
// BASELINE_LOADS
}
namespace production {
// PRODUCTION_LOADS
}

using TMEM = std::array<uint8_t, 4096>;
static std::mt19937 randomBytes(425);
static uint64_t wordCases = 0, sequenceCases = 0, hashCases = 0;

template<bool RGBA32, bool TLUT>
static void words(const std::vector<uint8_t> &ram) {
    // Include every byte alignment, both odd/even row permutations, every
    // legal destination and all 256 byte values in each source position.
    // The extra bytes let us compare unaligned fallback destinations at the
    // edge without reading or writing outside the test allocation.
    std::array<uint8_t, 4112> expected{}, actual{};
    for (auto &byte : expected) byte = uint8_t(randomBytes());
    for (uint32_t sample = 0; sample < 256; sample++) {
        for (uint32_t srcAlign = 0; srcAlign < 8; srcAlign++) {
            for (uint32_t dst = 0; dst < (RGBA32 ? 2048U : 4096U); dst++) {
                for (uint32_t swap : {0U, 4U}) {
                    actual = expected;
                    baseline::loadWord<RGBA32, TLUT>(expected.data(), dst, swap, ram.data(), sample * 16 + srcAlign);
                    production::loadWord<RGBA32, TLUT>(actual.data(), dst, swap, ram.data(), sample * 16 + srcAlign);
                    assert(actual == expected);
                    wordCases++;
                }
            }
        }
    }
}

static void hashes(const TMEM &expected, const TMEM &actual, uint32_t sample) {
    // All published hash versions, all pixel sizes/formats, palette banks,
    // row padding, odd partial rows, wrapping and both palette modes.
    RT64::LoadTile tile{};
    tile.siz = sample % 4;
    tile.fmt = (sample / 4) % 5;
    tile.tmem = (sample / 20) % 512;
    tile.line = (sample / 7) % 65;
    tile.palette = (sample / 11) % 16;
    const uint16_t width = 1 + (sample % 129);
    const uint16_t height = 1 + ((sample / 3) % 33);
    for (uint32_t tlut : {0U, 2U, 3U}) {
        for (uint32_t version = 1; version <= RT64::TMEMHasher::CurrentHashVersion; version++) {
            assert(RT64::TMEMHasher::hash(expected.data(), tile, width, height, tlut, version) ==
                RT64::TMEMHasher::hash(actual.data(), tile, width, height, tlut, version));
            hashCases++;
        }
    }
}

template<bool RGBA32, bool BLOCK, bool TLUT>
static void sequences(const std::vector<uint8_t> &ram) {
    TMEM expected{}, actual{};
    for (auto &byte : expected) byte = uint8_t(randomBytes());
    actual = expected;
    for (uint32_t sample = 0; sample < 4096; sample++) {
        const uint32_t src = randomBytes() % 8192;
        const uint32_t srcStride = randomBytes() % 513;
        const uint32_t dst = (randomBytes() % 512) * 8;
        const uint32_t dstStride = (randomBytes() % 513) * 8;
        const uint32_t count = randomBytes() % 514; // zero, wrap, multiple wraps
        const uint32_t rows = BLOCK ? 1 : randomBytes() % 9;
        const uint32_t dxt = sample; // every 12-bit DXT, including zero
        // Row skipping in the pinned loader has its own addressing semantics;
        // keep these within TMEM and test the skip path separately below.
        const uint32_t safeDst = (!BLOCK && dstStride && rows > 1 && count * (RGBA32 ? 4U : 8U) <= dstStride) ? 0 : dst;
        baseline::loadToTMEMCommon<RGBA32, BLOCK, TLUT>(expected.data(), ram.data(), src, srcStride,
            safeDst, dstStride, count, rows, dxt);
        production::loadToTMEMCommon<RGBA32, BLOCK, TLUT>(actual.data(), ram.data(), src, srcStride,
            safeDst, dstStride, count, rows, dxt);
        assert(actual == expected);
        sequenceCases++;
        hashes(expected, actual, sample);
    }
    // Loads large enough to skip overwritten rows, with even/odd skip counts.
    if constexpr (!BLOCK) {
        for (uint32_t rows : {512U, 513U, 514U, 1024U}) {
            baseline::loadToTMEMCommon<RGBA32, BLOCK, TLUT>(expected.data(), ram.data(), 0, 8, 0, 8, 1, rows);
            production::loadToTMEMCommon<RGBA32, BLOCK, TLUT>(actual.data(), ram.data(), 0, 8, 0, 8, 1, rows);
            assert(actual == expected);
            sequenceCases++;
            hashes(expected, actual, rows);
        }
    }
}

// Optional isolated timing. Full-game A/B measurements are still required.
using BlockLoader = void (*)(uint8_t *, const uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

static void benchmark(const std::vector<uint8_t> &ram) {
    TMEM tmem{};
    volatile uint64_t checksum = 0;
    for (uint32_t pass = 0; pass < 6; pass++) {
        // Indirect calls keep the compiler from specializing a whole load for
        // these constant dimensions. The renderer receives them at runtime.
        BlockLoader volatile load = (pass % 2) ? production::loadToTMEMCommon<false, true, false> :
            baseline::loadToTMEMCommon<false, true, false>;
        const auto start = std::chrono::steady_clock::now();
        for (uint32_t i = 0; i < 100000; i++) {
            load(tmem.data(), ram.data(), (i % 128) * 8, 256, 0, 0, 256, 1, 64);
            checksum += tmem[i % tmem.size()];
        }
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("%s %.3f ms checksum=%llu\n", pass % 2 ? "aligned" : "byte", ms, (unsigned long long)checksum);
    }
}

int main(int argc, char **argv) {
    std::vector<uint8_t> ram(1 << 20);
    for (uint32_t i = 0; i < ram.size(); i++) ram[i] = uint8_t(i / 16 + (i % 16) * 17);
    if (argc == 2 && std::string(argv[1]) == "--benchmark") {
        benchmark(ram);
        return 0;
    }
    words<false, false>(ram);
    words<true, false>(ram);
    words<false, true>(ram);
    words<true, true>(ram);
    for (auto &byte : ram) byte = uint8_t(randomBytes());
    sequences<false, false, false>(ram);
    sequences<false, true, false>(ram);
    sequences<true, false, false>(ram);
    sequences<true, true, false>(ram);
    sequences<false, false, true>(ram);
    sequences<true, false, true>(ram);
    std::printf("TMEM equivalence passed: %llu words, %llu sequences, %llu hashes\n",
        (unsigned long long)wordCases, (unsigned long long)sequenceCases, (unsigned long long)hashCases);
}

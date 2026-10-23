// Behavioral checks must also run in Release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
// RT64 v5 must see only loaded texels for bounded USA gauge/logo slices.
// Synthetic TMEM: no ROM data. Exercise the same hasher as the renderer.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <set>
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "contrib/xxHash/xxhash.h"
#include "common/rt64_load_types.h"
#include "shared/rt64_f3d_defines.h"
#include "common/rt64_tmem_hasher.h"

int main() {
    // Title logo RGBA16 loads fill the first 2048 bytes. Demo rendering can
    // change the other half without changing any visible logo texels.
    {
        RT64::LoadTile tile{};
        tile.siz = G_IM_SIZ_16b;
        tile.fmt = G_IM_FMT_RGBA;
        tile.line = 16;
        std::array<uint8_t, 4096> tmem{};
        std::set<uint64_t> wrapped, bounded;
        for (unsigned seed = 0; seed < 256; ++seed) {
            for (unsigned i = 0; i < tmem.size(); ++i)
                tmem[i] = uint8_t(i < 2048 ? i * 17 : i * 13 + seed);
            wrapped.insert(RT64::TMEMHasher::hash(tmem.data(), tile, 64, 32, 0, 5));
            bounded.insert(RT64::TMEMHasher::hash(tmem.data(), tile, 64, 16, 0, 5));
        }
        assert(wrapped.size() == 256);
        assert(bounded.size() == 1);
        const auto unchanged = *bounded.begin();
        // Both ends of the loaded rectangle contribute to its identity.
        for (auto index : {0u, 2047u}) {
            tmem[index] ^= 1;
            assert(RT64::TMEMHasher::hash(tmem.data(), tile, 64, 16, 0, 5) != unchanged);
            tmem[index] ^= 1;
        }
    }
    for (const auto dimensions : {std::array<unsigned, 4>{40, 6, 64, 8},
                                  std::array<unsigned, 4>{16, 57, 16, 64}}) {
        RT64::LoadTile tile{};
        tile.siz = G_IM_SIZ_32b;
        tile.fmt = G_IM_FMT_RGBA;
        tile.line = dimensions[0] / 4;
        std::array<uint8_t, 4096> tmem{};
        const auto active = dimensions[0] * dimensions[1] * 2;
        std::set<uint64_t> wrapped, bounded;
        for (unsigned seed = 0; seed < 256; ++seed) {
            for (unsigned half : {0u, 2048u}) {
                for (unsigned i = 0; i < 2048; ++i) {
                    tmem[half + i] = uint8_t(i < active ? i * 17 : i * 13 + seed);
                }
            }
            wrapped.insert(RT64::TMEMHasher::hash(tmem.data(), tile,
                dimensions[2], dimensions[3], 0, 5));
            bounded.insert(RT64::TMEMHasher::hash(tmem.data(), tile,
                dimensions[0], dimensions[1], 0, 5));
        }
        assert(wrapped.size() == 256);
        assert(bounded.size() == 1);
        const auto unchanged = *bounded.begin();
        tmem[0] ^= 1;
        assert(RT64::TMEMHasher::hash(tmem.data(), tile,
            dimensions[0], dimensions[1], 0, 5) != unchanged);
    }
}

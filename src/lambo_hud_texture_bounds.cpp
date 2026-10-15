// USA HUD texture bounds, applied on the graphics consumer before RT64 reads
// the task-owned F3DEX list. These maps and suffixes use wrap/mirror masks larger
// than their loaded rectangle; their unused TMEM otherwise changes pack hashes.
// Only exact source + RGBA/CI/IA format + stride + tile bounds are accepted.
// DP completion is armed only after send_dl returns; the guest scheduler
// cannot recycle this task while we walk/interpret it. See docs/HUD.md for
// the ownership fence and intended source-level emitter replacement.
// RDRAM words are host-endian word-swapped N64 memory. See docs/HUD.md.
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace {
constexpr uint32_t ram_size = 0x800000;
struct Walk {
    uint8_t* ram;
    uint32_t segments[16]{};
    uint32_t source = 0;
    uint32_t tile = 0;
    uint32_t remaining = 200000;

    uint32_t read(uint32_t off) const {
        uint32_t value;
        std::memcpy(&value, ram + off, 4);
        return value;
    }
    bool resolve(uint32_t address, uint32_t& off) const {
        const auto hi = address >> 24;
        if (hi >= 0x80 && hi <= 0x9f) off = address & 0x1fffffff;
        else if (hi < 16) off = (segments[hi] & 0x1fffffff) + (address & 0xffffff);
        else return false;
        return off <= ram_size - 8 && (off & 7) == 0;
    }
    void bounds(uint32_t bounds) {
        if (!tile) return;
        const auto t0 = read(tile), t1 = read(tile + 4);
        const auto unclamped = t1 & ~0x00080200u;
        bool map_source = false;
        for (auto address : {0x121a88u, 0x122710u, 0x123398u,
                             0x124020u, 0x124ca8u, 0x125930u}) {
            map_source |= source == address;
        }
        // Texture tiles have zero origins, no palette/shift, and either wrap
        // or mirror. Preserve mirror while enabling clamp on both dimensions.
        const bool map = map_source && t0 == 0xF5600A00u && bounds == 0x0013C13Cu &&
            (unclamped == 0x0001C070u || unclamped == 0x0005C170u);
        const bool suffix = source == 0x165028u && t0 == 0xF5400C00u && bounds == 0x0017C01Cu &&
            (unclamped == 0x00010070u || unclamped == 0x00050170u);
        if (map || suffix) {
            const uint32_t value = t1 | 0x00080200u;
            std::memcpy(ram + tile + 4, &value, 4);
        }
        tile = 0;
    }
    void run(uint32_t address, unsigned depth = 0) {
        uint32_t off;
        if (depth > 16 || !resolve(address, off)) return;
        while (remaining && off <= ram_size - 8) {
            --remaining;
            const auto w0 = read(off), w1 = read(off + 4);
            switch (w0 >> 24) {
            case 0xBC:
                if ((w0 & 255) == 6) segments[((w0 >> 8) & 0xffff) / 4 & 15] = w1;
                break;
            case 0x06:
                if ((w0 >> 16) & 255) {
                    if (!resolve(w1, off)) return;
                    continue;
                }
                run(w1, depth + 1);
                break;
            case 0xB8: return;
            case 0xFD:
                // The USA assets are direct physical/KSEG0 pointers, never
                // inferred from similar pixels or from a replacement pack.
                source = w1 & 0x1fffffff;
                tile = 0;
                break;
            case 0xF5:
                if ((w1 >> 24) == 0) tile = off;
                break;
            case 0xF2:
                if (w0 == 0xF2000000) bounds(w1);
                break;
            default: break;
            }
            off += 8;
        }
    }
};
}

extern "C" void lambo_hud_texture_bounds(uint8_t* rdram, uint32_t address) {
    if (rdram) Walk{rdram}.run(address);
}

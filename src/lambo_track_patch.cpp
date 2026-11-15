#include "lambo_track_patch.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <fstream>
#include <memory>
#include <utility>
#include <vector>

#include "lambo_log.h"
#include "recomp.h"

namespace lambo::track_patch {
namespace {

constexpr std::array<uint8_t, 8> kMagic = {
    'A', 'L', 'T', 'R', 'K', 'P', 'V', '1',
};
constexpr uint16_t kFormatVersion = 1;
constexpr uint16_t kHeaderSize = 64;
constexpr uint32_t kPvsCorrectionFlag = 1;
constexpr uint64_t kSupportedRomHash = UINT64_C(0x525201d7279f34e3);
constexpr uint8_t kCircuitCount = 6;
constexpr uint8_t kSlotsPerRow = 10;
constexpr uint16_t kMinRows = 2;
constexpr uint16_t kMaxRows = 200;
constexpr uint32_t kEditSize = 8;
constexpr uint32_t kMaxEdits =
    static_cast<uint32_t>(kMaxRows) * kSlotsPerRow;
constexpr size_t kMaxPackageSize =
    2u * 1024u * 1024u;
constexpr uint32_t kMaxBlocks = 608;

constexpr uint32_t kRdramBase = 0x80000000u;
constexpr uint32_t kRdramEnd = 0x80800000u;
constexpr uint32_t kTrackContextPointerAddress = 0x80098238u;
constexpr uint32_t kCurrentCircuitAddress = 0x800CE794u;
constexpr uint32_t kPvsRowSize = kSlotsPerRow * sizeof(int16_t);

constexpr uint64_t kFnvOffsetBasis = UINT64_C(14695981039346656037);
constexpr uint64_t kFnvPrime = UINT64_C(1099511628211);

struct Edit {
    uint16_t row = 0;
    uint8_t slot = 0;
    int16_t expected_old = 0;
    int16_t replacement = 0;
};

struct Package {
    uint8_t circuit = 0;
    uint16_t row_count = 0;
    uint64_t base_hash = 0;
    uint64_t patched_hash = 0;
    uint64_t id = 0;
    std::vector<Edit> edits;
    struct Block {
        uint8_t kind = 0;
        uint16_t index = 0;
        uint64_t before = 0;
        uint64_t after = 0;
        std::vector<uint8_t> data;
    };
    std::vector<Block> blocks;
};

// Use the shared_ptr atomic operations supported by both libc++ and libstdc++.
std::shared_ptr<const Package> g_package;
std::atomic<const char*> g_last_error{nullptr};

uint16_t read_le_u16(const uint8_t* bytes) noexcept {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(bytes[0]) |
        (static_cast<uint16_t>(bytes[1]) << 8));
}

int16_t read_le_s16(const uint8_t* bytes) noexcept {
    return static_cast<int16_t>(read_le_u16(bytes));
}

uint32_t read_le_u32(const uint8_t* bytes) noexcept {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t read_le_u64(const uint8_t* bytes) noexcept {
    return static_cast<uint64_t>(read_le_u32(bytes)) |
           (static_cast<uint64_t>(read_le_u32(bytes + 4)) << 32);
}

uint64_t fnv_byte(uint64_t hash, uint8_t byte) noexcept {
    return (hash ^ byte) * kFnvPrime;
}

uint64_t hash_bytes(const uint8_t* bytes, size_t size) noexcept {
    uint64_t hash = kFnvOffsetBasis;
    for (size_t i = 0; i < size; ++i) {
        hash = fnv_byte(hash, bytes[i]);
    }
    return hash;
}

LoadResult load_error(LoadResult result, const char* message) noexcept {
    g_last_error.store(message, std::memory_order_release);
    return result;
}

bool is_kseg0_range(uint32_t address, uint32_t size) noexcept {
    if (address < kRdramBase || address > kRdramEnd) {
        return false;
    }
    const uint32_t offset = address - kRdramBase;
    return offset <= (kRdramEnd - kRdramBase) &&
           size <= (kRdramEnd - kRdramBase) - offset;
}

uint32_t read_guest_word(const uint8_t* rdram, uint32_t address) noexcept {
    return static_cast<uint32_t>(MEM_W(0, static_cast<gpr>(static_cast<int32_t>(address))));
}

int16_t read_guest_halfword(const uint8_t* rdram, uint32_t address) noexcept {
    return static_cast<int16_t>(MEM_H(0, static_cast<gpr>(static_cast<int32_t>(address))));
}

void write_guest_halfword(uint8_t* rdram, uint32_t address, int16_t value) noexcept {
    MEM_H(0, static_cast<gpr>(static_cast<int32_t>(address))) = value;
}

uint64_t hash_live_pvs(const uint8_t* rdram, uint32_t pvs_base,
                       uint16_t row_count) noexcept {
    uint64_t hash = kFnvOffsetBasis;
    const uint32_t value_count =
        static_cast<uint32_t>(row_count) * kSlotsPerRow;
    for (uint32_t index = 0; index < value_count; ++index) {
        const uint16_t value = static_cast<uint16_t>(
            read_guest_halfword(rdram, pvs_base + index * 2u));
        // The package fingerprint is over the logical N64 byte stream, not the
        // host's word-swapped RDRAM representation.
        hash = fnv_byte(hash, static_cast<uint8_t>(value >> 8));
        hash = fnv_byte(hash, static_cast<uint8_t>(value));
    }
    return hash;
}

uint16_t be16(const std::vector<uint8_t>& data, size_t offset) noexcept {
    return (uint16_t(data[offset]) << 8) | data[offset + 1];
}
uint32_t be32(const std::vector<uint8_t>& data, size_t offset) noexcept {
    return (uint32_t(be16(data, offset)) << 16) | be16(data, offset + 2);
}

bool valid_bundle_data(const Package& package, uint32_t vertices) noexcept {
    const Package::Block* vertex_block = nullptr;
    const Package::Block* collision_block = nullptr;
    const Package::Block* waypoint_block = nullptr;
    for (const auto& block : package.blocks) {
        if (block.kind == 1) vertex_block = &block;
        if (block.kind == 2) collision_block = &block;
        if (block.kind == 3) waypoint_block = &block;
    }
    if (!vertex_block || !collision_block) return false;
    const size_t vertex_count = vertex_block->data.size() / 16;
    const size_t triangle_count = collision_block->data.size() / 16;
    if (vertex_count > 32768 || triangle_count > 32768) return false;
    for (const auto& block : package.blocks) {
        // Unchanged buffers still receive the full live fingerprint guard.
        if (block.before == block.after) continue;
        const auto& data = block.data;
        if (block.kind == 0) {
            for (size_t i = 0; i < data.size(); i += 2) {
                int16_t value = static_cast<int16_t>(be16(data, i));
                if (value < -1 || value >= package.row_count) return false;
            }
        } else if (block.kind == 2) {
            for (size_t i = 0; i < data.size(); i += 16) {
                for (size_t j = 0; j < 3; ++j) {
                    if (be16(data, i + j * 2) >= vertex_count) return false;
                    const int16_t neighbor = static_cast<int16_t>(be16(data, i + 6 + j * 2));
                    if (neighbor < -1 || (neighbor >= 0 && size_t(neighbor) >= triangle_count)) return false;
                }
                if (be16(data, i + 12) >= package.row_count || be16(data, i + 14) > 3) return false;
            }
        } else if (block.kind == 3) {
            for (size_t i = 0; i < data.size(); i += 16) {
                for (size_t j = 0; j < 8; j += 4) {
                    uint32_t bits = be32(data, i + j);
                    float value;
                    std::memcpy(&value, &bits, 4);
                    if (!std::isfinite(value) || std::fabs(value) > 100000) return false;
                }
            }
        } else if (block.kind == 4) {
            for (size_t row = 0; row < package.row_count; ++row)
                for (size_t offset = 32; offset < 64; offset += 2)
                    if (be16(data, row * 64 + offset) >= vertex_count) return false;
        } else if (block.kind == 8) {
            if (!waypoint_block) return false;
            for (size_t i = 0; i < data.size(); i += 28) {
                for (size_t j = 0; j < 12; j += 4) {
                    int32_t coordinate = static_cast<int32_t>(be32(data, i + j));
                    if (coordinate < -1000000 || coordinate > 1000000) return false;
                }
                if (be32(data, i + 12) >= 360 || be32(data, i + 16) >= triangle_count ||
                    be32(data, i + 24) >= waypoint_block->data.size() / 16) return false;
            }
        } else if (block.kind >= 5 && block.kind <= 7) {
            std::array<bool, 64> loaded{};
            bool ended = false;
            for (size_t i = 0; i < data.size(); i += 8) {
                const uint32_t w0 = be32(data, i), w1 = be32(data, i + 4);
                if (ended) { if (w0 || w1) return false; continue; }
                const uint8_t op = w0 >> 24;
                if (op == 0xB8) { if (w1) return false; ended = true; }
                else if (op == 4) {
                    const uint32_t count = (w0 >> 10) & 63, first = ((w0 >> 16) & 255) / 2;
                    if (!count || first + count > 64 || w1 < vertices || (w1 - vertices) % 16 ||
                        uint64_t(w1) + count * 16 > uint64_t(vertices) + vertex_block->data.size()) return false;
                    for (uint32_t slot = first; slot < first + count; ++slot) loaded[slot] = true;
                } else if (op == 0xB1 || op == 0xBF) {
                    for (uint32_t word : {op == 0xB1 ? w0 : w1, w1}) {
                        for (unsigned shift : {0u, 8u, 16u}) {
                            const uint32_t index = (word >> shift) & 255;
                            if ((index & 1u) || index / 2 >= loaded.size() || !loaded[index / 2]) return false;
                        }
                    }
                } else if (op != 0xE7 && op != 0xB6 && op != 0xB7 && op != 0xBB &&
                           op != 0xFC && op != 0xBA && op != 0xB9) return false;
            }
            if (!ended) return false;
        }
    }
    return true;
}

// Fixed-layout bundles resolve named track buffers, never caller-supplied
// addresses. Post-loader/savestate hooks run on the game thread before scene
// rebuilding. Preflight every span/hash/overlap before any guest byte is written.
// Context +00/+04/+08/+0C/+10 are guest pointers; the 16-slot spawn grid is
// inline at +14. All package bytes use logical big-endian guest order.
ApplyResult apply_bundle(uint8_t* rdram, uint32_t context, const Package& package) noexcept {
    if (!is_kseg0_range(context, 0x14 + 16 * 28)) return ApplyResult::BadContext;
    const uint32_t segments = read_guest_word(rdram, context);
    const uint32_t pvs = read_guest_word(rdram, context + 4);
    const uint32_t waypoints = read_guest_word(rdram, context + 8);
    const uint32_t collision = read_guest_word(rdram, context + 12);
    const uint32_t vertices = read_guest_word(rdram, context + 16);
    if ((segments | collision | vertices | waypoints) & 3u ||
        !is_kseg0_range(segments, package.row_count * 64u) ||
        !(waypoints < vertices && vertices < collision && collision < segments))
        return ApplyResult::BadContext;
    uint32_t first_list = collision;
    for (uint32_t row = 0; row < package.row_count; ++row) {
        for (uint32_t layer = 4; layer <= 12; layer += 4) {
            const uint32_t pointer = read_guest_word(rdram, segments + row * 64 + layer);
            if (pointer && (pointer < vertices || pointer >= collision || (pointer & 7u)))
                return ApplyResult::BadContext;
            if (pointer) first_list = std::min(first_list, pointer);
        }
    }
    std::array<uint32_t, kMaxBlocks> addresses{};
    bool all_before = true, all_after = true;
    for (size_t i = 0; i < package.blocks.size(); ++i) {
        const auto& block = package.blocks[i];
        const uint32_t size = static_cast<uint32_t>(block.data.size());
        uint32_t base = 0, limit = 0;
        switch (block.kind) {
        case 0: base = pvs; limit = package.row_count * 20u; break;
        case 1: base = vertices; limit = first_list - vertices; break;
        case 2: base = collision; limit = segments - collision; break;
        case 3: base = waypoints; limit = vertices - waypoints; break;
        case 4: base = segments; limit = package.row_count * 64u; break;
        case 8: base = context + 0x14; limit = 16 * 28; break;
        // USA race minimap image table; 80x80 IA4, followed by eight padding
        // bytes which are deliberately outside the replacement span.
        case 9: base = read_guest_word(rdram, 0x801265B0u + package.circuit * 4u); limit = 3200; break;
        default: {
            base = read_guest_word(rdram, segments + block.index * 64u + (block.kind - 4u) * 4u);
            if (!base) return ApplyResult::BadContext;
            uint32_t end = collision;
            for (uint32_t row = 0; row < package.row_count; ++row) {
                for (uint32_t layer = 4; layer <= 12; layer += 4) {
                    uint32_t other = read_guest_word(rdram, segments + row * 64 + layer);
                    if (other > base) end = std::min(end, other);
                }
            }
            limit = end - base;
            break;
        }
        }
        if (size > limit || !is_kseg0_range(base, size) || (base & 3u)) return ApplyResult::BadContext;
        if ((block.kind == 0 || block.kind == 2 || block.kind == 4 || block.kind == 8 || block.kind == 9) && size != limit)
            return ApplyResult::BadContext;
        // Segment pointers remain stock; replacement data only changes offsets,
        // surface selectors and cull indices. This also preserves idempotent resolution.
        if (block.kind == 4) {
            for (uint32_t row = 0; row < package.row_count; ++row)
                for (uint32_t byte = 0; byte < 16; ++byte)
                    if (rdram[((base - kRdramBase) + row * 64 + byte) ^ 3u] != block.data[row * 64 + byte])
                        return ApplyResult::BadContext;
        }
        for (size_t previous = 0; previous < i; ++previous) {
            const uint32_t other = addresses[previous];
            if (base < other + package.blocks[previous].data.size() && other < base + size)
                return ApplyResult::BadContext;
        }
        addresses[i] = base;
        uint64_t hash = kFnvOffsetBasis;
        for (uint32_t byte = 0; byte < size; ++byte)
            hash = fnv_byte(hash, rdram[((base - kRdramBase) + byte) ^ 3u]);
        all_before = all_before && hash == block.before;
        all_after = all_after && hash == block.after;
    }
    if (!valid_bundle_data(package, vertices)) return ApplyResult::BadContext;
    // This authoring lane retains stock event/range topology. Route scalars,
    // events and spawn range IDs cannot become arbitrary guest table indices.
    for (size_t i = 0; i < package.blocks.size(); ++i) {
        const auto& block = package.blocks[i];
        if (block.kind != 3 && block.kind != 8) continue;
        const size_t stride = block.kind == 3 ? 16 : 28;
        const size_t first = block.kind == 3 ? 8 : 20;
        const size_t last = block.kind == 3 ? 16 : 24;
        for (size_t record = 0; record < block.data.size(); record += stride)
            for (size_t byte = first; byte < last; ++byte)
                if (rdram[((addresses[i] - kRdramBase) + record + byte) ^ 3u] != block.data[record + byte])
                    return ApplyResult::BadContext;
    }
    if (all_after) return ApplyResult::AlreadyApplied;
    if (!all_before) return ApplyResult::BaseMismatch;
    for (size_t i = 0; i < package.blocks.size(); ++i) {
        const auto& data = package.blocks[i].data;
        for (size_t byte = 0; byte < data.size(); ++byte)
            rdram[((addresses[i] - kRdramBase) + byte) ^ 3u] = data[byte];
    }
    return ApplyResult::Applied;
}

} // namespace

LoadResult load_package(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return load_error(LoadResult::IoError, "could not open Track Lab package");
    }

    const std::streampos end = input.tellg();
    if (end < 0) {
        return load_error(LoadResult::IoError, "could not determine package size");
    }
    const auto file_size = static_cast<uint64_t>(end);
    if (file_size < kHeaderSize || file_size > kMaxPackageSize) {
        return load_error(LoadResult::InvalidFormat, "package size is outside format limits");
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(file_size));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return load_error(LoadResult::IoError, "could not read complete Track Lab package");
    }

    const uint8_t* const header = bytes.data();
    const bool bundle = std::memcmp(header, "ALTRKBD1", 8) == 0;
    if (!bundle && std::memcmp(header, kMagic.data(), kMagic.size()) != 0) {
        return load_error(LoadResult::InvalidFormat, "invalid Track Lab package magic");
    }
    if (read_le_u16(header + 8) != kFormatVersion) {
        return load_error(LoadResult::InvalidFormat, "unsupported Track Lab package version");
    }
    if (read_le_u16(header + 10) != kHeaderSize) {
        return load_error(LoadResult::InvalidFormat, "invalid Track Lab header size");
    }
    if (read_le_u32(header + 12) != bytes.size()) {
        return load_error(LoadResult::InvalidFormat, "declared package size does not match file");
    }
    if (read_le_u64(header + 16) != kSupportedRomHash) {
        return load_error(LoadResult::IncompatibleRom, "package targets a different ROM");
    }
    if (read_le_u32(header + 24) != (bundle ? 2u : kPvsCorrectionFlag)) {
        return load_error(LoadResult::InvalidFormat, "unsupported Track Lab package flags");
    }

    Package candidate;
    candidate.circuit = header[28];
    const uint8_t slots = header[29];
    candidate.row_count = read_le_u16(header + 30);
    const uint32_t edit_count = read_le_u32(header + 32);
    const uint32_t payload_size = read_le_u32(header + 36);
    candidate.base_hash = read_le_u64(header + 40);
    candidate.patched_hash = read_le_u64(header + 48);

    if (candidate.circuit >= kCircuitCount) {
        return load_error(LoadResult::InvalidFormat, "circuit index is outside the game range");
    }
    if (slots != kSlotsPerRow) {
        return load_error(LoadResult::InvalidFormat, "PVS slot count must be 10");
    }
    if (candidate.row_count < kMinRows || candidate.row_count > kMaxRows) {
        return load_error(LoadResult::InvalidFormat, "PVS row count is outside safe limits");
    }
    if (bundle) {
        if (!edit_count || edit_count > kMaxBlocks || payload_size != bytes.size() - kHeaderSize ||
            read_le_u64(header + 56) != 0 || candidate.base_hash || candidate.patched_hash)
            return load_error(LoadResult::InvalidFormat, "invalid track bundle header");
        size_t offset = kHeaderSize;
        uint32_t previous = 0;
        for (uint32_t i = 0; i < edit_count; ++i) {
            if (bytes.size() - offset < 32) return load_error(LoadResult::InvalidFormat, "truncated track block");
            const auto* encoded = bytes.data() + offset;
            Package::Block block;
            block.kind = encoded[0];
            block.index = read_le_u16(encoded + 2);
            const uint32_t size = read_le_u32(encoded + 4);
            block.before = read_le_u64(encoded + 16);
            block.after = read_le_u64(encoded + 24);
            const uint32_t key = block.kind * 65536u + block.index;
            const bool list = block.kind >= 5 && block.kind <= 7;
            if (block.kind > 9 || encoded[1] || read_le_u64(encoded + 8) ||
                (list ? block.index >= candidate.row_count : block.index != 0) ||
                (i && key <= previous) || !size || size > bytes.size() - offset - 32 ||
                (size % (list ? 8u : block.kind == 8 ? 28u : block.kind == 0 ? 20u : 16u)))
                return load_error(LoadResult::InvalidFormat, "invalid track block descriptor");
            offset += 32;
            block.data.assign(bytes.begin() + offset, bytes.begin() + offset + size);
            if (hash_bytes(block.data.data(), size) != block.after)
                return load_error(LoadResult::InvalidFormat, "track block result hash mismatch");
            candidate.blocks.push_back(std::move(block));
            previous = key;
            offset += size;
        }
        if (offset != bytes.size()) return load_error(LoadResult::InvalidFormat, "trailing track bundle data");
        candidate.id = hash_bytes(bytes.data(), bytes.size());
        if (!candidate.id) candidate.id = 1;
        auto next = std::make_shared<const Package>(std::move(candidate));
        std::atomic_store_explicit(&g_package, std::move(next), std::memory_order_release);
        g_last_error.store(nullptr, std::memory_order_release);
        return LoadResult::Loaded;
    }
    if (edit_count > static_cast<uint32_t>(candidate.row_count) * kSlotsPerRow) {
        return load_error(LoadResult::InvalidFormat, "too many PVS edits for declared rows");
    }
    if (payload_size != edit_count * kEditSize ||
        static_cast<uint64_t>(kHeaderSize) + payload_size != bytes.size()) {
        return load_error(LoadResult::InvalidFormat, "PVS edit payload length is inconsistent");
    }
    if (read_le_u64(header + 56) != 0) {
        return load_error(LoadResult::InvalidFormat, "reserved Track Lab header bytes are nonzero");
    }

    candidate.edits.reserve(edit_count);
    uint32_t previous_coordinate = 0;
    bool have_previous_coordinate = false;
    for (uint32_t index = 0; index < edit_count; ++index) {
        const uint8_t* const encoded =
            bytes.data() + kHeaderSize + static_cast<size_t>(index) * kEditSize;
        Edit edit;
        edit.row = read_le_u16(encoded);
        edit.slot = encoded[2];
        const uint8_t reserved = encoded[3];
        edit.expected_old = read_le_s16(encoded + 4);
        edit.replacement = read_le_s16(encoded + 6);

        if (reserved != 0) {
            return load_error(LoadResult::InvalidFormat, "reserved PVS edit byte is nonzero");
        }
        if (edit.row >= candidate.row_count || edit.slot >= kSlotsPerRow) {
            return load_error(LoadResult::InvalidFormat, "PVS edit coordinate is outside the table");
        }
        if (edit.replacement != -1 &&
            (edit.replacement < 0 || edit.replacement >= candidate.row_count)) {
            return load_error(LoadResult::InvalidFormat, "replacement is not a valid segment or -1");
        }

        const uint32_t coordinate =
            static_cast<uint32_t>(edit.row) * kSlotsPerRow + edit.slot;
        if (have_previous_coordinate && coordinate <= previous_coordinate) {
            return load_error(LoadResult::InvalidFormat,
                              "PVS edits must be sorted and have unique coordinates");
        }
        previous_coordinate = coordinate;
        have_previous_coordinate = true;
        candidate.edits.push_back(edit);
    }

    candidate.id = hash_bytes(bytes.data(), bytes.size());
    // Zero is the public sentinel for "disabled". Preserve deterministic ids
    // even for the theoretical package whose FNV value is exactly zero.
    if (candidate.id == 0) {
        candidate.id = 1;
    }
    auto next_package = std::make_shared<const Package>(std::move(candidate));
    std::atomic_store_explicit(&g_package, std::shared_ptr<const Package>{std::move(next_package)}, std::memory_order_release);
    g_last_error.store(nullptr, std::memory_order_release);
    return LoadResult::Loaded;
}

void disable() {
    std::atomic_store_explicit(&g_package, std::shared_ptr<const Package>{}, std::memory_order_release);
    g_last_error.store(nullptr, std::memory_order_release);
}

ApplyResult apply_to_active_track(uint8_t* rdram) noexcept {
    const std::shared_ptr<const Package> package =
        std::atomic_load_explicit(&g_package, std::memory_order_acquire);
    if (!package) {
        return ApplyResult::Disabled;
    }
    if (rdram == nullptr) {
        return ApplyResult::BadContext;
    }

    const int16_t live_circuit =
        read_guest_halfword(rdram, kCurrentCircuitAddress);
    if (live_circuit != package->circuit) {
        return ApplyResult::WrongCircuit;
    }

    const uint32_t context =
        read_guest_word(rdram, kTrackContextPointerAddress);
    if ((context & 3u) != 0 || !is_kseg0_range(context, 12)) {
        return ApplyResult::BadContext;
    }
    const uint32_t pvs_base = read_guest_word(rdram, context + 4u);
    const uint32_t pvs_end = read_guest_word(rdram, context + 8u);
    if ((pvs_base & 1u) != 0 || (pvs_end & 1u) != 0 ||
        pvs_end < pvs_base || !is_kseg0_range(pvs_base, pvs_end - pvs_base)) {
        return ApplyResult::BadContext;
    }

    const uint32_t pvs_bytes = pvs_end - pvs_base;
    if (pvs_bytes % kPvsRowSize != 0) {
        return ApplyResult::BadContext;
    }
    const uint32_t live_row_count = pvs_bytes / kPvsRowSize;
    if (live_row_count < kMinRows || live_row_count > kMaxRows ||
        live_row_count != package->row_count) {
        return ApplyResult::BadContext;
    }

    if (!package->blocks.empty()) return apply_bundle(rdram, context, *package);

    const uint64_t live_hash =
        hash_live_pvs(rdram, pvs_base, package->row_count);
    if (live_hash != package->base_hash) {
        return live_hash == package->patched_hash
                   ? ApplyResult::AlreadyApplied
                   : ApplyResult::BaseMismatch;
    }

    bool changes_value = false;
    for (const Edit& edit : package->edits) {
        const uint32_t address = pvs_base +
            static_cast<uint32_t>(edit.row) * kPvsRowSize +
            static_cast<uint32_t>(edit.slot) * 2u;
        const int16_t live_value = read_guest_halfword(rdram, address);
        if (live_value != edit.expected_old) {
            return ApplyResult::BaseMismatch;
        }
        changes_value = changes_value || live_value != edit.replacement;
    }

    if (!changes_value) {
        return ApplyResult::AlreadyApplied;
    }

    for (const Edit& edit : package->edits) {
        const uint32_t address = pvs_base +
            static_cast<uint32_t>(edit.row) * kPvsRowSize +
            static_cast<uint32_t>(edit.slot) * 2u;
        write_guest_halfword(rdram, address, edit.replacement);
    }
    return ApplyResult::Applied;
}

uint64_t active_package_id() noexcept {
    const std::shared_ptr<const Package> package =
        std::atomic_load_explicit(&g_package, std::memory_order_acquire);
    return package ? package->id : 0;
}

const char* apply_result_name(ApplyResult result) noexcept {
    switch (result) {
        case ApplyResult::Disabled:       return "disabled";
        case ApplyResult::WrongCircuit:   return "wrong circuit";
        case ApplyResult::BadContext:     return "bad track context";
        case ApplyResult::BaseMismatch:   return "track fingerprint mismatch";
        case ApplyResult::AlreadyApplied: return "already applied";
        case ApplyResult::Applied:        return "applied";
    }
    return "unknown result";
}

const char* last_error() noexcept {
    return g_last_error.load(std::memory_order_acquire);
}

} // namespace lambo::track_patch

namespace {

void log_hook_result(const char* hook,
                     lambo::track_patch::ApplyResult result) {
    using lambo::track_patch::ApplyResult;
    if (result == ApplyResult::Disabled) {
        return;
    }

    const unsigned long long id = static_cast<unsigned long long>(
        lambo::track_patch::active_package_id());
    const char* const result_name =
        lambo::track_patch::apply_result_name(result);
    const bool savestate_mismatch =
        result == ApplyResult::BaseMismatch &&
        std::strcmp(hook, "post-savestate-load") == 0;
    if ((result == ApplyResult::BadContext ||
         result == ApplyResult::BaseMismatch) && !savestate_mismatch) {
        LAMBO_LOG_WARN("track", "%s package %016llx: %s\n",
                       hook, id, result_name);
    } else {
        // WrongCircuit is an expected skip when a correction's other circuit
        // is loaded. Applied/idempotent outcomes are useful at info level but
        // should not open a console under the default warning threshold.
        LAMBO_LOG_INFO("track", "%s package %016llx: %s\n",
                       hook, id, result_name);
    }
}

} // namespace

extern "C" void lambo_track_patch_on_track_loaded(uint8_t* rdram) {
    const auto result = lambo::track_patch::apply_to_active_track(rdram);
    log_hook_result("post-track-load", result);
}

extern "C" void lambo_track_patch_on_savestate_loaded(uint8_t* rdram) {
    const auto result = lambo::track_patch::apply_to_active_track(rdram);
    log_hook_result("post-savestate-load", result);
}

extern "C" uint64_t lambo_track_patch_active_package_id(void) {
    return lambo::track_patch::active_package_id();
}

#include "lambo_track_patch.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

#include "lambo_log.h"

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
    static_cast<size_t>(kHeaderSize) +
    static_cast<size_t>(kMaxEdits) * kEditSize;

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
    bool enabled = false;
    uint8_t circuit = 0;
    uint16_t row_count = 0;
    uint64_t base_hash = 0;
    uint64_t patched_hash = 0;
    uint64_t id = 0;
    std::vector<Edit> edits;
};

Package g_package;
std::string g_last_error;

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

LoadResult load_error(LoadResult result, const char* message) {
    g_last_error = message;
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
    // N64ModernRuntime stores word-swapped RDRAM: aligned guest words are
    // native little-endian values, while guest halfwords use address XOR 2.
    const uint32_t offset = address - kRdramBase;
    return read_le_u32(rdram + offset);
}

int16_t read_guest_halfword(const uint8_t* rdram, uint32_t address) noexcept {
    const uint32_t offset = (address - kRdramBase) ^ 2u;
    return read_le_s16(rdram + offset);
}

void write_guest_halfword(uint8_t* rdram, uint32_t address, int16_t value) noexcept {
    const uint32_t offset = (address - kRdramBase) ^ 2u;
    const uint16_t bits = static_cast<uint16_t>(value);
    rdram[offset] = static_cast<uint8_t>(bits);
    rdram[offset + 1] = static_cast<uint8_t>(bits >> 8);
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

uint64_t hash_patched_pvs(const uint8_t* rdram, uint32_t pvs_base,
                          const Package& package) noexcept {
    uint64_t hash = kFnvOffsetBasis;
    size_t next_edit = 0;
    const uint32_t value_count =
        static_cast<uint32_t>(package.row_count) * kSlotsPerRow;
    for (uint32_t index = 0; index < value_count; ++index) {
        int16_t value = read_guest_halfword(rdram, pvs_base + index * 2u);
        if (next_edit < package.edits.size()) {
            const Edit& edit = package.edits[next_edit];
            const uint32_t edit_index =
                static_cast<uint32_t>(edit.row) * kSlotsPerRow + edit.slot;
            if (edit_index == index) {
                value = edit.replacement;
                ++next_edit;
            }
        }
        const uint16_t bits = static_cast<uint16_t>(value);
        hash = fnv_byte(hash, static_cast<uint8_t>(bits >> 8));
        hash = fnv_byte(hash, static_cast<uint8_t>(bits));
    }
    return hash;
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
    if (file_size > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        return load_error(LoadResult::InvalidFormat, "package is too large to read");
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(file_size));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return load_error(LoadResult::IoError, "could not read complete Track Lab package");
    }

    const uint8_t* const header = bytes.data();
    if (std::memcmp(header, kMagic.data(), kMagic.size()) != 0) {
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
    if (read_le_u32(header + 24) != kPvsCorrectionFlag) {
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
    candidate.enabled = true;
    g_package = std::move(candidate);
    g_last_error.clear();
    return LoadResult::Loaded;
}

void disable() {
    g_package = Package{};
    g_last_error.clear();
}

ApplyResult apply_to_active_track(uint8_t* rdram) noexcept {
    const Package& package = g_package;
    if (!package.enabled) {
        return ApplyResult::Disabled;
    }
    if (rdram == nullptr) {
        return ApplyResult::BadContext;
    }

    const int16_t live_circuit =
        read_guest_halfword(rdram, kCurrentCircuitAddress);
    if (live_circuit != package.circuit) {
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
        live_row_count != package.row_count) {
        return ApplyResult::BadContext;
    }

    const uint64_t live_hash =
        hash_live_pvs(rdram, pvs_base, package.row_count);
    if (live_hash != package.base_hash) {
        return live_hash == package.patched_hash
                   ? ApplyResult::AlreadyApplied
                   : ApplyResult::BaseMismatch;
    }

    bool changes_value = false;
    for (const Edit& edit : package.edits) {
        const uint32_t address = pvs_base +
            static_cast<uint32_t>(edit.row) * kPvsRowSize +
            static_cast<uint32_t>(edit.slot) * 2u;
        const int16_t live_value = read_guest_halfword(rdram, address);
        if (live_value != edit.expected_old) {
            return ApplyResult::BaseMismatch;
        }
        changes_value = changes_value || live_value != edit.replacement;
    }

    // Compute the complete result before touching memory. This catches a
    // malformed/tampered package even when its declared base fingerprint is
    // correct and guarantees all-or-nothing writes.
    if (hash_patched_pvs(rdram, pvs_base, package) != package.patched_hash) {
        return ApplyResult::BaseMismatch;
    }
    if (!changes_value) {
        return ApplyResult::AlreadyApplied;
    }

    for (const Edit& edit : package.edits) {
        const uint32_t address = pvs_base +
            static_cast<uint32_t>(edit.row) * kPvsRowSize +
            static_cast<uint32_t>(edit.slot) * 2u;
        write_guest_halfword(rdram, address, edit.replacement);
    }
    return ApplyResult::Applied;
}

uint64_t active_package_id() noexcept {
    return g_package.enabled ? g_package.id : 0;
}

const char* apply_result_name(ApplyResult result) noexcept {
    switch (result) {
        case ApplyResult::Disabled:       return "disabled";
        case ApplyResult::WrongCircuit:   return "wrong circuit";
        case ApplyResult::BadContext:     return "bad track context";
        case ApplyResult::BaseMismatch:   return "PVS fingerprint mismatch";
        case ApplyResult::AlreadyApplied: return "already applied";
        case ApplyResult::Applied:        return "applied";
    }
    return "unknown result";
}

const std::string& last_error() noexcept {
    return g_last_error;
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
    if (result == ApplyResult::BadContext ||
        result == ApplyResult::BaseMismatch) {
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

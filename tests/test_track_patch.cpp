#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "lambo_track_patch.h"

namespace {

constexpr uint32_t kRdramBase = 0x80000000u;
constexpr uint32_t kContextPointerAddress = 0x80098238u;
constexpr uint32_t kCurrentCircuitAddress = 0x800CE794u;
constexpr uint32_t kContextAddress = 0x80001000u;
constexpr uint32_t kPvsAddress = 0x80002000u;
constexpr uint64_t kRomHash = UINT64_C(0x525201d7279f34e3);
constexpr uint64_t kFnvOffsetBasis = UINT64_C(14695981039346656037);
constexpr uint64_t kFnvPrime = UINT64_C(1099511628211);

struct TestEdit {
    uint16_t row;
    uint8_t slot;
    int16_t expected;
    int16_t replacement;
};

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void put_u16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void put_s16(std::vector<uint8_t>& bytes, size_t offset, int16_t value) {
    put_u16(bytes, offset, static_cast<uint16_t>(value));
}

void put_u32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8] = static_cast<uint8_t>(value >> shift);
    }
}

void put_u64(std::vector<uint8_t>& bytes, size_t offset, uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes[offset + shift / 8] = static_cast<uint8_t>(value >> shift);
    }
}

uint64_t fnv_bytes(const uint8_t* bytes, size_t size) {
    uint64_t hash = kFnvOffsetBasis;
    for (size_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * kFnvPrime;
    }
    return hash;
}

uint64_t hash_table(const std::vector<int16_t>& table) {
    uint64_t hash = kFnvOffsetBasis;
    for (int16_t value : table) {
        const uint16_t bits = static_cast<uint16_t>(value);
        hash = (hash ^ static_cast<uint8_t>(bits >> 8)) * kFnvPrime;
        hash = (hash ^ static_cast<uint8_t>(bits)) * kFnvPrime;
    }
    return hash;
}

std::vector<uint8_t> make_package(uint8_t circuit, uint16_t rows,
                                  const std::vector<int16_t>& base_table,
                                  const std::vector<TestEdit>& edits) {
    std::vector<int16_t> patched = base_table;
    for (const TestEdit& edit : edits) {
        const size_t index = static_cast<size_t>(edit.row) * 10 + edit.slot;
        if (index < patched.size()) {
            patched[index] = edit.replacement;
        }
    }

    const uint32_t payload_size = static_cast<uint32_t>(edits.size() * 8);
    std::vector<uint8_t> bytes(64 + payload_size);
    const char magic[8] = {'A', 'L', 'T', 'R', 'K', 'P', 'V', '1'};
    for (size_t i = 0; i < sizeof(magic); ++i) {
        bytes[i] = static_cast<uint8_t>(magic[i]);
    }
    put_u16(bytes, 8, 1);
    put_u16(bytes, 10, 64);
    put_u32(bytes, 12, static_cast<uint32_t>(bytes.size()));
    put_u64(bytes, 16, kRomHash);
    put_u32(bytes, 24, 1);
    bytes[28] = circuit;
    bytes[29] = 10;
    put_u16(bytes, 30, rows);
    put_u32(bytes, 32, static_cast<uint32_t>(edits.size()));
    put_u32(bytes, 36, payload_size);
    put_u64(bytes, 40, hash_table(base_table));
    put_u64(bytes, 48, hash_table(patched));

    for (size_t index = 0; index < edits.size(); ++index) {
        const TestEdit& edit = edits[index];
        const size_t offset = 64 + index * 8;
        put_u16(bytes, offset, edit.row);
        bytes[offset + 2] = edit.slot;
        put_s16(bytes, offset + 4, edit.expected);
        put_s16(bytes, offset + 6, edit.replacement);
    }
    return bytes;
}

bool write_file(const std::filesystem::path& path,
                const std::vector<uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

void write_word(std::vector<uint8_t>& rdram, uint32_t address,
                uint32_t value) {
    const size_t offset = address - kRdramBase;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        rdram[offset + shift / 8] = static_cast<uint8_t>(value >> shift);
    }
}

void write_halfword(std::vector<uint8_t>& rdram, uint32_t address,
                    int16_t value) {
    const size_t offset = (address - kRdramBase) ^ 2u;
    const uint16_t bits = static_cast<uint16_t>(value);
    rdram[offset] = static_cast<uint8_t>(bits);
    rdram[offset + 1] = static_cast<uint8_t>(bits >> 8);
}

int16_t read_halfword(const std::vector<uint8_t>& rdram, uint32_t address) {
    const size_t offset = (address - kRdramBase) ^ 2u;
    const uint16_t bits = static_cast<uint16_t>(rdram[offset]) |
                          (static_cast<uint16_t>(rdram[offset + 1]) << 8);
    return static_cast<int16_t>(bits);
}

void set_live_track(std::vector<uint8_t>& rdram, uint8_t circuit,
                    const std::vector<int16_t>& table, uint16_t rows) {
    std::fill(rdram.begin(), rdram.end(), 0);
    write_word(rdram, kContextPointerAddress, kContextAddress);
    write_word(rdram, kContextAddress + 4, kPvsAddress);
    write_word(rdram, kContextAddress + 8,
               kPvsAddress + static_cast<uint32_t>(rows) * 20u);
    write_halfword(rdram, kCurrentCircuitAddress, circuit);
    for (size_t i = 0; i < table.size(); ++i) {
        write_halfword(rdram, kPvsAddress + static_cast<uint32_t>(i * 2),
                       table[i]);
    }
}

int run_python_interop(const std::filesystem::path& package_path) {
    using lambo::track_patch::ApplyResult;
    using lambo::track_patch::LoadResult;

    // This is the logical PVS table produced by make_rdram() in
    // tests/test_track_lab.py.  Python owns the package bytes; this mode only
    // supplies the corresponding live guest memory and checks the result.
    const uint16_t rows = 3;
    const uint8_t circuit = 2;
    const std::vector<int16_t> base_table = {
         0, -1,  2,     -2, -32768,  1, -1, -1, -1, -1,
         1,  0, -1,     -1,     -1,  2, -1, -1, -1, -1,
         2,  1,  0,     -1,     -1, -1, -1, -1, -1, -1,
    };
    auto expected_table = base_table;
    expected_table[0 * 10 + 1] = 2;
    expected_table[0 * 10 + 3] = 1;
    expected_table[0 * 10 + 4] = 0;
    expected_table[1 * 10 + 5] = -1;

    lambo::track_patch::disable();
    const auto load_result = lambo::track_patch::load_package(package_path);
    if (load_result != LoadResult::Loaded) {
        std::cerr << "FAIL: native loader rejected Python package: "
                  << lambo::track_patch::last_error() << '\n';
        return 1;
    }

    std::vector<uint8_t> rdram(8 * 1024 * 1024);
    set_live_track(rdram, circuit, base_table, rows);
    const auto apply_result =
        lambo::track_patch::apply_to_active_track(rdram.data());
    if (apply_result != ApplyResult::Applied) {
        std::cerr << "FAIL: native runtime did not apply Python package: "
                  << lambo::track_patch::apply_result_name(apply_result) << '\n';
        return 1;
    }

    for (size_t index = 0; index < expected_table.size(); ++index) {
        const int16_t actual = read_halfword(
            rdram, kPvsAddress + static_cast<uint32_t>(index * 2));
        if (actual != expected_table[index]) {
            std::cerr << "FAIL: Python package produced " << actual
                      << " at PVS cell " << index << "; expected "
                      << expected_table[index] << '\n';
            return 1;
        }
    }

    if (lambo::track_patch::apply_to_active_track(rdram.data()) !=
        ApplyResult::AlreadyApplied) {
        std::cerr << "FAIL: Python package is not idempotent after native application\n";
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    using lambo::track_patch::ApplyResult;
    using lambo::track_patch::LoadResult;

    if (argc == 3 && std::string(argv[1]) == "--python-interop") {
        return run_python_interop(argv[2]);
    }
    if (argc != 1) {
        std::cerr << "usage: lambo_track_patch_tests "
                     "[--python-interop package.altrk]\n";
        return 2;
    }

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto package_path = std::filesystem::temp_directory_path() /
        ("lambo-track-patch-test-" + std::to_string(unique) + ".altrk");
    std::error_code cleanup_error;
    std::filesystem::remove(package_path, cleanup_error);

    lambo::track_patch::disable();
    std::vector<uint8_t> rdram(8 * 1024 * 1024);
    expect(lambo::track_patch::active_package_id() == 0,
           "disabled state has package id zero");
    expect(lambo::track_patch::apply_to_active_track(rdram.data()) ==
               ApplyResult::Disabled,
           "disabled patch does not inspect RDRAM");
    expect(lambo::track_patch::load_package(package_path) == LoadResult::IoError,
           "missing package reports I/O error");
    expect(!lambo::track_patch::last_error().empty(),
           "failed package load records a diagnostic");

    const uint16_t rows = 3;
    const uint8_t circuit = 1;
    const std::vector<int16_t> base_table = {
         0,  1, -1, -2, -1, -1, -1, -1, -1, -1,
         0,  2, -1, -1, -1, -1, -1, -1, -1, -1,
         1,  2,  0, -1, -1, -1, -1, -1, -1, -1,
    };
    const std::vector<TestEdit> edits = {
        {0, 2, -1, 2},
        {1, 0,  0, -1},
    };
    const auto valid_package = make_package(circuit, rows, base_table, edits);

    auto malformed = valid_package;
    malformed[0] = 'X';
    expect(write_file(package_path, malformed), "writes malformed-magic fixture");
    expect(lambo::track_patch::load_package(package_path) == LoadResult::InvalidFormat,
           "bad magic is rejected");

    malformed = valid_package;
    put_u64(malformed, 16, kRomHash ^ 1u);
    expect(write_file(package_path, malformed), "writes wrong-ROM fixture");
    expect(lambo::track_patch::load_package(package_path) ==
               LoadResult::IncompatibleRom,
           "wrong ROM fingerprint is distinguished from malformed data");

    malformed = valid_package;
    put_u32(malformed, 24, 3);
    expect(write_file(package_path, malformed), "writes unsupported-flags fixture");
    expect(lambo::track_patch::load_package(package_path) == LoadResult::InvalidFormat,
           "unknown flag bits are rejected");

    malformed = valid_package;
    malformed[56] = 1;
    expect(write_file(package_path, malformed), "writes reserved-header fixture");
    expect(lambo::track_patch::load_package(package_path) == LoadResult::InvalidFormat,
           "nonzero header reservation is rejected");

    malformed = valid_package;
    malformed[64 + 3] = 1;
    expect(write_file(package_path, malformed), "writes reserved-edit fixture");
    expect(lambo::track_patch::load_package(package_path) == LoadResult::InvalidFormat,
           "nonzero edit reservation is rejected");

    const std::vector<TestEdit> duplicate_edits = {
        {0, 2, -1, 2},
        {0, 2, -1, 1},
    };
    malformed = make_package(circuit, rows, base_table, duplicate_edits);
    expect(write_file(package_path, malformed), "writes duplicate-edit fixture");
    expect(lambo::track_patch::load_package(package_path) == LoadResult::InvalidFormat,
           "duplicate edit coordinates are rejected");

    malformed = valid_package;
    put_s16(malformed, 64 + 6, 3);
    expect(write_file(package_path, malformed), "writes bad-segment fixture");
    expect(lambo::track_patch::load_package(package_path) == LoadResult::InvalidFormat,
           "replacement segment must be in range or -1");
    expect(lambo::track_patch::active_package_id() == 0,
           "rejected packages never become active");

    expect(write_file(package_path, valid_package), "writes valid package fixture");
    expect(lambo::track_patch::load_package(package_path) == LoadResult::Loaded,
           "valid portable package loads");
    expect(lambo::track_patch::last_error().empty(),
           "successful load clears the load diagnostic");
    const uint64_t valid_id = fnv_bytes(valid_package.data(), valid_package.size());
    expect(valid_id != 0 && lambo::track_patch::active_package_id() == valid_id,
           "active id is FNV-1a of the complete package");
    expect(lambo_track_patch_active_package_id() == valid_id,
           "C package-id wrapper uses the same active state");
    expect(std::string(lambo::track_patch::apply_result_name(ApplyResult::Applied)) ==
               "applied" &&
               std::string(lambo::track_patch::apply_result_name(
                   ApplyResult::BaseMismatch)) == "PVS fingerprint mismatch",
           "apply results have stable runtime diagnostic names");

    malformed = valid_package;
    malformed[0] = 'X';
    expect(write_file(package_path, malformed), "rewrites invalid reload fixture");
    expect(lambo::track_patch::load_package(package_path) == LoadResult::InvalidFormat,
           "invalid reload is rejected");
    expect(lambo::track_patch::active_package_id() == valid_id,
           "failed reload leaves the known-good package active");

    set_live_track(rdram, circuit + 1, base_table, rows);
    auto before = rdram;
    expect(lambo::track_patch::apply_to_active_track(rdram.data()) ==
               ApplyResult::WrongCircuit,
           "package only applies to its declared circuit");
    expect(rdram == before, "wrong-circuit rejection performs no writes");

    set_live_track(rdram, circuit, base_table, rows);
    write_word(rdram, kContextPointerAddress, 0x7FFFFFFCu);
    before = rdram;
    expect(lambo::track_patch::apply_to_active_track(rdram.data()) ==
               ApplyResult::BadContext,
           "out-of-RDRAM track context is rejected");
    expect(rdram == before, "bad-context rejection performs no writes");

    set_live_track(rdram, circuit, base_table, rows);
    write_word(rdram, kContextAddress + 8, kPvsAddress + 2u * 20u);
    before = rdram;
    expect(lambo::track_patch::apply_to_active_track(rdram.data()) ==
               ApplyResult::BadContext,
           "live PVS row count must exactly match the package");
    expect(rdram == before, "row-count rejection performs no writes");

    set_live_track(rdram, circuit, base_table, rows);
    write_halfword(rdram, kPvsAddress + 2u * 20u + 8u * 2u, 2);
    before = rdram;
    expect(lambo::track_patch::apply_to_active_track(rdram.data()) ==
               ApplyResult::BaseMismatch,
           "unknown live PVS fingerprint is rejected");
    expect(rdram == before, "base-fingerprint rejection performs no writes");

    const std::vector<TestEdit> wrong_expected_edits = {
        {0, 2, 0, 2},
        {1, 0, 0, -1},
    };
    const auto wrong_expected_package =
        make_package(circuit, rows, base_table, wrong_expected_edits);
    expect(write_file(package_path, wrong_expected_package),
           "writes expected-value mismatch fixture");
    expect(lambo::track_patch::load_package(package_path) == LoadResult::Loaded,
           "expected values remain raw signed halfwords at parse time");
    expect(lambo::track_patch::active_package_id() != valid_id,
           "complete-package id includes edit preconditions");
    set_live_track(rdram, circuit, base_table, rows);
    before = rdram;
    expect(lambo::track_patch::apply_to_active_track(rdram.data()) ==
               ApplyResult::BaseMismatch,
           "per-edit expected value is checked after the base fingerprint");
    expect(rdram == before, "expected-value rejection is transactional");

    auto wrong_patched_hash = valid_package;
    put_u64(wrong_patched_hash, 48, hash_table(base_table));
    expect(write_file(package_path, wrong_patched_hash),
           "writes wrong-result-fingerprint fixture");
    expect(lambo::track_patch::load_package(package_path) == LoadResult::Loaded,
           "result fingerprint is checked against live data at apply time");
    set_live_track(rdram, circuit, base_table, rows);
    before = rdram;
    expect(lambo::track_patch::apply_to_active_track(rdram.data()) ==
               ApplyResult::BaseMismatch,
           "simulated patched fingerprint must match before writes");
    expect(rdram == before, "result-fingerprint rejection is transactional");

    expect(write_file(package_path, valid_package), "restores valid fixture");
    expect(lambo::track_patch::load_package(package_path) == LoadResult::Loaded,
           "valid package reloads after rejection cases");
    set_live_track(rdram, circuit, base_table, rows);
    expect(lambo::track_patch::apply_to_active_track(rdram.data()) ==
               ApplyResult::Applied,
           "known base PVS is patched successfully");
    expect(read_halfword(rdram, kPvsAddress + 0u * 20u + 2u * 2u) == 2 &&
               read_halfword(rdram, kPvsAddress + 1u * 20u) == -1,
           "all declared edits are written");
    expect(read_halfword(rdram, kPvsAddress + 0u * 20u + 3u * 2u) == -2,
           "unedited raw negative PVS values are preserved");
    before = rdram;
    expect(lambo::track_patch::apply_to_active_track(rdram.data()) ==
               ApplyResult::AlreadyApplied,
           "second application is idempotent");
    expect(rdram == before, "already-applied path performs no writes");

    set_live_track(rdram, circuit, base_table, rows);
    lambo_track_patch_on_track_loaded(rdram.data());
    expect(read_halfword(rdram, kPvsAddress + 0u * 20u + 2u * 2u) == 2,
           "post-track-load C wrapper applies the package");
    before = rdram;
    lambo_track_patch_on_savestate_loaded(rdram.data());
    expect(rdram == before,
           "post-savestate C wrapper is quiet and idempotent on patched memory");

    const std::vector<TestEdit> no_edits;
    const auto no_op_package =
        make_package(circuit, rows, base_table, no_edits);
    expect(no_op_package.size() == 64, "zero-edit package is header-only");
    expect(write_file(package_path, no_op_package), "writes zero-edit fixture");
    expect(lambo::track_patch::load_package(package_path) == LoadResult::Loaded,
           "zero-edit package is a valid compile/check artifact");
    set_live_track(rdram, circuit, base_table, rows);
    before = rdram;
    expect(lambo::track_patch::apply_to_active_track(rdram.data()) ==
               ApplyResult::AlreadyApplied,
           "zero-edit package reports that its target is already present");
    expect(rdram == before, "zero-edit package performs no writes");

    lambo::track_patch::disable();
    expect(lambo::track_patch::active_package_id() == 0 &&
               lambo_track_patch_active_package_id() == 0,
           "disable clears native and C-wrapper package identity");
    before = rdram;
    lambo_track_patch_on_track_loaded(rdram.data());
    expect(rdram == before, "disabled wrapper is a no-op");

    std::filesystem::remove(package_path, cleanup_error);
    return failures == 0 ? 0 : 1;
}

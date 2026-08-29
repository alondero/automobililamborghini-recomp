#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "json/json.hpp"
#include "lambo_config.h"
#include "lambo_player_name.h"
#include "recomp.h"

namespace {

constexpr uint32_t kCurrentDriverAddress = 0x800CE6A6u;
constexpr uint32_t kPlayerOneNameAddress = 0x800A4826u;
constexpr uint32_t kRecordsProfileAddress = 0x800A4160u;
constexpr std::size_t kNameBufferSize = 13;
constexpr std::size_t kRecordsProfileSize = 12;
constexpr std::size_t kRecordsTableOffset = 0x402;
constexpr std::size_t kLeaderboardNameStride = 14;
constexpr std::size_t kLeaderboardNameSlots = 5;

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream in(path);
    nlohmann::json result;
    in >> result;
    return result;
}

void write_guest_bytes(uint8_t* rdram, gpr address, const std::string& text,
                       std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        MEM_B(i, address) =
            i < text.size() ? static_cast<unsigned char>(text[i]) : 0;
    }
}

std::string read_guest_string(uint8_t* rdram, gpr address, std::size_t max_length) {
    std::string result;
    for (std::size_t i = 0; i < max_length; ++i) {
        const unsigned char ch = MEM_BU(i, address);
        if (ch == 0) break;
        result.push_back(static_cast<char>(ch));
    }
    return result;
}

std::vector<uint8_t> read_guest_bytes(uint8_t* rdram, gpr address,
                                      std::size_t size) {
    std::vector<uint8_t> result;
    result.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        result.push_back(MEM_BU(i, address));
    }
    return result;
}

} // namespace

// lambo_player_name.cpp uses the config module only for its default config path.
// The player-name test overrides that path with LAMBO_PLAYER_CONFIG, but still
// links lambo_config.cpp for the production dependency.
namespace ultramodern::renderer {
void set_graphics_config(const GraphicsConfig&) {}
}

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() /
                     ("lambo-player-name-test-" + std::to_string(unique));
    const auto player_path = dir / "player.json";
#if defined(_WIN32)
    _putenv_s("LAMBO_PLAYER_CONFIG", player_path.string().c_str());
#else
    setenv("LAMBO_PLAYER_CONFIG", player_path.string().c_str(), 1);
#endif

    std::vector<uint8_t> memory(0x800000);
    uint8_t* rdram = memory.data();
    const gpr driver_index = (gpr)(int32_t)kCurrentDriverAddress;
    const gpr player_one_name = (gpr)(int32_t)kPlayerOneNameAddress;
    const gpr records_profile = (gpr)(int32_t)kRecordsProfileAddress;
    const gpr high_score_names = records_profile +
                                 static_cast<gpr>(kRecordsTableOffset);

    MEM_H(0, driver_index) = 1;
    const std::string edited = "CHAMP";
    write_guest_bytes(rdram, player_one_name, edited, kNameBufferSize);
    write_guest_bytes(rdram, records_profile, "TITUS LAM64", kRecordsProfileSize);
    for (std::size_t slot = 0; slot < kLeaderboardNameSlots; ++slot) {
        write_guest_bytes(rdram, high_score_names +
                                      static_cast<gpr>(slot * kLeaderboardNameStride),
                          "TITUS" + std::to_string(slot),
                          kLeaderboardNameStride);
    }
    const auto original_profile =
        read_guest_bytes(rdram, records_profile, kRecordsProfileSize);
    const auto original_high_scores = read_guest_bytes(
        rdram, high_score_names, kLeaderboardNameSlots * kLeaderboardNameStride);

    lambo_player_name_save(rdram);
    expect(read_json(player_path).at("name") == edited,
           "confirmed ROM name persists");
    expect(read_guest_bytes(rdram, records_profile, kRecordsProfileSize) ==
               original_profile,
           "saving a name does not overwrite the records profile");
    expect(read_guest_bytes(rdram, high_score_names,
                            kLeaderboardNameSlots * kLeaderboardNameStride) ==
               original_high_scores,
           "saving a name does not overwrite any leaderboard row");

    write_guest_bytes(rdram, player_one_name, std::string{}, kNameBufferSize);
    lambo_player_name_seed(rdram);
    expect(read_guest_string(rdram, player_one_name, kNameBufferSize) == edited,
           "next run seeds the persisted name into the ROM buffer");
    expect(read_guest_bytes(rdram, records_profile, kRecordsProfileSize) ==
               original_profile,
           "seeding a name does not overwrite the records profile");
    expect(read_guest_bytes(rdram, high_score_names,
                            kLeaderboardNameSlots * kLeaderboardNameStride) ==
               original_high_scores,
           "seeding a name does not overwrite any leaderboard row");

    const std::string max_length_name = "ABCDEFGHIJKL";
    write_guest_bytes(rdram, player_one_name, max_length_name, kNameBufferSize);
    lambo_player_name_save(rdram);
    expect(read_json(player_path).at("name") == max_length_name,
           "the ROM's 12-character name limit persists");
    for (std::size_t i = 0; i < kNameBufferSize; ++i) {
        MEM_B(i, player_one_name) = static_cast<int8_t>(0xCC);
    }
    MEM_B(kNameBufferSize, player_one_name) = static_cast<int8_t>(0xA5);
    lambo_player_name_seed(rdram);
    expect(read_guest_string(rdram, player_one_name, kNameBufferSize) ==
               max_length_name,
           "a 12-character name is seeded intact");
    expect(MEM_BU(kNameBufferSize - 1, player_one_name) == 0,
           "a 12-character name is NUL-terminated inside its 13-byte buffer");
    expect(MEM_BU(kNameBufferSize, player_one_name) == 0xA5,
           "seeding a maximum-length name does not cross the buffer boundary");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return failures == 0 ? 0 : 1;
}

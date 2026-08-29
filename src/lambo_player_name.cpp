#include "lambo_player_name.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "json/json.hpp"
#include "lambo_config.h"
#include "lambo_log.h"
#include "recomp.h"

namespace {

// Verified against the ROM's name editor (func_8003CD84 setup and the append /
// delete helpers at 0x8003F3C0-0x8003F4E8). Driver indices are one-based and
// each name occupies 13 bytes: up to 12 keyboard characters plus a NUL.
// The ROM's result insertion at 0x8003F8E8 copies 13 bytes from these buffers
// into only the leaderboard row earned by that driver.
constexpr uint32_t kCurrentDriverAddr = 0x800CE6A6u;
constexpr uint32_t kDriverNamesBase = 0x800A4819u;
constexpr int kPlayerOne = 1;
constexpr std::size_t kNameStride = 13;
constexpr std::size_t kMaxNameLength = kNameStride - 1;
constexpr const char* kPlayerConfigFile = "player.json";

gpr name_addr(int driver) {
    return (gpr)(int32_t)(kDriverNamesBase +
                          static_cast<uint32_t>(driver) *
                              static_cast<uint32_t>(kNameStride));
}

bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > kMaxNameLength) return false;
    for (unsigned char ch : name) {
        if (ch != ' ' && (ch < 'A' || ch > 'Z')) return false;
    }
    return true;
}

std::filesystem::path player_config_path() {
    if (const char* path = std::getenv("LAMBO_PLAYER_CONFIG")) {
        return std::filesystem::path{path};
    }
    return lambo::config::app_config_dir() / kPlayerConfigFile;
}

std::string load_saved_name() {
    const std::filesystem::path path = player_config_path();
    std::ifstream in{path};
    if (!in.good()) return {};
    try {
        nlohmann::json json;
        in >> json;
        const auto field = json.find("name");
        if (field == json.end() || !field->is_string()) return {};
        std::string name = field->get<std::string>();
        return valid_name(name) ? name : std::string{};
    } catch (const nlohmann::json::exception& e) {
        LAMBO_LOG_WARN("name", "%s unparseable (%s); keeping ROM default\n",
                  path.string().c_str(), e.what());
        return {};
    }
}

void save_name(const std::string& name) {
    const std::filesystem::path path = player_config_path();
    const std::filesystem::path tmp = path.string() + ".tmp";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream out{tmp};
    if (!out.good()) {
        LAMBO_LOG_ERROR("name", "cannot write %s\n", tmp.string().c_str());
        return;
    }
    out << nlohmann::json{{"name", name}}.dump(4) << '\n';
    out.flush();
    if (!out.good()) {
        LAMBO_LOG_ERROR("name", "write to %s failed\n", tmp.string().c_str());
        return;
    }
    out.close();

    std::filesystem::rename(tmp, path, ec);
#if defined(_WIN32)
    if (ec) {
        ec.clear();
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
    }
#endif
    if (ec) {
        LAMBO_LOG_ERROR("name", "cannot publish %s\n", path.string().c_str());
    }
}

int current_driver(uint8_t* rdram) {
    return (int16_t)MEM_H(0, (gpr)(int32_t)kCurrentDriverAddr);
}

std::string read_name(uint8_t* rdram, gpr src) {
    std::string name;
    for (std::size_t i = 0; i < kMaxNameLength; ++i) {
        const unsigned char ch = MEM_BU(i, src);
        if (ch == 0) break;
        name.push_back(static_cast<char>(ch));
    }
    return name;
}

void write_name(uint8_t* rdram, gpr dst, const std::string& name) {
    for (std::size_t i = 0; i < kNameStride; ++i) {
        const bool has_character = i < kMaxNameLength && i < name.size();
        MEM_B(i, dst) = has_character ? static_cast<unsigned char>(name[i]) : 0;
    }
}

} // namespace

extern "C" void lambo_player_name_seed(uint8_t* rdram) {
    if (current_driver(rdram) != kPlayerOne) return;

    const std::string saved = load_saved_name();
    if (!valid_name(saved)) return;

    write_name(rdram, name_addr(kPlayerOne), saved);
    LAMBO_LOG_INFO("name", "seeded player name: %s\n", saved.c_str());
}

extern "C" void lambo_player_name_save(uint8_t* rdram) {
    if (current_driver(rdram) != kPlayerOne) return;

    const std::string name = read_name(rdram, name_addr(kPlayerOne));
    if (!valid_name(name)) return;

    save_name(name);
    LAMBO_LOG_INFO("name", "saved player name: %s\n", name.c_str());
}

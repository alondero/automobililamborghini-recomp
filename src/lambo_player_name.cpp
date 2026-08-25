#include "lambo_player_name.h"

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
constexpr uint32_t kCurrentDriverAddr = 0x800CE6A6u;
constexpr uint32_t kDriverNamesBase = 0x800A4819u;
constexpr int kPlayerOne = 1;
constexpr int kNameStride = 13;
constexpr int kMaxNameLength = 12;
constexpr const char* kPlayerConfigFile = "player.json";

gpr name_addr(int driver) {
    return (gpr)(int32_t)(kDriverNamesBase + uint32_t(driver * kNameStride));
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
        LAMBO_LOG("name", "%s unparseable (%s); keeping ROM default\n",
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
        LAMBO_LOG("name", "cannot write %s\n", tmp.string().c_str());
        return;
    }
    out << nlohmann::json{{"name", name}}.dump(4) << '\n';
    out.flush();
    if (!out.good()) {
        LAMBO_LOG("name", "write to %s failed\n", tmp.string().c_str());
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
        LAMBO_LOG("name", "cannot publish %s\n", path.string().c_str());
    }
}

int current_driver(uint8_t* rdram) {
    return (int16_t)MEM_H(0, (gpr)(int32_t)kCurrentDriverAddr);
}

} // namespace

extern "C" void lambo_player_name_seed(uint8_t* rdram) {
    if (current_driver(rdram) != kPlayerOne) return;

    const std::string saved = load_saved_name();
    if (!valid_name(saved)) return;

    const gpr dst = name_addr(kPlayerOne);
    for (int i = 0; i < kNameStride; ++i) {
        MEM_B(i, dst) = i < (int)saved.size() ? saved[(size_t)i] : 0;
    }
    LAMBO_LOG("name", "seeded player name: %s\n", saved.c_str());
}

extern "C" void lambo_player_name_save(uint8_t* rdram) {
    if (current_driver(rdram) != kPlayerOne) return;

    const gpr src = name_addr(kPlayerOne);
    std::string name;
    for (int i = 0; i < kMaxNameLength; ++i) {
        const unsigned char ch = MEM_BU(i, src);
        if (ch == 0) break;
        name.push_back((char)ch);
    }
    if (!valid_name(name)) return;

    save_name(name);
    LAMBO_LOG("name", "saved player name: %s\n", name.c_str());
}

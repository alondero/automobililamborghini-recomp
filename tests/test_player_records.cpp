#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "json/json.hpp"
#include "lambo_player_name.h"
#include "recomp.h"

extern "C" void func_8002A228(uint8_t*, recomp_context*);
extern "C" void func_800401F0(uint8_t*, recomp_context*);
// The lap routine's only external call plays the new-record sound.
extern "C" void func_80066F84(uint8_t*, recomp_context*) {}
namespace lambo::config {
std::filesystem::path app_config_dir() { std::abort(); }
}

namespace {
constexpr gpr addr(uint32_t value) { return (gpr)(int32_t)value; }
constexpr gpr profile = addr(0x800A4160);
constexpr gpr player = addr(0x800A4826);
int failures = 0;
void expect(bool ok, const std::string& message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void put(uint8_t* rdram, gpr address, const std::string& name) {
    for (int i = 0; i < 13; ++i) MEM_B(i, address) = i < name.size() ? name[i] : 0;
}
std::string get(uint8_t* rdram, gpr address) {
    std::string result;
    for (int i = 0; i < 13 && MEM_BU(i, address); ++i)
        result += (char)MEM_BU(i, address);
    return result;
}
void check_name(uint8_t* rdram, gpr address, const std::string& expected,
                const std::string& label) {
    const auto actual = get(rdram, address);
    expect(actual == expected, label + ": expected " + expected + ", got " + actual);
}
}

int main() {
    const auto dir = std::filesystem::temp_directory_path() /
        ("lambo-record-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    const auto path = dir / "player.json";
#ifdef _WIN32
    _putenv_s("LAMBO_PLAYER_CONFIG", path.string().c_str());
#else
    setenv("LAMBO_PLAYER_CONFIG", path.string().c_str(), 1);
#endif
    for (const std::string saved : {"ADAM", "Adam"}) {
        std::ofstream(path) << nlohmann::json{{"name", saved}};
        for (int mode : {0, 2}) {
            for (int mirror : {0, 1}) {
                std::vector<uint8_t> memory(0x800000);
                auto* rdram = memory.data();
                put(rdram, player, "TITUS");
                put(rdram, player + 13, "GUEST");
                put(rdram, profile, "TITUS LAM64");
                // Do not enter the editor: its driver selector is deliberately zero.
                MEM_H(0, addr(0x800CE6B4)) = mode;
                MEM_H(0, addr(0x800CE79C)) = mirror;
                MEM_H(0, addr(0x800CE78C)) = 3; // configured laps
                MEM_H(0, addr(0x800A5EF0)) = 1; // next crossing completes lap one
                MEM_H(0, addr(0x800A5EF2)) = 1; // crossing counter before increment
                MEM_H(0, addr(0x800B69B6)) = 1; // vehicle belongs to player one
                MEM_H(0, addr(0x8009876C)) = 30; // lap timer: 0:30:00
                MEM_W(0, addr(0x80098844)) = 3600; // previous best: 1:00:00
                recomp_context ctx{};
                ctx.r29 = addr(0x807FF000);
                func_8002A228(rdram, &ctx);
                const gpr lap_name = profile + (mode ? (mirror ? 0x17A : 0x72)
                                                               : (mirror ? 0x61E : 0x516));
                check_name(rdram, lap_name, "ADAM", "no-edit lap mode=" +
                    std::to_string(mode) + " mirror=" + std::to_string(mirror) + " saved=" + saved);
                expect(MEM_W(0, addr(0x80098844)) == 1800, "ROM records the completed 30-second lap");

                // The vehicle index is still zero, but this vehicle now belongs
                // to player two. The lap hook must use the resolved owner.
                put(rdram, player, "LOCAL");
                MEM_H(0, addr(0x800CE6A6)) = 1;
                MEM_H(0, addr(0x800A5EF0)) = 1;
                MEM_H(0, addr(0x800A5EF2)) = 1;
                MEM_H(0, addr(0x800B69B6)) = 2;
                MEM_H(0, addr(0x80098774)) = 20;
                ctx = {};
                ctx.r29 = addr(0x807FF000);
                func_8002A228(rdram, &ctx);
                check_name(rdram, lap_name, "GUEST", "lap uses resolved guest owner");
                check_name(rdram, player, "LOCAL", "guest lap preserves player one");

                // Exercise leaderboard restoration independently of the lap hook.
                put(rdram, player, "TITUS");
                const int times = mirror ? 0x448 : 0x3DA;
                const int names = mirror ? 0x470 : 0x402;
                for (int rank = 0; rank < 5; ++rank) {
                    MEM_H(times + rank * 8 + 2, profile) = rank + 1;
                    put(rdram, profile + names + rank * 14, "OLD" + std::to_string(rank));
                }
                MEM_H(2, addr(0x80098E60)) = 0;
                MEM_H(4, addr(0x80098E60)) = 30;
                ctx = {};
                func_800401F0(rdram, &ctx);
                expect(ctx.r2 == 0, "ROM awards first place");
                check_name(rdram, profile + names, "ADAM", "no-edit leaderboard saved=" + saved);
                check_name(rdram, profile + names + 14, "OLD0", "ROM shifts previous winner");
                check_name(rdram, player + 13, "GUEST", "guest name preserved");
                check_name(rdram, profile, "TITUS LAM64", "profile header preserved");

                // A guest can earn a record while the editor still selects player one.
                MEM_H(0, addr(0x800CE6A6)) = 1;
                MEM_H(4, addr(0x80098E68)) = 20;
                ctx = {};
                ctx.r4 = 1;
                func_800401F0(rdram, &ctx);
                check_name(rdram, profile + names, "GUEST", "guest owns their earned record");
                check_name(rdram, profile + names + 14, "ADAM", "previous player record preserved");
            }
        }
        std::vector<uint8_t> memory(0x800000);
        auto* rdram = memory.data();
        put(rdram, player, "TITUS");
        MEM_H(0, addr(0x800CE6A6)) = 1;
        lambo_player_name_seed(rdram);
        check_name(rdram, player, "ADAM", "editor seed saved=" + saved);
    }

    for (const std::string saved : {"", "NAME123", "ABCDEFGHIJKLM", "\xC3\xA9"}) {
        std::ofstream(path) << nlohmann::json{{"name", saved}};
        std::vector<uint8_t> memory(0x800000);
        auto* rdram = memory.data();
        put(rdram, player, "TITUS");
        lambo_player_name_restore_for_record(rdram, 0);
        check_name(rdram, player, "TITUS", "unsupported saved name preserves ROM buffer");
    }
    {
        std::vector<uint8_t> memory(0x800000);
        auto* rdram = memory.data();
        std::ofstream(path) << nlohmann::json{{"name", "abcdefghijkl"}};
        MEM_B(13, player) = 0x55;
        lambo_player_name_restore_for_record(rdram, 0);
        check_name(rdram, player, "ABCDEFGHIJKL", "maximum length normalized intact");
        expect(MEM_BU(12, player) == 0 && MEM_BU(13, player) == 0x55,
               "terminator stays inside player buffer");
        MEM_H(0, addr(0x800CE6A6)) = 1;
        put(rdram, player, "CHAMP");
        lambo_player_name_save(rdram);
        put(rdram, player, "TITUS");
        lambo_player_name_restore_for_record(rdram, 0);
        check_name(rdram, player, "CHAMP", "confirmed edits take precedence over previous saved name");
    }
    std::filesystem::remove(path);
    std::filesystem::remove(dir);
    return failures ? 1 : 0;
}

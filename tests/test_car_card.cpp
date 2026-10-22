#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "lambo_car_card.h"
#include "lambo_vehicle.h"
#include "recomp.h"

extern "C" void lambo_car_card_draw(uint8_t*);
namespace lambo::config {
static bool show_card = false;
bool car_select_info() { return show_card; }
}

namespace card = lambo::car_card;

static void require(bool condition, const std::string& message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

static gpr address(uint32_t value) { return static_cast<int32_t>(value); }

int main() {
    require(card::tenths_from_updates(107) == 36, "107 updates is 3.6 s");
    require(card::tenths_from_updates(103) == 34, "103 updates is 3.4 s");
    require(card::tenths_from_updates(89) == 30, "89 updates rounds to 3.0 s");
    require(card::stats_for_category(-1) == nullptr && card::stats_for_category(8) == nullptr,
            "only eight physics categories exist");

    const card::CardText diablo = card::card_text(0, false, 0);
    require(diablo.name == "LAMBORGHINI DIABLO", "category 0 is the Diablo");
    require(diablo.lines[0] == "514 HP   590 NM   7000 RPM", "Diablo engine line: " + diablo.lines[0]);
    require(diablo.lines[1] == "TOP SPEED 323 KM/H   5 GEARS", "Diablo speed line: " + diablo.lines[1]);
    require(diablo.lines[2] == "0-100 KM/H 3.6 S   NOVICE", "Diablo novice line: " + diablo.lines[2]);
    const card::CardText viper = card::card_text(6, true, 1);
    require(viper.name == "DODGE VIPER", "category 6 is the Viper");
    require(viper.lines[1] == "TOP SPEED 193 MPH   5 GEARS", "MPH top speed: " + viper.lines[1]);
    require(viper.lines[2] == "0-60 MPH 2.3 S   EXPERT", "expert MPH time: " + viper.lines[2]);
    require(card::card_text(8, false, 0).name.empty(), "unknown category has no card");

    std::vector<uint8_t> memory(8 * 1024 * 1024);
    uint8_t* rdram = memory.data();
    const uint32_t block = 0x800BF240u;
    const uint32_t dl = block + 0x1C0u;
    // Scheduler fields follow the display-list arena (docs/sky-panorama.md).
    const uint32_t arena_end = block + 0x79C0u;
    // The selecting player is 1-based; the cursor array is 0-based.
    const uint32_t player_two_cursor = LAMBO_GUEST_PLAYER_MODEL_CURSOR_ADDR + LAMBO_GUEST_PLAYER_MODEL_CURSOR_STRIDE;
    auto reset = [&](uint32_t cursor) {
        MEM_W(0, address(0x800A2BFCu)) = static_cast<int32_t>(block);
        MEM_W(0, address(0x800A39CCu)) = static_cast<int32_t>(cursor);
        MEM_H(0, address(0x800CE6ACu)) = 6;
        MEM_H(0, address(0x80098562u)) = 13;
        MEM_H(0, address(0x800CE6A6u)) = 2;
        MEM_H(0, address(player_two_cursor)) = 18;  // a Viper model
        MEM_H(0, address(0x8013D490u + 18 * 24)) = 6;
        MEM_H(0, address(0x800CE7A4u)) = 0;
        MEM_H(0, address(0x800CE802u)) = 1;
        for (int c = 0x21; c < 0x60; ++c) {
            MEM_H(0, address(0x8015DA16u + c * 2)) = static_cast<int16_t>(c);
            MEM_H(0, address(0x8015DA8Au + c * 2)) = 12;
        }
        // Punctuation maps past '9' in the USA ROM's table.
        MEM_H(0, address(0x8015DA16u + '-' * 2)) = 0x3B;
        MEM_H(0, address(0x8015DA16u + '.' * 2)) = 0x3C;
        MEM_H(0, address(0x8015DA16u + '/' * 2)) = 0x3D;
    };
    auto cursor = [&] { return static_cast<uint32_t>(MEM_W(0, address(0x800A39CCu))); };

    reset(dl);
    lambo_car_card_draw(rdram);
    require(cursor() == dl, "disabled card must not emit");

    lambo::config::show_card = true;
    lambo_car_card_draw(rdram);
    const card::CardText viper_novice = card::card_text(6, true, 0);
    size_t glyphs = 0;
    for (char c : viper_novice.name) glyphs += c != ' ';
    for (const std::string& line : viper_novice.lines) for (char c : line) glyphs += c != ' ';
    require(cursor() == dl + (15 + glyphs * 10) * 8, "card emits setup, two strips and ten commands per glyph");
    require(static_cast<uint32_t>(MEM_W(8, address(dl))) == 0xBA001402u &&
            static_cast<uint32_t>(MEM_W(12, address(dl))) == 0x00100000u,
            "card selects the menu's two-cycle mode");
    require((static_cast<uint32_t>(MEM_W(48, address(dl))) >> 24) == 0xF6, "first strip is a fill rectangle");
    // First glyph of "DODGE VIPER" is 'D': texture 'D' - '0' glyphs past the base.
    const uint32_t first_glyph = dl + 15 * 8;
    require(static_cast<uint32_t>(MEM_W(0, address(first_glyph))) == 0xFD180000u &&
            static_cast<uint32_t>(MEM_W(4, address(first_glyph))) == 0x8013F820u + ('D' - '0') * 0x400u,
            "glyph texture comes from the menu chrome font");
    require((static_cast<uint32_t>(MEM_W(7 * 8, address(first_glyph))) >> 24) == 0xE4,
            "glyph is drawn with a texture rectangle");
    require(static_cast<uint32_t>(MEM_W(9 * 8 + 4, address(first_glyph))) == 0x06660666u,
            "name glyphs scale to ten pixels");

    // The dispatcher uses the same menu renderer before racing (6) and in
    // each return/results phase (10, 12, 14, 16). A stale CAR screen during
    // racing or loading must not draw a card.
    const uint32_t card_bytes = static_cast<uint32_t>(15 + glyphs * 10) * 8u;
    for (int state = 0; state <= 17; ++state) {
        reset(dl);
        MEM_H(0, address(0x800CE6ACu)) = static_cast<int16_t>(state);
        lambo_car_card_draw(rdram);
        const bool menu = state == 6 || state == 10 || state == 12 || state == 14 || state == 16;
        require(cursor() == dl + (menu ? card_bytes : 0),
                "CAR details gate in game state " + std::to_string(state));

        reset(dl);
        MEM_H(0, address(0x800CE6ACu)) = static_cast<int16_t>(state);
        MEM_H(0, address(0x80098562u)) = 12;
        lambo_car_card_draw(rdram);
        require(cursor() == dl, "other screens must not emit in state " + std::to_string(state));

        reset(dl);
        MEM_H(0, address(0x800CE6ACu)) = static_cast<int16_t>(state);
        lambo::config::show_card = false;
        lambo_car_card_draw(rdram);
        require(cursor() == dl, "disabled card must not emit in state " + std::to_string(state));
        lambo::config::show_card = true;
        lambo_car_card_draw(rdram);
        require(cursor() == dl + (menu ? card_bytes : 0),
                "re-enabled card must draw immediately in menu state " + std::to_string(state));
    }

    reset(dl);
    MEM_H(0, address(0x80098562u)) = 12;
    lambo_car_card_draw(rdram);
    require(cursor() == dl, "other menu screens must not emit");

    reset(dl);
    MEM_H(0, address(0x800CE6ACu)) = 8;
    lambo_car_card_draw(rdram);
    require(cursor() == dl, "races must not emit");

    reset(dl);
    MEM_H(0, address(player_two_cursor)) = 24;
    lambo_car_card_draw(rdram);
    require(cursor() == dl, "invalid model cursor must not emit");

    reset(dl);
    MEM_H(0, address(0x800CE6A6u)) = 0;
    lambo_car_card_draw(rdram);
    require(cursor() == dl, "invalid selecting player must not emit");

    // The card plus the game's own FULLSYNC/ENDDL must end at the arena end.
    const uint32_t last_fit = arena_end - card_bytes - 16u;
    reset(last_fit);
    lambo_car_card_draw(rdram);
    require(cursor() == last_fit + card_bytes, "card must draw when it exactly fits the arena");
    reset(last_fit + 8u);
    lambo_car_card_draw(rdram);
    require(cursor() == last_fit + 8u, "card must not write into the scheduler fields");

    reset(dl + 4u);
    lambo_car_card_draw(rdram);
    require(cursor() == dl + 4u, "misaligned display-list cursor must not emit");

    reset(dl - 8u);
    lambo_car_card_draw(rdram);
    require(cursor() == dl - 8u, "cursor before the display-list arena must not emit");

    reset(0);
    lambo_car_card_draw(rdram);
    require(cursor() == 0, "uninitialised cursor must not emit");

    reset(dl);
    MEM_W(0, address(0x800A2BFCu)) = static_cast<int32_t>(block + 0x7A50u + 8u);
    lambo_car_card_draw(rdram);
    require(cursor() == dl, "unrecognised task pointer must not emit");

    // The second of the two double-buffered tasks is accepted.
    const uint32_t second = block + 0x7A50u;
    reset(second + 0x1C0u);
    MEM_W(0, address(0x800A2BFCu)) = static_cast<int32_t>(second);
    lambo_car_card_draw(rdram);
    require(cursor() == second + 0x1C0u + card_bytes, "second task buffer must draw");
}

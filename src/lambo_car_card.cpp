#include "lambo_car_card.h"

#include <cstdio>

#include "lambo_config.h"
#include "lambo_vehicle.h"
#include "recomp.h"

namespace lambo::car_card {
namespace {

// Category order matches the engine table at 0x8013E0F0. Names come from the
// model previews (the ROM has no car-name strings). Power, torque, rev limit,
// gears and top speed derive from that table; tests/test_car_card_rom.py
// recomputes them from the ROM. Acceleration is measured, see
// docs/evidence/car-select-card-usa.json.
constexpr std::array<CarStats, 8> kCars{{
    {"LAMBORGHINI DIABLO", 514, 590, 7000, 5, 323, 200, {107, 100}, {103, 95}},
    {"PORSCHE 959", 485, 555, 6700, 6, 324, 201, {89, 72}, {84, 67}},
    {"FERRARI F50", 531, 485, 8700, 6, 324, 201, {96, 83}, {92, 79}},
    {"MCLAREN F1", 550, 590, 7600, 6, 331, 205, {83, 70}, {80, 66}},
    {"LAMBORGHINI COUNTACH", 571, 610, 7200, 5, 308, 191, {78, 67}, {74, 63}},
    {"BUGATTI EB110", 558, 605, 8200, 6, 326, 202, {81, 76}, {78, 72}},
    {"DODGE VIPER", 527, 730, 6000, 5, 312, 193, {81, 73}, {78, 68}},
    {"FERRARI TESTAROSSA", 497, 590, 6500, 5, 317, 196, {97, 85}, {93, 80}},
}};

} // namespace

const CarStats* stats_for_category(int category) {
    if (category < 0 || category >= static_cast<int>(kCars.size())) return nullptr;
    return &kCars[category];
}

int tenths_from_updates(int updates) {
    return (updates * 10 + 15) / 30;
}

CardText card_text(int category, bool mph, int difficulty) {
    const CarStats* car = stats_for_category(category);
    if (car == nullptr) return {};
    const int column = difficulty == 1 ? 1 : 0;
    const int tenths = tenths_from_updates(
        mph ? car->updates_to_60_mph[column] : car->updates_to_100_kmh[column]);
    char buffer[64];
    CardText text;
    text.name = car->name;
    std::snprintf(buffer, sizeof(buffer), "%u HP   %u NM   %u RPM",
                  car->horsepower, car->torque_nm, car->rev_limit_rpm);
    text.lines[0] = buffer;
    std::snprintf(buffer, sizeof(buffer), "TOP SPEED %u %s   %u GEARS",
                  mph ? car->top_mph : car->top_kmh, mph ? "MPH" : "KM/H", car->gears);
    text.lines[1] = buffer;
    std::snprintf(buffer, sizeof(buffer), "%s %d.%d S   %s",
                  mph ? "0-60 MPH" : "0-100 KM/H", tenths / 10, tenths % 10,
                  column == 1 ? "EXPERT" : "NOVICE");
    text.lines[2] = buffer;
    return text;
}

} // namespace lambo::car_card

namespace {

using lambo::car_card::CardText;

constexpr uint32_t kDlCursor = 0x800A39CCu;
// Current frame's task pointer, one of two double-buffered tasks (slot +
// 0x68, stride 0x7A50). Its display list runs from task + 0x1C0 up to the
// scheduler fields at task + 0x79C0 (docs/sky-panorama.md).
constexpr uint32_t kCurrentTask = 0x800A2BFCu;
constexpr uint32_t kFirstTask = 0x800BF240u;
constexpr uint32_t kTaskStride = 0x7A50u;
constexpr uint32_t kListStart = 0x1C0u;
constexpr uint32_t kListEnd = 0x79C0u;
// Card command counts: strip and glyph setup, then one load and texture
// rectangle per glyph.
constexpr uint32_t kSetupCommands = 15;
constexpr uint32_t kCommandsPerGlyph = 10;
// The game's own FULLSYNC and ENDDL follow the card.
constexpr uint32_t kGameTailBytes = 16;
constexpr uint32_t kMenuScreen = 0x80098562u;
constexpr uint32_t kSelectingPlayer = 0x800CE6A6u;  // 1-based
constexpr uint32_t kModelDescriptors = 0x8013D490u; // 24 bytes; +0 category
constexpr uint32_t kSpeedUnits = 0x800CE802u;       // 0 KM/H, 1 MPH
// Blue chrome font drawn by func_8004CCB4 (runtime 0x8004C0B4) and resident
// on the car screen: per-character glyph map and advance tables, and 16x16
// RGBA32 glyphs indexed from '0'.
constexpr uint32_t kGlyphMap = 0x8015DA16u;
constexpr uint32_t kGlyphAdvance = 0x8015DA8Au;
constexpr uint32_t kGlyphTextures = 0x8013F820u;
constexpr int kScreenCar = 13;

// Preview viewport is x 53-265, y 64-152; strips stay inside its frame.
constexpr int kLeft = 54;
constexpr int kRight = 265;
constexpr int kCentreX = 159;
constexpr int kNameTop = 65;
constexpr int kNameBottom = 77;
constexpr float kNameScale = 0.625f;
constexpr int kStatsTop = 124;
constexpr int kStatsBottom = 152;
constexpr float kStatsScale = 0.5f;
constexpr int kStatsLineStep = 9;

int16_t read_h(uint8_t* rdram, uint32_t address) {
    return MEM_H(0, static_cast<gpr>(static_cast<int32_t>(address)));
}

uint32_t read_w(uint8_t* rdram, uint32_t address) {
    return static_cast<uint32_t>(MEM_W(0, static_cast<gpr>(static_cast<int32_t>(address))));
}

void emit(uint8_t* rdram, uint32_t w0, uint32_t w1) {
    const gpr cursor_address = static_cast<int32_t>(kDlCursor);
    const gpr cursor = MEM_W(0, cursor_address);
    MEM_W(0, cursor) = static_cast<int32_t>(w0);
    MEM_W(4, cursor) = static_cast<int32_t>(w1);
    MEM_W(0, cursor_address) = static_cast<int32_t>(cursor + 8);
}

uint32_t fixed(float pixels) {
    return static_cast<uint32_t>(pixels * 4.0f + 0.5f) & 0xFFF;
}

void emit_strip(uint8_t* rdram, int top, int bottom) {
    emit(rdram, 0xF6000000u | (fixed(kRight) << 12) | fixed(bottom),
         (fixed(kLeft) << 12) | fixed(top));
}

// Text uses printable characters only; advances follow the game's tables
// (space is 10 pixels in func_8004CCB4).
float advance(uint8_t* rdram, char c) {
    if (c == ' ') return 10.0f;
    return read_h(rdram, kGlyphAdvance + static_cast<uint8_t>(c) * 2u);
}

float text_width(uint8_t* rdram, const std::string& text) {
    float width = 0.0f;
    for (char c : text) width += advance(rdram, c);
    return width;
}

void emit_text(uint8_t* rdram, const std::string& text, float top, float scale) {
    const float size = 16.0f * scale;
    const uint32_t step = static_cast<uint32_t>(1024.0f / scale + 0.5f) & 0xFFFF;
    float x = kCentreX - text_width(rdram, text) * scale / 2.0f;
    for (char c : text) {
        const int16_t glyph = c == ' ' ? 0x20 : read_h(rdram, kGlyphMap + static_cast<uint8_t>(c) * 2u);
        if (glyph >= 0x30 && glyph < 0x60) {
            const uint32_t texture = kGlyphTextures + static_cast<uint32_t>(glyph - 0x30) * 0x400u;
            // Same load sequence as func_8004CCB4; only the rectangle scales.
            emit(rdram, 0xFD180000u, texture);
            emit(rdram, 0xF5180000u, 0x07010040u);
            emit(rdram, 0xE6000000u, 0);
            emit(rdram, 0xF3000000u, 0x070FF100u);
            emit(rdram, 0xE7000000u, 0);
            emit(rdram, 0xF5180800u, 0x00010040u);
            emit(rdram, 0xF2000000u, 0x0003C03Cu);
            emit(rdram, 0xE4000000u | (fixed(x + size) << 12) | fixed(top + size),
                 (fixed(x) << 12) | fixed(top));
            emit(rdram, 0xB4000000u, 0);
            emit(rdram, 0xB3000000u, (step << 16) | step);
        }
        x += advance(rdram, c) * scale;
    }
}

size_t glyph_count(const CardText& text) {
    size_t count = 0;
    for (char c : text.name) count += c != ' ';
    for (const std::string& line : text.lines)
        for (char c : line) count += c != ' ';
    return count;
}

} // namespace

// USA ROM func_800028D0 before 0x80002480: every state's frame reaches this
// join just before the end-of-frame FULLSYNC/ENDDL, so the card draws over
// the finished car-select screen and the stock frame is otherwise unchanged.
extern "C" void lambo_car_card_draw(uint8_t* rdram) {
    if (!lambo::config::car_select_info()) return;
    // func_800028D0 dispatches all five steady menu phases to func_80038D6C
    // (runtime 0x8003816C). Returning from a race keeps phase 10, rather than
    // returning to the initial menu phase 6. Race/loading phases can retain
    // the CAR screen value, so checking only the screen would draw over races.
    switch (read_h(rdram, LAMBO_GUEST_GAME_STATE_ADDR)) {
        case 6: case 10: case 12: case 14: case 16: break;
        default: return;
    }
    if (read_h(rdram, kMenuScreen) != kScreenCar) return;
    const int player = read_h(rdram, kSelectingPlayer);
    if (player < 1 || player > 4) return;
    const int model = read_h(rdram, LAMBO_GUEST_PLAYER_MODEL_CURSOR_ADDR +
                                        static_cast<uint32_t>(player - 1) * LAMBO_GUEST_PLAYER_MODEL_CURSOR_STRIDE);
    if (model < 0 || model >= 24) return;
    const int category = read_h(rdram, kModelDescriptors + static_cast<uint32_t>(model) * 24u);
    const CardText text = lambo::car_card::card_text(
        category, read_h(rdram, kSpeedUnits) == 1, read_h(rdram, LAMBO_GUEST_MENU_DIFFICULTY_ADDR));
    if (text.name.empty()) return;

    // Draw only into a recognised task's display-list arena, and skip the card
    // rather than reach the scheduler fields. Subtract from the verified end
    // instead of adding to an untrusted cursor.
    const uint32_t task = read_w(rdram, kCurrentTask);
    if (task != kFirstTask && task != kFirstTask + kTaskStride) return;
    const uint32_t cursor = read_w(rdram, kDlCursor);
    const uint32_t needed =
        (kSetupCommands + static_cast<uint32_t>(glyph_count(text)) * kCommandsPerGlyph) * 8u + kGameTailBytes;
    if ((cursor & 7u) != 0 || cursor < task + kListStart || cursor > task + kListEnd - needed) return;

    // Translucent strips use the panel's own render mode, combiner and colour.
    // Both blend only in the second cycle, so the menu draws in two-cycle mode.
    emit(rdram, 0xE7000000u, 0);
    emit(rdram, 0xBA001402u, 0x00100000u);
    emit(rdram, 0xED000000u, 0x005003C0u);
    emit(rdram, 0xB900031Du, 0x0C184240u);
    emit(rdram, 0xFCFFFFFFu, 0xFFFDF638u);
    emit(rdram, 0xFA000000u, 0x181512C4u);
    emit_strip(rdram, kNameTop, kNameBottom);
    emit_strip(rdram, kStatsTop, kStatsBottom);

    // Glyph state as set by func_8004CCB4.
    emit(rdram, 0xE7000000u, 0);
    emit(rdram, 0xBA000C02u, 0x00002000u);
    emit(rdram, 0xBA000E02u, 0);
    emit(rdram, 0xBA001301u, 0);
    emit(rdram, 0xB900031Du, 0x0C1841C8u);
    emit(rdram, 0xFCFFFFFFu, 0xFFFCF238u);
    emit(rdram, 0xBB000001u, 0xFFFFFFFFu);
    emit_text(rdram, text.name, kNameTop + 1.0f, kNameScale);
    for (size_t i = 0; i < text.lines.size(); ++i)
        emit_text(rdram, text.lines[i], static_cast<float>(kStatsTop + 1 + static_cast<int>(i) * kStatsLineStep), kStatsScale);
}

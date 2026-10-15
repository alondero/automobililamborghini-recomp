#pragma once

#include <array>
#include <cstdint>
#include <string>

// Car-select info card: identity and derived stats per physics category.
// Derivation and measurement method: docs/CAR_DIFFERENCES.md.
namespace lambo::car_card {

struct CarStats {
    const char* name;
    uint16_t horsepower;
    uint16_t torque_nm;
    uint16_t rev_limit_rpm;
    uint8_t gears;
    uint16_t top_kmh;
    uint16_t top_mph;
    // Race updates (30 per race-clock second) from launch, indexed by
    // difficulty 0 Novice / 1 Expert.
    std::array<uint8_t, 2> updates_to_100_kmh;
    std::array<uint8_t, 2> updates_to_60_mph;
};

// Indexed by the model descriptor's physics category (0-7), not the cursor.
const CarStats* stats_for_category(int category);

struct CardText {
    std::string name;
    std::array<std::string, 3> lines;
};

// mph follows the game's SPEEDS IN option; difficulty selects the measured
// acceleration column. Returns empty text for an unknown category.
CardText card_text(int category, bool mph, int difficulty);

// Tenths of a second from race updates, rounded to nearest.
int tenths_from_updates(int updates);

} // namespace lambo::car_card

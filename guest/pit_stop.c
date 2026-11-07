#include "pit_stop.h"

enum PitStage { PIT_ENTER, PIT_WAIT_FOR_FUEL, PIT_FUEL, PIT_TYRES, PIT_EXIT };
enum TyreDirection { TURN_LEFT = 1, TURN_RIGHT, TURN_UP, TURN_DOWN };

static void advance_nozzles(u8 slot) {
    /* Authored animation records are 24 bytes apart; the moving component is
     * the word at +28 relative to the selected record in each model buffer. */
    *(s32*)&pit_model_a[slot][pit_animation_a[slot].frame * 24 + 28] += 0x400;
    *(s32*)&pit_model_b[slot][pit_animation_b[slot].frame * 24 + 28] += 0x400;
    pit_animation_a[slot].speed = 200;
    pit_animation_b[slot].speed = 200;
}

static s8 automatic_fuel_input(s32 limit, s8 physical) {
    if (limit < 1 || limit > 44) return physical;
    s32 stick = 7;
    for (s32 candidate = 8; candidate <= 70; ++candidate) {
        if ((s32)((candidate - 7) * 0.7) >= limit - 1) break;
        stick = candidate;
    }
    return (s8)stick;
}

static void update_tyre_input(u8 slot, s8 x, s8 y, s32 automatic) {
    s32 direction = pit_directions[pit_direction_index[slot]];
    if (automatic) {
        switch (direction) {
            case TURN_LEFT:  x = -60; y = 0; break;
            case TURN_RIGHT: x = 60; y = 0; break;
            case TURN_UP:    x = 0; y = 60; break;
            case TURN_DOWN:  x = 0; y = -60; break;
        }
    }
    s32 wrong = 0, correct = 0, pan = 0;
    switch (direction) {
        case TURN_RIGHT: wrong = y < -40; correct = x >= 41; pan = 0x30; break;
        case TURN_DOWN:  wrong = x < -40; correct = y < -40; pan = 0x38; break;
        case TURN_LEFT:  wrong = y >= 41; correct = x < -40; pan = 0x48; break;
        case TURN_UP:    wrong = x >= 41; correct = y >= 41; pan = 0x50; break;
    }
    if (wrong && pit_turns[slot] != 0) {
        func_8006B320(slot);
        pit_sound = slot ? 0x50 : 0x19;
        func_80066F84(pit_sound, 0.5f, 0x2710, pan, 0);
        pit_turns[slot] = 0;
    }
    if (correct) {
        ++pit_turns[slot];
        pit_direction_index[slot] = (pit_direction_index[slot] + 1) & 3;
    }
}

static void update_fitted_tyre(u8 slot) {
    if (pit_turns[slot] > 16) pit_turns[slot] = 16;
    if ((pit_previous_turns[slot] >> 2) != (pit_turns[slot] >> 2))
        pit_previous_turns[slot] = pit_turns[slot];
    s32 tyre = pit_turns[slot] >> 2;
    if (tyre == 0) return;
    tyre = (tyre + (pit_fuel[slot] & 3) + 2) & 3;
    pit_tyres_fitted[slot][tyre] = 1;
    pit_scratch = tyre * 4;
    for (s32 part = 0; part < 4; ++part) {
        PitTyrePart* geometry = pit_tyre_parts[slot][pit_scratch + part];
        geometry->displayed = geometry->authored;
    }
    /* The original map starts at record zero +2; racer slot zero is unused. */
    current_vehicle = racer_bodies[0][slot + 1];
    current_category = vehicles[current_vehicle].category;
    float value = tyre < 2
        ? pit_front_tyre_values[(s16)current_category][race_difficulty]
        : pit_rear_tyre_values[(s16)current_category][race_difficulty];
    if (race_difficulty == 0 && vehicles[current_vehicle].channel >= 5)
        value = (float)((double)value * 0.95);
    vehicles[current_vehicle].tyre_value[tyre] = value;
}

/* USA runtime 0x8006A870, ROM 0x6B470..0x6CA74. The two original
 * button-mask arguments are unused: this routine consumes the
 * game's per-slot pressed-button arrays. Called on the game thread only. */
__attribute__((section(".recomp_patch")))
void func_8006B470(s8 stick_x, s8 stick_y, s32 buttons, s32 previous_buttons, u8 slot) {
    (void)buttons;
    (void)previous_buttons;
    if (slot >= 2) return;
    const s32 automatic = lambo_get_automatic_pit_stops();
    const s32 fuel_sound = slot ? 0x54 : 0x2e;
    const s32 nozzle_sound = slot ? 0x53 : 0x2d;
    switch (pit_stage_order[pit_stage_index[slot]]) {
        case PIT_ENTER:
            if (pit_entry_pending[slot] == 0 && pit_animation_a[slot].frame >= 10) {
                ++pit_stage_index[slot];
                pit_pressure_limit[slot] = 44 - (pit_fuel[slot] >> 7);
                pit_sound = slot ? 0x4e : 0x1d;
                func_80066F84(pit_sound, 1.0f, 0x6d60, 0x40, 0);
            }
            break;
        case PIT_WAIT_FOR_FUEL:
            if (pit_animation_a[slot].frame >= 14) advance_nozzles(slot);
            if (automatic) stick_y = 8;
            if (stick_y >= 8 || (pit_pressed_buttons[slot] & 0x4000)) {
                ++pit_stage_index[slot];
                func_80066F84(nozzle_sound, 0.5f, 0x4000, 0x40, 0);
            }
            break;
        case PIT_FUEL:
            pit_model_a[slot][0x189] = 0;
            pit_model_b[slot][0x1b9] = 0;
            pit_pressure_limit[slot] = 44 - (pit_fuel[slot] >> 7);
            if (pit_spill_ticks[slot] != 0) {
                func_800673A0(fuel_sound);
                if (--pit_spill_ticks[slot] == 0) pit_pressure[slot] = 0;
            } else {
                if (automatic) stick_y = automatic_fuel_input(pit_pressure_limit[slot], stick_y);
                /* The signed-byte narrowing precedes the clamp in the ROM. */
                stick_y = (s8)(stick_y - 7);
                if (stick_y < 0) stick_y = 0;
                if (stick_y >= 64) stick_y = 63;
                if (pit_pressure[slot] >= pit_pressure_limit[slot] && pit_animation_a[slot].frame >= 13) {
                    advance_nozzles(slot);
                    func_8006B320(slot);
                    pit_spill_ticks[slot] = 10;
                    pit_pressure[slot] = pit_pressure_limit[slot] + 1;
                } else {
                    stick_y = (s8)(s32)((double)(float)stick_y * 0.7);
                    s32 change = stick_y - pit_pressure[slot];
                    if (change > 12) change = 12;
                    if (change < -6) change = -6;
                    pit_pressure[slot] += change;
                    if (pit_pressure[slot] > pit_pressure_limit[slot]) {
                        pit_pressure[slot] = pit_pressure_limit[slot];
                    } else if (pit_pressure[slot] > 0 && stick_y < pit_pressure_limit[slot]) {
                        pit_delivery_fraction = (float)pit_pressure[slot] / (float)pit_pressure_limit[slot];
                        if ((double)pit_delivery_fraction < 0.8)
                            pit_delivery_fraction = (float)((double)pit_delivery_fraction * 0.5);
                        else
                            pit_delivery_fraction = (float)(((double)pit_delivery_fraction - 0.8) * 3.0 + 0.4);
                        pit_fuel[slot] += (s32)(func_80075730((float)((double)pit_delivery_fraction * 1.57079628)) * 64.0f);
                    }
                    if (func_80067428(fuel_sound) == 0)
                        func_80066F84(fuel_sound, 0.5f, 0x4268, 0x40, 0);
                }
            }
            pit_scratch = pit_pressed_buttons[slot] & 0x4000;
            if ((pit_scratch && (pit_fuel[slot] >> 6) >= 2) || (pit_fuel[slot] >> 6) >= 46) {
                pit_pressed_buttons[slot] &= ~pit_scratch;
                pit_consumed_buttons[slot] |= pit_scratch;
                racers[current_channel].fuel = (float)((double)pit_fuel[slot] * 0.0339559);
                pit_model_b[slot][0x1b9] = 200;
                pit_model_a[slot][0x189] = 200;
                ++pit_stage_index[slot];
                func_80066F84(nozzle_sound, 0.5f, 0x2710, 0x40, 0);
                func_800673A0(fuel_sound);
                pit_turns[slot] = pit_pressure[slot] = pit_pressure_limit[slot] = 0;
            }
            break;
        case PIT_TYRES:
            pit_model_a[slot][0x399] = 0;
            pit_model_b[slot][0x339] = 0;
            if (pit_turns[slot] >= 3) {
                pit_turning[slot] = 1;
                if (pit_turn_start[slot] == 0) pit_turn_start[slot] = game_tick;
                else if ((u32)(pit_turns[slot] * 8) < game_tick - pit_turn_start[slot]) {
                    if (pit_turns[slot] < 16) pit_turns[slot] = 0;
                    else {
                        ++pit_stage_index[slot];
                        pit_sound = slot ? 0x29 : 0x2a;
                        func_800670BC(pit_sound, 0.5f, 0x7fff, 0x40, 0, 0xc3500);
                    }
                }
            } else {
                pit_turning[slot] = 0;
                pit_turn_start[slot] = game_tick;
            }
            if (pit_control_scheme[slot] == 2) ++pit_turns[slot];
            else update_tyre_input(slot, stick_x, stick_y, automatic);
            update_fitted_tyre(slot);
            break;
        case PIT_EXIT:
            if (func_80067428(fuel_sound) != 0) func_800673A0(fuel_sound);
            if (pit_exit_animation[slot].frame >= 28) ++pit_stage_index[slot];
            break;
    }
}

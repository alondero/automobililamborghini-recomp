#include "vehicle_update.h"

/* Only input snapshots cross to the host. The game thread owns all demand,
 * accumulator and latch updates. Bit 30 requests a read-only diagnostic. */
#define INPUT_ANALOG 0x80000000u
#define INPUT_PROBE  0x40000000u
#define brake_ramp ((f32*)0x80901440u)

static s32 input_port(void) {
    return update_channel >= 1 && update_channel <= 4 && update_vehicle_index >= 0
        ? update_channel - 1 : -1;
}

static void apply_throttle(s16 previous, s32 captured) {
    s32 port = input_port();
    if (port < 0) return;
    u32 sample = lambo_vehicle_input(port, 0);
    if ((sample & (INPUT_ANALOG | INPUT_PROBE)) == 0) return;
    if (sample & INPUT_ANALOG) {
        Vehicle* vehicle = &vehicles_update[update_vehicle_index];
        s32 pressed = selected_pad == &controller_pads[port] &&
            (selected_pad->buttons & 0xa000) != 0;
        if (!pressed) {
            s16 limit = racers_update[update_channel].field_44;
            s32 target = limit <= 0 ? 0 :
                (((sample & 0xffffu) * (u32)limit + 32767u) / 65535u);
            s32 demand = captured ? previous : vehicle->throttle_demand;
            if (demand < target) {
                demand += 10;
                if (demand > target) demand = target;
            } else if (demand > target) {
                demand -= 10;
                if (demand < target) demand = target;
            }
            vehicle->throttle_demand = (s16)demand;
        }
    }
    if (sample & INPUT_PROBE) lambo_vehicle_observe(0, 0, sample);
}

static void apply_brake(void) {
    s32 port = input_port();
    if (port < 0) return;
    u32 sample = lambo_vehicle_input(port, 1);
    if ((sample & (INPUT_ANALOG | INPUT_PROBE)) == 0) return;
    s32 pressed = selected_pad == &controller_pads[port] &&
        (selected_pad->buttons & 0x4000) != 0;
    if ((sample & INPUT_ANALOG) && !pressed) {
        f32 target = ((f32)(sample & 0xffffu) / 65535.0f) * 16.0f;
        f32 ramp = brake_ramp[port];
        if (ramp < target) {
            ramp += 1.0f;
            if (ramp > target) ramp = target;
        } else {
            ramp = target;
        }
        brake_ramp[port] = ramp;
        Vehicle* vehicle = &vehicles_update[update_vehicle_index];
        vehicle->brake_demand = ramp;
        u16 latch = (u16)vehicle->brake_latch;
        vehicle->brake_latch = (s16)(ramp > 0.0f ? latch | 1u : latch & ~1u);
    } else {
        brake_ramp[port] = 0.0f;
    }
    if (sample & INPUT_PROBE) lambo_vehicle_observe(1, 0, sample);
}

static s32 player_one_steering(void) {
    return update_channel == 1 && selected_pad == &controller_pads[0] &&
        control_schemes[1] != 2 && update_vehicle_index >= 0;
}

static s16 steering_raw(s8 raw) {
    u32 settings = lambo_vehicle_input(0, 2);
    s32 deadzone = settings & 255u;
    if (deadzone == 4 || !player_one_steering()) return raw;
    s32 magnitude = raw < 0 ? -raw : raw;
    if (raw == 0 || magnitude < deadzone) return 0;
    magnitude = magnitude - deadzone + 4;
    return (s16)(raw < 0 ? -magnitude : magnitude);
}

static void steering_direct(void) {
    u32 settings = lambo_vehicle_input(0, 2);
    if (!player_one_steering()) return;
    s16 authored = steering_work;
    if (settings & 256u) {
        s32 raw = controller_pads[0].stick_x;
        s32 magnitude = raw < 0 ? -raw : raw;
        s32 index = magnitude - (s32)(settings & 255u);
        if (index > 61) index = 61;
        s32 curve = raw == 0 || index < 0 ? 0 : (update_difficulty != 0 ?
            expert_steering_curve[index] : novice_steering_curve[index]);
        steering_work = (s16)(raw < 0 ? -curve : curve);
        vehicles_update[update_vehicle_index].steering_accumulator = steering_work;
    }
    lambo_vehicle_observe(2, authored, settings);
}

/* Recovered USA linked-object update. Calls retain their original ordering.
 * Arithmetic casts preserve the original halfword stores and float rounding. */
__attribute__((section(".recomp_patch")))
void func_80019D20(s16 first_vehicle) {
    /* Original stack+0x3e sentinel: initialized to -1, never assigned again
     * and never exposed to a callee. Retained with the authored loop branch. */
    s16 next_vehicle_override;
    s16 throttle_previous = 0;
    s32 throttle_captured = 0;
    Racer *racer_record_1;
    Racer *racer_record_2;
    Racer *racer_record_3;
    Racer *racer_record_4;
    Racer *racer_record_5;
    Racer *racer_record_6;
    Racer *racer_record_7;
    Racer *racer_record_8;
    Racer *racer_record_9;
    Racer *racer_record_10;
    Racer *racer_record_11;
    Vehicle *vehicle_record_1;
    Vehicle *vehicle_record_2;
    Vehicle *vehicle_record_3;
    Vehicle *vehicle_record_4;
    Vehicle *vehicle_record_5;
    Vehicle *vehicle_record_6;
    Vehicle *vehicle_record_7;
    Vehicle *vehicle_record_8;
    Vehicle *vehicle_record_9;
    Vehicle *vehicle_record_10;
    Vehicle *vehicle_record_11;
    Vehicle *vehicle_record_12;
    Vehicle *vehicle_record_13;
    Vehicle *vehicle_record_14;
    Vehicle *vehicle_record_15;
    Vehicle *vehicle_record_16;
    Vehicle *vehicle_record_17;
    Vehicle *vehicle_record_18;
    Vehicle *vehicle_record_19;
    Vehicle *vehicle_record_20;
    Vehicle *vehicle_record_21;
    Vehicle *vehicle_record_22;
    Vehicle *vehicle_record_23;
    Vehicle *vehicle_record_24;
    Vehicle *vehicle_record_25;
    Vehicle *vehicle_record_26;
    Vehicle *vehicle_record_27;
    Vehicle *vehicle_record_28;
    Vehicle *vehicle_record_29;
    Vehicle *vehicle_record_30;
    f32 *float_field_1;
    f32 float_value_1;
    f32 float_value_2;
    f32 float_value_3;
    f32 float_value_4;
    f32 float_value_5;
    f32 float_value_6;
    s16 *signed_halfword_1;
    s16 *signed_halfword_2;
    s16 *signed_halfword_3;
    s16 *signed_halfword_4;
    s16 *signed_halfword_5;
    s16 *signed_halfword_6;
    s16 *signed_halfword_7;
    s16 *signed_halfword_8;
    s16 *signed_halfword_9;
    s16 *signed_halfword_10;
    s16 *signed_halfword_11;
    s16 *signed_halfword_12;
    s16 *signed_halfword_13;
    s16 *signed_halfword_14;
    s16 *signed_halfword_15;
    s16 *signed_halfword_16;
    s16 *signed_halfword_17;
    s16 signed_value_1;
    s16 signed_value_2;
    s16 signed_value_3;
    s16 signed_value_4;
    s16 signed_value_5;
    s16 signed_value_6;
    s16 signed_value_7;
    s16 signed_value_8;
    s16 signed_value_9;
    s16 signed_value_10;
    s16 signed_value_11;
    s16 signed_value_12;
    s16 signed_value_13;
    s16 signed_value_14;
    s16 signed_value_15;
    s16 signed_value_16;
    s16 signed_value_17;
    s16 signed_value_18;
    s16 signed_value_19;
    s16 signed_value_20;
    s16 signed_value_21;
    s16 signed_value_22;
    s16 signed_value_23;
    s16 signed_value_24;
    s16 signed_value_25;
    s16 signed_value_26;
    s16 signed_value_27;
    s16 signed_value_28;
    s16 signed_value_29;
    s32 integer_value_1;
    s32 integer_value_2;
    s32 integer_value_3;
    s32 integer_value_4;
    u16 *unsigned_halfword_1;
    u16 *unsigned_halfword_2;
    u16 *unsigned_halfword_3;
    u16 *unsigned_halfword_4;
    u16 *unsigned_halfword_5;
    u16 *unsigned_halfword_6;
    u16 *unsigned_halfword_7;
    u16 *unsigned_halfword_8;
    u16 *unsigned_halfword_9;
    u16 *unsigned_halfword_10;
    u16 unsigned_value_1;
    u16 unsigned_value_2;
    void *record_bytes_1;
    void *record_bytes_2;
    void *record_bytes_3;
    void *record_bytes_4;
    void *record_bytes_5;
    void *record_bytes_6;
    void *record_bytes_7;
    void *record_bytes_8;
    void *record_bytes_9;
    void *record_bytes_10;
    void *record_bytes_11;
    void *record_bytes_12;

    update_vehicle_index = first_vehicle;
    D_800B9814 = D_8013D2D8;
    if (update_vehicle_index != -1) {
        do {
            next_vehicle_override = -1;
            throttle_captured = 0;
            steering_work = 0;
            unsigned_value_1 = (u16) vehicles_update[update_vehicle_index].channel;
            /* Expanded participants use AI behavior even with channels >=13;
             * authored scenery retains cases 13/14. */
            if (unsigned_value_1 >= 13 && update_vehicle_index >= 0 &&
                update_vehicle_index < 64 &&
                (vehicles_update[update_vehicle_index].flags & 8)) unsigned_value_1 = 5;
            switch (unsigned_value_1) {
            /* Human channels share controller and race-state processing. */
            case 1:
            case 2:
            case 3:
            case 4:
                update_channel = vehicles_update[update_vehicle_index].channel;
                update_scratch_index = update_channel - 1;
                selected_pad = &controller_pads[update_scratch_index];
                if (D_800CE6B4 == 4) {
                    func_80028250();
                    if (D_800CE6B0 >= 3) {
                        func_8002D37C();
                    }
                    func_8002622C();
                } else if (racers_update[update_channel].field_02 == 1) {
                    if (D_800A2D40[update_channel] != 0xB4) {
                        signed_halfword_6 = &D_800A2D40[update_channel];
                        *signed_halfword_6 += 4;
                        if (D_800A2D40[update_channel] >= 0xB5) {
                            D_800A2D40[update_channel] = 0xB4;
                        }
                    }
                    if ((s32) D_800A2D28[update_channel] < 0x64) {
                        D_800A2D28[update_channel] = 0x1F4;
                    }
                    if ((s32) D_800A2D28[update_channel] < D_8008934C[D_800CE6A4]) {
                        unsigned_halfword_8 = &D_800A2D28[update_channel];
                        *unsigned_halfword_8 += 0x19;
                    }
                    if (D_8008934C[D_800CE6A4] < (s32) D_800A2D28[update_channel]) {
                        unsigned_halfword_9 = &D_800A2D28[update_channel];
                        *unsigned_halfword_9 -= 0x19;
                    }
                    if (racers_update[update_channel].field_06 != 1) {
                        D_800986AE[update_channel] = 8;
                    } else {
                        D_800986AE[update_channel] = 6;
                    }
                    D_800986BE[update_channel] = 0x258;
                    func_8002D37C();
                    func_80028250();
                    func_8002622C();
                } else if (racers_update[update_channel].field_50 == 0) {
                    if (control_schemes[update_channel] == 2) {
                        if ((D_800A3A00[update_scratch_index] & 4) && (D_800A2D10[update_channel] < 3)) {
                            signed_halfword_4 = &D_800A39F8[update_scratch_index];
                            *signed_halfword_4 = (u16) *signed_halfword_4 | 4;
                            unsigned_halfword_6 = &D_800A3A00[update_scratch_index];
                            *unsigned_halfword_6 &= ~4;
                            signed_halfword_5 = &D_800A2D10[update_channel];
                            *signed_halfword_5 += 1;
                            if ((D_800CE6A4 >= 3) && (D_800A2D10[update_channel] & 1)) {
                                D_800A2D10[update_channel] = 2;
                            }
                        }
                        if ((D_800A3A00[update_scratch_index] & 8) && (D_800A2D10[update_channel] != 0)) {
                            signed_halfword_14 = &D_800A39F8[update_scratch_index];
                            *signed_halfword_14 = (u16) *signed_halfword_14 | 8;
                            unsigned_halfword_1 = &D_800A3A00[update_scratch_index];
                            *unsigned_halfword_1 &= ~8;
                            signed_halfword_15 = &D_800A2D10[update_channel];
                            *signed_halfword_15 -= 1;
                            if ((D_800CE6A4 >= 3) && (D_800A2D10[update_channel] & 1)) {
                                D_800A2D10[update_channel] = 0;
                            }
                        }
                    } else {
                        if ((D_800A3A00[update_scratch_index] & 0x400) && (D_800A2D10[update_channel] < 3)) {
                            signed_halfword_11 = &D_800A39F8[update_scratch_index];
                            *signed_halfword_11 = (u16) *signed_halfword_11 | 0x400;
                            unsigned_halfword_2 = &D_800A3A00[update_scratch_index];
                            *unsigned_halfword_2 &= ~0x400;
                            signed_halfword_12 = &D_800A2D10[update_channel];
                            *signed_halfword_12 += 1;
                            if ((D_800CE6A4 >= 3) && (D_800A2D10[update_channel] & 1)) {
                                D_800A2D10[update_channel] = 2;
                            }
                        }
                        if ((D_800A3A00[update_scratch_index] & 0x800) && (D_800A2D10[update_channel] != 0)) {
                            signed_halfword_8 = &D_800A39F8[update_scratch_index];
                            *signed_halfword_8 = (u16) *signed_halfword_8 | 0x800;
                            unsigned_halfword_4 = &D_800A3A00[update_scratch_index];
                            *unsigned_halfword_4 &= ~0x800;
                            signed_halfword_9 = &D_800A2D10[update_channel];
                            *signed_halfword_9 -= 1;
                            if ((D_800CE6A4 >= 3) && (D_800A2D10[update_channel] & 1)) {
                                D_800A2D10[update_channel] = 0;
                            }
                        }
                    }
                    D_800A2D40[update_channel] = 0;
                    if ((D_800CE6B4 == 1) && (D_800CE76E == 0)) {
                        vehicles_update[update_vehicle_index].throttle_demand = 0;
                        if (vehicles_update[update_vehicle_index].speed < 0xA) {
                            vehicles_update[update_vehicle_index].speed = 0;
                            if (D_800CE6A4 == 2) {
                                if ((GUEST_FIELD(racers_update, s16 *, 0x86) == 1) || (GUEST_FIELD(racers_update, s16 *, 0x10A) == 1)) {
                                    if (racers_update[update_channel].field_02 != 1) {
                                        D_800986AE[update_channel] = 0xA;
                                        D_800986BE[update_channel] = 0x258;
                                        if (D_800CE6B0 != 5) {
                                            D_800CE6B0 = 5;
                                            D_800CE6B2 = 0x96;
                                            racer_record_7 = &racers_update[update_channel];
                                            racer_record_7->field_06 = racer_record_7->field_04;
                                            integer_value_4 = update_channel * 0x60;
                                            integer_value_3 = D_800CE6BA * 8;
                                            GUEST_FIELD((u8*)D_80098C7E + ((update_channel * 0x60) + (D_800CE6BA * 8)), s16 *, 0) = -1;
                                            signed_value_1 = GUEST_FIELD((integer_value_4 + integer_value_3 + (u8*)D_80098CD8), s16 *, -0x5A);
                                            GUEST_FIELD((integer_value_4 + integer_value_3 + (u8*)D_80098CD8), s16 *, -0x5C) = signed_value_1;
                                            GUEST_FIELD((u8*)D_80098C7A + ((update_channel * 0x60) + (D_800CE6BA * 8)), s16 *, 0) = signed_value_1;
                                        }
                                    }
                                } else if ((vehicles_update[GUEST_FIELD(D_800CE818, s16 *, 2)].speed == 0) && (vehicles_update[GUEST_FIELD(D_800CE818, s16 *, 4)].speed == 0)) {
                                    if (D_800CE6B0 != 4) {
                                        D_800CE6B0 = 4;
                                        D_800CE6B2 = 0x96;
                                    }
                                    D_800986AE[update_channel] = 9;
                                    D_800986BE[update_channel] = 0x258;
                                }
                            } else {
                                if (D_800CE6B0 != 4) {
                                    D_800CE6B0 = 4;
                                    D_800CE6B2 = 0x96;
                                }
                                D_800986AE[update_channel] = 9;
                                D_800986BE[update_channel] = 0x258;
                            }
                        } else if ((D_800CE6A4 == 2) && ((GUEST_FIELD(racers_update, s16 *, 0x86) == 1) || (GUEST_FIELD(racers_update, s16 *, 0x10A) == 1)) && (racers_update[update_channel].field_02 != 1)) {
                            D_800986AE[update_channel] = 0xA;
                            D_800986BE[update_channel] = 0x258;
                            racers_update[update_channel].field_02 = 2;
                        }
                    } else if (control_schemes[update_channel] == 2) {
                        if (D_800CE6B0 >= 3) {
                            D_80098830 = (s16) selected_pad->stick_y;
                            if (D_80098830 > 0) {
                                signed_value_18 = D_80098830 - 7;
                                D_80098830 = signed_value_18;
                                if (signed_value_18 < 0) {
                                    D_80098830 = 0;
                                } else if (D_80098830 >= 0x41) {
                                    D_80098830 = 0x40;
                                }
                                D_80098830 = (s16) (s32) ((f64) (f32) D_80098830 * 1.5625);
                                signed_value_22 = D_80098830 - vehicles_update[update_vehicle_index].throttle_demand;
                                D_80098830 = signed_value_22;
                                if (signed_value_22 > 0) {
                                    if (D_80098830 >= 0xB) {
                                        D_80098830 = 0xA;
                                    }
                                } else if (D_80098830 < -0xA) {
                                    D_80098830 = -0xA;
                                }
                                vehicle_record_22 = &vehicles_update[update_vehicle_index];
                                vehicle_record_22->throttle_demand += D_80098830;
                            } else {
                                vehicles_update[update_vehicle_index].throttle_demand = 0;
                            }
                        } else {
                            vehicles_update[update_vehicle_index].throttle_demand = 0;
                        }
                    } else if ((throttle_previous = vehicles_update[update_vehicle_index].throttle_demand,
                        throttle_captured = (lambo_vehicle_input(update_channel - 1, 0) &
                            (INPUT_ANALOG | INPUT_PROBE)) != 0,
                        selected_pad->buttons & 0xA000)) {
                        vehicle_record_17 = &vehicles_update[update_vehicle_index];
                        signed_value_2 = vehicle_record_17->throttle_demand + 0xA;
                        vehicle_record_17->throttle_demand = signed_value_2;
                        if (racers_update[update_channel].field_44 < signed_value_2) {
                            vehicles_update[update_vehicle_index].throttle_demand = racers_update[update_channel].field_44;
                        }
                    } else {
                        vehicle_record_27 = &vehicles_update[update_vehicle_index];
                        vehicle_record_27->throttle_demand -= 0xA;
                        if (vehicles_update[update_vehicle_index].throttle_demand < 0) {
                            vehicles_update[update_vehicle_index].throttle_demand = 0;
                        }
                    }
                    apply_throttle(throttle_previous, throttle_captured);
                    if (vehicles_update[update_vehicle_index].field_b0 != 0) {
                        if (control_schemes[update_channel] == 2) {
                            signed_value_28 = D_800A3A00[update_scratch_index] & 0x810;
                            D_80098830 = signed_value_28;
                            if (signed_value_28 != 0) {
                                signed_halfword_2 = &D_800A39F8[update_scratch_index];
                                *signed_halfword_2 = (u16) *signed_halfword_2 | D_80098830;
                                unsigned_halfword_3 = &D_800A3A00[update_scratch_index];
                                *unsigned_halfword_3 &= ~D_80098830;
                                vehicle_record_18 = &vehicles_update[update_vehicle_index];
                                if (GUEST_FIELD((u8*)D_8013E0F8 + (vehicle_record_18->category * 0x34), s16 *, 0) != vehicle_record_18->gear_state) {
                                    vehicle_record_6 = &vehicles_update[update_vehicle_index];
                                    vehicle_record_6->gear_state += 1;
                                }
                            }
                            signed_value_23 = D_800A3A00[update_scratch_index] & 0x420;
                            D_80098830 = signed_value_23;
                            if (signed_value_23 != 0) {
                                signed_halfword_3 = &D_800A39F8[update_scratch_index];
                                *signed_halfword_3 = (u16) *signed_halfword_3 | D_80098830;
                                unsigned_halfword_10 = &D_800A3A00[update_scratch_index];
                                *unsigned_halfword_10 &= ~D_80098830;
                                if (vehicles_update[update_vehicle_index].gear_state >= 2) {
                                    vehicle_record_24 = &vehicles_update[update_vehicle_index];
                                    vehicle_record_24->gear_state -= 1;
                                }
                            }
                        } else {
                            if (D_800A3A00[update_scratch_index] & 0x10) {
                                signed_halfword_16 = &D_800A39F8[update_scratch_index];
                                *signed_halfword_16 = (u16) *signed_halfword_16 | 0x10;
                                unsigned_halfword_7 = &D_800A3A00[update_scratch_index];
                                *unsigned_halfword_7 &= ~0x10;
                                vehicle_record_28 = &vehicles_update[update_vehicle_index];
                                if (GUEST_FIELD((u8*)D_8013E0F8 + (vehicle_record_28->category * 0x34), s16 *, 0) != vehicle_record_28->gear_state) {
                                    vehicle_record_19 = &vehicles_update[update_vehicle_index];
                                    vehicle_record_19->gear_state += 1;
                                }
                            }
                            if (D_800A3A00[update_scratch_index] & 4) {
                                signed_halfword_13 = &D_800A39F8[update_scratch_index];
                                *signed_halfword_13 = (u16) *signed_halfword_13 | 4;
                                unsigned_halfword_5 = &D_800A3A00[update_scratch_index];
                                *unsigned_halfword_5 &= ~4;
                                if (vehicles_update[update_vehicle_index].gear_state >= 2) {
                                    vehicle_record_7 = &vehicles_update[update_vehicle_index];
                                    vehicle_record_7->gear_state -= 1;
                                }
                            }
                        }
                    }
                    if (control_schemes[update_channel] == 2) {
                        if (selected_pad->buttons & 0x2000) {
                            vehicle_record_8 = &vehicles_update[update_vehicle_index];
                            vehicle_record_8->brake_latch |= 0x8000;
                        } else {
                            vehicle_record_1 = &vehicles_update[update_vehicle_index];
                            vehicle_record_1->brake_latch &= 0x7FFF;
                        }
                    } else if (selected_pad->buttons & 1) {
                        vehicle_record_25 = &vehicles_update[update_vehicle_index];
                        vehicle_record_25->brake_latch |= 0x8000;
                    } else {
                        vehicle_record_11 = &vehicles_update[update_vehicle_index];
                        vehicle_record_11->brake_latch &= 0x7FFF;
                    }
                    if (control_schemes[update_channel] == 2) {
                        if (selected_pad->stick_y < -7) {
                            func_80029DCC();
                            signed_value_27 = -7 - selected_pad->stick_y;
                            D_80098830 = signed_value_27;
                            if (signed_value_27 >= 0x41) {
                                D_80098830 = 0x40;
                            }
                            D_FLT_800CE790 = (f32) ((f64) (f32) D_80098830 * 0.015625);
                            vehicles_update[update_vehicle_index].brake_demand = (f32) (s16) (s32) ((f64) D_FLT_800CE790 * 16.0);
                        } else if (racers_update[update_channel].field_02 == 2) {
                            func_80029DCC();
                            vehicles_update[update_vehicle_index].brake_demand = 16.0f;
                        } else {
                            func_8002A070();
                        }
                    } else if ((selected_pad->buttons & 0x4000) || (racers_update[update_channel].field_02 == 2)) {
                        if (!(vehicles_update[update_vehicle_index].brake_latch & 0x7FFF)) {
                            vehicles_update[update_vehicle_index].brake_demand = 1.0f;
                        }
                        func_80029DCC();
                        vehicle_record_12 = &vehicles_update[update_vehicle_index];
                        vehicle_record_12->brake_demand = (f32) ((f64) vehicle_record_12->brake_demand + 1.0);
                        if ((f64) vehicles_update[update_vehicle_index].brake_demand > 16.0) {
                            vehicles_update[update_vehicle_index].brake_demand = 16.0f;
                        }
                    } else {
                        func_8002A070();
                    }
                    apply_brake();
                    if (control_schemes[update_channel] == 1) {
                        unsigned_value_2 = selected_pad->buttons;
                        if ((unsigned_value_2 & 0xA000) && (unsigned_value_2 & 0x4000)) {
                            vehicles_update[update_vehicle_index].field_ae = 1;
                        } else {
                            vehicles_update[update_vehicle_index].field_ae = 0;
                        }
                        if (D_800CE6B0 < 3) {
                            vehicles_update[update_vehicle_index].field_ae = 1;
                        }
                    }
                    steering_work = steering_raw(selected_pad->stick_x);
                    if (control_schemes[update_channel] != 2) {
                        if (D_800CE7B0 != 0) {
                            D_80098730 = vehicles_update[update_vehicle_index].category;
                            vehicle_record_29 = &vehicles_update[update_vehicle_index];
                            D_FLT_800CE790 = (f32) ((f64) (vehicle_record_29->field_d4 + vehicle_record_29->field_d0) * 0.5);
                            D_FLT_800CE790 = (f32) (1.0 - ((1.0 - (f64) (GUEST_FIELD((u8*)D_FLT_8013E290 + (((s16) D_80098730 * 8) + (update_difficulty * 4)), f32 *, 0) / D_FLT_800CE790)) * D_DBL_8008D9D0));
                        } else {
                            D_FLT_800CE790 = 1.0f;
                        }
                        if (D_800A3A16[update_channel] == 0) {
                            if (steering_work >= 0) {
                                signed_value_29 = steering_work - 4;
                                steering_work = signed_value_29;
                                if (signed_value_29 < 0) {
                                    steering_work = 0;
                                } else {
                                    if (steering_work >= 0x3E) {
                                        steering_work = 0x3D;
                                    }
                                    if (update_difficulty != 0) {
                                        steering_work = expert_steering_curve[steering_work];
                                    } else {
                                        steering_work = novice_steering_curve[steering_work];
                                    }
                                }
                            } else {
                                steering_work = -steering_work;
                                signed_value_21 = steering_work - 4;
                                steering_work = signed_value_21;
                                if (signed_value_21 < 0) {
                                    steering_work = 0;
                                } else {
                                    if (steering_work >= 0x3E) {
                                        steering_work = 0x3D;
                                    }
                                    if (update_difficulty != 0) {
                                        steering_work = -expert_steering_curve[steering_work];
                                    } else {
                                        steering_work = -novice_steering_curve[steering_work];
                                    }
                                }
                            }
                            steering_work = (s16) (s32) ((f32) steering_work * D_FLT_800CE790);
                        } else {
                            if (steering_work >= 0) {
                                signed_value_19 = steering_work - 4;
                                steering_work = signed_value_19;
                                if (signed_value_19 < 0) {
                                    steering_work = 0;
                                } else if (steering_work >= 0x3C) {
                                    if (D_800A3A1E[update_channel] < 0) {
                                        D_800A3A1E[update_channel] = D_8008933C[update_difficulty];
                                    } else if (D_800A3A1E[update_channel] >= vehicles_update[update_vehicle_index].steering_accumulator) {
                                        signed_halfword_7 = &D_800A3A20[update_channel];
                                        GUEST_FIELD(signed_halfword_7, s16 *, -2) = (s16) (GUEST_FIELD(signed_halfword_7, s16 *, -2) + D_80089340[update_difficulty]);
                                        signed_value_3 = D_800A3A1E[update_channel];
                                        steering_work = signed_value_3;
                                        if (D_80089338[update_difficulty] < signed_value_3) {
                                            signed_value_4 = D_80089338[update_difficulty];
                                            D_800A3A1E[update_channel] = signed_value_4;
                                            steering_work = signed_value_4;
                                        }
                                    } else {
                                        steering_work = D_8008933C[update_difficulty];
                                    }
                                } else if (D_800A3A1E[update_channel] < 0) {
                                    steering_work = 0;
                                    D_800A3A1E[update_channel] = 0;
                                } else if (D_8008933C[update_difficulty] < D_800A3A1E[update_channel]) {
                                    signed_halfword_10 = &D_800A3A20[update_channel];
                                    GUEST_FIELD(signed_halfword_10, s16 *, -2) = (s16) (GUEST_FIELD(signed_halfword_10, s16 *, -2) - (D_80089340[update_difficulty] * 2));
                                    steering_work = D_800A3A1E[update_channel];
                                } else {
                                    integer_value_1 = (s32) (D_FLT_80089344[update_difficulty] * (f32) steering_work);
                                    steering_work = (s16) integer_value_1;
                                    D_800A3A1E[update_channel] = (s16) integer_value_1;
                                }
                            } else {
                                signed_value_14 = steering_work + 4;
                                steering_work = signed_value_14;
                                if (signed_value_14 > 0) {
                                    steering_work = 0;
                                } else if (steering_work < -0x3B) {
                                    if (D_800A3A1E[update_channel] > 0) {
                                        signed_value_15 = D_8008933C[update_difficulty] * -1;
                                        D_800A3A1E[update_channel] = signed_value_15;
                                        steering_work = signed_value_15;
                                    } else if (vehicles_update[update_vehicle_index].steering_accumulator >= D_800A3A1E[update_channel]) {
                                        signed_halfword_1 = &D_800A3A20[update_channel];
                                        GUEST_FIELD(signed_halfword_1, s16 *, -2) = (s16) (GUEST_FIELD(signed_halfword_1, s16 *, -2) - D_80089340[update_difficulty]);
                                        signed_value_5 = D_800A3A1E[update_channel];
                                        steering_work = signed_value_5;
                                        if (signed_value_5 < -D_80089338[update_difficulty]) {
                                            signed_value_9 = D_80089338[update_difficulty] * -1;
                                            D_800A3A1E[update_channel] = signed_value_9;
                                            steering_work = signed_value_9;
                                        }
                                    } else {
                                        steering_work = -D_8008933C[update_difficulty];
                                    }
                                } else if (D_800A3A1E[update_channel] > 0) {
                                    steering_work = 0;
                                    D_800A3A1E[update_channel] = 0;
                                } else if (D_800A3A1E[update_channel] < -D_8008933C[update_difficulty]) {
                                    signed_halfword_17 = &D_800A3A20[update_channel];
                                    GUEST_FIELD(signed_halfword_17, s16 *, -2) = (s16) (GUEST_FIELD(signed_halfword_17, s16 *, -2) + (D_80089340[update_difficulty] * 2));
                                    steering_work = D_800A3A1E[update_channel];
                                } else {
                                    integer_value_2 = (s32) (D_FLT_80089344[update_difficulty] * (f32) steering_work);
                                    steering_work = (s16) integer_value_2;
                                    D_800A3A1E[update_channel] = (s16) integer_value_2;
                                }
                            }
                            steering_work = (s16) (s32) ((f32) steering_work * D_FLT_800CE790);
                        }
                        D_80098830 = steering_work - vehicles_update[update_vehicle_index].steering_accumulator;
                        if (steering_slew_limits[update_difficulty] < D_80098830) {
                            steering_work = steering_slew_limits[update_difficulty] + vehicles_update[update_vehicle_index].steering_accumulator;
                        }
                        if (D_80098830 < -steering_slew_limits[update_difficulty]) {
                            steering_work = vehicles_update[update_vehicle_index].steering_accumulator - steering_slew_limits[update_difficulty];
                        }
                        vehicles_update[update_vehicle_index].steering_accumulator = steering_work;
                    } else {
                        if (steering_work >= 0) {
                            signed_value_20 = steering_work - 7;
                            steering_work = signed_value_20;
                            if (signed_value_20 < 0) {
                                steering_work = 0;
                            }
                            if (steering_work >= 0x3D) {
                                steering_work = 0x3C;
                            }
                        } else {
                            signed_value_12 = steering_work + 7;
                            steering_work = signed_value_12;
                            if (signed_value_12 > 0) {
                                steering_work = 0;
                            }
                            if (steering_work < -0x3C) {
                                steering_work = -0x3C;
                            }
                        }
                        steering_work = (s16) ((s16) steering_work >> 1);
                    }
                    if (vehicles_update[update_vehicle_index].speed >= 0x3D) {
                        D_FLT_8009884C = vehicles_update[update_vehicle_index].field_40;
                        D_FLT_800988D8 = vehicles_update[update_vehicle_index].field_44;
                        D_FLT_800CE788 = (f32) ((f64) vehicles_update[update_vehicle_index].steering_demand * D_DBL_8008D9D8);
                        D_FLT_800CE7C0 = func_80075730(D_FLT_800CE788);
                        D_FLT_800CE7C8 = func_800758F0(D_FLT_800CE788);
                        D_FLT_80098EF0 = (D_FLT_8009884C * D_FLT_800CE7C8) + (D_FLT_800988D8 * D_FLT_800CE7C0);
                        D_FLT_80099190 = (D_FLT_800988D8 * D_FLT_800CE7C8) - (D_FLT_8009884C * D_FLT_800CE7C0);
                        D_FLT_800A2BB0 = vehicles_update[update_vehicle_index].field_6c;
                        D_FLT_800A2BB8 = vehicles_update[update_vehicle_index].field_74;
                        float_value_5 = (D_FLT_800A2BB0 * D_FLT_80098EF0) + (D_FLT_800A2BB8 * D_FLT_80099190);
                        D_FLT_800CE790 = float_value_5;
                        if (float_value_5 < 0.0f) {
                            D_FLT_800CE790 = -D_FLT_800CE790;
                        }
                        D_80098830 = (s16) (s32) ((f64) D_FLT_800CE790 * D_DBL_8008D9E0);
                        steering_work = (s16) (s32) (D_FLT_8008926C[D_80098830] * (f32) steering_work);
                    }
                    steering_direct();
                    vehicles_update[update_vehicle_index].steering_demand = steering_work;
                    func_80028250();
                    func_8002622C();
                } else {
                    func_80028250();
                    if (D_800CE808 == 0) {
                        if (D_800CE7A8 == 0) {
                            D_800A2D40[update_channel] = 0x3C;
                        } else {
                            D_800A2D40[update_channel] = 0x78;
                        }
                        if (D_800CE6A4 < 2) {
                            D_800A2D28[update_channel] = 0x384;
                        } else {
                            D_800A2D28[update_channel] = 0x4B0;
                        }
                    }
                    signed_value_6 = racers_update[update_channel].field_50;
                    switch (signed_value_6) {
                    case 1:
                        D_8020FBCB[update_channel] = 1;
                        vehicles_update[update_vehicle_index].field_64 = 0.0f;
                        vehicles_update[update_vehicle_index].field_68 = 1.0f;
                        if (D_800CE7A8 == 0) {
                            D_80098730 = (u16) update_channel;
                            D_FLT_800CE788 = GUEST_FIELD((u8*)D_FLT_801E9ED8 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                        } else {
                            D_80098730 = 2 - update_channel;
                            D_FLT_800CE788 = (f32) ((f64) GUEST_FIELD((u8*)D_FLT_801E9ED8 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0) + D_DBL_8008D9E8);
                        }
                        D_FLT_800CE7C0 = func_80075730(D_FLT_800CE788);
                        D_FLT_800CE7C8 = func_800758F0(D_FLT_800CE788);
                        D_FLT_800A2BB0 = GUEST_FIELD((u8*)D_FLT_801E9ECC + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0) - vehicles_update[update_vehicle_index].position_x;
                        D_FLT_800A2BB8 = GUEST_FIELD((u8*)D_FLT_801E9ED4 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0) - vehicles_update[update_vehicle_index].position_z;
                        D_FLT_80098EF0 = vehicles_update[update_vehicle_index].field_6c;
                        D_FLT_80099190 = vehicles_update[update_vehicle_index].field_74;
                        float_value_1 = (D_FLT_800A2BB0 * D_FLT_800CE7C0) + (D_FLT_800A2BB8 * D_FLT_800CE7C8);
                        D_FLT_800CE7A0 = float_value_1;
                        if (float_value_1 <= func_80075720((D_FLT_80098EF0 * D_FLT_80098EF0) + (D_FLT_80099190 * D_FLT_80099190))) {
                            vehicles_update[update_vehicle_index].position_x = GUEST_FIELD((u8*)D_FLT_801E9ECC + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                            vehicles_update[update_vehicle_index].position_z = GUEST_FIELD((u8*)D_FLT_801E9ED4 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                            vehicle_record_30 = &vehicles_update[update_vehicle_index];
                            vehicles_update[update_vehicle_index].field_74 = 0.0f;
                            vehicle_record_30->field_6c = vehicle_record_30->field_74;
                            if ((D_DBL_8008D9F0 < (f64) vehicles_update[update_vehicle_index].field_24) || (racer_record_1 = &racers_update[update_channel], racer_record_1->field_2e -= 1, (racers_update[update_channel].field_2e <= 0))) {
                                racer_record_3 = &racers_update[update_channel];
                                racer_record_3->field_50 += 1;
                            }
                        } else if (D_FLT_800CE7A0 < 1000.0f) {
                            if (vehicles_update[update_vehicle_index].speed >= 0xB) {
                                func_80029DCC();
                                vehicles_update[update_vehicle_index].brake_demand = 15.0f;
                            } else {
                                vehicle_record_13 = &vehicles_update[update_vehicle_index];
                                vehicle_record_13->throttle_demand += 0xA;
                                if (vehicles_update[update_vehicle_index].throttle_demand >= 0x65) {
                                    vehicles_update[update_vehicle_index].throttle_demand = 0x64;
                                }
                            }
                        }
                        func_8002622C();
                        break;
                    case 2:
                        func_8002A070();
                        vehicles_update[update_vehicle_index].field_24 = 1.0f;
                        vehicle_record_20 = &vehicles_update[update_vehicle_index];
                        vehicles_update[update_vehicle_index].field_28 = 0.0f;
                        vehicle_record_20->field_20 = vehicle_record_20->field_28;
                        func_80037BC0(update_vehicle_index);
                        if (D_800CE7A8 == 0) {
                            D_80098730 = (u16) update_channel;
                        } else {
                            D_80098730 = 2 - update_channel;
                        }
                        vehicles_update[update_vehicle_index].position_x = GUEST_FIELD((u8*)D_FLT_801E9ECC + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                        vehicles_update[update_vehicle_index].position_y = (D_FLT_80229C9C[update_channel] * 2.0f) + GUEST_FIELD((u8*)D_FLT_801E9ED0 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0) + 3.0f;
                        vehicles_update[update_vehicle_index].position_z = GUEST_FIELD((u8*)D_FLT_801E9ED4 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                        vehicles_update[update_vehicle_index].field_70 = 0.0f;
                        D_800986A0 = vehicles_update[update_vehicle_index].field_02;
                        if (D_801126A1[update_channel] == 0) {
                            record_bytes_8 = ((s16) D_800986A0 << 6) + (u8*)D_800B8910;
                            GUEST_FIELD(record_bytes_8, u16 *, 0xC) = (u16) (GUEST_FIELD(record_bytes_8, u16 *, 0xC) & ~0x40);
                        } else {
                            record_bytes_3 = ((s16) D_800986A0 << 6) + (u8*)D_800B8910;
                            GUEST_FIELD(record_bytes_3, u16 *, 0xC) = (u16) (GUEST_FIELD(record_bytes_3, u16 *, 0xC) | 0x40);
                        }
                        D_800986A0 = (u16) GUEST_FIELD((u8*)D_800B8912 + ((s16) D_800986A0 << 6), s16 *, 0);
                        if (D_8011269F[update_channel] == 0) {
                            record_bytes_6 = ((s16) D_800986A0 << 6) + (u8*)D_800B8910;
                            GUEST_FIELD(record_bytes_6, u16 *, 0xC) = (u16) (GUEST_FIELD(record_bytes_6, u16 *, 0xC) & ~0x40);
                        } else {
                            record_bytes_12 = ((s16) D_800986A0 << 6) + (u8*)D_800B8910;
                            GUEST_FIELD(record_bytes_12, u16 *, 0xC) = (u16) (GUEST_FIELD(record_bytes_12, u16 *, 0xC) | 0x40);
                        }
                        D_800986A0 = (u16) GUEST_FIELD((u8*)D_800B8912 + ((s16) D_800986A0 << 6), s16 *, 0);
                        if (D_801126A5[update_channel] == 0) {
                            record_bytes_5 = ((s16) D_800986A0 << 6) + (u8*)D_800B8910;
                            GUEST_FIELD(record_bytes_5, u16 *, 0xC) = (u16) (GUEST_FIELD(record_bytes_5, u16 *, 0xC) & ~0x40);
                        } else {
                            record_bytes_7 = ((s16) D_800986A0 << 6) + (u8*)D_800B8910;
                            GUEST_FIELD(record_bytes_7, u16 *, 0xC) = (u16) (GUEST_FIELD(record_bytes_7, u16 *, 0xC) | 0x40);
                        }
                        D_800986A0 = (u16) GUEST_FIELD((u8*)D_800B8912 + ((s16) D_800986A0 << 6), s16 *, 0);
                        if (D_801126A3[update_channel] == 0) {
                            record_bytes_9 = ((s16) D_800986A0 << 6) + (u8*)D_800B8910;
                            GUEST_FIELD(record_bytes_9, u16 *, 0xC) = (u16) (GUEST_FIELD(record_bytes_9, u16 *, 0xC) & ~0x40);
                        } else {
                            record_bytes_4 = ((s16) D_800986A0 << 6) + (u8*)D_800B8910;
                            GUEST_FIELD(record_bytes_4, u16 *, 0xC) = (u16) (GUEST_FIELD(record_bytes_4, u16 *, 0xC) | 0x40);
                        }
                        update_scratch_index = GUEST_FIELD(&vehicles_update[GUEST_FIELD(&vehicles_update[update_vehicle_index], s16 *, 0x5A)], s16 *, 0x5A);
                        D_800986A8 = vehicles_update[update_scratch_index].field_0c;
                        func_80014D50(update_vehicle_index, D_800986A8);
                        if (D_8020FBDB[update_channel] != 0) {
                            racer_record_4 = &racers_update[update_channel];
                            racer_record_4->field_50 += 1;
                        }
                        racers_update[update_channel].field_66 = -1;
                        racer_record_9 = &racers_update[update_channel];
                        GUEST_FIELD(racer_record_9, s16 *, 0x64) = (s16) GUEST_FIELD(racer_record_9, s16 *, 0x66);
                        racers_update[update_channel].field_76 = -1;
                        racer_record_10 = &racers_update[update_channel];
                        signed_value_8 = GUEST_FIELD(racer_record_10, s16 *, 0x76);
                        GUEST_FIELD(racer_record_10, s16 *, 0x74) = signed_value_8;
                        racers_update[update_channel].field_72 = signed_value_8;
                        racers_update[update_channel].field_70 = signed_value_8;
                        func_80025834(update_vehicle_index);
                        break;
                    case 3:
                        if (D_800CE7A8 == 0) {
                            D_80098730 = (u16) update_channel;
                            D_FLT_800CE788 = GUEST_FIELD((u8*)D_FLT_801E9ED8 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                        } else {
                            D_80098730 = 2 - update_channel;
                            D_FLT_800CE788 = (f32) ((f64) GUEST_FIELD((u8*)D_FLT_801E9ED8 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0) + D_DBL_8008D9F8);
                        }
                        D_FLT_800CE7C0 = func_80075730(D_FLT_800CE788);
                        D_FLT_800CE7C8 = func_800758F0(D_FLT_800CE788);
                        D_FLT_800A2BB0 = vehicles_update[update_vehicle_index].position_x - GUEST_FIELD((u8*)D_FLT_801E9ECC + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                        D_FLT_800A2BB8 = vehicles_update[update_vehicle_index].position_z - GUEST_FIELD((u8*)D_FLT_801E9ED4 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                        D_800986A0 = vehicles_update[update_vehicle_index].field_02;
                        record_bytes_1 = ((s16) D_800986A0 << 6) + (u8*)D_800B8910;
                        GUEST_FIELD(record_bytes_1, u16 *, 0xC) = (u16) (GUEST_FIELD(record_bytes_1, u16 *, 0xC) & ~0x40);
                        D_800986A0 = (u16) GUEST_FIELD((u8*)D_800B8912 + ((s16) D_800986A0 << 6), s16 *, 0);
                        record_bytes_10 = ((s16) D_800986A0 << 6) + (u8*)D_800B8910;
                        GUEST_FIELD(record_bytes_10, u16 *, 0xC) = (u16) (GUEST_FIELD(record_bytes_10, u16 *, 0xC) & ~0x40);
                        D_800986A0 = (u16) GUEST_FIELD((u8*)D_800B8912 + ((s16) D_800986A0 << 6), s16 *, 0);
                        record_bytes_11 = ((s16) D_800986A0 << 6) + (u8*)D_800B8910;
                        GUEST_FIELD(record_bytes_11, u16 *, 0xC) = (u16) (GUEST_FIELD(record_bytes_11, u16 *, 0xC) & ~0x40);
                        D_800986A0 = (u16) GUEST_FIELD((u8*)D_800B8912 + ((s16) D_800986A0 << 6), s16 *, 0);
                        record_bytes_2 = ((s16) D_800986A0 << 6) + (u8*)D_800B8910;
                        GUEST_FIELD(record_bytes_2, u16 *, 0xC) = (u16) (GUEST_FIELD(record_bytes_2, u16 *, 0xC) & ~0x40);
                        D_FLT_800CE7A0 = func_80075720((D_FLT_800A2BB0 * D_FLT_800A2BB0) + (D_FLT_800A2BB8 * D_FLT_800A2BB8));
                        if (D_FLT_800CE7A0 > 1000.0f) {
                            racers_update[update_channel].field_50 = 0;
                            D_800A2D40[update_channel] = 0;
                        }
                        vehicles_update[update_vehicle_index].gear_state = 1;
                        vehicles_update[update_vehicle_index].throttle_demand = 0x64;
                        func_8002D37C();
                        func_8002622C();
                        break;
                    }
                }
                break;
            /* AI channels use the same linked pool and service-state tables. */
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
                update_channel = vehicles_update[update_vehicle_index].channel - 4;
                vehicle_record_9 = &vehicles_update[update_vehicle_index];
                vehicle_record_9->flags &= ~1;
                D_80098830 = GUEST_FIELD((D_800BF1C4 + (racers_update[update_channel].field_24 * 0x10)), s16 *, 0xC);
                D_800986D8 = 0;
                signed_value_13 = D_800B6758[D_800986D8];
                D_8009870A = signed_value_13;
                D_800986D8 += 1;
                if ((signed_value_13 + 1) != 0) {
find_ai_visible_segment:
                    if (D_80098830 == D_8009870A) {
                        vehicle_record_21 = &vehicles_update[update_vehicle_index];
                        vehicle_record_21->flags |= 1;
                    } else {
                        signed_value_10 = D_800B6758[D_800986D8];
                        D_8009870A = signed_value_10;
                        D_800986D8 += 1;
                        if ((signed_value_10 + 1) != 0) {
                            goto find_ai_visible_segment;
                        }
                    }
                }
                func_80028250();
                if (racers_update[update_channel].field_50 == 0) {
                    if (D_800CE6B0 >= 3) {
                        func_8002D37C();
                    }
                    func_8002622C();
                } else {
                    func_80028250();
                    D_80098730 = 0;
find_service_slot:
                    if (D_80098EFC[(s16) D_80098730] != update_channel) {
                        signed_value_16 = (s16) D_80098730 + 1;
                        D_80098730 = (u16) signed_value_16;
                        if (signed_value_16 < 3) {
                            goto find_service_slot;
                        }
                    }
                    signed_value_7 = racers_update[update_channel].field_50;
                    switch (signed_value_7) {
                    case 1:
                        vehicles_update[update_vehicle_index].field_64 = 0.0f;
                        vehicles_update[update_vehicle_index].field_68 = 1.0f;
                        if (D_800CE7A8 == 0) {
                            D_FLT_800CE788 = GUEST_FIELD((u8*)D_FLT_801E9ED8 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                        } else {
                            D_FLT_800CE788 = (f32) ((f64) GUEST_FIELD((u8*)D_FLT_801E9ED8 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0) + D_DBL_8008DA00);
                        }
                        D_FLT_800CE7C0 = func_80075730(D_FLT_800CE788);
                        vehicles_update[update_vehicle_index].field_40 = D_FLT_800CE7C0;
                        D_FLT_800CE7C8 = func_800758F0(D_FLT_800CE788);
                        vehicles_update[update_vehicle_index].field_44 = D_FLT_800CE7C8;
                        D_FLT_800A2BB0 = GUEST_FIELD((u8*)D_FLT_801E9ECC + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0) - vehicles_update[update_vehicle_index].position_x;
                        D_FLT_800A2BB8 = GUEST_FIELD((u8*)D_FLT_801E9ED4 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0) - vehicles_update[update_vehicle_index].position_z;
                        D_FLT_80098EF0 = vehicles_update[update_vehicle_index].field_6c;
                        D_FLT_80099190 = vehicles_update[update_vehicle_index].field_74;
                        float_value_2 = (D_FLT_800A2BB0 * D_FLT_800CE7C0) + (D_FLT_800A2BB8 * D_FLT_800CE7C8);
                        D_FLT_800CE7A0 = float_value_2;
                        if (float_value_2 <= func_80075720((D_FLT_80098EF0 * D_FLT_80098EF0) + (D_FLT_80099190 * D_FLT_80099190))) {
                            vehicles_update[update_vehicle_index].position_x = GUEST_FIELD((u8*)D_FLT_801E9ECC + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                            vehicles_update[update_vehicle_index].position_z = GUEST_FIELD((u8*)D_FLT_801E9ED4 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                            vehicle_record_10 = &vehicles_update[update_vehicle_index];
                            vehicles_update[update_vehicle_index].field_74 = 0.0f;
                            vehicle_record_10->field_6c = vehicle_record_10->field_74;
                            signed_value_11 = D_80099194 + 1;
                            D_80099194 = signed_value_11;
                            if (signed_value_11 >= 0xA) {
                                D_80099194 = 0;
                            }
                            racers_update[update_channel].field_2e = D_801E9EB8[D_80099194];
                            D_FLT_800CE790 = (f32) ((100.0 - (f64) racers_update[update_channel].field_58) * D_DBL_8008DA08);
                            racer_record_2 = &racers_update[update_channel];
                            racer_record_2->field_2e += (s16) (s32) D_FLT_800CE790;
                            racer_record_11 = &racers_update[update_channel];
                            racer_record_11->field_50 += 1;
                        } else if (D_FLT_800CE7A0 < 1000.0f) {
                            if (vehicles_update[update_vehicle_index].speed >= 0xB) {
                                func_80029DCC();
                                vehicles_update[update_vehicle_index].brake_demand = 15.0f;
                            } else {
                                func_8002A070();
                                vehicle_record_14 = &vehicles_update[update_vehicle_index];
                                vehicle_record_14->throttle_demand += 0xA;
                                if (vehicles_update[update_vehicle_index].throttle_demand >= 0x65) {
                                    vehicles_update[update_vehicle_index].throttle_demand = 0x64;
                                }
                            }
                        } else {
                            func_8002A070();
                            vehicle_record_15 = &vehicles_update[update_vehicle_index];
                            vehicle_record_15->throttle_demand += 0xA;
                            if (vehicles_update[update_vehicle_index].throttle_demand >= 0x65) {
                                vehicles_update[update_vehicle_index].throttle_demand = 0x64;
                            }
                        }
                        func_8002622C();
                        break;
                    case 2:
                        func_8002A070();
                        vehicles_update[update_vehicle_index].field_24 = 1.0f;
                        vehicle_record_2 = &vehicles_update[update_vehicle_index];
                        vehicles_update[update_vehicle_index].field_28 = 0.0f;
                        vehicle_record_2->field_20 = vehicle_record_2->field_28;
                        func_80037BC0(update_vehicle_index);
                        vehicles_update[update_vehicle_index].position_x = GUEST_FIELD((u8*)D_FLT_801E9ECC + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                        vehicles_update[update_vehicle_index].position_z = GUEST_FIELD((u8*)D_FLT_801E9ED4 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                        vehicle_record_3 = &vehicles_update[update_vehicle_index];
                        vehicles_update[update_vehicle_index].field_74 = 0.0f;
                        vehicle_record_3->field_6c = vehicle_record_3->field_74;
                        racer_record_5 = &racers_update[update_channel];
                        racer_record_5->field_2e -= 1;
                        if (racers_update[update_channel].field_2e <= 0) {
                            racers_update[update_channel].field_58 = 100.0f;
                            update_scratch_index = (s16) vehicles_update[update_vehicle_index].category;
                            if (update_difficulty == 0) {
                                float_value_3 = (f32) ((f64) GUEST_FIELD((u8*)D_FLT_8013E290 + ((update_scratch_index * 8) + (update_difficulty * 4)), f32 *, 0) * D_DBL_8008DA10);
                                vehicles_update[update_vehicle_index].field_d4 = float_value_3;
                                vehicles_update[update_vehicle_index].field_d0 = float_value_3;
                                float_value_6 = (f32) ((f64) GUEST_FIELD((u8*)D_FLT_8013E2D0 + ((update_scratch_index * 8) + (update_difficulty * 4)), f32 *, 0) * D_DBL_8008DA18);
                                vehicles_update[update_vehicle_index].field_dc = float_value_6;
                                float_field_1 = &vehicles_update[update_vehicle_index].field_d8;
                            } else {
                                float_value_4 = GUEST_FIELD((u8*)D_FLT_8013E290 + ((update_scratch_index * 8) + (update_difficulty * 4)), f32 *, 0);
                                vehicles_update[update_vehicle_index].field_d4 = float_value_4;
                                vehicles_update[update_vehicle_index].field_d0 = float_value_4;
                                float_value_6 = GUEST_FIELD((u8*)D_FLT_8013E2D0 + ((update_scratch_index * 8) + (update_difficulty * 4)), f32 *, 0);
                                vehicles_update[update_vehicle_index].field_dc = float_value_6;
                                float_field_1 = &vehicles_update[update_vehicle_index].field_d8;
                            }
                            *float_field_1 = float_value_6;
                            racer_record_8 = &racers_update[update_channel];
                            racer_record_8->field_50 += 1;
                            racers_update[update_channel].field_52 = 0;
                            racers_update[update_channel].field_02 = 0;
                            racer_record_6 = &racers_update[update_channel];
                            racer_record_6->field_2e = (racer_record_6->field_04 << 5) + D_8008940C[update_difficulty];
                        }
                        break;
                    case 3:
                        if (D_800CE7A8 == 0) {
                            D_FLT_800CE788 = GUEST_FIELD((u8*)D_FLT_801E9ED8 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                        } else {
                            D_FLT_800CE788 = (f32) ((f64) GUEST_FIELD((u8*)D_FLT_801E9ED8 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0) + D_DBL_8008DA20);
                        }
                        D_FLT_800CE7C0 = func_80075730(D_FLT_800CE788);
                        D_FLT_800CE7C8 = func_800758F0(D_FLT_800CE788);
                        D_FLT_800A2BB0 = vehicles_update[update_vehicle_index].position_x - GUEST_FIELD((u8*)D_FLT_801E9ECC + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                        D_FLT_800A2BB8 = vehicles_update[update_vehicle_index].position_z - GUEST_FIELD((u8*)D_FLT_801E9ED4 + ((D_800CE794 * 0x30) + ((s16) D_80098730 * 0x10)), f32 *, 0);
                        D_FLT_800CE7A0 = func_80075720((D_FLT_800A2BB0 * D_FLT_800A2BB0) + (D_FLT_800A2BB8 * D_FLT_800A2BB8));
                        if (D_FLT_800CE7A0 > 1000.0f) {
                            racers_update[update_channel].field_50 = 0;
                            D_80098EFC[(s16) D_80098730] = 0;
                        }
                        vehicles_update[update_vehicle_index].gear_state = 1;
                        vehicles_update[update_vehicle_index].throttle_demand = 0x64;
                        func_8002D37C();
                        func_8002622C();
                        break;
                    }
                }
                D_FLT_800A2BB0 = vehicles_update[update_vehicle_index].position_x - GUEST_FIELD(D_FLT_800A2DD0, f32 *, 4);
                D_FLT_800A2BB4 = vehicles_update[update_vehicle_index].position_y - GUEST_FIELD(D_FLT_800A2DF8, f32 *, 4);
                D_FLT_800A2BB8 = vehicles_update[update_vehicle_index].position_z - GUEST_FIELD(D_FLT_800A2E20, f32 *, 4);
                D_FLT_800CE7A0 = func_80075720((D_FLT_800A2BB0 * D_FLT_800A2BB0) + (D_FLT_800A2BB4 * D_FLT_800A2BB4) + (D_FLT_800A2BB8 * D_FLT_800A2BB8));
                if (D_FLT_800CE7A0 < D_FLT_800A2BF0) {
                    D_FLT_800A2BF0 = D_FLT_800CE7A0;
                    D_800A2BEC = update_channel;
                }
                break;
            /* Authored scenery visibility follows the drawn-segment list. */
            case 14:
                vehicle_record_4 = &vehicles_update[update_vehicle_index];
                vehicle_record_4->flags &= ~2;
                D_80098830 = (s16) vehicles_update[update_vehicle_index].category;
                D_800986D8 = 0;
                signed_value_25 = D_800B6758[D_800986D8];
                D_8009870A = signed_value_25;
                D_800986D8 += 1;
                if ((signed_value_25 + 1) != 0) {
find_scenery_flag_two:
                    if (D_80098830 == D_8009870A) {
                        vehicle_record_26 = &vehicles_update[update_vehicle_index];
                        vehicle_record_26->flags |= 2;
                        vehicle_record_16 = &vehicles_update[update_vehicle_index];
                        vehicle_record_16->field_c2 -= 6;
                    } else {
                        signed_value_26 = D_800B6758[D_800986D8];
                        D_8009870A = signed_value_26;
                        D_800986D8 += 1;
                        if ((signed_value_26 + 1) != 0) {
                            goto find_scenery_flag_two;
                        }
                    }
                }
                break;
            case 13:
                vehicle_record_5 = &vehicles_update[update_vehicle_index];
                vehicle_record_5->flags &= ~1;
                D_80098830 = (s16) vehicles_update[update_vehicle_index].category;
                D_800986D8 = 0;
                signed_value_24 = D_800B6758[D_800986D8];
                D_8009870A = signed_value_24;
                D_800986D8 += 1;
                if ((signed_value_24 + 1) != 0) {
find_scenery_flag_one:
                    if (D_80098830 == D_8009870A) {
                        vehicle_record_23 = &vehicles_update[update_vehicle_index];
                        vehicle_record_23->flags |= 1;
                    } else {
                        signed_value_17 = D_800B6758[D_800986D8];
                        D_8009870A = signed_value_17;
                        D_800986D8 += 1;
                        if ((signed_value_17 + 1) != 0) {
                            goto find_scenery_flag_one;
                        }
                    }
                }
                break;
            default:
                break;
            }
            if (next_vehicle_override > 0) {
                update_vehicle_index = next_vehicle_override;
            } else {
                update_vehicle_index = vehicles_update[update_vehicle_index].field_06;
            }
        } while (update_vehicle_index != -1);
    }
}

#ifndef LAMBO_GUEST_PIT_STOP_H
#define LAMBO_GUEST_PIT_STOP_H

/* USA guest layouts. These types are compiled for MIPS, never cast over host
 * RDRAM. Addresses live in symbols.ld; unknown fields retain neutral names. */
typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;

typedef struct {
    s16 frame;
    s16 speed;
    u8 unknown_04[20];
} PitAnimation;

typedef struct {
    u8 unknown_00[8];
    u8 authored;
    u8 displayed;
} PitTyrePart;

typedef struct {
    u8 unknown_00[0x0e];
    s16 channel;
    u8 unknown_10[2];
    u16 category;
    u8 unknown_14[0xbc];
    float tyre_value[4];
    u8 unknown_e0[0x2c];
} PitVehicle;

typedef struct {
    u8 unknown_00[0x58];
    float fuel;
    u8 unknown_5c[0x28];
} PitRacer;

_Static_assert(sizeof(PitAnimation) == 0x18, "pit animation stride");
_Static_assert(sizeof(PitVehicle) == 0x10c, "vehicle stride");
_Static_assert(sizeof(PitRacer) == 0x84, "racer stride");
_Static_assert(__builtin_offsetof(PitVehicle, channel) == 0x0e, "vehicle channel");
_Static_assert(__builtin_offsetof(PitVehicle, category) == 0x12, "vehicle category");
_Static_assert(__builtin_offsetof(PitVehicle, tyre_value) == 0xd0, "vehicle tyres");
_Static_assert(__builtin_offsetof(PitRacer, fuel) == 0x58, "racer fuel");

extern s8 pit_stage_index[2];
extern u8 pit_stage_order[];
extern s8 pit_entry_pending[2];
extern u8 pit_turning[2];
extern s32 pit_fuel[2], pit_pressure[2], pit_pressure_limit[2];
extern s16 pit_spill_ticks[2];
extern s32 pit_turns[2], pit_previous_turns[2], pit_direction_index[2];
extern s32 pit_directions[4];
extern u32 pit_turn_start[2], game_tick;
extern s32 pit_sound;
extern u8 pit_tyres_fitted[2][4];
extern PitTyrePart* pit_tyre_parts[2][16];
extern PitAnimation pit_animation_a[2], pit_animation_b[2], pit_exit_animation[2];
extern u8 pit_model_a[2][0x3a8], pit_model_b[2][0x348];
extern u16 pit_pressed_buttons[2], pit_consumed_buttons[2];
extern s16 pit_control_scheme[2];
extern s16 current_channel, race_difficulty, current_vehicle, pit_scratch;
extern u16 current_category;
extern s16 racer_bodies[][12];
extern PitVehicle vehicles[64];
extern PitRacer racers[];
extern float pit_delivery_fraction;
extern float pit_front_tyre_values[][2], pit_rear_tyre_values[][2];

/* Existing game functions, named by the current recomp's symbol table. */
void func_80066F84(s32 sound, float volume, s32 pitch, s32 pan, s32 unused);
void func_800670BC(s32 sound, float volume, s32 pitch, s32 pan, s32 unused, s32 duration);
void func_800673A0(s32 sound);
s32 func_80067428(s32 sound);
void func_8006B320(u8 slot);
float func_80075730(float angle);

/* Host setting query. No game state or guest pointer crosses this interface. */
s32 lambo_get_automatic_pit_stops(void);

#endif

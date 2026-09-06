#ifndef LAMBO_VEHICLE_H
#define LAMBO_VEHICLE_H

#include <stddef.h>
#include <stdint.h>

// Guest layout schema, NOT a host view of RDRAM. Use offsetof with MEM_H/MEM_W:
// librecomp's word-swizzled RDRAM does not have native C struct byte order.
// Unknown fields deliberately remain unnamed. Evidence: ROM constructor at
// runtime 0x80011370; control/physics consumers listed in CAR_DIFFERENCES.md.
#pragma pack(push, 1)
typedef struct LamboVehicleRecord {
    uint8_t unknown_00[0x0E];
    int16_t channel;
    uint8_t unknown_10[2];
    uint16_t category;
    float position[3];
    uint8_t unknown_20[0x70];
    int32_t speed;
    uint8_t unknown_94[0x0C];
    float brake_demand;
    uint8_t unknown_a4[4];
    int16_t gear_state;
    int16_t throttle_demand;
    int16_t brake_latch;
    uint8_t unknown_ae[6];
    int16_t steering_accumulator;
    uint8_t unknown_b6[0x1A];
    float physics_scratch[4];
    uint8_t unknown_e0[0x2C];
} LamboVehicleRecord;
#pragma pack(pop)

#define LAMBO_VEHICLE_BASE UINT32_C(0x800B69A8)
#define LAMBO_GUEST_CURRENT_VEHICLE_ADDR UINT32_C(0x80098398)
#define LAMBO_GUEST_GAME_STATE_ADDR UINT32_C(0x800CE6AC)
#define LAMBO_GUEST_RACE_DIFFICULTY_ADDR UINT32_C(0x800CE79C)
#define LAMBO_GUEST_MENU_DIFFICULTY_ADDR UINT32_C(0x800CE7A4)
#define LAMBO_GUEST_PLAYER_MODEL_CURSOR_ADDR UINT32_C(0x800CE7E8)
#define LAMBO_GUEST_PLAYER_MODEL_CURSOR_STRIDE UINT32_C(2)
#ifdef __cplusplus
#define LAMBO_LAYOUT_ASSERT static_assert
#else
#define LAMBO_LAYOUT_ASSERT _Static_assert
#endif
LAMBO_LAYOUT_ASSERT(sizeof(LamboVehicleRecord) == 0x10C, "vehicle stride");
LAMBO_LAYOUT_ASSERT(offsetof(LamboVehicleRecord, channel) == 0x0E, "channel offset");
LAMBO_LAYOUT_ASSERT(offsetof(LamboVehicleRecord, category) == 0x12, "category offset");
LAMBO_LAYOUT_ASSERT(offsetof(LamboVehicleRecord, position) == 0x14, "position offset");
LAMBO_LAYOUT_ASSERT(offsetof(LamboVehicleRecord, speed) == 0x90, "speed offset");
LAMBO_LAYOUT_ASSERT(offsetof(LamboVehicleRecord, brake_demand) == 0xA0, "brake offset");
LAMBO_LAYOUT_ASSERT(offsetof(LamboVehicleRecord, gear_state) == 0xA8, "gear offset");
LAMBO_LAYOUT_ASSERT(offsetof(LamboVehicleRecord, throttle_demand) == 0xAA, "throttle offset");
LAMBO_LAYOUT_ASSERT(offsetof(LamboVehicleRecord, brake_latch) == 0xAC, "latch offset");
LAMBO_LAYOUT_ASSERT(offsetof(LamboVehicleRecord, steering_accumulator) == 0xB4, "steering offset");
LAMBO_LAYOUT_ASSERT(offsetof(LamboVehicleRecord, physics_scratch) == 0xD0, "scratch offset");
#undef LAMBO_LAYOUT_ASSERT
#endif

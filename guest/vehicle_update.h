#ifndef LAMBO_GUEST_VEHICLE_UPDATE_H
#define LAMBO_GUEST_VEHICLE_UPDATE_H
/* USA guest-only layouts, compiled for big-endian MIPS. Neutral field names
 * mean only width and offset are recovered, not gameplay semantics. */
typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;
typedef float f32;
typedef double f64;
/* Offsets below are bytes, including the few negative aliases in the ROM. */
#define GUEST_FIELD(base, type, offset) (*(type)((u8*)(base) + (offset)))
typedef struct {
    u16 flags;
    u16 field_02;
    u8 unknown_04[2];
    s16 field_06;
    u8 unknown_08[4];
    s16 field_0c;
    s16 channel;
    u8 unknown_10[2];
    u16 category;
    f32 position_x;
    f32 position_y;
    f32 position_z;
    f32 field_20;
    f32 field_24;
    f32 field_28;
    u8 unknown_2c[20];
    f32 field_40;
    f32 field_44;
    u8 unknown_48[18];
    s16 field_5a;
    u8 unknown_5c[8];
    f32 field_64;
    f32 field_68;
    f32 field_6c;
    f32 field_70;
    f32 field_74;
    u8 unknown_78[24];
    s32 speed;
    u8 unknown_94[12];
    f32 brake_demand;
    u8 unknown_a4[4];
    s16 gear_state;
    s16 throttle_demand;
    s16 brake_latch;
    s16 field_ae;
    s16 field_b0;
    s16 steering_demand;
    s16 steering_accumulator;
    u8 unknown_b6[12];
    s16 field_c2;
    u8 unknown_c4[12];
    f32 field_d0;
    f32 field_d4;
    f32 field_d8;
    f32 field_dc;
    u8 unknown_e0[44];
} Vehicle;
_Static_assert(sizeof(Vehicle) == 0x10c, "Vehicle stride");
_Static_assert(__builtin_offsetof(Vehicle, channel) == 0x0e, "channel");
_Static_assert(__builtin_offsetof(Vehicle, speed) == 0x90, "speed");
_Static_assert(__builtin_offsetof(Vehicle, brake_demand) == 0xa0, "brake demand");
_Static_assert(__builtin_offsetof(Vehicle, throttle_demand) == 0xaa, "throttle demand");
_Static_assert(__builtin_offsetof(Vehicle, brake_latch) == 0xac, "brake latch");
_Static_assert(__builtin_offsetof(Vehicle, steering_demand) == 0xb2, "steering demand");
_Static_assert(__builtin_offsetof(Vehicle, steering_accumulator) == 0xb4, "steering accumulator");
typedef struct {
    u8 unknown_00[2];
    s16 field_02;
    s16 field_04;
    s16 field_06;
    u8 unknown_08[28];
    s16 field_24;
    u8 unknown_26[8];
    s16 field_2e;
    u8 unknown_30[20];
    s16 field_44;
    u8 unknown_46[10];
    s16 field_50;
    s16 field_52;
    u8 unknown_54[4];
    f32 field_58;
    u8 unknown_5c[8];
    s16 field_64;
    s16 field_66;
    u8 unknown_68[8];
    s16 field_70;
    s16 field_72;
    s16 field_74;
    s16 field_76;
    u8 unknown_78[12];
} Racer;
_Static_assert(sizeof(Racer) == 0x84, "Racer stride");
typedef struct { u16 buttons; s8 stick_x, stick_y; u8 error, padding; } Pad;
#define steering_slew_limits ((s16*)0x80089170u)
#define expert_steering_curve ((s16*)0x80089174u)
#define novice_steering_curve ((s16*)0x800891f0u)
#define D_80089338 ((s16*)0x80089338u)
#define D_8008933C ((s16*)0x8008933cu)
#define D_80089340 ((s16*)0x80089340u)
#define D_8008934C ((s16*)0x8008934cu)
#define D_8008940C ((s16*)0x8008940cu)
#define update_vehicle_index (*(s16*)0x80098398u)
#define steering_work (*(s16*)0x80098694u)
#define D_800986A0 (*(u16*)0x800986a0u)
#define D_800986A8 (*(s16*)0x800986a8u)
#define D_800986AE ((s16*)0x800986aeu)
#define D_800986BE ((s16*)0x800986beu)
#define D_800986D8 (*(s16*)0x800986d8u)
#define update_scratch_index (*(s16*)0x800986e8u)
#define D_8009870A (*(s16*)0x8009870au)
#define D_80098730 (*(u16*)0x80098730u)
#define D_80098830 (*(s16*)0x80098830u)
#define D_80098C7A ((s16*)0x80098c7au)
#define D_80098C7E ((s16*)0x80098c7eu)
#define D_80098CD8 ((s16*)0x80098cd8u)
#define D_80098EFC ((s16*)0x80098efcu)
#define D_80099194 (*(s16*)0x80099194u)
#define D_800A2BEC (*(s16*)0x800a2becu)
#define D_800A2D10 ((s16*)0x80900000u)
#define D_800A2D28 ((u16*)0x80900100u)
#define D_800A2D40 ((s16*)0x80900200u)
#define controller_pads ((Pad*)0x800a39e0u)
#define D_800A39F8 ((s16*)0x800a39f8u)
#define D_800A3A00 ((u16*)0x800a3a00u)
#define control_schemes ((u16*)0x800a3a0eu)
#define D_800A3A16 ((u16*)0x800a3a16u)
#define D_800A3A1E ((s16*)0x800a3a1eu)
#define D_800A3A20 ((s16*)0x800a3a20u)
#define racers_update ((Racer*)0x80902000u)
#define D_800B6758 ((s16*)0x800b6758u)
#define D_800B8910 ((s16*)0x80918000u)
#define D_800B8912 ((s16*)0x80918002u)
#define D_800BF1C4 (*(s32*)0x800bf1c4u)
#define D_800CE6A4 (*(s16*)0x800ce6a4u)
#define update_channel (*(s16*)0x800ce6aau)
#define D_800CE6B0 (*(s16*)0x800ce6b0u)
#define D_800CE6B2 (*(s16*)0x800ce6b2u)
#define D_800CE6B4 (*(s16*)0x800ce6b4u)
#define D_800CE6BA (*(s16*)0x800ce6bau)
#define D_800CE76E (*(s16*)0x800ce76eu)
#define D_800CE794 (*(s16*)0x800ce794u)
#define update_difficulty (*(s16*)0x800ce79cu)
#define D_800CE7A8 (*(s16*)0x800ce7a8u)
#define D_800CE7B0 (*(s16*)0x800ce7b0u)
#define D_800CE808 (*(s16*)0x800ce808u)
#define D_800CE818 ((s16*)0x80901100u)
#define D_8011269F ((u8*)0x8011269fu)
#define D_801126A1 ((u8*)0x801126a1u)
#define D_801126A3 ((u8*)0x801126a3u)
#define D_801126A5 ((u8*)0x801126a5u)
#define D_8013D2D8 ((s16*)0x8013d2d8u)
#define D_8013E0F8 ((s16*)0x8013e0f8u)
#define D_801E9EB8 ((s16*)0x801e9eb8u)
#define D_8020FBCB ((u8*)0x8020fbcbu)
#define D_8020FBDB ((u8*)0x8020fbdbu)
#define D_DBL_8008D9D0 (*(f64*)0x8008d9d0u)
#define D_DBL_8008D9D8 (*(f64*)0x8008d9d8u)
#define D_DBL_8008D9E0 (*(f64*)0x8008d9e0u)
#define D_DBL_8008D9E8 (*(f64*)0x8008d9e8u)
#define D_DBL_8008D9F0 (*(f64*)0x8008d9f0u)
#define D_DBL_8008D9F8 (*(f64*)0x8008d9f8u)
#define D_DBL_8008DA00 (*(f64*)0x8008da00u)
#define D_DBL_8008DA08 (*(f64*)0x8008da08u)
#define D_DBL_8008DA10 (*(f64*)0x8008da10u)
#define D_DBL_8008DA18 (*(f64*)0x8008da18u)
#define D_DBL_8008DA20 (*(f64*)0x8008da20u)
#define D_FLT_8008926C ((f32*)0x8008926cu)
#define D_FLT_80089344 ((f32*)0x80089344u)
#define D_FLT_8009884C (*(f32*)0x8009884cu)
#define D_FLT_800988D8 (*(f32*)0x800988d8u)
#define D_FLT_80098EF0 (*(f32*)0x80098ef0u)
#define D_FLT_80099190 (*(f32*)0x80099190u)
#define D_FLT_800A2BB0 (*(f32*)0x800a2bb0u)
#define D_FLT_800A2BB4 (*(f32*)0x800a2bb4u)
#define D_FLT_800A2BB8 (*(f32*)0x800a2bb8u)
#define D_FLT_800A2BF0 (*(f32*)0x800a2bf0u)
#define D_FLT_800A2DD0 ((f32*)0x80900600u)
#define D_FLT_800A2DF8 ((f32*)0x80900700u)
#define D_FLT_800A2E20 ((f32*)0x80900800u)
#define vehicles_update ((Vehicle*)0x80910000u)
#define D_FLT_800CE788 (*(f32*)0x800ce788u)
#define D_FLT_800CE790 (*(f32*)0x800ce790u)
#define D_FLT_800CE7A0 (*(f32*)0x800ce7a0u)
#define D_FLT_800CE7C0 (*(f32*)0x800ce7c0u)
#define D_FLT_800CE7C8 (*(f32*)0x800ce7c8u)
#define D_FLT_8013E290 ((f32*)0x8013e290u)
#define D_FLT_8013E2D0 ((f32*)0x8013e2d0u)
#define D_FLT_801E9ECC ((f32*)0x801e9eccu)
#define D_FLT_801E9ED0 ((f32*)0x801e9ed0u)
#define D_FLT_801E9ED4 ((f32*)0x801e9ed4u)
#define D_FLT_801E9ED8 ((f32*)0x801e9ed8u)
#define D_FLT_80229C9C ((f32*)0x80229c9cu)
#define selected_pad (*(Pad**)0x800a39dcu)
#define D_800B9814 (*(s16**)0x800b9814u)
void func_80014D50(s16, s16);
void func_80025834(s16);     
void func_8002622C(void);        
void func_80028250(void);        
void func_80029DCC(void);        
void func_8002A070(void);        
void func_8002D37C(void);        
void func_80037BC0(s16);     
f32 func_80075720(f32);         
f32 func_80075730(f32);         
f32 func_800758F0(f32);         
u32 lambo_vehicle_input(u32 port, u32 kind);
void lambo_vehicle_observe(u32 kind, s32 authored, u32 snapshot);
#endif

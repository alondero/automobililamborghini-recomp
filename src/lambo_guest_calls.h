#ifndef LAMBO_GUEST_CALLS_H
#define LAMBO_GUEST_CALLS_H
#include "recomp.h"
#ifdef __cplusplus
extern "C" {
#endif
// Host ABI declarations for calls emitted by the guest patch recompiler.
void lambo_get_automatic_pit_stops(uint8_t*, recomp_context*);
void lambo_get_extra_vehicles(uint8_t*, recomp_context*);
void func_8003F40C(uint8_t*, recomp_context*);
void func_8003F56C(uint8_t*, recomp_context*);
void lambo_get_menu_stick_sensitivity(uint8_t*, recomp_context*);
void func_800427D4(uint8_t*, recomp_context*);
void func_8003CD84(uint8_t*, recomp_context*);
void func_80019D20(uint8_t*, recomp_context*);
void lambo_vehicle_input(uint8_t*, recomp_context*);
void lambo_vehicle_observe(uint8_t*, recomp_context*);
void func_80014D50(uint8_t*, recomp_context*);
void func_80025834(uint8_t*, recomp_context*);
void func_8002622C(uint8_t*, recomp_context*);
void func_80028250(uint8_t*, recomp_context*);
void func_80029DCC(uint8_t*, recomp_context*);
void func_8002A070(uint8_t*, recomp_context*);
void func_8002D37C(uint8_t*, recomp_context*);
void func_80037BC0(uint8_t*, recomp_context*);
void func_80075720(uint8_t*, recomp_context*);
void func_800758F0(uint8_t*, recomp_context*);
void func_8006B470(uint8_t*, recomp_context*);
void func_80066F84(uint8_t*, recomp_context*);
void func_800670BC(uint8_t*, recomp_context*);
void func_800673A0(uint8_t*, recomp_context*);
void func_80067428(uint8_t*, recomp_context*);
void func_8006B320(uint8_t*, recomp_context*);
void func_80075730(uint8_t*, recomp_context*);
#ifdef __cplusplus
}
#endif
#endif

#ifndef LAMBO_GUEST_CALLS_H
#define LAMBO_GUEST_CALLS_H
#include "recomp.h"
#ifdef __cplusplus
extern "C" {
#endif
// Host ABI declarations for calls emitted by the guest patch recompiler.
void lambo_get_automatic_pit_stops(uint8_t*, recomp_context*);
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

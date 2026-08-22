#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "recomp.h"

void func_8007F780(uint8_t* rdram, recomp_context* ctx);
void func_8006A7A0(uint8_t* rdram, recomp_context* ctx);
void func_8006A82C(uint8_t* rdram, recomp_context* ctx);
void func_8006A8B4(uint8_t* rdram, recomp_context* ctx);
void func_8006A910(uint8_t* rdram, recomp_context* ctx);

static int rumble_state;
static int motor_init_calls;

void osContGetReadData(void* pads) {
    (void)pads;
}

int osContSetCh(uint8_t* rdram, unsigned char ch) {
    (void)rdram;
    (void)ch;
    return 0;
}

void osContStartReadData_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}

void lambo_pak_set_rumble(int on) {
    rumble_state = on;
}

void osSetEventMesg_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
}

void func_8007AF60(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;
    motor_init_calls++;
}

int main(void) {
    uint8_t* rdram = calloc(1, 0x800000);
    recomp_context ctx = { 0 };
    gpr pak_status = (gpr)(int32_t)0x8011C6D0u;
    gpr rumble_present = (gpr)(int32_t)0x80110F08u;
    gpr rumble_active = (gpr)(int32_t)0x80110F18u;
    gpr rumble_request = (gpr)(int32_t)0x80110F28u;

    if (rdram == NULL) {
        fprintf(stderr, "failed to allocate RDRAM\n");
        return 1;
    }

    ctx.r5 = pak_status;
    MEM_B(0, pak_status) = (signed char)0xFE;
    MEM_W(0, rumble_present) = 1;

    func_8007F780(rdram, &ctx);

    if (MEM_W(0, rumble_present) != 0) {
        fprintf(stderr, "Controller Pak poll left the Rumble Pak present flag set\n");
        free(rdram);
        return 1;
    }

    ctx.r4 = 0;
    ctx.r5 = 0x32;
    func_8006A8B4(rdram, &ctx);
    if (MEM_W(0, rumble_request) != 0x32) {
        fprintf(stderr, "native rumble request was lost while the Controller Pak was present\n");
        free(rdram);
        return 1;
    }

    ctx.r4 = 0;
    func_8006A7A0(rdram, &ctx);
    if (rumble_state != 1 || MEM_W(0, rumble_active) != 1) {
        fprintf(stderr, "native rumble start did not preserve the game's motor state\n");
        free(rdram);
        return 1;
    }

    func_8006A82C(rdram, &ctx);
    if (rumble_state != 0 || MEM_W(0, rumble_active) != 0) {
        fprintf(stderr, "native rumble stop did not preserve the game's motor state\n");
        free(rdram);
        return 1;
    }

    MEM_W(0, rumble_request) = 0x50;
    func_8006A910(rdram, &ctx);
    if (rumble_state != 1 || MEM_W(0, rumble_active) != 1) {
        fprintf(stderr, "rumble engine ignored a hard-on request while the Controller Pak was present\n");
        free(rdram);
        return 1;
    }

    MEM_W(0, rumble_request) = 3;
    func_8006A910(rdram, &ctx);
    if (rumble_state != 0 || MEM_W(0, rumble_active) != 0 ||
        MEM_W(0, rumble_request) != 0 || motor_init_calls != 1) {
        fprintf(stderr, "rumble engine did not run the stop path while the Controller Pak was present\n");
        free(rdram);
        return 1;
    }

    free(rdram);
    return 0;
}

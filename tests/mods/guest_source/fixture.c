/* Original developer fixture. Preserve game RAM around calls through the
 * original USA symbols, exercising runtime regeneration of compiled patches. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
__attribute__((noinline, weak, used, section(".recomp_import.*")))
int lambo_log_v1(const char* message) { (void)message; return 0; }
extern void func_8003F40C(void), func_8003F56C(void), func_800427D4(void);
extern void func_80019D20(s16 first_vehicle);
static unsigned entries[4], returns[4];
#define HOOK(symbol, index) \
    __attribute__((retain, section(".recomp_hook." #symbol))) \
    void enter_##symbol(void) { ++entries[index]; } \
    __attribute__((retain, section(".recomp_hook_return." #symbol))) \
    void return_##symbol(void) { ++returns[index]; }
HOOK(func_8003F40C, 0)
HOOK(func_8003F56C, 1)
HOOK(func_800427D4, 2)
__attribute__((retain, section(".recomp_hook.func_80019D20")))
void enter_vehicle_update(void) { ++entries[3]; }
__attribute__((retain, section(".recomp_hook_return.func_80019D20")))
void return_vehicle_update(void) {
    ++returns[3];
    if (returns[3] == 2 && entries[3] == 2)
        lambo_log_v1("[guest-source-smoke] active race updater entry/return passed");
}

static void copy(u8* dest, const u8* source, unsigned size) {
    while (size--) *dest++ = *source++;
}
#define H(a) (*(volatile s16*)(a))
__attribute__((retain, section(".recomp_callback.*:recomp_on_init")))
void verify_guest_patches(void) {
    const u32 addresses[] = {0x80098560,0x800a39d8,0x800ce6a6,0x800ce7e8,
                            0x800985c0,0x8013d490,0x8009873e,0x80098760,
                            0x80098398,0x800b9814,0x80098830};
    const unsigned sizes[] = {96,56,2,8,16,576,2,2,2,4,2};
    u8 saved[766];
    const unsigned regions=sizeof(addresses)/sizeof(addresses[0]);
    _Static_assert(sizeof(addresses)/sizeof(addresses[0]) == sizeof(sizes)/sizeof(sizes[0]),
                   "each saved region needs its byte count");
    unsigned offset=0;
    for (unsigned i=0;i<regions;++i) {
        copy(saved+offset,(const u8*)addresses[i],sizes[i]);
        offset+=sizes[i];
    }
    H(0x800ce6a6)=1;
    H(0x800ce7e8)=5;
    for (unsigned i=0;i<8;++i) H(0x800985c0+i*2)=1;
    for (unsigned i=0;i<24;++i) H(0x8013d490+i*24)=0;
    func_8003F56C();
    int right=H(0x800ce7e8)==6;
    func_8003F40C();
    int left=H(0x800ce7e8)==5;

    for (unsigned i=0;i<96;++i) ((volatile u8*)0x80098560)[i]=0;
    for (unsigned i=0;i<56;++i) ((volatile u8*)0x800a39d8)[i]=0;
    H(0x80098560)=2;
    H(0x80098562)=2;
    H(0x800985a0)=320;
    H(0x800985a2)=240;
    H(0x800985b0)=1;
    H(0x800985b2)=160;
    H(0x800985b4)=120;
    *(volatile u8*)0x800a39e2=17;
    func_800427D4();
    /* Runner publishes sensitivity 1.5: round(17*1.5)-7 == 19. */
    int menu=H(0x8009873e)==19 && H(0x800985b2)==162;
    // The empty-list path touches only the index and table pointer. Active
    // human/AI processing is exercised later by the runner's warped race.
    func_80019D20(-1);
    int vehicle=H(0x80098398)==-1 && *(volatile u32*)0x800b9814==0x8013d2d8;
    offset=0;
    for (unsigned i=0;i<regions;++i) {
        copy((u8*)addresses[i],saved+offset,sizes[i]);
        offset+=sizes[i];
    }
    int hooks=1;
    for (unsigned i=0;i<4;++i) hooks &= entries[i]==1 && returns[i]==1;
    lambo_log_v1(right && left && menu && vehicle && hooks ?
        "[guest-source-smoke] original calls, entry/return hooks and setting helper passed" :
        "[guest-source-smoke] FAILED");
}

/* Read-only observations of real menu dispatch: no synthetic guest calls.
 * USA bindings are documented in docs/guest-vehicle-select.md and
 * docs/menu-input.md. Compiled big-endian MIPS loads use guest addresses;
 * these are not host-side reads of the runtime's word-swapped RDRAM. */
typedef signed short s16;
#define H(a) (*(volatile s16*)(a))
__attribute__((noinline, weak, used, section(".recomp_import.*")))
int lambo_log_v1(const char* message) { (void)message; return 0; }
static int before[2];
static int model(void) { return H(0x800ce7e8 + 2 * (H(0x800ce6a6) - 1)); }
static void report(int direction) {
    int after = model();
    char message[] = "[menu-walk] left  model 00 -> 00 on car screen";
    if (direction) {
        message[12]='r'; message[13]='i'; message[14]='g'; message[15]='h'; message[16]='t';
    }
    message[24]='0'+before[direction]/10; message[25]='0'+before[direction]%10;
    message[30]='0'+after/10; message[31]='0'+after%10;
    if (H(0x80098562)==13 && H(0x800ce6ac)==6) lambo_log_v1(message);
}
__attribute__((retain, section(".recomp_hook.func_8003F40C")))
void left_entry(void) { before[0]=model(); }
__attribute__((retain, section(".recomp_hook_return.func_8003F40C")))
void left_return(void) { report(0); }
__attribute__((retain, section(".recomp_hook.func_8003F56C")))
void right_entry(void) { before[1]=model(); }
__attribute__((retain, section(".recomp_hook_return.func_8003F56C")))
void right_return(void) { report(1); }
__attribute__((retain, section(".recomp_hook_return.func_800427D4")))
void menu_return(void) {
    static int observed;
    if (!observed && H(0x80098562)==13 && H(0x800ce6ac)==6) {
        observed=1;
        lambo_log_v1("[menu-walk] menu handler returned on car screen");
    }
}

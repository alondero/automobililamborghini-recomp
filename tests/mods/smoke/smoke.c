// Original test code; no game assets or ROM bytes. The idle-thread entry runs
// once, after boot has established a guest stack. Hooking the ROM entrypoint
// instead would run C before $sp is initialized.
__attribute__((noinline, weak, used, section(".recomp_import.*")))
int lambo_log_v1(const char* message) { return 0; }

__attribute__((retain, section(".recomp_hook.BootIdleThread")))
void mod_smoke(void) {
    lambo_log_v1("[mod-smoke] boot hook executed");
}

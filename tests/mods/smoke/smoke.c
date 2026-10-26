// Original test code; no game assets or ROM bytes. The idle-thread entry runs
// once, after boot has established a guest stack. Hooking the ROM entrypoint
// instead would run C before $sp is initialized.
__attribute__((noinline, weak, used, section(".recomp_import.*")))
int lambo_log_v1(const char* message) { return 0; }

__attribute__((noinline, weak, used, section(".recomp_import.*")))
unsigned recomp_get_config_u32(const char* key) { return 0; }

__attribute__((noinline, weak, used, section(".recomp_import.*")))
unsigned recompui_context_root(unsigned context) { return 0; }

__attribute__((retain, section(".recomp_hook.BootIdleThread")))
void mod_smoke(void) {
    lambo_log_v1("[mod-smoke] boot hook executed");
    // The pinned frontend returns this symbolic root ID without dereferencing
    // a context. This checks a real UI import without creating a headless UI.
    if (recompui_context_root(0) == 0xFFFFFFFEu) {
        lambo_log_v1("[mod-smoke] UI import executed");
    }
    switch (recomp_get_config_u32("mode")) {
        case 0: lambo_log_v1("[mod-smoke] config default read"); break;
        case 1: lambo_log_v1("[mod-smoke] config override read"); break;
    }
}

// Original test code; no game assets or ROM bytes. Init callbacks run after
// the runtime establishes the mod heap and a guest thread stack.
#include "../../../include/lambo/mod_imports.c"
__attribute__((noinline, weak, used, section(".recomp_import.*")))
int lambo_log_v1(const char* message) { return 0; }

__attribute__((noinline, weak, used, section(".recomp_import.*")))
unsigned recomp_get_config_u32(const char* key) { return 0; }

__attribute__((noinline, weak, used, section(".recomp_import.*")))
unsigned recompui_context_root(unsigned context) { return 0; }

__attribute__((noinline, weak, used, section(".recomp_import..")))
unsigned lambo_native_smoke(unsigned value) { return 0; }

static unsigned extension;
static unsigned race_started;
static unsigned frame_checked;

__attribute__((retain, section(".recomp_hook.BootIdleThread")))
void smoke_boot_hook(void) {
    lambo_log_v1("[mod-smoke] boot hook executed");
}

__attribute__((retain, section(".recomp_callback.*:recomp_on_load")))
void smoke_pak_load(void) {
    lambo_log_v1("[mod-smoke] Pak load event executed");
}

#ifdef LAMBO_SMOKE_PROTECTED_HOOK
__attribute__((retain, section(".recomp_hook.BootLoadInitialAssets")))
#else
__attribute__((retain, section(".recomp_callback.*:recomp_on_init")))
#endif
void mod_smoke(void) {
    lambo_log_v1("[mod-smoke] init callback executed");
    // The pinned frontend returns this symbolic root ID without dereferencing
    // a context. This checks a real UI import without creating a headless UI.
    if (recompui_context_root(0) == 0xFFFFFFFEu) {
        lambo_log_v1("[mod-smoke] UI import executed");
    }
    switch (recomp_get_config_u32("mode")) {
        case 0: lambo_log_v1("[mod-smoke] config default read"); break;
        case 1: lambo_log_v1("[mod-smoke] config override read"); break;
    }
    extension = lambo_register_racer_extension_v1(8);
    unsigned value = 99;
    unsigned map = lambo_store_create_v1(LAMBO_MAP);
    unsigned set = lambo_store_create_v1(LAMBO_SET);
    unsigned slots = lambo_store_create_v1(LAMBO_SLOTS);
    unsigned slot = lambo_store_insert_v1(slots, 123);
    if (lambo_mod_api_version() == 1 && extension && map && set && slots && slot &&
        lambo_native_smoke(0x1234) == (0x1234 ^ 0x4C414D42u) &&
        lambo_game_state == lambo_get_game_value_v1(LAMBO_GAME_STATE) &&
        lambo_store_put_v1(map, 7, 0) == 1 &&
        lambo_store_get_v1(map, 7, &value) == 1 && value == 0 &&
        lambo_store_put_v1(set, 7, 99) == 1 &&
        lambo_store_get_v1(set, 7, &value) == 1 && value == 1 &&
        lambo_store_get_v1(slots, slot, &value) == 1 && value == 123 &&
        lambo_store_erase_v1(slots, slot) == 1 &&
        lambo_store_get_v1(slots, slot, &value) == 0 &&
        lambo_store_destroy_v1(map) == 1 &&
        lambo_store_destroy_v1(set) == 1 &&
        lambo_store_destroy_v1(slots) == 1) {
        lambo_log_v1("[mod-smoke] full API initialized");
    }
}

__attribute__((retain, section(".recomp_callback.*:lambo_on_race_start_v1")))
void smoke_race_start(void) {
    unsigned racer = lambo_get_racer_v1(1);
    unsigned* data = lambo_get_racer_extension_v1(racer, extension);
    if (racer && lambo_get_racer_body_v1(racer) && data && data[0] == 0 && data[1] == 0) {
        data[0] = 0x12345678;
        race_started = racer;
        lambo_log_v1("[mod-smoke] race extension initialized");
    }
}

__attribute__((retain, section(".recomp_callback.*:lambo_on_frame_v1")))
void smoke_frame(unsigned state) {
    if (state == 8 && race_started && !frame_checked) {
        unsigned* data = lambo_get_racer_extension_v1(race_started, extension);
        if (data && data[0] == 0x12345678) {
            frame_checked = 1;
            lambo_log_v1("[mod-smoke] race frame executed");
        }
    }
}

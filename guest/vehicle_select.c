/* Complete USA left/right selection handlers. See docs/guest-vehicle-select.md.
 * Game-thread menu state; the host supplies only the live cheat setting. */
typedef signed short s16;
typedef unsigned char u8;

typedef struct {
    s16 category;
    u8 unknown_02[22];
} VehicleDescriptor;
_Static_assert(sizeof(VehicleDescriptor) == 24, "USA model descriptor stride");

extern s16 selection_player;
extern s16 selection_models[4];
extern s16 selection_availability[8];
extern VehicleDescriptor selection_descriptors[24];
extern int lambo_get_extra_vehicles(void);

/* Each original check loads availability before sampling the cheat. Keep the
 * query inside the loop so live toggles retain the old per-check semantics.
 * Stock callers supply player 1..4 and model 0..23, with an available category.
 * An all-locked table with the cheat off retains the ROM's nonterminating loop. */
static int selectable(s16 model) {
    s16 available = selection_availability[selection_descriptors[model].category];
    return lambo_get_extra_vehicles() || available != 0;
}

__attribute__((section(".recomp_patch")))
void func_8003F40C(void) {
    s16* cursor = &selection_models[selection_player - 1];
    do {
        *cursor = (s16)(*cursor - 1);
        if (*cursor < 0) *cursor = 23;
    } while (!selectable(*cursor));
}

__attribute__((section(".recomp_patch")))
void func_8003F56C(void) {
    s16* cursor = &selection_models[selection_player - 1];
    do {
        *cursor = (s16)(*cursor + 1);
        if (*cursor >= 24) *cursor = 0;
    } while (!selectable(*cursor));
}

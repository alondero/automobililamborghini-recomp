#ifndef LAMBO_GUEST_MENU_INPUT_H
#define LAMBO_GUEST_MENU_INPUT_H

/* USA menu state, compiled for big-endian MIPS. Unknown fields stay unnamed.
 * The game thread owns these fields during the pre-race menu dispatcher. */
typedef signed char MenuS8;
typedef unsigned char MenuU8;
typedef signed short MenuS16;
typedef unsigned short MenuU16;
typedef signed int MenuS32;

typedef struct {
    MenuU16 buttons;
    MenuS8 x, y;
    MenuU8 unknown_04[2];
} MenuPad;

typedef struct {
    MenuS16 unknown_00;
    MenuS16 availability;
    void (*activate)(void);
    MenuU8 unknown_08[8];
    MenuS16 neighbor[4]; /* up, down, left, right; -1 means no neighbor */
    MenuU8 unknown_18[8];
} MenuItem;

typedef struct {
    MenuS16 screen, next_screen, history_depth;
    MenuU8 unknown_06[2];
    MenuS16 history[16];
    MenuItem *items;
    MenuS16 item, next_item, transition, highlight;
    MenuS16 unknown_34, screen_delta, highlight_delta, unknown_3a;
    MenuS16 left, top, right, bottom;
    MenuU8 unknown_44[10];
    MenuS16 port, typing, cursor_x, cursor_y, character, target_x, target_y, name_length;
} MenuState;

_Static_assert(sizeof(MenuPad) == 6, "menu pad stride");
_Static_assert(sizeof(MenuItem) == 32, "menu item stride");
_Static_assert(__builtin_offsetof(MenuItem, neighbor) == 0x10, "menu item links");
_Static_assert(__builtin_offsetof(MenuState, items) == 0x28, "menu item pointer");
_Static_assert(__builtin_offsetof(MenuState, port) == 0x4e, "menu input port");
_Static_assert(__builtin_offsetof(MenuState, name_length) == 0x5c, "name cursor");

extern volatile MenuState menu_state;
extern volatile MenuPad menu_pads[4];
extern volatile MenuU16 menu_pressed[4], menu_consumed[4];
extern volatile MenuS16 menu_repeat[4], menu_back_enabled[];
extern volatile MenuS16 menu_selected_player, menu_default_port;
extern volatile MenuS16 menu_velocity_x, menu_velocity_y;
extern volatile MenuS16 menu_transition_busy, menu_game_state;
extern volatile MenuU8 menu_names[];
extern double lambo_get_menu_stick_sensitivity(void);
extern void func_80066F84(MenuS32, float, MenuS32, MenuS32, MenuS32);
extern void func_8003CD84(void);
void func_800427D4(void);
#endif

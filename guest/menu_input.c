#include "menu_input.h"

/* Recovered from ROM 0x427D4..0x4384C, including the old split at 0x42C4C.
 * The host supplies only sensitivity. Each original input read samples it
 * separately, preserving live setting changes without altering shared pads. */
static MenuS32 scaled_stick(MenuS32 value) {
    double scale = lambo_get_menu_stick_sensitivity();
    if (!(scale > 1.0)) return value;
    double scaled = value * scale;
    MenuS32 rounded = (MenuS32)(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
    if (rounded > 127) rounded = 127;
    if (rounded < -127) rounded = -127;
    return rounded;
}

static MenuS16 cursor_velocity(MenuS32 raw, MenuS32 limit) {
    if (raw > 0) {
        raw -= 7;
        if (raw < 0) raw = 0;
        else if (raw > limit) raw = limit;
    } else {
        raw += 7;
        if (raw > 0) raw = 0;
        else if (raw < -limit) raw = -limit;
    }
    return (MenuS16)raw;
}

static void consume(MenuU16 mask) {
    menu_consumed[menu_state.port] |= mask;
    menu_pressed[menu_state.port] &= (MenuU16)~mask;
}

static void sound(MenuS32 id, float volume, MenuS32 pitch) {
    func_80066F84(id, volume, pitch, 0x40, 0);
}

static void move_item(MenuS32 direction) {
    MenuS16 next = menu_state.items[menu_state.item].neighbor[direction];
    if (next == -1) return;
    /* Up plays its sound before changing state; the other directions after.
     * The sound callback may inspect menu state, so keep that ordering. */
    if (direction == 0) sound(0x34, 0.5f, 0x5dc0);
    menu_state.next_item = menu_state.items[menu_state.item].neighbor[direction];
    if (direction == 0 && menu_state.next_item == 100) {
        menu_state.typing = 1;
        menu_state.cursor_y = menu_state.bottom - 0x38;
        menu_repeat[menu_state.port] = 0;
        return;
    }
    while (menu_state.items[menu_state.next_item].availability < 0)
        menu_state.next_item = menu_state.items[menu_state.next_item].neighbor[direction];
    menu_state.highlight = 255;
    menu_state.highlight_delta = -8;
    if (direction != 0) sound(0x34, 0.5f, 0x5dc0);
}

static void update_typing(void) {
    menu_velocity_x = cursor_velocity(scaled_stick(menu_pads[menu_state.port].x), 48);
    if (menu_velocity_x == 0 && (menu_pads[menu_state.port].buttons & 0x300))
        menu_velocity_x = (menu_pads[menu_state.port].buttons & 0x200) ? -48 : 48;
    menu_state.cursor_x += menu_velocity_x >> 3;
    if (menu_state.cursor_x > menu_state.right - 22)
        menu_state.cursor_x = menu_state.right - 22;
    if (menu_state.cursor_x < menu_state.left + 22)
        menu_state.cursor_x = menu_state.left + 22;

    menu_velocity_y = cursor_velocity(scaled_stick(menu_pads[menu_state.port].y), 32);
    if (menu_velocity_y == 0 && (menu_pads[menu_state.port].buttons & 0xc00))
        menu_velocity_y = (menu_pads[menu_state.port].buttons & 0x400) ? -32 : 32;
    menu_state.cursor_y -= menu_velocity_y >> 3;
    if (menu_state.cursor_y < menu_state.top + 76)
        menu_state.cursor_y = menu_state.top + 76;
    if (menu_state.cursor_y > menu_state.bottom - 52) {
        menu_state.typing = 0;
        menu_repeat[menu_state.port] = 1;
        if (scaled_stick(menu_pads[menu_state.port].x) >= 51) consume(0x100);
        if (scaled_stick(menu_pads[menu_state.port].x) < -50) consume(0x200);
        if (menu_state.cursor_x < 110) menu_state.next_item = 2;
        else if (menu_state.cursor_x >= 211) menu_state.next_item = 0;
        else menu_state.next_item = 1;
    }
    if (menu_velocity_x == 0 && menu_velocity_y == 0 && menu_state.character > 0) {
        if (menu_state.cursor_x > menu_state.target_x) --menu_state.cursor_x;
        if (menu_state.cursor_x < menu_state.target_x) ++menu_state.cursor_x;
        if (menu_state.cursor_y > menu_state.target_y) --menu_state.cursor_y;
        if (menu_state.cursor_y < menu_state.target_y) ++menu_state.cursor_y;
    }
    if (menu_state.name_length == 12) {
        menu_state.typing = 0;
        menu_state.next_item = 2;
        menu_repeat[menu_state.port] = 1;
    }
    if (menu_pressed[menu_state.port] & 0x8000) {
        consume(0x8000);
        sound(0x38, 0.5f, 0x3e80);
        menu_names[menu_selected_player * 13 + menu_state.name_length] = (MenuU8)menu_state.character;
        ++menu_state.name_length;
        menu_names[menu_selected_player * 13 + menu_state.name_length] = 0;
    }
}

__attribute__((section(".recomp_patch")))
void func_800427D4(void) {
    if ((menu_state.screen >= 13 && menu_state.screen < 18) ||
        (menu_state.screen >= 21 && menu_state.screen < 25))
        menu_state.port = menu_selected_player - 1;
    else menu_state.port = menu_default_port;

    if (menu_state.typing != 0) {
        update_typing();
        return;
    }
    /* This scratch halfword is shared with other menu/pit code and is an
     * observable guest write, not a local button snapshot. */
    extern volatile MenuS16 menu_button_scratch;
    menu_button_scratch = menu_pressed[menu_state.port];
    if (menu_state.transition >= 201 && menu_state.screen == menu_state.next_screen) {
        if ((menu_button_scratch & 0x4000) && menu_state.screen == menu_state.next_screen &&
            menu_back_enabled[menu_state.screen] != 0) {
            /* Back consumes before deciding whether there is history. */
            consume(0x4000);
            if (menu_state.history_depth != 0) {
                menu_state.history_depth = (MenuS16)(menu_state.history_depth - 1);
                menu_state.next_screen = menu_state.history[menu_state.history_depth];
                menu_state.history[menu_state.history_depth] = -1;
                menu_state.screen_delta = -32;
                menu_state.highlight_delta = 0;
                menu_state.highlight = 0;
                sound(0x31, 0.4f, 0x6590);
                return;
            }
        }
        if (menu_button_scratch & 0x9000) {
            menu_button_scratch &= 0x9000;
            consume((MenuU16)menu_button_scratch);
            sound(0x38, 0.5f, 0x3e80);
            if (menu_state.screen == menu_state.next_screen)
                menu_state.items[menu_state.item].activate();
        } else if (menu_button_scratch & 0x800) {
            consume(0x800); move_item(0);
        } else if (menu_button_scratch & 0x400) {
            consume(0x400); move_item(1);
        } else if (menu_button_scratch & 0x200) {
            consume(0x200); move_item(2);
        } else if (menu_button_scratch & 0x100) {
            consume(0x100); move_item(3);
        }
    } else if (menu_state.screen != menu_state.next_screen && menu_state.transition == 0) {
        if (menu_state.next_screen >= 0) {
            sound(0x30, 0.5f, 0x4e20);
            func_8003CD84();
        } else if (menu_transition_busy == 0) menu_game_state = -menu_state.next_screen;
    }
}

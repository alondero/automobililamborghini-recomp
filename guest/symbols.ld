/* USA symbols consumed by the pit source patch. Racer/body tables use the
 * port's expanded-field layout; all other symbols retain their ROM layout. */
pit_stage_index = 0x8020FBC8;
pit_stage_order = 0x8020FBC0;
pit_entry_pending = 0x8020FBCC;
pit_turning = 0x8020FBD8;
pit_fuel = 0x8020FCA4;
pit_pressure = 0x8020FC9C;
pit_pressure_limit = 0x80229CA8;
pit_spill_ticks = 0x80229CB0;
pit_turns = 0x8020FBA0;
pit_previous_turns = 0x8020FBA8;
pit_direction_index = 0x8020FBB0;
pit_directions = 0x8020FB90;
pit_turn_start = 0x80229C78;
game_tick = 0x800A2D00;
pit_sound = 0x80229C80;
pit_tyres_fitted = 0x8020FB38;
pit_tyre_parts = 0x8020FC1C;
pit_animation_a = 0x801FBCF4;
pit_animation_b = 0x801FDE24;
pit_exit_animation = 0x80207158;
pit_model_a = 0x801FB5A4;
pit_model_b = 0x801FD794;
pit_pressed_buttons = 0x800A3A00;
pit_consumed_buttons = 0x800A39F8;
pit_control_scheme = 0x800A3A10;
current_channel = 0x800CE6AA;
race_difficulty = 0x800CE79C;
current_vehicle = 0x8009869C;
pit_scratch = 0x80098830;
current_category = 0x800986E8;
racer_bodies = 0x80901100;
vehicles = 0x80910000;
racers = 0x80902000;
pit_delivery_fraction = 0x800CE790;
pit_front_tyre_values = 0x8013E290;
pit_rear_tyre_values = 0x8013E2D0;

/* N64Recomp's reserved manual host-call range. */
lambo_get_automatic_pit_stops = 0x8F000000;
lambo_get_extra_vehicles = 0x8F000004;

/* USA vehicle-selection state; descriptors are loaded from ROM 0xAEE80. */
selection_player = 0x800CE6A6;
selection_models = 0x800CE7E8;
selection_availability = 0x800985C0;
selection_descriptors = 0x8013D490;

/* USA menu input state and setting query. */
lambo_get_menu_stick_sensitivity = 0x8F000008;
menu_state = 0x80098560;
menu_pads = 0x800A39E0;
menu_pressed = 0x800A3A00;
menu_consumed = 0x800A39F8;
menu_repeat = 0x800A3A08;
menu_back_enabled = 0x80089A94;
menu_selected_player = 0x800CE6A6;
menu_default_port = 0x800A39DA;
menu_velocity_x = 0x8009873E;
menu_velocity_y = 0x80098760;
menu_transition_busy = 0x800A2D08;
menu_game_state = 0x800CE6AC;
menu_names = 0x800A4819;
menu_button_scratch = 0x80098830;
lambo_vehicle_input = 0x8F00000C;
lambo_vehicle_observe = 0x8F000010;

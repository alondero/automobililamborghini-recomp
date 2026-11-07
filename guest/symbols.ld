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

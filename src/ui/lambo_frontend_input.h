#pragma once
#include "controls/lambo_controls.h"
#include <mutex>
namespace lambo::ui {
// Frontend contexts and profiles are mutable on the render thread. The SDL
// pump takes this lock before touching them; guest callbacks use snapshots.
std::recursive_mutex& frontend_mutex();
void create_frontend_pedal_settings();
void configure_frontend_input_defaults();
// Call after SDL controller initialization and profile loading, before polling.
void initialize_frontend_controllers();
void import_frontend_profiles();
void save_frontend_preferences();
lambo::controls::EvaluatedState sample_frontend_pedals();
}

#ifndef LAMBO_INPUT_GATE_H
#define LAMBO_INPUT_GATE_H

#include <cstdint>

namespace lambo::input_gate {

void set_ui_capture(bool capturing);
bool guest_input_suppressed();
void publish_physical_snapshot(uint32_t snapshot);
uint32_t guest_snapshot();
void clear_before_release();

} // namespace lambo::input_gate

#endif

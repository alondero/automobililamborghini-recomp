#pragma once

#include <cstdint>

namespace lambo::vi {
// Promote the game's private VI context at retrace and forward it to the runtime.
void promote_context(uint8_t* rdram);
}

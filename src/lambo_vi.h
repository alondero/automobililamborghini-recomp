#pragma once

#include <cstdint>

namespace lambo::vi {
// Runtime VI-callback hook. On the runtime callback thread, copy the game's
// two 0x30-byte private contexts at guest pointers 0x8008D1A0/0x8008D1A4,
// preserving 32-bit RDRAM word order, then forward mode, framebuffer, and
// blanking state to the host VI API. Null or out-of-range pointers are a
// no-op. This fixed-address bridge is transitional; see docs/architecture.md
// and docs/reference/current-state.md.
void promote_context(uint8_t* rdram);
}

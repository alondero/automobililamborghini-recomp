#pragma once
#include <cstdint>

namespace lambo::render {
// The game producer starts one sample epoch per native graphics task. Sky,
// rain and HUD helpers on that thread share one coherent renderer snapshot.
// Resizes/config changes become visible on the next task, never mid-task.
inline thread_local uint64_t aspect_epoch = 0;
inline void begin_aspect_frame() { ++aspect_epoch; }
}

#pragma once
#include <atomic>
#include <cstdint>

namespace lambo::settings {
// Change notification only, not a replacement for each value's owning lock.
// Publishers bump after updating config, cheats or the saved driver name.
inline std::atomic<uint64_t> generation{1};
inline uint64_t revision() { return generation.load(std::memory_order_acquire); }
inline void changed() { generation.fetch_add(1, std::memory_order_release); }
}

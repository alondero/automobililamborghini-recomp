#ifndef LAMBO_SETTINGS_REVISION_H
#define LAMBO_SETTINGS_REVISION_H

#include <atomic>
#include <cstdint>

namespace lambo::settings {
// Invalidation only: this does not provide a coherent config snapshot or
// replace its locks. Release/acquire orders value updates before invalidation.
// Every port settings writer (including file reloads and guest name saves)
// must call changed() after updating values consumed by the frontend.
inline std::atomic<uint64_t> generation{1};
inline uint64_t revision() { return generation.load(std::memory_order_acquire); }
inline void changed() { generation.fetch_add(1, std::memory_order_release); }
}

#endif

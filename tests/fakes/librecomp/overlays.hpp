// Test-only stand-in for recomp::overlays::register_base_export. The real
// signature at lib/N64ModernRuntime @ cdf5abbd is
// (const std::string&, recomp_func_t*); a template keeps this fake
// independent of recomp.h while still accepting the production call. This
// export is unrelated to the container bug under test.
#ifndef LAMBO_TEST_FAKE_LIBRECOMP_OVERLAYS_HPP
#define LAMBO_TEST_FAKE_LIBRECOMP_OVERLAYS_HPP

#include <string>

namespace recomp::overlays {

template <typename Func> void register_base_export(const std::string&, Func) {}

} // namespace recomp::overlays

#endif

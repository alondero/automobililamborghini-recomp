#ifndef LAMBO_RT64_H
#define LAMBO_RT64_H

// RT64 renderer for the pivot runtime — the DEFAULT presenter (#58, flipped 2026-07-02
// after RT64 present was confirmed on-screen and the VI mode converged to ares).
//
// RT64 is a hard BUILD dependency (recomp/lib/rt64 submodule, always compiled in) but
// not a hard RUNTIME dependency: LAMBO_HEADLESS=1 — a PERMANENT harness knob, same
// class as LAMBO_MODERN_MAX_VIS, not an A/B lever — skips the window and RT64 entirely
// and uses the headless swrender. swrender stays as the MEASUREMENT INSTRUMENT: it
// rasterises into the N64-resolution RDRAM framebuffer that the port-vs-ares harness
// byte-compares, which RT64's native-resolution swapchain output can never be. RT64
// setup failure (no Vulkan device, no display) also falls back to headless swrender.

#include <memory>

#include "ultramodern/renderer_context.hpp"

namespace lambo_rt64 {

// True unless the harness asked for headless (LAMBO_HEADLESS=1). When true, the port
// creates an SDL window and presents through RT64.
bool enabled();

// Instantiates the RT64-backed RendererContext. Returns nullptr when RT64 setup fails
// (no Vulkan device, no window, ...) so the caller can fall back to headless swrender.
std::unique_ptr<ultramodern::renderer::RendererContext>
create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle,
                      bool developer_mode);

} // namespace lambo_rt64

#endif // LAMBO_RT64_H

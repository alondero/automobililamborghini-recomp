#ifndef LAMBO_RT64_RENDER_INTERFACE_BUILDERS_H
#define LAMBO_RT64_RENDER_INTERFACE_BUILDERS_H

// RT64's pinned revision keeps the descriptor/pipeline builders in Plume's
// namespace. Newer RT64 revisions re-export them from RT64; keep this small
// compatibility bridge local to the RmlUi renderer adapter.
#include "plume_render_interface_builders.h"

namespace RT64 {
using plume::RenderDescriptorSetBuilder;
using plume::RenderPipelineLayoutBuilder;
}

#endif

// ROM-free check of RT64's texture table on drivers without descriptor indexing.
// Some Android system drivers (Vulkan 1.1 without VK_EXT_descriptor_indexing) cannot
// allocate RT64's variable-size table: vkAllocateDescriptorSets fails and the renderer
// crashes. Those devices need a fixed-size table whose every slot stays valid.
#include <cstdlib>
#include <iostream>
#include <set>
#include <vector>

#include "render/rt64_descriptor_sets.h"

using namespace plume;
using namespace RT64;

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct RecordingDescriptorSet : RenderDescriptorSet {
    struct Write {
        uint32_t index;
        const RenderTexture *texture;
    };

    std::vector<Write> writes;

    void setBuffer(uint32_t, const RenderBuffer *, uint64_t, const RenderBufferStructuredView *, const RenderBufferFormattedView *) override {}
    void setTexture(uint32_t index, const RenderTexture *texture, RenderTextureLayout, const RenderTextureView *) override {
        writes.push_back({ index, texture });
    }
    void setSampler(uint32_t, const RenderSampler *) override {}
    void setAccelerationStructure(uint32_t, const RenderAccelerationStructure *) override {}
};

RecordingDescriptorSet *record(FramebufferRendererDescriptorTextureSet &set) {
    auto recorder = std::make_unique<RecordingDescriptorSet>();
    RecordingDescriptorSet *raw = recorder.get();
    set.descriptorSet = std::move(recorder);
    return raw;
}

// Any non-null pointer works: the recorder never dereferences it.
const RenderTexture *placeholder() {
    return reinterpret_cast<const RenderTexture *>(uintptr_t(0x10));
}

} // namespace

int main() {
    RenderDeviceCapabilities withIndexing;
    withIndexing.descriptorIndexing = true;
    RenderDeviceCapabilities withoutIndexing;
    withoutIndexing.descriptorIndexing = false;
    require(FramebufferRendererDescriptorTextureSet::usesBoundlessRange(withIndexing),
        "drivers with descriptor indexing keep RT64's variable-size table");
    require(!FramebufferRendererDescriptorTextureSet::usesBoundlessRange(withoutIndexing),
        "drivers without descriptor indexing must not get a variable-size table");

    // Pipeline layouts are built from default sets without a device, so the layout
    // description itself must reflect the choice or the bound set will not match.
    FramebufferRendererDescriptorTextureSet boundless(true, nullptr, 100);
    require(boundless.builder.descriptorSetDesc.lastRangeIsBoundless,
        "variable-size table keeps its boundless range");
    require(boundless.textureCacheSize == 100, "variable-size table keeps its requested size");

    FramebufferRendererDescriptorTextureSet fixed(false, nullptr, 100);
    const RenderDescriptorSetDesc &fixedDesc = fixed.builder.descriptorSetDesc;
    require(!fixedDesc.lastRangeIsBoundless, "fixed table must not request a boundless range");
    require(fixedDesc.boundlessRangeSize == 0, "fixed table must not request a variable count");
    require(fixedDesc.descriptorRangesCount == 1, "fixed table has one texture range");
    require(fixedDesc.descriptorRanges[0].count == FramebufferRendererDescriptorTextureSet::UpperRange,
        "fixed table spans every texture slot the shaders declare");
    require(fixed.textureCacheSize == FramebufferRendererDescriptorTextureSet::UpperRange,
        "fixed table already holds every slot, so the cache never forces a rebuild");

    // Without partially bound descriptors every slot must hold a valid texture.
    RecordingDescriptorSet *fixedWrites = record(fixed);
    fixed.clearAllSlots(placeholder());
    std::set<uint32_t> written;
    for (const RecordingDescriptorSet::Write &write : fixedWrites->writes) {
        require(write.texture == placeholder(), "empty slots point at the placeholder");
        written.insert(write.index);
    }
    require(written.size() == FramebufferRendererDescriptorTextureSet::UpperRange,
        "every fixed slot is written before the table is bound");
    require(*written.rbegin() == FramebufferRendererDescriptorTextureSet::UpperRange - 1,
        "the last fixed slot is written");

    fixedWrites->writes.clear();
    fixed.clearSlot(42, placeholder());
    require(fixedWrites->writes.size() == 1 && fixedWrites->writes[0].index == 42 &&
        fixedWrites->writes[0].texture == placeholder(),
        "a released fixed slot returns to the placeholder");

    // Desktop and Turnip keep upstream behaviour: no extra descriptor writes.
    RecordingDescriptorSet *boundlessWrites = record(boundless);
    boundless.clearAllSlots(placeholder());
    boundless.clearSlot(42, placeholder());
    require(boundlessWrites->writes.empty(), "variable-size tables are left untouched");

    std::cout << "rt64 texture table: fixed and variable-size layouts verified\n";
    return 0;
}

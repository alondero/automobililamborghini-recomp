// Compiled with Plume's real descriptor constructor and pool builder appended.
// The Vulkan boundary models drivers that charge a variable binding's layout
// capacity to its pool, even when the allocation requests fewer descriptors.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

using VkDevice = int;
using VkDescriptorType = int;
using VkDescriptorPool = int;
using VkDescriptorSet = int;
using VkResult = int;
constexpr int VK_NULL_HANDLE = 0, VK_SUCCESS = 0;
constexpr int VK_ERROR_OUT_OF_POOL_MEMORY = -1000069000;
constexpr int VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO = 1;
constexpr int VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO = 2;
constexpr int VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO = 3;
constexpr int VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT = 1;
struct VkDescriptorPoolSize { int type; uint32_t descriptorCount; };
struct VkDescriptorPoolCreateInfo {
    int sType, flags;
    uint32_t maxSets;
    const VkDescriptorPoolSize *pPoolSizes;
    uint32_t poolSizeCount;
};
struct VkDescriptorSetAllocateInfo {
    int sType;
    VkDescriptorPool descriptorPool;
    const int *pSetLayouts;
    uint32_t descriptorSetCount;
    const void *pNext;
};
struct VkDescriptorSetVariableDescriptorCountAllocateInfo {
    int sType;
    const uint32_t *pDescriptorCounts;
    uint32_t descriptorSetCount;
};
struct RenderDescriptorRange { int type; uint32_t count; };
struct RenderDescriptorSetDesc {
    const RenderDescriptorRange *descriptorRanges;
    uint32_t descriptorRangesCount;
    bool lastRangeIsBoundless;
    uint32_t boundlessRangeSize;
};
struct VulkanDevice { VkDevice vk = 1; };
struct VulkanDescriptorSetLayout {
    int vk = 1;
    VulkanDescriptorSetLayout(VulkanDevice *, const RenderDescriptorSetDesc &) {}
};
struct VulkanDescriptorSet {
    VulkanDevice *device = nullptr;
    VulkanDescriptorSetLayout *setLayout = nullptr;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet vk = VK_NULL_HANDLE;
    VulkanDescriptorSet(VulkanDevice *, const RenderDescriptorSetDesc &);
    ~VulkanDescriptorSet() { delete setLayout; }
    static VkDescriptorPool createDescriptorPool(VulkanDevice *,
        const std::unordered_map<VkDescriptorType, uint32_t> &, bool);
};
int toVk(int type) { return type; }

static std::unordered_map<int, uint32_t> poolCounts, layoutCounts;
static std::vector<uint32_t> requestedCounts;
static int allocations = 0, pools = 0, destroyed = 0;
static bool chargeLayout = false, failAlways = false;

VkResult vkCreateDescriptorPool(VkDevice, const VkDescriptorPoolCreateInfo *info,
                               const void *, VkDescriptorPool *pool) {
    assert(info->maxSets == 1);
    poolCounts.clear();
    for (uint32_t i = 0; i < info->poolSizeCount; ++i)
        poolCounts[info->pPoolSizes[i].type] = info->pPoolSizes[i].descriptorCount;
    *pool = ++pools;
    return VK_SUCCESS;
}
void vkDestroyDescriptorPool(VkDevice, VkDescriptorPool pool, const void *) {
    assert(pool != VK_NULL_HANDLE);
    ++destroyed;
}
VkResult vkAllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo *info,
                                  VkDescriptorSet *set) {
    ++allocations;
    assert(info->descriptorPool == pools);
    if (info->pNext) {
        const auto *count = static_cast<const VkDescriptorSetVariableDescriptorCountAllocateInfo *>(info->pNext);
        assert(count->descriptorSetCount == 1);
        requestedCounts.push_back(*count->pDescriptorCounts);
    }
    if (failAlways) return VK_ERROR_OUT_OF_POOL_MEMORY;
    if (chargeLayout) {
        for (auto [type, count] : layoutCounts)
            if (poolCounts[type] < count) return VK_ERROR_OUT_OF_POOL_MEMORY;
    }
    *set = 1;
    return VK_SUCCESS;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string scenario = argv[1];
    chargeLayout = scenario != "normal";
    failAlways = scenario == "persistent-failure" || scenario == "full-capacity-failure";
    // Multiple ranges of the same type must accumulate, including on retry.
    RenderDescriptorRange ranges[] = {{1, 7}, {2, 3}, {1, 8192}};
    const bool variable = scenario != "fixed";
    const uint32_t requested = scenario == "nonzero-count" ? 100 :
        (scenario == "full-capacity-failure" ? 8192 : 0);
    RenderDescriptorSetDesc desc{ranges, 3, variable, requested};
    layoutCounts = {{1, 8199}, {2, 3}};
    VulkanDevice device;
    VulkanDescriptorSet set(&device, desc);
    assert(set.vk != VK_NULL_HANDLE);
    assert(poolCounts[2] == 3);
    if (scenario == "layout-capacity" || scenario == "nonzero-count") {
        assert(allocations == 2 && pools == 2 && destroyed == 1);
        assert(poolCounts[1] == 8199);
        const uint32_t actualCount = std::max(requested, 1U);
        assert((requestedCounts == std::vector<uint32_t>{actualCount, actualCount}));
    } else {
        assert(allocations == 1 && pools == 1 && destroyed == 0);
        if (scenario == "normal") {
            assert(poolCounts[1] == 8);
            assert((requestedCounts == std::vector<uint32_t>{1}));
        } else {
            assert(poolCounts[1] == 8199 && requestedCounts.empty());
        }
    }
}

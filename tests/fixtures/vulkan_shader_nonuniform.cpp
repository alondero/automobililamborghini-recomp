// Compiled with Plume's real shader-module construction appended. The Vulkan
// boundary saves the SPIR-V a driver would receive.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using VkDevice = int;
using VkShaderModule = int;
using VkResult = int;
constexpr int VK_NULL_HANDLE = 0, VK_SUCCESS = 0;
constexpr int VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO = 16;
struct VkShaderModuleCreateInfo {
    int sType;
    const void *pNext;
    uint32_t flags;
    size_t codeSize;
    const uint32_t *pCode;
};
enum class RenderShaderFormat { UNKNOWN, DXIL, SPIRV, METAL };
struct RenderDeviceCapabilities { bool descriptorIndexing = false; };
struct VulkanDevice {
    VkDevice vk = 1;
    RenderDeviceCapabilities capabilities;
};
struct VulkanShader {
    VkShaderModule vk = VK_NULL_HANDLE;
    std::string entryPointName;
    VulkanDevice *device = nullptr;
    RenderShaderFormat format = RenderShaderFormat::UNKNOWN;
    VulkanShader(VulkanDevice *, const void *, uint64_t, const char *, RenderShaderFormat);
};

static std::string received;
VkResult vkCreateShaderModule(VkDevice, const VkShaderModuleCreateInfo *info, const void *,
                              VkShaderModule *module) {
    received.assign(reinterpret_cast<const char *>(info->pCode), info->codeSize);
    *module = 1;
    return VK_SUCCESS;
}

// fixture <input.spv> <output.spv> indexing|no-indexing
int main(int argc, char **argv) {
    assert(argc == 4);
    std::ifstream input(argv[1], std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::vector<uint32_t> words((bytes.size() + 3) / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    VulkanDevice device;
    device.capabilities.descriptorIndexing = std::string(argv[3]) == "indexing";
    VulkanShader shader(&device, words.data(), bytes.size(), "PSMain", RenderShaderFormat::SPIRV);
    std::ofstream(argv[2], std::ios::binary) << received;
    return shader.vk == 1 ? 0 : 1;
}

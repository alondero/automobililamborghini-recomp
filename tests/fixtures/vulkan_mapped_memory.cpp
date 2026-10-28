// The real VulkanBuffer declaration and methods are inserted by the Python test.
// Separate CPU/GPU bytes make missing cache maintenance observable without a GPU.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using VkResult = int;
using VkDeviceSize = uint64_t;
using VkBuffer = int;
using VmaAllocation = int;
constexpr int VK_NULL_HANDLE = 0, VK_SUCCESS = 0;
struct VmaAllocationInfo {};
enum class RenderHeapType { UNKNOWN, DEFAULT, UPLOAD, READBACK, GPU_UPLOAD };
struct RenderBufferDesc { uint64_t size = 96; RenderHeapType heapType = RenderHeapType::UPLOAD; };
struct RenderRange { uint64_t begin, end; };
struct RenderBuffer {};
struct RenderBufferFormattedView {};
enum class RenderFormat {};
using RenderBarrierStages = int;
struct RenderBarrierStage { static constexpr int NONE = 0; };
struct VulkanPool {};
struct VulkanDevice { int allocator = 1; };
// BUFFER_DECLARATION
VulkanBuffer::~VulkanBuffer() = default;

static std::array<uint8_t, 256> hostBytes{}, deviceBytes{};
static bool coherent = false;
static int mappings = 0;
static std::string scenario;
static void require(bool ok, const char *message) {
    if (!ok) { fprintf(stderr, "%s\n", message); std::exit(2); }
}
static void event(const char *name) { puts(name); fflush(stdout); }
static auto &gpuBytes() { return coherent ? hostBytes : deviceBytes; }

VkResult vmaMapMemory(int, VmaAllocation, void **data) {
    event("map");
    if (scenario == "map-failure") return -5;
    ++mappings;
    *data = hostBytes.data();
    return VK_SUCCESS;
}
void vmaUnmapMemory(int, VmaAllocation) {
    require(mappings > 0, "unbalanced unmap");
    --mappings;
    event("unmap");
}
VkResult vmaInvalidateAllocation(int, VmaAllocation, VkDeviceSize offset, VkDeviceSize size) {
    require(mappings > 0 && size > 0 && offset + size <= 96, "invalid invalidation");
    printf("invalidate %llu %llu\n", (unsigned long long)offset, (unsigned long long)size);
    fflush(stdout);
    if (scenario == "invalidate-failure") return -4;
    if (!coherent) std::copy_n(deviceBytes.data() + offset, size, hostBytes.data() + offset);
    return VK_SUCCESS;
}
VkResult vmaFlushAllocation(int, VmaAllocation, VkDeviceSize offset, VkDeviceSize size) {
    require(mappings > 0 && size > 0 && offset + size <= 96, "invalid flush");
    printf("flush %llu %llu\n", (unsigned long long)offset, (unsigned long long)size);
    fflush(stdout);
    if (scenario == "flush-failure") return -4;
    if (!coherent) std::copy_n(hostBytes.data() + offset, size, deviceBytes.data() + offset);
    return VK_SUCCESS;
}

int main(int argc, char **argv) {
    require(argc == 3, "scenario and coherence required");
    scenario = argv[1];
    coherent = std::string(argv[2]) == "coherent";
    VulkanDevice device;
    VulkanBuffer buffer;
    buffer.device = &device;
    buffer.allocation = 1;
    if (scenario == "map-failure" || scenario == "invalid-allocation" ||
        scenario.rfind("invalid-read-", 0) == 0) {
        RenderRange range{0, 96};
        uint32_t subresource = 0;
        if (scenario == "invalid-allocation") buffer.allocation = VK_NULL_HANDLE;
        if (scenario == "invalid-read-reversed") range = {20, 10};
        if (scenario == "invalid-read-end") range = {0, 97};
        if (scenario == "invalid-read-empty") range = {97, 97};
        if (scenario == "invalid-read-overflow") range = {1, UINT64_MAX};
        if (scenario == "invalid-read-subresource") subresource = 1;
        buffer.map(subresource, &range);
        require(false, "invalid map continued");
    } else if (scenario == "unmatched-unmap") {
        buffer.unmap(0, nullptr);
        require(false, "unmatched unmap continued");
    } else if (scenario.rfind("invalid-write-", 0) == 0 || scenario == "double-unmap") {
        buffer.map(0, nullptr);
        RenderRange range{0, 0};
        uint32_t subresource = 0;
        if (scenario == "invalid-write-reversed") range = {20, 10};
        if (scenario == "invalid-write-end") range = {0, 97};
        if (scenario == "invalid-write-empty") range = {97, 97};
        if (scenario == "invalid-write-overflow") range = {1, UINT64_MAX};
        if (scenario == "invalid-write-subresource") subresource = 1;
        buffer.unmap(subresource, &range);
        if (scenario == "double-unmap") buffer.unmap(0, &range);
        require(false, "invalid unmap continued");
    } else if (scenario == "invalidate-failure") {
        buffer.desc.heapType = RenderHeapType::READBACK;
        buffer.map(0, nullptr);
        require(false, "failed invalidation continued");
    } else if (scenario == "nested") {
        auto *first = static_cast<uint8_t *>(buffer.map(0, nullptr));
        auto *second = static_cast<uint8_t *>(buffer.map(0, nullptr));
        require(first == second, "nested map changed base pointer");
        first[1] = 31;
        second[2] = 32;
        const RenderRange a{1, 2}, b{2, 3};
        buffer.unmap(0, &a);
        buffer.unmap(0, &b);
        require(gpuBytes()[1] == 31 && gpuBytes()[2] == 32, "nested writes lost");
    } else if (scenario == "empty-zero" || scenario == "empty-middle" ||
               scenario == "empty-end" || scenario == "readback-empty") {
        if (scenario == "readback-empty") buffer.desc.heapType = RenderHeapType::READBACK;
        const uint64_t offset = scenario == "empty-end" ? 96 : (scenario == "empty-middle" ? 17 : 0);
        const RenderRange empty{offset, offset};
        buffer.map(0, &empty);
        buffer.unmap(0, &empty);
    } else if (scenario == "partial" || scenario == "full" || scenario == "tail" ||
               scenario == "default-explicit" || scenario == "readback-explicit-write") {
        if (scenario == "default-explicit") buffer.desc.heapType = RenderHeapType::DEFAULT;
        if (scenario == "readback-explicit-write") buffer.desc.heapType = RenderHeapType::READBACK;
        const RenderRange range = scenario == "full" ? RenderRange{0, 96} :
            (scenario == "tail" ? RenderRange{95, 96} : RenderRange{7, 30});
        hostBytes.fill(19);
        gpuBytes().fill(23);
        event("submit");
        gpuBytes()[range.begin] = 83;
        event("wait");
        auto *data = static_cast<uint8_t *>(buffer.map(0, &range));
        event("cpu-read");
        require(data == hostBytes.data() && data[range.begin] == 83, "partial read missed GPU data or changed base");
        if (!coherent && range.begin != 0) require(data[0] == 19, "invalidated outside requested range");
        event("cpu-write");
        data[range.begin] = 42;
        buffer.unmap(0, &range);
        event("submit");
        require(gpuBytes()[range.begin] == 42, "partial flush missed CPU data");
        if (!coherent && range.begin != 0) require(gpuBytes()[0] == 23, "flushed outside requested range");
    } else if (scenario == "readback-default") {
        buffer.desc.heapType = RenderHeapType::READBACK;
        event("submit");
        gpuBytes()[17] = 83;
        event("wait");
        const auto *data = static_cast<uint8_t *>(buffer.map(0, nullptr));
        event("cpu-read");
        require(data == hostBytes.data() && data[17] == 83, "CPU saw stale GPU data");
        buffer.unmap(0, nullptr);
    } else {
        require(scenario == "upload-default" || scenario == "gpu-upload-default" ||
                scenario == "flush-failure", "unknown scenario");
        if (scenario == "gpu-upload-default") buffer.desc.heapType = RenderHeapType::GPU_UPLOAD;
        auto *data = static_cast<uint8_t *>(buffer.map(0, nullptr));
        require(data == hostBytes.data(), "map must return allocation base");
        event("cpu-write");
        data[17] = 42;
        buffer.unmap(0, nullptr);
        event("submit");
        require(gpuBytes()[17] == 42, "GPU saw stale CPU data");
    }
    require(mappings == 0, "mapping leaked");
}

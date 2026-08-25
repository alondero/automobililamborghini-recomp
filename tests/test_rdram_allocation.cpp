#include <cstddef>
#include <cstdint>
#include <iostream>

#include <windows.h>

#include "librecomp/rdram_memory.hpp"

namespace {
constexpr std::size_t kMiB = 1024ULL * 1024ULL;
constexpr std::size_t kAddressSpaceSize = 4096ULL * kMiB;
constexpr std::size_t kAccessibleSize = 512ULL * kMiB;

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    expect(job != nullptr, "create a memory-limited job object");
    if (job == nullptr) return 1;

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit = 1024ULL * kMiB;
    expect(SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                   &limits, sizeof(limits)) != 0,
           "set the process commit limit");
    expect(AssignProcessToJobObject(job, GetCurrentProcess()) != 0,
           "put the test process in the memory-limited job");
    if (failures != 0) {
        CloseHandle(job);
        return failures;
    }

    void* eager = VirtualAlloc(nullptr, kAddressSpaceSize,
                               MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    expect(eager == nullptr,
           "the previous eager 4 GiB commit reproduces under a 1 GiB commit limit");
    if (eager != nullptr) VirtualFree(eager, 0, MEM_RELEASE);

    std::uint8_t* rdram = recomp::allocate_rdram_memory(kAddressSpaceSize,
                                                        kAccessibleSize);
    expect(rdram != nullptr,
           "reserve 4 GiB while committing only the accessible 512 MiB");
    if (rdram != nullptr) {
        MEMORY_BASIC_INFORMATION accessible{};
        MEMORY_BASIC_INFORMATION guard{};
        expect(VirtualQuery(rdram, &accessible, sizeof(accessible)) != 0 &&
                   accessible.State == MEM_COMMIT &&
                   accessible.Protect == PAGE_READWRITE,
               "the accessible RDRAM prefix is committed read-write");
        expect(VirtualQuery(rdram + kAccessibleSize, &guard, sizeof(guard)) != 0 &&
                   guard.State == MEM_RESERVE &&
                   guard.Protect == 0,
               "the remainder stays reserved and inaccessible as a guard");
        expect(recomp::release_rdram_memory(rdram, kAddressSpaceSize),
               "release the complete RDRAM address-space reservation");
    }

    CloseHandle(job);
    return failures;
}

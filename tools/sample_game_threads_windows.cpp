// Diagnostic wall-clock RIP sampler for the 30 Hz guest threads. This is not
// an on-CPU profiler: suspension changes scheduling, and blocked samples count.
// Keep it outside the game executable; see docs/performance.md for the protocol.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>

#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

struct SampledThread {
    DWORD id;
    HANDLE handle;
    std::wstring name;
    unsigned long long initial_cpu;
    std::chrono::steady_clock::time_point cpu_start;
};

bool read_cpu(HANDLE handle, unsigned long long& value) {
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(handle, &created, &exited, &kernel, &user)) return false;
    value = ((unsigned long long)kernel.dwHighDateTime << 32) + kernel.dwLowDateTime
          + ((unsigned long long)user.dwHighDateTime << 32) + user.dwLowDateTime;
    return true;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: sample_game_threads_windows PID SECONDS (0 < seconds <= 60)\n");
        return 2;
    }
    char *pid_end, *seconds_end;
    errno = 0;
    const unsigned long pid_value = std::strtoul(argv[1], &pid_end, 10);
    const bool bad_pid = errno == ERANGE || argv[1][0] < '0' || argv[1][0] > '9';
    const double duration = std::strtod(argv[2], &seconds_end);
    if (bad_pid || *pid_end || pid_end == argv[1] || pid_value == 0 ||
        pid_value > std::numeric_limits<DWORD>::max() ||
        *seconds_end || seconds_end == argv[2] || !std::isfinite(duration) ||
        duration <= 0 || duration > 60) return 2;

    std::vector<SampledThread> threads;
    unsigned long failures = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 1;
    THREADENTRY32 entry{sizeof(entry)};
    if (Thread32First(snapshot, &entry)) do {
        if (entry.th32OwnerProcessID != pid_value) continue;
        HANDLE handle = OpenThread(THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME |
                                  THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID);
        if (!handle) {
            std::fprintf(stderr, "THREAD_UNAVAILABLE %lu\n", entry.th32ThreadID);
            ++failures;
            continue;
        }
        unsigned long long initial_cpu;
        if (!read_cpu(handle, initial_cpu)) {
            std::fprintf(stderr, "CPU_UNAVAILABLE %lu\n", entry.th32ThreadID);
            ++failures;
            CloseHandle(handle);
            continue;
        }
        const auto cpu_start = std::chrono::steady_clock::now();
        PWSTR description = nullptr;
        GetThreadDescription(handle, &description);
        std::wstring name = description ? description : L"";
        LocalFree(description);
        threads.push_back({entry.th32ThreadID, handle, name, initial_cpu, cpu_start});
    } while (Thread32Next(snapshot, &entry));
    CloseHandle(snapshot);
    if (threads.empty()) {
        std::fprintf(stderr, "No readable threads for PID %lu\n", pid_value);
        return 1;
    }

    HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                             static_cast<DWORD>(pid_value));
    MODULEENTRY32 module{sizeof(module)};
    if (modules != INVALID_HANDLE_VALUE && Module32First(modules, &module)) {
        do {
            std::fprintf(stderr, "MODULE %llx %lu %s\n",
                (unsigned long long)module.modBaseAddr, module.modBaseSize, module.szModule);
        } while (Module32Next(modules, &module));
        CloseHandle(modules);
    } else {
        std::fprintf(stderr, "Cannot enumerate target modules: Windows error %lu\n", GetLastError());
        if (modules != INVALID_HANDLE_VALUE) CloseHandle(modules);
        for (const auto& thread : threads) CloseHandle(thread.handle);
        return 1;
    }
    for (const auto& thread : threads)
        std::fprintf(stderr, "THREAD %lu %ls\n", thread.id, thread.name.c_str());

    const auto start = std::chrono::steady_clock::now();
    std::fprintf(stderr, "START %lld\n", (long long)std::chrono::duration_cast<
        std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    timeBeginPeriod(1);
    unsigned long round = 0;
    bool guest_found = false;
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < duration) {
        for (const auto& thread : threads) {
            // The runtime names translated OSThreads "Game Thread N". Renderer
            // and RSP threads get CPU counters but are not suspended here.
            if (thread.name.find(L"Game Thread ") != 0) continue;
            guest_found = true;
            if (SuspendThread(thread.handle) == DWORD(-1)) { ++failures; continue; }
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            const BOOL sampled = GetThreadContext(thread.handle, &context);
            // Resume before formatting or allocating. The handle retains the
            // exact original thread even if the process later reuses its TID.
            if (ResumeThread(thread.handle) == DWORD(-1)) ++failures;
            if (sampled) std::printf("%lu,%lu,%llx\n", round, thread.id,
                                      (unsigned long long)context.Rip);
            else ++failures;
        }
        ++round;
        Sleep(2);
    }
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    timeEndPeriod(1);
    std::fprintf(stderr, "SAMPLE_SECONDS %.6f\n", seconds);
    for (const auto& thread : threads) {
        unsigned long long final_cpu;
        if (read_cpu(thread.handle, final_cpu) && final_cpu >= thread.initial_cpu) {
            const double counter_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - thread.cpu_start).count();
            std::fprintf(stderr, "CPU %lu %ls %.6f seconds %.6f wall\n", thread.id,
                thread.name.c_str(), (final_cpu - thread.initial_cpu) * 1e-7, counter_seconds);
        } else {
            std::fprintf(stderr, "CPU_UNAVAILABLE %lu\n", thread.id);
            ++failures;
        }
        CloseHandle(thread.handle);
    }
    if (!guest_found) ++failures;
    std::fprintf(stderr, "ROUNDS %lu FAILURES %lu\n", round, failures);
    return failures ? 1 : 0;
}

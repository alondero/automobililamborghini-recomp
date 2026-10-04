#include <cstdio>
#include <cstdlib>
#include <string>

#include "lambo_rt_binding.h"

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
}

int main() {
    using lambo::rt::PublishedTasks;
    {
        // A task binds to the id RT64 actually assigns, not a predicted one.
        PublishedTasks<std::string> tasks;
        tasks.stage(std::string("task 1"));
        tasks.published(7);
        tasks.finish();
        require(tasks.find(7) && *tasks.find(7) == "task 1", "task did not bind to its published id");
        require(!tasks.find(8), "a predicted next id was bound");
    }
    {
        // A task that publishes nothing must not lend its identity to the next
        // Workload published outside it (for example a paused re-submission).
        PublishedTasks<std::string> tasks;
        tasks.published(6);
        tasks.stage(std::string("silent"));
        tasks.finish();
        tasks.published(7);
        require(!tasks.find(7), "an unpublished task bound to a later Workload");
    }
    {
        // A second Workload from one task gets no identity and stays native.
        PublishedTasks<std::string> tasks;
        tasks.stage(std::string("double"));
        tasks.published(4);
        tasks.published(5);
        tasks.finish();
        require(tasks.find(4) && *tasks.find(4) == "double", "first Workload lost its task");
        require(!tasks.find(5), "one task identity bound to two Workloads");
    }
    {
        // Staging replaces an unclaimed value; a disabled task stages nothing.
        PublishedTasks<std::string> tasks;
        tasks.stage(std::string("old"));
        tasks.stage(std::nullopt);
        tasks.published(9);
        require(!tasks.find(9), "a disabled task inherited the previous task's identity");
    }
    {
        // Old bindings are retired; invalidation clears both staged and bound values.
        PublishedTasks<std::string> tasks;
        for (uint64_t id = 1; id <= 40; ++id) {
            tasks.stage(std::to_string(id));
            tasks.published(id);
            tasks.finish();
        }
        require(!tasks.find(1), "retired Workload id still bound");
        require(tasks.find(40) && *tasks.find(40) == "40", "latest Workload id missing");
        tasks.stage(std::string("pending"));
        tasks.invalidate();
        tasks.published(41);
        require(!tasks.find(40) && !tasks.find(41), "invalidation kept a binding");
    }
    std::puts("rt binding tests passed");
    return 0;
}

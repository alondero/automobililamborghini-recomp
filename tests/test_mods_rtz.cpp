// Regression test for issue #227: exercises the production
// lambo::mods::register_content() against stub runtime functions and
// intercepts the container registration call. The runtime rejects extensions
// containing a dot and prepends the dot itself, so passing ".rtz" registers
// nothing and texture packs stay invisible in the Mods tab.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "lambo_log.h"
#include "lambo_mods.h"
// Test fake (tests/fakes); production builds use the runtime submodule.
#include "librecomp/mods.hpp"

namespace recomp::mods::stub {
size_t context_order_index = 0;
std::filesystem::path context_filename;
}

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// Observed production registration calls.
recomp::mods::ModContentType seen_content{};
bool saw_content = false;
std::string seen_extension;
std::vector<recomp::mods::ModContentTypeId> seen_content_types;
bool seen_requires_manifest = true;
int container_calls = 0;
bool stub_container_result = true;

// Captured log output.
LamboLogLevel seen_level = LAMBO_LEVEL_INFO;
std::string seen_tag;
std::string seen_message;
bool saw_log = false;
}

// Production default threshold, so the test also proves the failure log is
// visible without --verbose.
volatile int g_log_threshold = LAMBO_LEVEL_WARN;
bool lambo_log_enabled = false;

// Production links this guest export from the mod API source; the test
// provides a no-op so register_content() can pass it to the stub.
extern "C" void lambo_log_v1(uint8_t*, recomp_context*) {}

void lambo_log_write(LamboLogLevel level, const char* tag, const char* format, ...) {
    saw_log = true;
    seen_level = level;
    seen_tag = tag != nullptr ? tag : "";
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    seen_message = buffer;
}

namespace recomp::mods {
ModContentTypeId register_mod_content_type(const ModContentType& type) {
    saw_content = true;
    seen_content = type;
    return ModContentTypeId{7};
}
bool register_mod_container_type(const std::string& extension,
                                 const std::vector<ModContentTypeId>& content_types,
                                 bool requires_manifest) {
    ++container_calls;
    seen_extension = extension;
    seen_content_types = content_types;
    seen_requires_manifest = requires_manifest;
    return stub_container_result;
}
}

int main() {
    lambo::mods::register_content();

    expect(saw_content, "texture content type is registered");
    expect(seen_content.content_filename == "rt64.json", "content is detected by rt64.json");
    expect(seen_content.allow_runtime_toggle, "texture content is runtime-toggleable");
    expect(seen_content.on_enabled != nullptr && seen_content.on_disabled != nullptr &&
               seen_content.on_reordered != nullptr,
           "texture content sets its callbacks");
    expect(container_calls == 1, "rtz container is registered exactly once");
    expect(seen_extension == "rtz", "container extension is rtz");
    expect(seen_extension.find('.') == std::string::npos,
           "container extension has no dot for the runtime to reject");
    expect(seen_content_types.size() == 1 && seen_content_types[0].value == 7,
           "container carries the registered texture content id");
    expect(!seen_requires_manifest, "rtz container needs no manifest");

    recomp::mods::ModContext context;
    recomp::mods::stub::context_filename = "coolpack.rtz";
    recomp::mods::ModHandle enabled{{"coolpack"}};
    seen_content.on_enabled(context, enabled);
    const auto after_enable = lambo::mods::take_texture_pack_update();
    expect(after_enable.has_value() && after_enable->size() == 1 &&
               after_enable->front().string() == "coolpack.rtz",
           "enabling a pack publishes its path");
    seen_content.on_disabled(context, enabled);
    const auto after_disable = lambo::mods::take_texture_pack_update();
    expect(after_disable.has_value() && after_disable->empty(),
           "disabling a pack publishes an empty snapshot");

    saw_log = false;
    stub_container_result = false;
    lambo::mods::register_content();
    expect(saw_log, "failed registration is logged");
    expect(seen_level == LAMBO_LEVEL_ERROR, "failed registration logs at error level");
    expect(seen_tag == "mods", "failed registration logs under the mods tag");

    return failures == 0 ? 0 : 1;
}

// Guards the .rtz mod-container registration (issue #227). The runtime's
// ModContext::register_container_type rejects extensions containing a dot and
// prepends the dot itself, so a dotted literal registers nothing and texture
// packs stay invisible in the Mods tab.
#include <cstring>
#include <iostream>

#include "lambo_mods.h"

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}

int main() {
    const char* extension = lambo::mods::texture_pack_container_extension;
    expect(extension != nullptr && extension[0] != '\0',
           "texture-pack container extension is set");
    expect(std::strchr(extension, '.') == nullptr,
           "texture-pack container extension has no dot");
    expect(std::strcmp(extension, "rtz") == 0,
           "texture-pack container extension is rtz");
    return failures == 0 ? 0 : 1;
}

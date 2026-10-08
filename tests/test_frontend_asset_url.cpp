#include "core/ui_document_source.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/URL.h>
#include <cstdio>

class LogCapture : public Rml::SystemInterface {
public:
    int errors = 0;
    bool LogMessage(Rml::Log::Type type, const Rml::String&) override {
        if (type == Rml::Log::LT_ERROR) ++errors;
        return true;
    }
};

int main() {
    LogCapture system;
    Rml::SetSystemInterface(&system);
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
    };
    const std::filesystem::path windows(u8"F:/Native port/\u00e9/assets/");
    // This reproduction pins the current RmlUi diagnostic, not our URL helper.
    // Revisit it if an upstream upgrade accepts native paths or stops logging
    // this error; the normalized-path checks below remain the fix's contract.
    Rml::URL malformed("F:\\Native port\\assets\\");
    check(system.errors > 0, "native Windows source reproduces the debugger error");
    system.errors = 0;
    const auto source = recompui::detail::document_source_url(windows);
    check(source.starts_with("F|/Native port/"), "Windows drive is encoded for RmlUi");
    Rml::URL url(source);
    check(system.errors == 0, "asset document source parses without a debugger error");
    Rml::String joined;
    system.JoinPath(joined, source, "icons/Keyboard.svg");
    check(joined == std::string(reinterpret_cast<const char*>(u8"F:/Native port/\u00e9/assets/icons/Keyboard.svg")),
          "relative icons resolve to an openable Windows path with UTF-8 preserved");
    system.JoinPath(joined, source, "../logo.png");
    check(joined == std::string(reinterpret_cast<const char*>(u8"F:/Native port/\u00e9/logo.png")),
          "parent segments resolve correctly");
    const auto posix = recompui::detail::document_source_url("/opt/native/assets/");
    check(posix == "/opt/native/assets/", "POSIX source path is unchanged");
    Rml::URL posix_url(posix);
    check(system.errors == 0, "both source paths parse without errors");
    Rml::SetSystemInterface(nullptr);
    if (!failures) std::puts("Frontend asset URL: all cases passed");
    return failures ? 1 : 0;
}

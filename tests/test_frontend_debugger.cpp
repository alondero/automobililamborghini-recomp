#include "core/ui_debugger.h"
#include <RmlUi/Core.h>
#include <cstdio>
#include <string>

class LogCapture : public Rml::SystemInterface {
public:
    int diagnostics = 0;
    int shutdown_warnings = 0;
    bool LogMessage(Rml::Log::Type, const Rml::String& message) override {
        if (message == "injected UI diagnostic") ++diagnostics;
        if (message.find("Unable to shutdown debugger") != std::string::npos) ++shutdown_warnings;
        return true;
    }
};
// Exercise RmlUi's real documents and event dispatch without a window or GPU.
class NoRender : public Rml::RenderInterface {
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 0; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

int main(int argc, char** argv) {
    const bool enabled = argc == 2 && std::string(argv[1]) == "enabled";
    int failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; } };
    LogCapture logs;
    NoRender render;
    Rml::SetSystemInterface(&logs);
    Rml::SetRenderInterface(&render);
    check(Rml::Initialise(), "RmlUi initializes");
    auto* context = Rml::CreateContext("test", {800, 600});
    if (!context) return 1;
    recompui::detail::UiDebugger debugger;
    debugger.initialise(context, enabled);
    Rml::Log::Message(Rml::Log::LT_ERROR, "injected UI diagnostic");
    context->Update();
    check(logs.diagnostics == 1, "diagnostic reaches system logger exactly once");
    auto* beacon = context->GetDocument("rmlui-debug-log-beacon");
    check(enabled ? beacon && beacon->IsVisible() : beacon == nullptr,
          "only developer mode creates and shows the diagnostic beacon");
    debugger.toggle();
    context->Update();
    check(Rml::Debugger::IsVisible() == enabled, "toggle respects startup option");
    debugger.toggle();
    context->Update();
    check(!Rml::Debugger::IsVisible(), "second toggle hides inspector");
    // A small document flow tests focus/show/hide with the diagnostic active.
    // This is not a substitute for the port's rendered launcher/settings test.
    auto* launcher = context->LoadDocumentFromMemory("<rml><head></head><body><button id='settings'>Settings</button></body></rml>");
    auto* settings = context->LoadDocumentFromMemory("<rml><head></head><body><button id='back'>Back</button></body></rml>");
    check(launcher && settings, "navigation documents load");
    if (launcher && settings) {
        launcher->Show(); context->Update();
        check(launcher->GetElementById("settings")->Focus(), "launcher control accepts focus");
        launcher->Hide(); settings->Show(); context->Update();
        check(!launcher->IsVisible() && settings->IsVisible(), "settings opens");
        check(settings->GetElementById("back")->Focus(), "settings control accepts focus");
        settings->Hide(); launcher->Show(); context->Update();
        check(launcher->IsVisible() && !settings->IsVisible(), "return to launcher");
    }
    debugger.shutdown();
    debugger.shutdown();
    check(logs.shutdown_warnings == 0, "disabled and repeated shutdown are quiet");
    Rml::Log::Message(Rml::Log::LT_ERROR, "injected UI diagnostic");
    check(logs.diagnostics == 2, "system logger remains active after debugger shutdown");
    Rml::Shutdown();
    Rml::SetRenderInterface(nullptr);
    Rml::SetSystemInterface(nullptr);
    if (!failures) std::printf("Frontend debugger %s: all checks passed\n", enabled ? "enabled" : "default");
    return failures ? 1 : 0;
}

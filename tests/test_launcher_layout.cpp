#include <RmlUi/Core.h>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

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
    if (argc != 2) return 1;
    std::ifstream file(argv[1]);
    if (!file) return 1;
    const std::string css((std::istreambuf_iterator<char>(file)), {});
    NoRender render;
    Rml::SetRenderInterface(&render);
    if (!Rml::Initialise()) return 1;
    auto* context = Rml::CreateContext("launcher-layout", {1600, 900});
    // Test the shipped stylesheet with real RmlUi media-query evaluation and resizing.
    // This does not claim to test the full rendered launcher or input navigation.
    auto* doc = context->LoadDocumentFromMemory("<rml><head><style>" + css +
        "</style></head><body><paddock-rail id='rail'/><paddock-actions id='actions'/>"
        "<paddock-status id='status'/></body></rml>");
    if (!doc) return 1;
    doc->Show();
    int failures = 0;
    for (float density : {1.0f, 1.5f, 2.0f}) {
        context->SetDensityIndependentPixelRatio(density);
        for (int width : {1600, 600, 800, 1000, 1001, 1600}) {
            context->SetDimensions({int(width * density), int(900 * density)});
            context->Update();
            const bool compact = width <= 1000;
            const bool rail_hidden = doc->GetElementById("rail")->GetProperty<Rml::Style::Display>("display") == Rml::Style::Display::None;
            const bool status_hidden = doc->GetElementById("status")->GetProperty<Rml::Style::Display>("display") == Rml::Style::Display::None;
            const float padding = doc->GetElementById("actions")->GetBox().GetEdge(Rml::BoxArea::Padding, Rml::BoxEdge::Left);
            if (rail_hidden != compact || status_hidden == compact ||
                std::abs(padding - (compact ? 24.0f : 64.0f) * density) > 0.1f) {
                std::fprintf(stderr, "FAIL: width=%d density=%g\n", width, density);
                ++failures;
            }
        }
    }
    Rml::Shutdown();
    Rml::SetRenderInterface(nullptr);
    if (!failures) std::puts("Launcher responsive stylesheet: all checks passed");
    return failures ? 1 : 0;
}

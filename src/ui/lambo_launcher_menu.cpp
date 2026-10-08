#include "ui/lambo_launcher_menu.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "recompui/recompui.h"
#include "elements/ui_image.h"
#include "util/file.h"
#include "librecomp/game.hpp"

namespace lambo::ui {
namespace {

// Giallo yellow sampled from the POOTERMAN fan-art wordmark
// (docs/lambo-fan-art.png). Used only for launcher accents; the shared
// settings theme is untouched.
constexpr recompui::Color kGiallo{255, 196, 0, 255};
constexpr recompui::Color kInk{243, 241, 233, 255};
constexpr recompui::Color kMuted{169, 176, 185, 255};
constexpr recompui::Color kLine{43, 48, 56, 255};

// Staged beside the executable by the build (assets/frontend is copied to
// <exe-dir>/assets). Resolved through the frontend's own asset helper, so it
// works both for local builds and installed releases. When it is missing the
// menu and status text remain usable instead of showing a broken image.
constexpr char kFanArtSrc[] = "lambo-fan-art.png";

// Keyboard/controller focus must reveal a row even when a narrow window
// wraps its description and makes the menu taller than the viewport.
class LauncherRowButton : public recompui::Button {
protected:
    void process_event(const recompui::Event& event) override {
        Button::process_event(event);
        if (event.type == recompui::EventType::Focus &&
            std::get<recompui::EventFocus>(event.variant).active) {
            scroll_into_view(false);
        }
    }
public:
    LauncherRowButton(recompui::ResourceId id, recompui::Element* parent)
        : Button(id, parent, "", recompui::ButtonStyle::Basic, recompui::ButtonSize::Small) {
        // Let wrapped titles/descriptions determine height, rather than
        // overlapping the next row when a narrow list needs more lines.
        remove_property(Rml::PropertyId::MaxHeight);
    }
};

} // namespace

void build_paddock_launcher(recompui::LauncherMenu* menu, const LauncherActions& actions) {
    using namespace recompui;
    menu->remove_default_title();
    // The framework's corner version inherits this color. The rail supplies
    // the single visible version; all port-owned text sets its own color.
    menu->set_color(Color{0, 0, 0, 0});

    auto context = get_launcher_context_id();
    Element* container = menu->get_menu_container();
    // The upstream container is absolutely positioned with 24dp insets.
    // Give the split an explicit viewport height so the rail's flex spacer
    // has space to consume and its status footer stays at the bottom.
    container->set_inset(0.0f);
    container->set_height(100.0f, Unit::Percent);
    container->set_background_color(Color{11, 12, 14, 255});
    container->set_display(Display::Flex);
    container->set_flex_direction(FlexDirection::Row);
    container->set_align_items(AlignItems::Stretch);

    // Brand rail: identity and system state.
    Element* rail = context.create_element<Element>(container);
    rail->set_display(Display::Flex);
    rail->set_flex_direction(FlexDirection::Column);
    rail->set_gap(14.0f);
    rail->set_width(400.0f);
    rail->set_height(100.0f, Unit::Percent);
    rail->set_flex_shrink(0.0f);
    rail->set_padding(48.0f);
    rail->set_background_color(Color{0, 0, 0, 255});
    rail->set_border_right_width(1.0f);
    rail->set_border_right_color(kLine);

    // Served through the same queued-bytes image path as the upstream game
    // thumbnails, so no file-URL resolution is involved at draw time.
    const std::filesystem::path art_path = recompui::file::get_asset_path(kFanArtSrc);
    std::error_code art_error;
    const bool art_ready = std::filesystem::exists(art_path, art_error);
    if (art_ready) {
        std::ifstream art_file(art_path, std::ios::binary);
        const std::vector<char> art_bytes((std::istreambuf_iterator<char>(art_file)),
                                          std::istreambuf_iterator<char>());
        if (!art_bytes.empty()) {
            constexpr char kArtSrc[] = "?/lambo/fan-art";
            recompui::queue_image_from_bytes_file(kArtSrc, art_bytes);
            Image* art = context.create_element<Image>(rail, kArtSrc);
            art->set_width(100.0f, Unit::Percent);
            art->set_flex_shrink(0.0f);
        }
    }
    Element* spacer = context.create_element<Element>(rail);
    spacer->set_flex_grow(1.0f);
    Label* credit = context.create_element<Label>(rail, "FAN ART BY POOTERMAN", LabelStyle::Small);
    credit->set_font_size(13.0f);
    credit->set_color(kMuted);

    // The ROM gate in main.cpp returns before the runtime starts unless
    // select_rom validates the USA dump, so a visible launcher implies a
    // verified ROM.
    Label* status = context.create_element<Label>(
        rail, "USA ROM VERIFIED", LabelStyle::Small);
    status->set_font_size(15.0f);
    status->set_color(Color{61, 220, 132, 255});
    status->set_border_top_width(1.0f);
    status->set_border_top_color(kLine);
    status->set_padding_top(20.0f);
    Label* version = context.create_element<Label>(rail,
        "NATIVE BUILD / v" + recomp::get_project_version().to_string(), LabelStyle::Small);
    version->set_font_size(14.0f);
    version->set_color(kMuted);

    // Menu side: numbered action rows.
    Element* menu_panel = context.create_element<Element>(container);
    menu_panel->set_display(Display::Flex);
    menu_panel->set_flex_direction(FlexDirection::Column);
    menu_panel->set_justify_content(JustifyContent::FlexStart);
    menu_panel->set_align_items(AlignItems::Stretch);
    menu_panel->set_flex_grow(1.0f);
    menu_panel->set_min_width(0.0f);
    menu_panel->set_padding(64.0f);
    menu_panel->set_overflow_y(Overflow::Auto);

    // Keep the list next to the rail. Cap its width on wide
    // displays while allowing it to shrink with the available menu space.
    Element* list = context.create_element<Element>(menu_panel);
    list->set_display(Display::Flex);
    list->set_flex_direction(FlexDirection::Column);
    list->set_width(100.0f, Unit::Percent);
    list->set_max_width(800.0f);
    list->set_as_navigation_container(NavigationType::Vertical);
    list->set_nav_wrapping(true);

    Label* heading = context.create_element<Label>(list, "MAIN MENU", LabelStyle::Small);
    heading->set_font_size(16.0f);
    heading->set_font_weight(700);
    heading->set_letter_spacing(3.0f);
    heading->set_margin_bottom(24.0f);
    heading->set_color(kMuted);

    const auto rows = paddock_rows();
    for (size_t i = 0; i < rows.size(); ++i) {
        const PaddockRow& row = rows[i];
        Button* entry = context.create_element<LauncherRowButton>(list);
        // Button clamps its own height to one line; rows carry a title plus a
        // description, so lift the clamp instead of overflowing the next row.
        entry->set_height_auto();
        entry->set_min_height(120.0f);
        entry->set_flex_shrink(0.0f);
        entry->set_width(100.0f, Unit::Percent);
        entry->set_display(Display::Flex);
        entry->set_flex_direction(FlexDirection::Row);
        entry->set_align_items(AlignItems::Center);
        entry->set_justify_content(JustifyContent::FlexStart);
        entry->set_gap(14.0f);
        entry->set_padding_top(20.0f);
        entry->set_padding_bottom(20.0f);
        entry->set_padding_left(16.0f);
        entry->set_padding_right(16.0f);
        entry->set_border_radius(0.0f);
        entry->set_border_width(0.0f);
        entry->set_border_left_width(4.0f);
        entry->set_border_left_color(theme::color::Transparent);
        entry->set_border_top_width(1.0f);
        entry->set_border_top_color(kLine);
        entry->get_hover_style()->set_border_color(kLine);
        entry->get_hover_style()->set_border_left_color(kGiallo);
        entry->get_hover_style()->set_background_color(Color{20, 22, 26, 255});
        entry->get_focus_style()->set_border_color(kLine);
        entry->get_focus_style()->set_border_left_color(kGiallo);
        entry->get_focus_style()->set_background_color(Color{27, 30, 36, 255});
        if (i + 1 == rows.size()) {
            entry->set_border_bottom_width(1.0f);
            entry->set_border_bottom_color(kLine);
        }
        entry->add_pressed_callback([actions, target = row.target] {
            activate_launcher(target, actions);
        });

        Label* number = context.create_element<Label>(entry, row.number, LabelStyle::Small);
        number->set_color(kGiallo);
        number->set_font_size(20.0f);
        number->set_font_weight(700);
        number->set_width(44.0f);
        number->set_flex_shrink(0.0f);
        Element* body = context.create_element<Element>(entry);
        body->set_display(Display::Flex);
        body->set_flex_direction(FlexDirection::Column);
        body->set_flex_grow(1.0f);
        body->set_min_width(0.0f);
        body->set_gap(6.0f);
        Label* title = context.create_element<Label>(body, row.title, theme::Typography::LabelMD);
        title->set_font_size(26.0f);
        title->set_line_height(32.0f);
        title->set_color(kInk);
        Label* description = context.create_element<Label>(body, row.description, LabelStyle::Small);
        description->set_font_size(17.0f);
        description->set_line_height(24.0f);
        description->set_color(kMuted);
        Label* chevron = context.create_element<Label>(entry, "\u203a", theme::Typography::Header3);
        chevron->set_color(kGiallo);

        if (i == 0) context.set_autofocus_element(entry);
    }
}

} // namespace lambo::ui

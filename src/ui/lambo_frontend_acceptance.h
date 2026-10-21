#pragma once

#include "lambo_harness_report.h"
#include "lambo_log.h"
#include "lambo_reset_observation.h"
#include <cstdlib>
#include <string_view>

namespace lambo::ui {

// Opt-in rendered acceptance driver. Run on the presentation thread, under the
// frontend mutex, after draw_hook has applied the real documents' updates.
// Clicks use RmlUi's event path, including the header callbacks and prompts.
// Guest state comes from the harness snapshot; this thread never touches RAM.
inline void tick_frontend_acceptance(recompui::IconButton* reset, recompui::IconButton* quit) {
    static const char* requested = std::getenv("LAMBO_UI_ACCEPTANCE");
    if (!requested) return;
    const std::string_view mode(requested);
    static int phase = 0;
    static int wait_frames = 0;
    static int resets = 0;
    static ResetObservation reset_observation;
    if (++wait_frames < 30) return;
    auto context = recompui::config::get_config_context_id();
    auto click = [&](recompui::IconButton* button) {
        if (!button || !button->is_enabled()) return false;
        auto* element = context.get_document()->GetElementById(button->get_id());
        if (!element) return false;
        element->Click();
        wait_frames = 0;
        return true;
    };
    if (mode == "quit") {
        if (phase || ultramodern::is_game_started()) return;
        recompui::config::open();
        if (click(quit)) {
            phase = 1;
            LAMBO_LOG_INFO("acceptance", "pre-game header Quit clicked\n");
        }
        return;
    }
    if (mode != "reset") return;
    const auto metrics = lambo::harness::snapshot();
    if (phase == 0) {
        if (metrics.current_state != 8 || metrics.swaps < 60) return;
        recompui::config::open();
        phase = 1;
        wait_frames = 0;
    } else if (phase == 1 || phase == 3) {
        if (phase == 3 && metrics.current_state != 8) return;
        if (click(reset)) ++phase;
    } else if (phase == 2 || phase == 4) {
        // The shared choice prompt has Confirm then Cancel in its controls.
        // Check its title and button count before selecting either action.
        auto* rml = context.get_document()->GetContext();
        for (int i = 0; i < rml->GetNumDocuments(); ++i) {
            auto* document = rml->GetDocument(i);
            if (!document->IsVisible() || document->GetInnerRML().find("Reset game?") == std::string::npos) continue;
            Rml::ElementList buttons;
            document->QuerySelectorAll(buttons, "button");
            if (buttons.size() != 2) return;
            if (phase == 4) reset_observation.requested(lambo_reset_generation());
            buttons[phase == 2 ? 1 : 0]->Click();
            if (phase == 2) {
                phase = 3;
                LAMBO_LOG_INFO("acceptance", "reset cancelled during race\n");
            } else {
                ++resets;
                phase = 5;
            }
            wait_frames = 0;
            return;
        }
    } else if (phase == 5 && reset_observation.completed(
                   lambo_reset_generation(), metrics.vis, metrics.current_state)) {
        if (resets < 2) {
            recompui::config::open();
            // The second confirmed reset starts from the live title screen.
            if (click(reset)) phase = 4;
        } else {
            phase = 6;
            LAMBO_LOG_INFO("acceptance", "two resets completed in the same process; title active\n");
            SDL_Event event{};
            event.type = SDL_QUIT;
            SDL_PushEvent(&event);
        }
    }
}

} // namespace lambo::ui

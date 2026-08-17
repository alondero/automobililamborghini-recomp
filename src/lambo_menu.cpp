#include "lambo_menu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include <SDL.h>

#include "lambo_config.h"
#include "lambo_log.h"
#include "ui/lambo_ui.h"

#if defined(_WIN32)
#include <SDL_syswm.h>

namespace {

SDL_Window* g_window = nullptr;
HWND g_hwnd = nullptr;
HMENU g_menu_bar = nullptr;
HMENU g_game_menu = nullptr;
HMENU g_resolution_menu = nullptr;
HMENU g_supersampling_menu = nullptr;
HMENU g_aspect_menu = nullptr;
HMENU g_hud_menu = nullptr;
HMENU g_rate_menu = nullptr;
HMENU g_aa_menu = nullptr;
HMENU g_hpfb_menu = nullptr;
HMENU g_api_menu = nullptr;
HMENU g_enhancements_menu = nullptr;
HMENU g_pvs_menu = nullptr;
HMENU g_draw_menu = nullptr;
HMENU g_fog_menu = nullptr;

enum Command : UINT {
    CMD_FULLSCREEN = 1000,
    CMD_SETTINGS,
    CMD_QUIT,

    CMD_RES_AUTO = 1100,
    CMD_RES_ORIGINAL,
    CMD_RES_ORIGINAL_2X,
    CMD_SS_1X,
    CMD_SS_2X,
    CMD_SS_3X,
    CMD_SS_4X,
    CMD_ASPECT_ORIGINAL,
    CMD_ASPECT_EXPAND,
    CMD_HUD_ORIGINAL,
    CMD_HUD_CLAMP_16X9,
    CMD_HUD_FULL,
    CMD_RATE_ORIGINAL,
    CMD_RATE_DISPLAY,
    CMD_RATE_30,
    CMD_RATE_60,
    CMD_RATE_90,
    CMD_RATE_120,
    CMD_RATE_144,
    CMD_RATE_165,
    CMD_RATE_240,
    CMD_AA_NONE,
    CMD_AA_2X,
    CMD_AA_4X,
    CMD_AA_8X,
    CMD_HPFB_AUTO,
    CMD_HPFB_ON,
    CMD_HPFB_OFF,
    CMD_API_AUTO,
    CMD_API_D3D12,
    CMD_API_VULKAN,

    CMD_FOG_MATCH = 1200,
    CMD_SKY_MATCH,
    CMD_NO_LOD,
    CMD_PVS_CIRCUIT_1,
    CMD_PVS_CIRCUIT_2,
    CMD_PVS_CIRCUIT_3,
    CMD_PVS_CIRCUIT_4,
    CMD_PVS_CIRCUIT_5,
    CMD_PVS_CIRCUIT_6,
    CMD_DRAW_1X,
    CMD_DRAW_1_5X,
    CMD_DRAW_2X,
    CMD_DRAW_3X,
    CMD_DRAW_UNLIMITED,
    CMD_FOG_OFF,
    CMD_FOG_50,
    CMD_FOG_75,
    CMD_FOG_100,
    CMD_FOG_150,
    CMD_FOG_200,
};

void append_item(HMENU menu, UINT id, const char* label) {
    AppendMenuA(menu, MF_STRING, id, label);
}

HMENU append_submenu(HMENU parent, const char* label) {
    HMENU child = CreatePopupMenu();
    AppendMenuA(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(child), label);
    return child;
}

void check(HMENU menu, UINT id, bool checked) {
    CheckMenuItem(menu, id, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

void radio(HMENU menu, UINT first, UINT last, UINT selected) {
    CheckMenuRadioItem(menu, first, last, selected, MF_BYCOMMAND);
}

bool close(double a, double b) {
    return std::abs(a - b) < 0.0001;
}

void refresh() {
    using namespace ultramodern::renderer;
    const GraphicsConfig cfg = lambo::config::current_graphics();
    check(g_game_menu, CMD_FULLSCREEN, g_window != nullptr &&
          (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0);

    radio(g_resolution_menu, CMD_RES_AUTO, CMD_RES_ORIGINAL_2X,
          cfg.res_option == Resolution::Auto ? CMD_RES_AUTO :
          cfg.res_option == Resolution::Original2x ? CMD_RES_ORIGINAL_2X : CMD_RES_ORIGINAL);
    radio(g_supersampling_menu, CMD_SS_1X, CMD_SS_4X,
          cfg.ds_option >= 1 && cfg.ds_option <= 4 ? CMD_SS_1X + cfg.ds_option - 1 : 0);
    radio(g_aspect_menu, CMD_ASPECT_ORIGINAL, CMD_ASPECT_EXPAND,
          cfg.ar_option == AspectRatio::Expand ? CMD_ASPECT_EXPAND :
          cfg.ar_option == AspectRatio::Original ? CMD_ASPECT_ORIGINAL : 0);
    radio(g_hud_menu, CMD_HUD_ORIGINAL, CMD_HUD_FULL,
          cfg.hr_option == HUDRatioMode::Full ? CMD_HUD_FULL :
          cfg.hr_option == HUDRatioMode::Clamp16x9 ? CMD_HUD_CLAMP_16X9 : CMD_HUD_ORIGINAL);

    UINT rate = CMD_RATE_ORIGINAL;
    if (cfg.rr_option == RefreshRate::Display) rate = CMD_RATE_DISPLAY;
    else if (cfg.rr_option == RefreshRate::Manual) {
        constexpr std::array<int, 7> values{30, 60, 90, 120, 144, 165, 240};
        auto it = std::find(values.begin(), values.end(), cfg.rr_manual_value);
        rate = it == values.end() ? 0 : CMD_RATE_30 + UINT(it - values.begin());
    }
    radio(g_rate_menu, CMD_RATE_ORIGINAL, CMD_RATE_240, rate);
    radio(g_aa_menu, CMD_AA_NONE, CMD_AA_8X,
          cfg.msaa_option == Antialiasing::MSAA8X ? CMD_AA_8X :
          cfg.msaa_option == Antialiasing::MSAA4X ? CMD_AA_4X :
          cfg.msaa_option == Antialiasing::MSAA2X ? CMD_AA_2X : CMD_AA_NONE);
    radio(g_hpfb_menu, CMD_HPFB_AUTO, CMD_HPFB_OFF,
          cfg.hpfb_option == HighPrecisionFramebuffer::On ? CMD_HPFB_ON :
          cfg.hpfb_option == HighPrecisionFramebuffer::Off ? CMD_HPFB_OFF : CMD_HPFB_AUTO);
    radio(g_api_menu, CMD_API_AUTO, CMD_API_VULKAN,
          cfg.api_option == GraphicsApi::D3D12 ? CMD_API_D3D12 :
          cfg.api_option == GraphicsApi::Vulkan ? CMD_API_VULKAN :
          cfg.api_option == GraphicsApi::Auto ? CMD_API_AUTO : 0);

    check(g_enhancements_menu, CMD_FOG_MATCH, lambo::config::widescreen_fog_match());
    check(g_enhancements_menu, CMD_SKY_MATCH, lambo::config::widescreen_sky_match());
    check(g_enhancements_menu, CMD_NO_LOD, lambo::config::no_lod());
    for (int i = 0; i < 6; ++i) check(g_pvs_menu, CMD_PVS_CIRCUIT_1 + i, lambo::config::no_lod_circuit(i));

    const double draw = lambo::config::global_draw_distance();
    radio(g_draw_menu, CMD_DRAW_1X, CMD_DRAW_UNLIMITED,
          draw <= 0.0 ? CMD_DRAW_UNLIMITED : close(draw, 3.0) ? CMD_DRAW_3X :
          close(draw, 2.0) ? CMD_DRAW_2X : close(draw, 1.5) ? CMD_DRAW_1_5X :
          close(draw, 1.0) ? CMD_DRAW_1X : 0);
    const double fog = lambo::config::global_fog_scale();
    radio(g_fog_menu, CMD_FOG_OFF, CMD_FOG_200,
          close(fog, 2.0) ? CMD_FOG_200 : close(fog, 1.5) ? CMD_FOG_150 :
          close(fog, 1.0) ? CMD_FOG_100 : close(fog, 0.75) ? CMD_FOG_75 :
          close(fog, 0.5) ? CMD_FOG_50 : close(fog, 0.0) ? CMD_FOG_OFF : 0);
    if (g_hwnd != nullptr) DrawMenuBar(g_hwnd);
}

void apply_graphics_command(UINT command) {
    using namespace ultramodern::renderer;
    GraphicsConfig cfg = lambo::config::current_graphics();
    switch (command) {
        case CMD_RES_AUTO: cfg.res_option = Resolution::Auto; break;
        case CMD_RES_ORIGINAL: cfg.res_option = Resolution::Original; break;
        case CMD_RES_ORIGINAL_2X: cfg.res_option = Resolution::Original2x; break;
        case CMD_SS_1X: case CMD_SS_2X: case CMD_SS_3X: case CMD_SS_4X:
            cfg.ds_option = int(command - CMD_SS_1X) + 1; break;
        case CMD_ASPECT_ORIGINAL: cfg.ar_option = AspectRatio::Original; break;
        case CMD_ASPECT_EXPAND: cfg.ar_option = AspectRatio::Expand; break;
        case CMD_HUD_ORIGINAL: cfg.hr_option = HUDRatioMode::Original; break;
        case CMD_HUD_CLAMP_16X9: cfg.hr_option = HUDRatioMode::Clamp16x9; break;
        case CMD_HUD_FULL: cfg.hr_option = HUDRatioMode::Full; break;
        case CMD_RATE_ORIGINAL: cfg.rr_option = RefreshRate::Original; break;
        case CMD_RATE_DISPLAY: cfg.rr_option = RefreshRate::Display; break;
        case CMD_RATE_30: case CMD_RATE_60: case CMD_RATE_90: case CMD_RATE_120:
        case CMD_RATE_144: case CMD_RATE_165: case CMD_RATE_240: {
            constexpr std::array<int, 7> values{30, 60, 90, 120, 144, 165, 240};
            cfg.rr_option = RefreshRate::Manual;
            cfg.rr_manual_value = values[command - CMD_RATE_30];
            break;
        }
        case CMD_AA_NONE: cfg.msaa_option = Antialiasing::None; break;
        case CMD_AA_2X: cfg.msaa_option = Antialiasing::MSAA2X; break;
        case CMD_AA_4X: cfg.msaa_option = Antialiasing::MSAA4X; break;
        case CMD_AA_8X: cfg.msaa_option = Antialiasing::MSAA8X; break;
        case CMD_HPFB_AUTO: cfg.hpfb_option = HighPrecisionFramebuffer::Auto; break;
        case CMD_HPFB_ON: cfg.hpfb_option = HighPrecisionFramebuffer::On; break;
        case CMD_HPFB_OFF: cfg.hpfb_option = HighPrecisionFramebuffer::Off; break;
        // RT64 selects its backend while constructing the renderer. Persist this
        // immediately, but the menu label is explicit that activation is next launch.
        case CMD_API_AUTO: cfg.api_option = GraphicsApi::Auto; break;
        case CMD_API_D3D12: cfg.api_option = GraphicsApi::D3D12; break;
        case CMD_API_VULKAN: cfg.api_option = GraphicsApi::Vulkan; break;
        default: return;
    }
    lambo::config::apply_graphics(cfg);
}

void dispatch(UINT command) {
    switch (command) {
        case CMD_FULLSCREEN: lambo::menu::toggle_fullscreen(); break;
        case CMD_SETTINGS: lambo::ui::open_settings(); break;
        case CMD_QUIT: {
            SDL_Event quit{};
            quit.type = SDL_QUIT;
            SDL_PushEvent(&quit);
            break;
        }
        case CMD_FOG_MATCH: lambo::config::set_widescreen_fog_match(!lambo::config::widescreen_fog_match()); break;
        case CMD_SKY_MATCH: lambo::config::set_widescreen_sky_match(!lambo::config::widescreen_sky_match()); break;
        case CMD_NO_LOD: lambo::config::set_no_lod(!lambo::config::no_lod()); break;
        case CMD_PVS_CIRCUIT_1: case CMD_PVS_CIRCUIT_2: case CMD_PVS_CIRCUIT_3:
        case CMD_PVS_CIRCUIT_4: case CMD_PVS_CIRCUIT_5: case CMD_PVS_CIRCUIT_6: {
            int circuit = int(command - CMD_PVS_CIRCUIT_1);
            lambo::config::set_no_lod_circuit(circuit, !lambo::config::no_lod_circuit(circuit));
            break;
        }
        case CMD_DRAW_1X: lambo::config::set_global_draw_distance(1.0); break;
        case CMD_DRAW_1_5X: lambo::config::set_global_draw_distance(1.5); break;
        case CMD_DRAW_2X: lambo::config::set_global_draw_distance(2.0); break;
        case CMD_DRAW_3X: lambo::config::set_global_draw_distance(3.0); break;
        case CMD_DRAW_UNLIMITED: lambo::config::set_global_draw_distance(0.0); break;
        case CMD_FOG_OFF: lambo::config::set_global_fog_scale(0.0); break;
        case CMD_FOG_50: lambo::config::set_global_fog_scale(0.5); break;
        case CMD_FOG_75: lambo::config::set_global_fog_scale(0.75); break;
        case CMD_FOG_100: lambo::config::set_global_fog_scale(1.0); break;
        case CMD_FOG_150: lambo::config::set_global_fog_scale(1.5); break;
        case CMD_FOG_200: lambo::config::set_global_fog_scale(2.0); break;
        default: apply_graphics_command(command); break;
    }
    refresh();
}

} // anonymous namespace

namespace lambo::menu {

void attach(SDL_Window* window) {
    g_window = window;
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) != SDL_TRUE) return;
    g_hwnd = info.info.win.window;

    g_menu_bar = CreateMenu();
    HMENU game = g_game_menu = append_submenu(g_menu_bar, "&Game");
    append_item(game, CMD_FULLSCREEN, "&Fullscreen\tF11");
    append_item(game, CMD_SETTINGS, "&Settings...");
    AppendMenuA(game, MF_SEPARATOR, 0, nullptr);
    append_item(game, CMD_QUIT, "E&xit");

    HMENU graphics = append_submenu(g_menu_bar, "&Graphics");
    HMENU resolution = g_resolution_menu = append_submenu(graphics, "Internal resolution");
    append_item(resolution, CMD_RES_AUTO, "Automatic (window scale)");
    append_item(resolution, CMD_RES_ORIGINAL, "Original");
    append_item(resolution, CMD_RES_ORIGINAL_2X, "Original 2x");
    HMENU supersampling = g_supersampling_menu = append_submenu(graphics, "Supersampling");
    append_item(supersampling, CMD_SS_1X, "1x"); append_item(supersampling, CMD_SS_2X, "2x");
    append_item(supersampling, CMD_SS_3X, "3x"); append_item(supersampling, CMD_SS_4X, "4x");
    HMENU aspect = g_aspect_menu = append_submenu(graphics, "Aspect ratio");
    append_item(aspect, CMD_ASPECT_ORIGINAL, "Original (4:3)");
    append_item(aspect, CMD_ASPECT_EXPAND, "Expand (widescreen)");
    HMENU hud = g_hud_menu = append_submenu(graphics, "HUD placement");
    append_item(hud, CMD_HUD_ORIGINAL, "Original (4:3)");
    append_item(hud, CMD_HUD_CLAMP_16X9, "Clamp to 16:9");
    append_item(hud, CMD_HUD_FULL, "Full width");
    HMENU rate = g_rate_menu = append_submenu(graphics, "Frame rate");
    append_item(rate, CMD_RATE_ORIGINAL, "Original"); append_item(rate, CMD_RATE_DISPLAY, "Display refresh rate");
    AppendMenuA(rate, MF_SEPARATOR, 0, nullptr);
    append_item(rate, CMD_RATE_30, "30 FPS"); append_item(rate, CMD_RATE_60, "60 FPS");
    append_item(rate, CMD_RATE_90, "90 FPS"); append_item(rate, CMD_RATE_120, "120 FPS");
    append_item(rate, CMD_RATE_144, "144 FPS"); append_item(rate, CMD_RATE_165, "165 FPS");
    append_item(rate, CMD_RATE_240, "240 FPS");
    HMENU aa = g_aa_menu = append_submenu(graphics, "Anti-aliasing");
    append_item(aa, CMD_AA_NONE, "Off"); append_item(aa, CMD_AA_2X, "MSAA 2x");
    append_item(aa, CMD_AA_4X, "MSAA 4x"); append_item(aa, CMD_AA_8X, "MSAA 8x");
    HMENU hpfb = g_hpfb_menu = append_submenu(graphics, "High-precision framebuffer");
    append_item(hpfb, CMD_HPFB_AUTO, "Automatic"); append_item(hpfb, CMD_HPFB_ON, "On");
    append_item(hpfb, CMD_HPFB_OFF, "Off");
    HMENU api = g_api_menu = append_submenu(graphics, "Graphics API (restart required)");
    append_item(api, CMD_API_AUTO, "Automatic"); append_item(api, CMD_API_D3D12, "Direct3D 12");
    append_item(api, CMD_API_VULKAN, "Vulkan");

    HMENU enhancements = g_enhancements_menu = append_submenu(g_menu_bar, "&Enhancements");
    append_item(enhancements, CMD_FOG_MATCH, "3P/4P widescreen fog");
    append_item(enhancements, CMD_SKY_MATCH, "3P/4P widescreen sky");
    append_item(enhancements, CMD_NO_LOD, "Remove N64 LOD reductions");
    HMENU pvs = g_pvs_menu = append_submenu(enhancements, "Full-track visibility by circuit");
    for (int i = 0; i < 6; ++i) {
        std::string label = "Circuit " + std::to_string(i + 1);
        append_item(pvs, CMD_PVS_CIRCUIT_1 + i, label.c_str());
    }
    HMENU draw = g_draw_menu = append_submenu(enhancements, "Draw distance");
    append_item(draw, CMD_DRAW_1X, "Original (1x)"); append_item(draw, CMD_DRAW_1_5X, "1.5x");
    append_item(draw, CMD_DRAW_2X, "2x"); append_item(draw, CMD_DRAW_3X, "3x");
    append_item(draw, CMD_DRAW_UNLIMITED, "Unlimited");
    HMENU fog = g_fog_menu = append_submenu(enhancements, "Fog density");
    append_item(fog, CMD_FOG_OFF, "Off"); append_item(fog, CMD_FOG_50, "50%");
    append_item(fog, CMD_FOG_75, "75%"); append_item(fog, CMD_FOG_100, "Original (100%)");
    append_item(fog, CMD_FOG_150, "150%"); append_item(fog, CMD_FOG_200, "200%");

    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0) {
        SetMenu(g_hwnd, g_menu_bar);
    }
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
    refresh();
}

bool handle_event(const SDL_Event& event) {
    if (event.type != SDL_SYSWMEVENT || event.syswm.msg == nullptr) return false;
    const SDL_SysWMmsg& msg = *event.syswm.msg;
    if (msg.subsystem != SDL_SYSWM_WINDOWS || msg.msg.win.msg != WM_COMMAND) return false;
    dispatch(LOWORD(msg.msg.win.wParam));
    return true;
}

void toggle_fullscreen() {
    if (g_window == nullptr) return;
    const bool fullscreen = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;
    if (SDL_SetWindowFullscreen(g_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        LAMBO_LOG("config", "fullscreen toggle FAILED: %s\n", SDL_GetError());
        return;
    }
    SetMenu(g_hwnd, fullscreen ? nullptr : g_menu_bar);
    auto cfg = lambo::config::current_graphics();
    cfg.wm_option = fullscreen ? ultramodern::renderer::WindowMode::Fullscreen
                               : ultramodern::renderer::WindowMode::Windowed;
    // Window mode is owned by SDL and deliberately not sent through RT64.
    lambo::config::update_saved_window_mode(cfg.wm_option);
    LAMBO_LOG("config", "fullscreen %s (menu / F11 / Alt+Enter)\n", fullscreen ? "ON" : "OFF");
    refresh();
}

} // namespace lambo::menu

#else

namespace {
SDL_Window* g_window = nullptr;
}

namespace lambo::menu {
void attach(SDL_Window* window) { g_window = window; }
bool handle_event(const SDL_Event&) { return false; }
void toggle_fullscreen() {
    if (g_window == nullptr) return;
    const bool fullscreen = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;
    if (SDL_SetWindowFullscreen(g_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        LAMBO_LOG("config", "fullscreen toggle FAILED: %s\n", SDL_GetError());
        return;
    }
    lambo::config::update_saved_window_mode(
        fullscreen ? ultramodern::renderer::WindowMode::Fullscreen
                   : ultramodern::renderer::WindowMode::Windowed);
    LAMBO_LOG("config", "fullscreen %s (F11 / Alt+Enter)\n", fullscreen ? "ON" : "OFF");
}
} // namespace lambo::menu

#endif

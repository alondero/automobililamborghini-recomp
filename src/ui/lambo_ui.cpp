#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#endif

#include "lambo_ui.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL.h>
#include <concurrentqueue.h>

#include "RmlUi/Core.h"
#include "RmlUi/Debugger.h"
#include "RmlUi_Platform_SDL.h"
#include "rt64_render_hooks.h"

#include "lambo_config.h"
#include "lambo_log.h"
#include "lambo_startup.h"
#include "lambo_ui_render_interface.h"

namespace {

using Clock = std::chrono::steady_clock;

struct QueuedEvent {
    SDL_Event event{};
};

class UiEventListener final : public Rml::EventListener {
public:
    using Callback = std::function<void(const std::string&)>;

    UiEventListener(Callback callback, std::string parameter)
        : callback_(std::move(callback)), parameter_(std::move(parameter)) {}

    void ProcessEvent(Rml::Event&) override { callback_(parameter_); }

private:
    Callback callback_;
    std::string parameter_;
};

class UiEventListenerInstancer final : public Rml::EventListenerInstancer {
public:
    using Callback = UiEventListener::Callback;

    void register_event(const std::string& name, Callback callback) {
        callbacks_[name] = std::move(callback);
    }

    Rml::EventListener* InstanceEventListener(const Rml::String& value,
                                              Rml::Element*) override {
        const size_t separator = value.find(':');
        const std::string name = value.substr(0, separator);
        auto callback = callbacks_.find(name);
        if (callback == callbacks_.end()) return nullptr;

        auto listener = listeners_.find(value);
        if (listener != listeners_.end()) return listener->second.get();

        const std::string parameter = separator == std::string::npos
            ? std::string{}
            : value.substr(separator + 1);
        auto instance = std::make_unique<UiEventListener>(callback->second, parameter);
        UiEventListener* result = instance.get();
        listeners_.emplace(value, std::move(instance));
        return result;
    }

private:
    std::unordered_map<std::string, Callback> callbacks_;
    std::unordered_map<std::string, std::unique_ptr<UiEventListener>> listeners_;
};

struct UiState {
    SDL_Window* window = nullptr;
    std::unique_ptr<SystemInterface_SDL> system_interface;
    lambo::ui::RmlRenderInterface_RT64 render_interface;
    Rml::Context* context = nullptr;
    Rml::ElementDocument* document = nullptr;
    UiEventListenerInstancer event_listener_instancer;
    std::vector<lambo::ui::Page> pages;
    lambo::ui::EntryPoint entry_point = lambo::ui::EntryPoint::Startup;
    int focused_key = SDLK_UNKNOWN;
    Clock::time_point next_repeat{};
    bool axis_x_latched = false;
    bool axis_y_latched = false;

    ~UiState() {
        if (document != nullptr && context != nullptr) {
            context->UnloadDocument(document);
            document = nullptr;
        }
        if (context != nullptr) {
            Rml::RemoveContext(context->GetName());
            context = nullptr;
        }
        render_interface.reset();
        Rml::Debugger::Shutdown();
        Rml::Shutdown();
    }
};

std::unique_ptr<UiState> g_state;
std::mutex g_state_mutex;
SDL_Window* g_window = nullptr;
std::atomic<bool> g_visible{false};
std::atomic<bool> g_capture{false};
std::atomic<int> g_requested_page{-1};
std::atomic<int> g_requested_entry_point{static_cast<int>(lambo::ui::EntryPoint::Startup)};
std::atomic<bool> g_requested_back{false};
std::atomic<lambo::StartupController*> g_startup_controller{nullptr};
moodycamel::ConcurrentQueue<QueuedEvent> g_event_queue;

std::filesystem::path asset_root() {
    if (const char* configured = std::getenv("LAMBO_ASSET_DIR")) {
        return std::filesystem::path(configured);
    }
    const auto cwd_ui = std::filesystem::current_path() / "assets" / "ui";
    if (std::filesystem::exists(cwd_ui)) return cwd_ui;
    if (char* base_path = SDL_GetBasePath()) {
        const auto executable_ui = std::filesystem::path(base_path) / "assets" / "ui";
        SDL_free(base_path);
        if (std::filesystem::exists(executable_ui)) return executable_ui;
    }
    const auto executable_ui = std::filesystem::current_path() / "ui";
    if (std::filesystem::exists(executable_ui)) return executable_ui;
    return cwd_ui;
}

const char* page_document(lambo::ui::Page page) {
    switch (page) {
        case lambo::ui::Page::Home:         return "launcher.rml";
        case lambo::ui::Page::Settings:     return "settings.rml";
        case lambo::ui::Page::Graphics:     return "pages/graphics.rml";
        case lambo::ui::Page::Enhancements: return "pages/enhancements.rml";
        case lambo::ui::Page::Controls:     return "pages/controls.rml";
        case lambo::ui::Page::Haptics:      return "pages/haptics.rml";
    }
    return "launcher.rml";
}

const char* page_title(lambo::ui::Page page) {
    switch (page) {
        case lambo::ui::Page::Home:         return "Home";
        case lambo::ui::Page::Settings:     return "Settings";
        case lambo::ui::Page::Graphics:     return "Graphics";
        case lambo::ui::Page::Enhancements: return "Enhancements";
        case lambo::ui::Page::Controls:     return "Controls";
        case lambo::ui::Page::Haptics:      return "Haptics";
    }
    return "Home";
}

void set_text(const char* id, const std::string& value) {
    if (g_state == nullptr || g_state->document == nullptr) return;
    if (Rml::Element* element = g_state->document->GetElementById(id)) {
        element->SetInnerRML(value);
    }
}

std::string graphics_summary() {
    const auto cfg = lambo::config::current_graphics();
    using namespace ultramodern::renderer;
    const char* api = cfg.api_option == GraphicsApi::D3D12 ? "Direct3D 12" :
                      cfg.api_option == GraphicsApi::Vulkan ? "Vulkan" : "Automatic";
    const char* aspect = cfg.ar_option == AspectRatio::Original ? "Original 4:3" : "Expand";
    return std::string("API: ") + api + " (restart required)<br/>Aspect: " + aspect +
           "<br/>Supersampling: " + std::to_string(cfg.ds_option) + "x";
}

void refresh_document_values() {
    if (g_state == nullptr || g_state->document == nullptr) return;
    set_text("version", "v1.0.0");
    set_text("graphics-summary", graphics_summary());
    set_text("enhancement-summary",
             std::string("Fog: ") + (lambo::config::widescreen_fog_match() ? "matched" : "original") +
             "<br/>Sky: " + (lambo::config::widescreen_sky_match() ? "matched" : "original") +
             "<br/>LOD: " + (lambo::config::no_lod() ? "removed" : "original") +
             "<br/>Draw distance: " + std::to_string(lambo::config::global_draw_distance()) + "x");
}

void load_page(lambo::ui::Page page, bool push_history) {
    if (g_state == nullptr || g_state->context == nullptr) return;
    if (g_state->document != nullptr) {
        g_state->context->UnloadDocument(g_state->document);
        g_state->document = nullptr;
    }

    const std::filesystem::path path = asset_root() / page_document(page);
    g_state->document = g_state->context->LoadDocument(path.string());
    if (g_state->document == nullptr) {
        LAMBO_LOG("ui", "failed to load %s\n", path.string().c_str());
        g_visible.store(false, std::memory_order_release);
        g_capture.store(false, std::memory_order_release);
        return;
    }
    g_state->document->Show();
    g_state->document->PullToFront();
    if (push_history) g_state->pages.push_back(page);
    refresh_document_values();
    if (Rml::Element* autofocus = g_state->document->GetElementById("autofocus")) {
        autofocus->Focus();
    }
    g_visible.store(true, std::memory_order_release);
    g_capture.store(true, std::memory_order_release);
    LAMBO_LOG("ui", "opened %s page\n", page_title(page));
}

void show_page(lambo::ui::Page page) {
    load_page(page, true);
}

void hide_pages() {
    if (g_state == nullptr) return;
    if (g_state->document != nullptr) {
        g_state->context->UnloadDocument(g_state->document);
        g_state->document = nullptr;
    }
    g_state->pages.clear();
    g_visible.store(false, std::memory_order_release);
    g_capture.store(false, std::memory_order_release);
}

void back() {
    if (g_state == nullptr || g_state->pages.empty()) return;
    if (g_state->pages.size() > 1) {
        g_state->pages.pop_back();
        load_page(g_state->pages.back(), false);
        return;
    }
    if (g_state->entry_point == lambo::ui::EntryPoint::InGameOverlay) hide_pages();
}

void apply_setting(const std::string& setting) {
    auto cfg = lambo::config::current_graphics();
    using namespace ultramodern::renderer;

    if (setting == "res:auto") cfg.res_option = Resolution::Auto;
    else if (setting == "res:original") cfg.res_option = Resolution::Original;
    else if (setting == "res:2x") cfg.res_option = Resolution::Original2x;
    else if (setting == "ss:1") cfg.ds_option = 1;
    else if (setting == "ss:2") cfg.ds_option = 2;
    else if (setting == "ss:3") cfg.ds_option = 3;
    else if (setting == "ss:4") cfg.ds_option = 4;
    else if (setting == "aspect:original") cfg.ar_option = AspectRatio::Original;
    else if (setting == "aspect:expand") cfg.ar_option = AspectRatio::Expand;
    else if (setting == "hud:original") cfg.hr_option = HUDRatioMode::Original;
    else if (setting == "hud:16x9") cfg.hr_option = HUDRatioMode::Clamp16x9;
    else if (setting == "hud:full") cfg.hr_option = HUDRatioMode::Full;
    else if (setting == "rate:original") cfg.rr_option = RefreshRate::Original;
    else if (setting == "rate:display") cfg.rr_option = RefreshRate::Display;
    else if (setting == "rate:30" || setting == "rate:60" || setting == "rate:120") {
        cfg.rr_option = RefreshRate::Manual;
        cfg.rr_manual_value = std::stoi(setting.substr(5));
    } else if (setting == "msaa:off") cfg.msaa_option = Antialiasing::None;
    else if (setting == "msaa:2") cfg.msaa_option = Antialiasing::MSAA2X;
    else if (setting == "msaa:4") cfg.msaa_option = Antialiasing::MSAA4X;
    else if (setting == "msaa:8") cfg.msaa_option = Antialiasing::MSAA8X;
    else if (setting == "hpfb:auto") cfg.hpfb_option = HighPrecisionFramebuffer::Auto;
    else if (setting == "hpfb:on") cfg.hpfb_option = HighPrecisionFramebuffer::On;
    else if (setting == "hpfb:off") cfg.hpfb_option = HighPrecisionFramebuffer::Off;
    else if (setting == "api:auto") cfg.api_option = GraphicsApi::Auto;
    else if (setting == "api:d3d12") cfg.api_option = GraphicsApi::D3D12;
    else if (setting == "api:vulkan") cfg.api_option = GraphicsApi::Vulkan;
    else if (setting == "fog:toggle") lambo::config::set_widescreen_fog_match(!lambo::config::widescreen_fog_match());
    else if (setting == "sky:toggle") lambo::config::set_widescreen_sky_match(!lambo::config::widescreen_sky_match());
    else if (setting == "lod:toggle") lambo::config::set_no_lod(!lambo::config::no_lod());
    else if (setting == "distance:1") lambo::config::set_global_draw_distance(1.0);
    else if (setting == "distance:1.5") lambo::config::set_global_draw_distance(1.5);
    else if (setting == "distance:2") lambo::config::set_global_draw_distance(2.0);
    else if (setting == "distance:unlimited") lambo::config::set_global_draw_distance(0.0);
    else if (setting == "fogdensity:off") lambo::config::set_global_fog_scale(0.0);
    else if (setting == "fogdensity:1") lambo::config::set_global_fog_scale(1.0);
    else if (setting == "fogdensity:1.5") lambo::config::set_global_fog_scale(1.5);
    else if (setting == "fogdensity:2") lambo::config::set_global_fog_scale(2.0);
    else return;

    if (setting.rfind("fog:", 0) != 0 && setting.rfind("sky:", 0) != 0 &&
        setting.rfind("lod:", 0) != 0 && setting.rfind("distance:", 0) != 0 &&
        setting.rfind("fogdensity:", 0) != 0) {
        lambo::config::apply_graphics(cfg);
    }
    refresh_document_values();
}

void process_action(const std::string& action, const std::string& parameter) {
    if (action == "play") {
        auto* controller = g_startup_controller.load(std::memory_order_acquire);
        if (controller != nullptr && controller->request_play()) {
            hide_pages();
            LAMBO_LOG("ui", "Play accepted; launcher hidden\n");
        }
    } else if (action == "quit") {
        if (auto* controller = g_startup_controller.load(std::memory_order_acquire)) {
            controller->request_exit();
        }
        SDL_Event event{};
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
    } else if (action == "settings") {
        show_page(lambo::ui::Page::Settings);
    } else if (action == "page") {
        if (parameter == "settings") show_page(lambo::ui::Page::Settings);
        else if (parameter == "graphics") show_page(lambo::ui::Page::Graphics);
        else if (parameter == "enhancements") show_page(lambo::ui::Page::Enhancements);
        else if (parameter == "controls") show_page(lambo::ui::Page::Controls);
        else if (parameter == "haptics") show_page(lambo::ui::Page::Haptics);
    } else if (action == "back") {
        back();
    } else if (action == "setting") {
        apply_setting(parameter);
    }
}

void install_event_handlers(UiState& state) {
    state.event_listener_instancer.register_event("play", [](const std::string&) { process_action("play", ""); });
    state.event_listener_instancer.register_event("quit", [](const std::string&) { process_action("quit", ""); });
    state.event_listener_instancer.register_event("settings", [](const std::string&) { process_action("settings", ""); });
    state.event_listener_instancer.register_event("page", [](const std::string& p) { process_action("page", p); });
    state.event_listener_instancer.register_event("back", [](const std::string&) { process_action("back", ""); });
    state.event_listener_instancer.register_event("setting", [](const std::string& p) { process_action("setting", p); });
}

void init_hook(RT64::RenderInterface* interface, RT64::RenderDevice* device) {
    std::lock_guard lock(g_state_mutex);
    if (g_state != nullptr || g_window == nullptr) return;

    auto state = std::make_unique<UiState>();
    state->window = g_window;
    state->system_interface = std::make_unique<SystemInterface_SDL>();
    state->system_interface->SetWindow(g_window);
    state->render_interface.init(interface, device);
    Rml::SetSystemInterface(state->system_interface.get());
    Rml::SetRenderInterface(state->render_interface.get_rml_interface());
    install_event_handlers(*state);
    if (!Rml::Initialise()) {
        LAMBO_LOG("ui", "RmlUi initialisation failed\n");
        return;
    }
    Rml::Factory::RegisterEventListenerInstancer(&state->event_listener_instancer);

    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(g_window, &width, &height);
    state->context = Rml::CreateContext("lamborghini", {width, height});
    if (state->context == nullptr) {
        LAMBO_LOG("ui", "RmlUi context creation failed\n");
        Rml::Shutdown();
        return;
    }
    const auto fonts = asset_root() / "fonts";
    const auto bundled_fonts = std::filesystem::current_path() / "lib" / "RmlUi" / "Samples" / "assets";
    const auto load_font = [&](const char* filename) {
        const auto configured_path = fonts / filename;
        const auto fallback_path = bundled_fonts / filename;
        if (std::filesystem::exists(configured_path)) {
            Rml::LoadFontFace(configured_path.string(), false);
        } else {
            Rml::LoadFontFace(fallback_path.string(), false);
        }
    };
    load_font("LatoLatin-Regular.ttf");
    load_font("LatoLatin-Bold.ttf");
    g_state = std::move(state);
    LAMBO_LOG("ui", "RmlUi render hooks initialised\n");
}

void process_key(int key) {
    if (g_state == nullptr || g_state->context == nullptr) return;
    if (key == SDLK_ESCAPE) {
        back();
        return;
    }
    g_state->context->ProcessKeyDown(RmlSDL::ConvertKey(key), RmlSDL::GetKeyModifierState());
    g_state->focused_key = key;
    g_state->next_repeat = Clock::now() + std::chrono::milliseconds(500);
}

void process_controller_axis(const SDL_ControllerAxisEvent& axis) {
    const bool horizontal = axis.axis == SDL_CONTROLLER_AXIS_LEFTX;
    const bool vertical = axis.axis == SDL_CONTROLLER_AXIS_LEFTY;
    if (!horizontal && !vertical) return;
    bool& latched = horizontal ? g_state->axis_x_latched : g_state->axis_y_latched;
    const int magnitude = std::abs(static_cast<int>(axis.value));
    if (magnitude < 9000) {
        latched = false;
        return;
    }
    if (latched) return;
    latched = true;
    process_key(horizontal
        ? (axis.value > 0 ? SDLK_RIGHT : SDLK_LEFT)
        : (axis.value > 0 ? SDLK_DOWN : SDLK_UP));
}

void process_queued_events() {
    QueuedEvent queued{};
    while (g_event_queue.try_dequeue(queued)) {
        SDL_Event& event = queued.event;
        if (g_state == nullptr || g_state->context == nullptr) continue;
        switch (event.type) {
            case SDL_KEYDOWN:
                if (!event.key.repeat) process_key(event.key.keysym.sym);
                break;
            case SDL_KEYUP:
                g_state->context->ProcessKeyUp(RmlSDL::ConvertKey(event.key.keysym.sym),
                                               RmlSDL::GetKeyModifierState());
                if (g_state->focused_key == event.key.keysym.sym) g_state->focused_key = SDLK_UNKNOWN;
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_A: process_key(SDLK_RETURN); break;
                    case SDL_CONTROLLER_BUTTON_B: process_key(SDLK_ESCAPE); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP: process_key(SDLK_UP); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: process_key(SDLK_DOWN); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: process_key(SDLK_LEFT); break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: process_key(SDLK_RIGHT); break;
                    default: break;
                }
                break;
            case SDL_CONTROLLERBUTTONUP:
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                    g_state->context->ProcessKeyUp(RmlSDL::ConvertKey(SDLK_RETURN), 0);
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                process_controller_axis(event.caxis);
                break;
            default:
                RmlSDL::InputEventHandler(g_state->context, event);
                break;
        }
    }

    if (g_state != nullptr && g_state->focused_key != SDLK_UNKNOWN &&
        Clock::now() >= g_state->next_repeat) {
        process_key(g_state->focused_key);
        g_state->next_repeat = Clock::now() + std::chrono::milliseconds(50);
    }
}

void draw_hook(RT64::RenderCommandList* command_list,
               RT64::RenderFramebuffer* swap_chain_framebuffer) {
    std::lock_guard lock(g_state_mutex);
    if (g_state == nullptr || g_state->context == nullptr || swap_chain_framebuffer == nullptr) return;

    const int requested = g_requested_page.exchange(-1, std::memory_order_acq_rel);
    if (requested >= 0) {
        g_state->entry_point = static_cast<lambo::ui::EntryPoint>(
            g_requested_entry_point.load(std::memory_order_acquire));
        g_state->pages.clear();
        show_page(static_cast<lambo::ui::Page>(requested));
    }
    if (g_requested_back.exchange(false, std::memory_order_acq_rel)) back();
    process_queued_events();
    if (!g_visible.load(std::memory_order_acquire)) return;

    const int width = swap_chain_framebuffer->getWidth();
    const int height = swap_chain_framebuffer->getHeight();
    g_state->context->SetDimensions({width, height});
    g_state->context->SetDensityIndependentPixelRatio(height / 1080.0f);
    g_state->context->Update();
    g_state->render_interface.start(command_list, width, height);
    g_state->context->Render();
    g_state->render_interface.end(command_list, swap_chain_framebuffer);
}

void deinit_hook() {
    std::lock_guard lock(g_state_mutex);
    g_state.reset();
    g_visible.store(false, std::memory_order_release);
    g_capture.store(false, std::memory_order_release);
}

} // namespace

namespace lambo::ui {

void set_window(SDL_Window* window) { g_window = window; }

void install_render_hooks() {
    RT64::SetRenderHooks(init_hook, draw_hook, deinit_hook);
}

void set_startup_controller(lambo::StartupController* controller) {
    g_startup_controller.store(controller, std::memory_order_release);
}

bool handle_event(const SDL_Event& event) {
    if (!g_capture.load(std::memory_order_acquire)) return false;
    switch (event.type) {
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_TEXTINPUT:
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERAXISMOTION:
        case SDL_WINDOWEVENT:
            g_event_queue.enqueue(QueuedEvent{event});
            return true;
        default:
            return false;
    }
}

void update_capture() {
    (void)g_capture.load(std::memory_order_acquire);
}

void open_launcher() {
    g_requested_entry_point.store(static_cast<int>(EntryPoint::Startup), std::memory_order_release);
    g_capture.store(true, std::memory_order_release);
    g_requested_page.store(static_cast<int>(Page::Home), std::memory_order_release);
}

void open_settings() {
    g_requested_entry_point.store(static_cast<int>(EntryPoint::InGameOverlay), std::memory_order_release);
    g_capture.store(true, std::memory_order_release);
    g_requested_page.store(static_cast<int>(Page::Settings), std::memory_order_release);
}

void close_top_page() {
    g_requested_back.store(true, std::memory_order_release);
}

bool is_initialized() {
    std::lock_guard lock(g_state_mutex);
    return g_state != nullptr && g_state->context != nullptr;
}
bool is_visible() { return g_visible.load(std::memory_order_acquire); }
bool captures_input() { return g_capture.load(std::memory_order_acquire); }

void shutdown() {
    RT64::SetRenderHooks(nullptr, nullptr, nullptr);
    deinit_hook();
}

} // namespace lambo::ui

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#endif

#include "lambo_ui.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
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
#include "lambo_ui_input.h"
#include "lambo_ui_render_interface.h"
#include "lambo_ui_settings.h"

#ifndef LAMBO_VERSION
#define LAMBO_VERSION "dev"
#endif

namespace {

using Clock = std::chrono::steady_clock;

enum class InputMode {
    Mouse,
    KeyboardOrController,
};

struct PageDescriptor {
    const char* document;
    const char* title;
};

constexpr std::array page_descriptors{
    PageDescriptor{"launcher.rml", "Home"},
    PageDescriptor{"settings.rml", "Settings"},
    PageDescriptor{"pages/graphics.rml", "Graphics"},
    PageDescriptor{"pages/enhancements.rml", "Enhancements"},
    PageDescriptor{"pages/controls.rml", "Controls"},
    PageDescriptor{"pages/haptics.rml", "Haptics"},
};

const PageDescriptor& page_descriptor(lambo::ui::Page page) {
    return page_descriptors.at(static_cast<size_t>(page));
}

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
    // RmlUi's memory font API retains these pointers until Rml::Shutdown.
    std::vector<std::vector<Rml::byte>> font_data;
    std::vector<lambo::ui::Page> pages;
    std::unordered_map<lambo::ui::Page, std::string> focused_element_by_page;
    lambo::ui::EntryPoint entry_point = lambo::ui::EntryPoint::Startup;
    std::optional<lambo::ui::Page> current_page;
    lambo::ui::ControllerNavigation controller_navigation;
    InputMode input_mode = InputMode::Mouse;

    void remember_focus();
    void restore_focus(lambo::ui::Page page);
    void set_input_mode(InputMode mode);
    void set_text(const char* id, const std::string& value);
    void refresh_document_values();
    void load_page(lambo::ui::Page page, bool push_history);
    void show_page(lambo::ui::Page page);
    void hide_pages();
    void back();
    void process_key_down(int key, bool repeat);
    void process_navigation_events(const std::vector<lambo::ui::NavigationEvent>& events);
    void process_queued_events();

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

void UiState::remember_focus() {
    if (!current_page.has_value() || context == nullptr) return;
    if (Rml::Element* focused = context->GetFocusElement();
        focused != nullptr && !focused->GetId().empty()) {
        focused_element_by_page[*current_page] = focused->GetId();
    }
}

void UiState::restore_focus(lambo::ui::Page page) {
    if (document == nullptr) return;
    Rml::Element* focus = nullptr;
    if (const auto saved = focused_element_by_page.find(page);
        saved != focused_element_by_page.end()) {
        focus = document->GetElementById(saved->second);
    }
    if (focus == nullptr) focus = document->GetElementById("autofocus");
    if (focus != nullptr) focus->Focus();
}

void UiState::set_input_mode(InputMode mode) {
    input_mode = mode;
    if (document == nullptr) return;
    document->SetClass("mouse-active", mode == InputMode::Mouse);
    document->SetClass("controller-active", mode == InputMode::KeyboardOrController);
    if (mode == InputMode::KeyboardOrController && context->GetFocusElement() == nullptr &&
        current_page.has_value()) {
        restore_focus(*current_page);
    }
}

void UiState::set_text(const char* id, const std::string& value) {
    if (document == nullptr) return;
    if (Rml::Element* element = document->GetElementById(id)) {
        element->SetInnerRML(value);
    }
}

void UiState::refresh_document_values() {
    if (document == nullptr) return;
    set_text("version", std::string("v") + LAMBO_VERSION);
    const auto values = lambo::ui::settings_snapshot();
    set_text("graphics-resolution", values.resolution);
    set_text("graphics-supersampling", values.supersampling);
    set_text("graphics-aspect", values.aspect_ratio);
    set_text("graphics-hud", values.hud_layout);
    set_text("graphics-refresh", values.refresh_rate);
    set_text("graphics-msaa", values.msaa);
    set_text("graphics-hpfb", values.framebuffer_precision);
    set_text("graphics-api", values.graphics_api);
    set_text("enhancement-fog", values.widescreen_fog);
    set_text("enhancement-sky", values.widescreen_sky);
    set_text("enhancement-lod", values.lod_removal);
    set_text("enhancement-distance", values.draw_distance);
    set_text("enhancement-fog-density", values.fog_density);
    for (size_t circuit = 0; circuit < values.circuit_visibility.size(); ++circuit) {
        const std::string id = "enhancement-circuit-" + std::to_string(circuit + 1);
        set_text(id.c_str(), values.circuit_visibility[circuit]);
    }
}

void UiState::load_page(lambo::ui::Page page, bool push_history) {
    if (context == nullptr) return;
    remember_focus();
    if (document != nullptr) {
        context->UnloadDocument(document);
        document = nullptr;
    }

    const PageDescriptor& descriptor = page_descriptor(page);
    const std::filesystem::path path = asset_root() / descriptor.document;
    document = context->LoadDocument(path.string());
    if (document == nullptr) {
        LAMBO_LOG("ui", "failed to load %s\n", path.string().c_str());
        g_visible.store(false, std::memory_order_release);
        g_capture.store(false, std::memory_order_release);
        current_page.reset();
        return;
    }
    document->Show();
    document->PullToFront();
    current_page = page;
    if (push_history) pages.push_back(page);
    refresh_document_values();
    restore_focus(page);
    set_input_mode(input_mode);
    g_visible.store(true, std::memory_order_release);
    g_capture.store(true, std::memory_order_release);
    LAMBO_LOG("ui", "opened %s page\n", descriptor.title);
}

void UiState::show_page(lambo::ui::Page page) {
    load_page(page, true);
}

void UiState::hide_pages() {
    remember_focus();
    if (document != nullptr) {
        context->UnloadDocument(document);
        document = nullptr;
    }
    pages.clear();
    current_page.reset();
    controller_navigation.reset();
    g_visible.store(false, std::memory_order_release);
    g_capture.store(false, std::memory_order_release);
}

void UiState::back() {
    if (pages.empty()) return;
    if (pages.size() > 1) {
        pages.pop_back();
        load_page(pages.back(), false);
        return;
    }
    if (entry_point == lambo::ui::EntryPoint::InGameOverlay) hide_pages();
}

void process_action(const std::string& action, const std::string& parameter) {
    if (action == "play") {
        auto* controller = g_startup_controller.load(std::memory_order_acquire);
        if (controller != nullptr && controller->request_play()) {
            if (g_state != nullptr) g_state->hide_pages();
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
        if (g_state != nullptr) g_state->show_page(lambo::ui::Page::Settings);
    } else if (action == "page") {
        if (g_state == nullptr) return;
        if (parameter == "settings") g_state->show_page(lambo::ui::Page::Settings);
        else if (parameter == "graphics") g_state->show_page(lambo::ui::Page::Graphics);
        else if (parameter == "enhancements") g_state->show_page(lambo::ui::Page::Enhancements);
        else if (parameter == "controls") g_state->show_page(lambo::ui::Page::Controls);
        else if (parameter == "haptics") g_state->show_page(lambo::ui::Page::Haptics);
    } else if (action == "back") {
        if (g_state != nullptr) g_state->back();
    } else if (action == "setting") {
        const auto setting = lambo::ui::setting_action_from_name(parameter);
        if (setting.has_value() && lambo::ui::apply_setting_action(*setting) && g_state != nullptr) {
            g_state->refresh_document_values();
        }
    }
}

void install_event_handlers(UiState& state) {
    state.event_listener_instancer.register_event("play", [](const std::string& p) { process_action("play", p); });
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
    state->font_data.reserve(2);
    const auto load_font = [&](const char* filename, Rml::Style::FontWeight weight) {
        const auto configured_path = fonts / filename;
        const auto fallback_path = bundled_fonts / filename;
        const std::filesystem::path* use_path = nullptr;
        if (std::filesystem::exists(configured_path)) {
            use_path = &configured_path;
        } else if (std::filesystem::exists(fallback_path)) {
            use_path = &fallback_path;
        } else {
            LAMBO_LOG("ui", "font %s not found (configured=%s, fallback=%s)\n",
                filename, configured_path.string().c_str(), fallback_path.string().c_str());
            return;
        }
        std::ifstream file(*use_path, std::ios::binary);
        state->font_data.emplace_back(std::istreambuf_iterator<char>(file),
                                      std::istreambuf_iterator<char>{});
        const auto& data = state->font_data.back();
        const bool ok = !data.empty() && Rml::LoadFontFace(
            data, "Lato", Rml::Style::FontStyle::Normal, weight, false);
        LAMBO_LOG("ui", "font %s: %s (path=%s)\n",
            filename, ok ? "loaded" : "FAILED", use_path->string().c_str());
    };
    load_font("LatoLatin-Regular.ttf", Rml::Style::FontWeight::Normal);
    load_font("LatoLatin-Bold.ttf", Rml::Style::FontWeight::Bold);
    g_state = std::move(state);
    LAMBO_LOG("ui", "RmlUi render hooks initialised\n");
}

std::optional<lambo::ui::NavigationKey> navigation_key_from_button(uint8_t button) {
    using lambo::ui::NavigationKey;
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A: return NavigationKey::Activate;
        case SDL_CONTROLLER_BUTTON_B: return NavigationKey::Back;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return NavigationKey::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return NavigationKey::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return NavigationKey::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return NavigationKey::Right;
        default: return std::nullopt;
    }
}

int sdl_key_from_navigation(lambo::ui::NavigationKey key) {
    using lambo::ui::NavigationKey;
    switch (key) {
        case NavigationKey::Activate: return SDLK_RETURN;
        case NavigationKey::Back: return SDLK_ESCAPE;
        case NavigationKey::Up: return SDLK_UP;
        case NavigationKey::Down: return SDLK_DOWN;
        case NavigationKey::Left: return SDLK_LEFT;
        case NavigationKey::Right: return SDLK_RIGHT;
        case NavigationKey::None: case NavigationKey::Count: return SDLK_UNKNOWN;
    }
    return SDLK_UNKNOWN;
}

void UiState::process_key_down(int key, bool repeat) {
    if (context == nullptr) return;
    set_input_mode(InputMode::KeyboardOrController);
    if (key == SDLK_ESCAPE) {
        if (!repeat) back();
        return;
    }
    context->ProcessKeyDown(RmlSDL::ConvertKey(key), RmlSDL::GetKeyModifierState());
}

void UiState::process_navigation_events(
    const std::vector<lambo::ui::NavigationEvent>& events) {
    using lambo::ui::NavigationEventType;
    using lambo::ui::NavigationKey;
    for (const auto& event : events) {
        if (event.type == NavigationEventType::Press) {
            set_input_mode(InputMode::KeyboardOrController);
        }
        if (event.key == NavigationKey::Back) {
            if (event.type == NavigationEventType::Press) back();
            continue;
        }
        const int key = sdl_key_from_navigation(event.key);
        if (key == SDLK_UNKNOWN) continue;
        if (event.type == NavigationEventType::Press) {
            context->ProcessKeyDown(RmlSDL::ConvertKey(key), 0);
        } else {
            context->ProcessKeyUp(RmlSDL::ConvertKey(key), 0);
        }
    }
}

void UiState::process_queued_events() {
    QueuedEvent queued{};
    while (g_event_queue.try_dequeue(queued)) {
        SDL_Event& event = queued.event;
        if (context == nullptr) continue;
        switch (event.type) {
            case SDL_KEYDOWN:
                process_key_down(event.key.keysym.sym, event.key.repeat != 0);
                break;
            case SDL_KEYUP:
                context->ProcessKeyUp(RmlSDL::ConvertKey(event.key.keysym.sym),
                                      RmlSDL::GetKeyModifierState());
                break;
            case SDL_CONTROLLERBUTTONDOWN: {
                if (const auto key = navigation_key_from_button(event.cbutton.button)) {
                    process_navigation_events(controller_navigation.button_down(*key, Clock::now()));
                }
                break;
            }
            case SDL_CONTROLLERBUTTONUP: {
                if (const auto key = navigation_key_from_button(event.cbutton.button)) {
                    process_navigation_events(controller_navigation.button_up(*key, Clock::now()));
                }
                break;
            }
            case SDL_CONTROLLERAXISMOTION: {
                std::optional<lambo::ui::NavigationAxis> axis;
                if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                    axis = lambo::ui::NavigationAxis::Horizontal;
                } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    axis = lambo::ui::NavigationAxis::Vertical;
                }
                if (axis.has_value()) {
                    process_navigation_events(
                        controller_navigation.axis_motion(*axis, event.caxis.value, Clock::now()));
                }
                break;
            }
            case SDL_CONTROLLERDEVICEREMOVED:
                controller_navigation.reset();
                break;
            case SDL_MOUSEMOTION:
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEWHEEL:
                set_input_mode(InputMode::Mouse);
                RmlSDL::InputEventHandler(context, event);
                break;
            case SDL_TEXTINPUT:
                set_input_mode(InputMode::KeyboardOrController);
                RmlSDL::InputEventHandler(context, event);
                break;
            default:
                RmlSDL::InputEventHandler(context, event);
                break;
        }
    }
    process_navigation_events(controller_navigation.update(Clock::now()));
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
        g_state->show_page(static_cast<lambo::ui::Page>(requested));
    }
    if (g_requested_back.exchange(false, std::memory_order_acq_rel)) g_state->back();
    g_state->process_queued_events();
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
            SDL_ShowCursor(SDL_ENABLE);
            g_event_queue.enqueue(QueuedEvent{event});
            return true;
        case SDL_KEYDOWN:
        case SDL_TEXTINPUT:
        case SDL_CONTROLLERBUTTONDOWN:
            SDL_ShowCursor(SDL_DISABLE);
            g_event_queue.enqueue(QueuedEvent{event});
            return true;
        case SDL_KEYUP:
        case SDL_CONTROLLERBUTTONUP:
            g_event_queue.enqueue(QueuedEvent{event});
            return true;
        case SDL_CONTROLLERAXISMOTION:
            if ((event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                 event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) &&
                lambo::ui::navigation_axis_active(event.caxis.value)) {
                SDL_ShowCursor(SDL_DISABLE);
            }
            g_event_queue.enqueue(QueuedEvent{event});
            return true;
        case SDL_CONTROLLERDEVICEREMOVED:
            g_event_queue.enqueue(QueuedEvent{event});
            return true;
        case SDL_WINDOWEVENT:
            g_event_queue.enqueue(QueuedEvent{event});
            return true;
        default:
            return false;
    }
}

void open_launcher() {
    g_requested_entry_point.store(static_cast<int>(EntryPoint::Startup), std::memory_order_release);
    SDL_ShowCursor(SDL_ENABLE);
    g_capture.store(true, std::memory_order_release);
    g_requested_page.store(static_cast<int>(Page::Home), std::memory_order_release);
}

void open_settings() {
    g_requested_entry_point.store(static_cast<int>(EntryPoint::InGameOverlay), std::memory_order_release);
    SDL_ShowCursor(SDL_ENABLE);
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

// RT64 renderer context for the pivot runtime — the DEFAULT presenter (#58, flipped
// 2026-07-02; headless harness runs opt out with LAMBO_HEADLESS=1, see lambo_rt64.h).
//
// Adapted from Zelda64Recomp's src/main/rt64_render_context.cpp (MIT), minus the
// texture-pack / mod / UI plumbing. The seam is identical to the headless swrender
// path: ultramodern's gfx thread calls RendererContext::send_dl(OSTask*) with the
// game's real F3DEX (v1) display list; RT64's HLE interpreter auto-detects the ucode
// from the task's ucode/ucode_data pointers and renders via plume (Vulkan on Linux).

#define HLSL_CPU
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>

#include "hle/rt64_application.h"
#include "hle/rt64_state.h"
#include "render/rt64_texture_cache.h"

#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"
#include "ultramodern/renderer_context.hpp"

#include "lambo_rt64.h"
#include "lambo_config.h"
#include "lambo_hud_widescreen.h"

namespace {

// RT64 wants RSP DMEM/IMEM and MI/DPC register storage; the pivot HLEs all of that,
// so hand RT64 dummy backing store exactly like Zelda64Recomp does.
uint8_t DMEM[0x1000];
uint8_t IMEM[0x1000];

uint32_t MI_INTR_REG = 0;
uint32_t DPC_START_REG = 0;
uint32_t DPC_END_REG = 0;
uint32_t DPC_CURRENT_REG = 0;
uint32_t DPC_STATUS_REG = 0;
uint32_t DPC_CLOCK_REG = 0;
uint32_t DPC_BUFBUSY_REG = 0;
uint32_t DPC_PIPEBUSY_REG = 0;
uint32_t DPC_TMEM_REG = 0;

void dummy_check_interrupts() {}

// Live swapchain handle for the background/skybox aspect-ratio fix (issue #3): the
// course frame-builder (func_8004384C) issues its own frustum matrix for the
// background layer with a HARDCODED 4/3 literal (0x3FAAAAAB) baked at the call site,
// so under RT64 Expand the skybox panels stay sized for 4:3 and don't reach the wide
// edges. lambo_ws_get_output_aspect_bits() below lets a patches.hook override that
// literal with the live output aspect each frame. This is the RAW output aspect: a
// symmetric skybox frustum always fills the real screen, so it just needs a wider tangent.
// The 2D HUD geometry shifts (issue #67) do NOT use this -- they honour hr_option's rect
// clamp via lambo_ws_get_hud_rect_aspect_bits() below, since a rect-aligned element and a
// full-screen frustum widen by different amounts under Clamp16x9.
//
// Written on the gfx thread (RT64Context ctor/dtor), read every frame on the CPU/
// game-logic thread inside lambo_ws_get_output_aspect_bits() below -- unlike
// get_resolution_scale() elsewhere in this file, which is only ever called from the
// gfx thread itself. atomic (not a plain pointer) so the CPU thread can't observe a
// torn or stale value; the dtor nulls it before `app` is torn down so a load racing
// shutdown sees either the fully-live pointer or nullptr, never a dangling one.
std::atomic<RT64::Application*> g_lambo_active_app{nullptr};

RT64::UserConfiguration::AspectRatio to_rt64(ultramodern::renderer::AspectRatio option) {
    switch (option) {
        case ultramodern::renderer::AspectRatio::Original:    return RT64::UserConfiguration::AspectRatio::Original;
        case ultramodern::renderer::AspectRatio::Expand:      return RT64::UserConfiguration::AspectRatio::Expand;
        case ultramodern::renderer::AspectRatio::Manual:      return RT64::UserConfiguration::AspectRatio::Manual;
        case ultramodern::renderer::AspectRatio::OptionCount: return RT64::UserConfiguration::AspectRatio::OptionCount;
    }
    return RT64::UserConfiguration::AspectRatio::Original;
}

RT64::UserConfiguration::Antialiasing to_rt64(ultramodern::renderer::Antialiasing option) {
    switch (option) {
        case ultramodern::renderer::Antialiasing::None:        return RT64::UserConfiguration::Antialiasing::None;
        case ultramodern::renderer::Antialiasing::MSAA2X:      return RT64::UserConfiguration::Antialiasing::MSAA2X;
        case ultramodern::renderer::Antialiasing::MSAA4X:      return RT64::UserConfiguration::Antialiasing::MSAA4X;
        case ultramodern::renderer::Antialiasing::MSAA8X:      return RT64::UserConfiguration::Antialiasing::MSAA8X;
        case ultramodern::renderer::Antialiasing::OptionCount: return RT64::UserConfiguration::Antialiasing::OptionCount;
    }
    return RT64::UserConfiguration::Antialiasing::None;
}

RT64::UserConfiguration::RefreshRate to_rt64(ultramodern::renderer::RefreshRate option) {
    switch (option) {
        case ultramodern::renderer::RefreshRate::Original:    return RT64::UserConfiguration::RefreshRate::Original;
        case ultramodern::renderer::RefreshRate::Display:     return RT64::UserConfiguration::RefreshRate::Display;
        case ultramodern::renderer::RefreshRate::Manual:      return RT64::UserConfiguration::RefreshRate::Manual;
        case ultramodern::renderer::RefreshRate::OptionCount: return RT64::UserConfiguration::RefreshRate::OptionCount;
    }
    return RT64::UserConfiguration::RefreshRate::Original;
}

RT64::UserConfiguration::InternalColorFormat to_rt64(ultramodern::renderer::HighPrecisionFramebuffer option) {
    switch (option) {
        case ultramodern::renderer::HighPrecisionFramebuffer::Off:         return RT64::UserConfiguration::InternalColorFormat::Standard;
        case ultramodern::renderer::HighPrecisionFramebuffer::On:          return RT64::UserConfiguration::InternalColorFormat::High;
        case ultramodern::renderer::HighPrecisionFramebuffer::Auto:        return RT64::UserConfiguration::InternalColorFormat::Automatic;
        case ultramodern::renderer::HighPrecisionFramebuffer::OptionCount: return RT64::UserConfiguration::InternalColorFormat::OptionCount;
    }
    return RT64::UserConfiguration::InternalColorFormat::Automatic;
}

void set_application_user_config(RT64::Application* application, const ultramodern::renderer::GraphicsConfig& config) {
    switch (config.res_option) {
        default:
        case ultramodern::renderer::Resolution::Auto:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
            application->userConfig.downsampleMultiplier = 1;
            break;
        case ultramodern::renderer::Resolution::Original:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            application->userConfig.resolutionMultiplier = std::max(config.ds_option, 1);
            application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
            break;
        case ultramodern::renderer::Resolution::Original2x:
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            application->userConfig.resolutionMultiplier = 2.0 * std::max(config.ds_option, 1);
            application->userConfig.downsampleMultiplier = std::max(config.ds_option, 1);
            break;
    }

    switch (config.hr_option) {
        default:
        case ultramodern::renderer::HUDRatioMode::Original:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Original;
            break;
        case ultramodern::renderer::HUDRatioMode::Clamp16x9:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Manual;
            application->userConfig.extAspectTarget = 16.0 / 9.0;
            break;
        case ultramodern::renderer::HUDRatioMode::Full:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Expand;
            break;
    }

    application->userConfig.aspectRatio = to_rt64(config.ar_option);
    application->userConfig.antialiasing = to_rt64(config.msaa_option);
    application->userConfig.refreshRate = to_rt64(config.rr_option);
    application->userConfig.refreshRateTarget = config.rr_manual_value;
    application->userConfig.internalColorFormat = to_rt64(config.hpfb_option);
    application->userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
}

ultramodern::renderer::SetupResult map_setup_result(RT64::Application::SetupResult rt64_result) {
    switch (rt64_result) {
        case RT64::Application::SetupResult::Success:                  return ultramodern::renderer::SetupResult::Success;
        case RT64::Application::SetupResult::DynamicLibrariesNotFound: return ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
        case RT64::Application::SetupResult::InvalidGraphicsAPI:       return ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
        case RT64::Application::SetupResult::GraphicsAPINotFound:      return ultramodern::renderer::SetupResult::GraphicsAPINotFound;
        case RT64::Application::SetupResult::GraphicsDeviceNotFound:   return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
    }
    return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
}

ultramodern::renderer::GraphicsApi map_graphics_api(RT64::UserConfiguration::GraphicsAPI api) {
    switch (api) {
        case RT64::UserConfiguration::GraphicsAPI::D3D12:     return ultramodern::renderer::GraphicsApi::D3D12;
        case RT64::UserConfiguration::GraphicsAPI::Vulkan:    return ultramodern::renderer::GraphicsApi::Vulkan;
        case RT64::UserConfiguration::GraphicsAPI::Metal:     return ultramodern::renderer::GraphicsApi::Metal;
        case RT64::UserConfiguration::GraphicsAPI::Automatic: return ultramodern::renderer::GraphicsApi::Auto;
    }
    return ultramodern::renderer::GraphicsApi::Auto;
}

class RT64Context final : public ultramodern::renderer::RendererContext {
public:
    RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool debug) {
        static unsigned char dummy_rom_header[0x40];

        // Wire the RT64 application core to the pivot runtime's state.
        RT64::Application::Core appCore{};
#if defined(_WIN32)
        appCore.window = window_handle.window;
#elif defined(__linux__) || defined(__ANDROID__)
        appCore.window = window_handle;
#elif defined(__APPLE__)
        appCore.window.window = window_handle.window;
        appCore.window.view = window_handle.view;
#endif

        appCore.checkInterrupts = dummy_check_interrupts;

        appCore.HEADER = dummy_rom_header;
        appCore.RDRAM = rdram; // N64Recomp/mupen byte-order convention -- RT64's native diet
        appCore.DMEM = DMEM;
        appCore.IMEM = IMEM;

        appCore.MI_INTR_REG = &MI_INTR_REG;

        appCore.DPC_START_REG = &DPC_START_REG;
        appCore.DPC_END_REG = &DPC_END_REG;
        appCore.DPC_CURRENT_REG = &DPC_CURRENT_REG;
        appCore.DPC_STATUS_REG = &DPC_STATUS_REG;
        appCore.DPC_CLOCK_REG = &DPC_CLOCK_REG;
        appCore.DPC_BUFBUSY_REG = &DPC_BUFBUSY_REG;
        appCore.DPC_PIPEBUSY_REG = &DPC_PIPEBUSY_REG;
        appCore.DPC_TMEM_REG = &DPC_TMEM_REG;

        // VI registers come from ultramodern's live VI state (the game's osViSwapBuffer
        // path writes these), so RT64 can locate the color framebuffer origin.
        ultramodern::renderer::ViRegs* vi_regs = ultramodern::renderer::get_vi_regs();
        appCore.VI_STATUS_REG = &vi_regs->VI_STATUS_REG;
        appCore.VI_ORIGIN_REG = &vi_regs->VI_ORIGIN_REG;
        appCore.VI_WIDTH_REG = &vi_regs->VI_WIDTH_REG;
        appCore.VI_INTR_REG = &vi_regs->VI_INTR_REG;
        appCore.VI_V_CURRENT_LINE_REG = &vi_regs->VI_V_CURRENT_LINE_REG;
        appCore.VI_TIMING_REG = &vi_regs->VI_TIMING_REG;
        appCore.VI_V_SYNC_REG = &vi_regs->VI_V_SYNC_REG;
        appCore.VI_H_SYNC_REG = &vi_regs->VI_H_SYNC_REG;
        appCore.VI_LEAP_REG = &vi_regs->VI_LEAP_REG;
        appCore.VI_H_START_REG = &vi_regs->VI_H_START_REG;
        appCore.VI_V_START_REG = &vi_regs->VI_V_START_REG;
        appCore.VI_V_BURST_REG = &vi_regs->VI_V_BURST_REG;
        appCore.VI_X_SCALE_REG = &vi_regs->VI_X_SCALE_REG;
        appCore.VI_Y_SCALE_REG = &vi_regs->VI_Y_SCALE_REG;

        RT64::ApplicationConfiguration appConfig;
        appConfig.appId = "lamborghini-recomp";
        appConfig.useConfigurationFile = false;

        app = std::make_unique<RT64::Application>(appCore, appConfig);

        auto& cur_config = ultramodern::renderer::get_graphics_config();
        set_application_user_config(app.get(), cur_config);
        app->userConfig.developerMode = debug;
        // Force gbi depth branches to prevent LODs from kicking in (Zelda64Recomp default).
        app->enhancementConfig.f3dex.forceBranch = true;
        // Scale LODs based on the output resolution.
        app->enhancementConfig.textureLOD.scale = true;

        switch (cur_config.api_option) {
            case ultramodern::renderer::GraphicsApi::D3D12:  app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12; break;
            case ultramodern::renderer::GraphicsApi::Vulkan: app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan; break;
            case ultramodern::renderer::GraphicsApi::Metal:  app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Metal; break;
            default:                                         app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Automatic; break;
        }

        uint32_t thread_id = 0;
#ifdef _WIN32
        thread_id = window_handle.thread_id;
#endif
        setup_result = map_setup_result(app->setup(thread_id));
        chosen_api = map_graphics_api(app->chosenGraphicsAPI);
        if (setup_result != ultramodern::renderer::SetupResult::Success) {
            std::fprintf(stderr, "[rt64] RT64::Application::setup FAILED (SetupResult=%d)\n",
                         (int)setup_result);
            app = nullptr;
            return;
        }

        std::fprintf(stderr, "[rt64] RT64 renderer initialised (api=%d)\n", (int)chosen_api);

        // Publish the app pointer for the skybox aspect-ratio override
        // (lambo_ws_get_output_aspect_bits reads this on the game-logic thread),
        // then wire texture replacement. Order matters: register first so the
        // dtor's reverse-order unpublish still fires while `app` is alive.
        g_lambo_active_app = app.get();

        // Texture replacement wiring (issue #9). RT64 already owns the whole
        // dump/hash/replace machinery; the port just points it at directories. Both
        // are opt-in (empty path = off) and independent of developerMode, so an
        // end-user pack loads without the F1 developer overlay.
        const std::string dump_dir = lambo::config::texture_dump_dir();
        if (!dump_dir.empty()) {
            // Setting this non-empty makes TextureManager::dumpTexture write every
            // uploaded texture (raw TMEM + RDRAM + tile JSON) to the directory.
            app->state->dumpingTexturesDirectory = std::filesystem::path(dump_dir);
            std::fprintf(stderr, "[rt64] texture dump enabled -> %s\n", dump_dir.c_str());
        }

        const std::string pack = lambo::config::texture_pack_path();
        if (!pack.empty()) {
            const bool ok = app->textureCache->loadReplacementDirectory(
                RT64::ReplacementDirectory(std::filesystem::path(pack)));
            std::fprintf(stderr, "[rt64] texture pack %s: %s\n",
                         ok ? "loaded" : "FAILED to load", pack.c_str());
        }
    }

    ~RT64Context() override {
        if (g_lambo_active_app == app.get()) {
            g_lambo_active_app = nullptr;
        }
    }

    bool valid() override { return static_cast<bool>(app); }

    bool update_config(const ultramodern::renderer::GraphicsConfig& old_config,
                       const ultramodern::renderer::GraphicsConfig& new_config) override {
        if (old_config == new_config) {
            return false;
        }
        // wm_option (fullscreen) is deliberately NOT handled here: the SDL window is
        // owned by the main thread (toggle_fullscreen in main.cpp), and RT64's
        // setFullScreen would fight SDL over the same HWND.
        set_application_user_config(app.get(), new_config);
        app->updateUserConfig(true);
        if (new_config.msaa_option != old_config.msaa_option) {
            app->updateMultisampling();
        }
        return true;
    }

    void enable_instant_present() override {
        app->enhancementConfig.presentation.mode = RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;
        app->updateEnhancementConfig();
    }

    void send_dl(const OSTask* task) override {
        static int count = 0;
        if (++count == 1) {
            std::fprintf(stderr,
                         "[rt64] first send_dl: ucode=0x%08x ucode_data=0x%08x dl=0x%08x\n",
                         (uint32_t)task->t.ucode, (uint32_t)task->t.ucode_data,
                         (uint32_t)task->t.data_ptr);
        }
        app->state->rsp->reset();
        app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
        app->processDisplayLists(app->core.RDRAM, task->t.data_ptr & 0x3FFFFFF, 0, true);
        // Same sustained-pipeline heartbeat as the headless context, so RT64 runs are
        // comparable against headless logs. VI_ORIGIN/STATUS prove the present path is
        // scanning out the game's REAL framebuffer (via the promote_vi_context bridge),
        // not the pre-game dummy at 0x80700000 / a blanked STATUS of 0.
        if (count % 30 == 0) {
            const ultramodern::renderer::ViRegs* vr = ultramodern::renderer::get_vi_regs();
            // Interpolation health (#1 display-rate rendering): viOriginalRate is the game's
            // detected update rate (30 for this title), targetRate the present pace RT64 aims
            // for (display Hz when RefreshRate::Display), and interp count/presented the
            // per-workload synthesized-frame counters -- count ~= targetRate/viOriginalRate
            // when interpolation is live; 0 means RT64 is presenting game frames raw.
            //
            // DIAGNOSTICS-GRADE SAMPLING: interpolatedMutex synchronises with the present
            // queue's counter updates, but RT64's workload thread writes the index/rate
            // fields WITHOUT any lock (rt64_workload_queue.cpp:1020-1044, :237), so a
            // fully synchronised read is impossible without patching the submodule. These
            // are aligned word loads sampled once per second for a log line -- treat a
            // single odd line as sampling noise, only a sustained pattern as signal.
            RT64::SharedQueueResources* sq = app->sharedQueueResources.get();
            uint32_t vi_rate, target_rate, swap_hz, interp_count, interp_presented;
            {
                std::lock_guard<std::mutex> lock(sq->interpolatedMutex);
                const RT64::InterpolatedFrameCounters& fc =
                    sq->interpolatedFrames[sq->interpolatedFramesIndex];
                vi_rate = sq->viOriginalRate;
                target_rate = sq->targetRate;
                swap_hz = sq->swapChainRate;
                interp_count = fc.count;
                interp_presented = fc.presented;
            }
            std::fprintf(stderr,
                         "[rt64] send_dl count=%d VI_ORIGIN=0x%08x VI_STATUS=0x%04x VI_WIDTH=%u"
                         " | viRate=%u targetRate=%u swapHz=%u interp count=%u presented=%u\n",
                         count, vr->VI_ORIGIN_REG, vr->VI_STATUS_REG, vr->VI_WIDTH_REG,
                         vi_rate, target_rate, swap_hz, interp_count, interp_presented);
        }
    }

    void update_screen() override {
        app->updateScreen();
    }

    void shutdown() override {
        if (app != nullptr) {
            app->end();
        }
    }

    uint32_t get_display_framerate() const override {
        return app->presentQueue->ext.sharedResources->swapChainRate;
    }

    float get_resolution_scale() const override {
        constexpr int ReferenceHeight = 240;
        switch (app->userConfig.resolution) {
            case RT64::UserConfiguration::Resolution::WindowIntegerScale:
                if (app->sharedQueueResources->swapChainHeight > 0) {
                    return std::max(float((app->sharedQueueResources->swapChainHeight + ReferenceHeight - 1) / ReferenceHeight), 1.0f);
                }
                return 1.0f;
            case RT64::UserConfiguration::Resolution::Manual:
                return float(app->userConfig.resolutionMultiplier);
            case RT64::UserConfiguration::Resolution::Original:
            default:
                return 1.0f;
        }
    }

private:
    std::unique_ptr<RT64::Application> app;
};

} // anonymous namespace

// Native for the issue #3 patches.hook (lamborghini.us.toml, func_8004384C): returns
// the live output aspect ratio as raw float bits, clamped to never go BELOW 4/3 so a
// narrower-than-4:3 window (or ar_option != Expand) degenerates to the original
// constant rather than squeezing the skybox. Called from recompiled game code on the
// game-logic thread, which races the gfx/render thread's swapchain resize handling;
// take the same configurationMutex RT64::SharedQueueResources::setSwapChainSize()
// locks when writing width+height together, so this never observes a torn pair.
extern "C" uint32_t lambo_ws_get_output_aspect_bits(void) {
    float aspect = 4.0f / 3.0f;
    const auto& cfg = ultramodern::renderer::get_graphics_config();
    RT64::Application* active_app = g_lambo_active_app.load(std::memory_order_acquire);
    if (cfg.ar_option == ultramodern::renderer::AspectRatio::Expand &&
        active_app != nullptr && active_app->sharedQueueResources) {
        auto& shared = *active_app->sharedQueueResources;
        std::scoped_lock<std::mutex> configuration_lock(shared.configurationMutex);
        uint32_t w = shared.swapChainWidth;
        uint32_t h = shared.swapChainHeight;
        if (w > 0 && h > 0) {
            float live = float(w) / float(h);
            if (live > aspect) {
                aspect = live;
            }
        }
    }
    uint32_t bits;
    std::memcpy(&bits, &aspect, sizeof(bits));
    return bits;
}

// (issue #67) Effective aspect the extended-GBI HUD rect pins travel to, as raw float
// bits. DISTINCT from lambo_ws_get_output_aspect_bits() above: that is the skybox's raw
// output aspect (#3, a symmetric frustum that always fills the real screen), whereas the
// gEXSetRectAlign HUD pins honour hr_option -- Full reaches the real edges, Clamp16x9
// stops at 16:9, Original doesn't move -- so the game-space HUD geometry shifts
// (src/lambo_hud_widescreen.c) key off THIS. Keying them off the raw output aspect would
// over-translate the geometry past the rects at any non-Full ultrawide output (e.g. the
// shipped Clamp16x9 default on a 21:9 monitor). Mirrors set_application_user_config()'s
// hr_option map plus the extAspectPercentage math in rt64_workload_queue.cpp:159-183.
// Same thread-safety contract and 4/3 floor as the skybox helper above.
extern "C" uint32_t lambo_ws_get_hud_rect_aspect_bits(void) {
    const float source = 4.0f / 3.0f;
    float aspect = source;
    const auto& cfg = ultramodern::renderer::get_graphics_config();
    RT64::Application* active_app = g_lambo_active_app.load(std::memory_order_acquire);
    if (cfg.ar_option == ultramodern::renderer::AspectRatio::Expand &&
        active_app != nullptr && active_app->sharedQueueResources) {
        auto& shared = *active_app->sharedQueueResources;
        std::scoped_lock<std::mutex> configuration_lock(shared.configurationMutex);
        uint32_t w = shared.swapChainWidth;
        uint32_t h = shared.swapChainHeight;
        if (w > 0 && h > 0) {
            float display = float(w) / float(h);
            if (display > source) {
                float ext_percentage;
                switch (cfg.hr_option) {
                    case ultramodern::renderer::HUDRatioMode::Full:
                        ext_percentage = 1.0f;
                        break;
                    case ultramodern::renderer::HUDRatioMode::Clamp16x9:
                        ext_percentage = lambo_ws_hud_clamp_ext_percentage(
                            display, source, 16.0f / 9.0f);
                        break;
                    case ultramodern::renderer::HUDRatioMode::Original:
                    default:
                        ext_percentage = 0.0f;
                        break;
                }
                aspect = lambo_ws_hud_effective_rect_aspect(display, source, ext_percentage);
            }
        }
    }
    uint32_t bits;
    std::memcpy(&bits, &aspect, sizeof(bits));
    return bits;
}

namespace lambo_rt64 {

bool enabled() {
    const char* v = std::getenv("LAMBO_HEADLESS");
    return !(v != nullptr && v[0] == '1');
}

std::unique_ptr<ultramodern::renderer::RendererContext>
create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle,
                      bool developer_mode) {
    auto ctx = std::make_unique<RT64Context>(rdram, window_handle, developer_mode);
    if (!ctx->valid()) {
        return nullptr;
    }
    return ctx;
}

} // namespace lambo_rt64

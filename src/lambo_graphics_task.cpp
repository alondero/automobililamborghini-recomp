// USA producer hook at 0x80005728, immediately before osSendMesg. Both renderers
// consume the corrected commands; neither renderer writes HUD/fog words anymore.
// The scheduler's task-reuse fence has released this slot to the guest builder.
// Asset loading and list construction precede publication on that guest thread.
// Layout, microcode identity, DMA limits and replacement plan: docs/HUD.md.
#include <chrono>
#include "lambo_f3dex.h"
#include "lambo_fog_widescreen.h"
#include "lambo_hud_texture_bounds.h"
#include "lambo_log.h"

extern "C" void lambo_graphics_task_prepare(uint8_t* ram, uint32_t task_address) {
    if (!ram) return;
    const auto task = task_address & 0x1fffffff;
    // OSTask is at scheduler-message + 0x10, i.e. builder task pointer - 0x58.
    // All fields below are native u32 words in word-swapped RAM, sizes in bytes.
    if (task != 0xbf1e8 && task != 0xc6c38) return;
    const auto field = [&](uint32_t offset) { return lambo::f3dex::read(ram, task + offset); };
    const uint32_t root = field(0x30) & 0x1fffffff;
    // Only the USA F3DEX v1 code/data pair emitted by 0x800055D0..0x80005620.
    // ucode_size (+0x14) is not authored by this builder and is not an identity.
    // Launcher, audio and unknown microcodes fail closed before list decoding.
    if (field(0) != 1 || (field(0x10) & 0x1fffffff) != 0x86b60 ||
        (field(0x18) & 0x1fffffff) != 0x8f210 ||
        field(0x1c) != 0x800 || root != task + 0x218) return;

    static uint64_t sequence = 0; // Guest producer only, diagnostic sampling.
    const bool timed = lambo_log_would_emit(LAMBO_LEVEL_DEBUG) && (++sequence <= 12 || sequence % 60 == 0);
    using Clock = std::chrono::steady_clock;
    const auto start = timed ? Clock::now() : Clock::time_point{};
    lambo::HudTextureBounds hud(ram);
    const auto fog = lambo::fog_policy(ram);
    const auto result = lambo::f3dex::walk(ram, root | 0x80000000u,
        [&](uint32_t offset, uint32_t w0, uint32_t w1) {
            hud.command(offset, w0, w1);
            fog.command(ram, offset, w0, w1);
        });
    if (result.budget_limit || result.depth_limit) {
        LAMBO_LOG_WARN("gfx-policy", "walk stopped: budget=%d depth=%d root=0x%08x commands=%u\n",
            result.budget_limit, result.depth_limit, root, result.commands);
    }
    if (timed) {
        const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
        LAMBO_LOG("gfx-policy", "prepare commands=%u dial=%u maps=%u suffix=%u elapsed_us=%.3f\n",
            result.commands, hud.dial, hud.maps, hud.suffix, us);
    }
}

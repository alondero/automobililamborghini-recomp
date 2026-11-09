// SPDX-License-Identifier: GPL-3.0-or-later
// Lock scopes for one main-thread frame pump (update_gfx_stub in main.cpp).
// The presentation thread holds the frontend mutex for every UI draw, so a
// phase that can block on the OS or a device must not run while the pump holds
// it. The host supplies the phases:
//   pump_audio()       SDL audio-device lifecycle. lambo_audio has its own locks
//                      and never touches recompui or recompinput state.
//   collect_events()   SDL_PollEvent into a local batch. SDL can block in here:
//                      the Windows move/size loop, or Android while paused.
//   update_frontend()  event dispatch, window reconciliation, input and rumble.
//                      These read or write state that settings callbacks change
//                      during a draw: recompui/recompinput, the graphics config
//                      snapshot, the driving settings and tilt state.
//   show_advisory()    modal message box; blocks until the player dismisses it.
#ifndef LAMBO_FRAME_PUMP_H
#define LAMBO_FRAME_PUMP_H

#include <mutex>

namespace lambo::frame_pump {

template <typename Mutex, typename Host>
void run(Mutex& frontend_mutex, Host& host) {
    host.pump_audio();
    host.collect_events();
    {
        std::lock_guard<Mutex> lock(frontend_mutex);
        host.update_frontend();
    }
    host.show_advisory();
}

} // namespace lambo::frame_pump

#endif // LAMBO_FRAME_PUMP_H

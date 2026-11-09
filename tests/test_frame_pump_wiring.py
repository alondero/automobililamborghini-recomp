#!/usr/bin/env python3
"""Ties main.cpp's frame pump to the lock scopes in src/lambo_frame_pump.h (issue #401).

tests/test_frame_pump.cpp proves which phases run under the frontend mutex. This
checks that the blocking calls live in the phases that run without it, so moving
SDL_PollEvent or the audio pump back into update_frontend() fails here.
"""

import sys
from pathlib import Path

try:  # unittest discovery imports this as tests.test_frame_pump_wiring
    from tests.test_quit_path import function_body, require
except ImportError:  # ctest runs it as a script from tests/
    from test_quit_path import function_body, require


def main() -> None:
    root = Path(sys.argv[1]).resolve()
    source = (root / "src" / "main.cpp").read_text(encoding="utf-8")

    stub = function_body(source, "static void update_gfx_stub(void*")
    require("lambo::frame_pump::run(lambo::ui::frontend_mutex()" in stub,
            "update_gfx_stub must run the pump through lambo::frame_pump::run")
    require("lock" not in stub.replace("frame_pump::run", ""),
            "update_gfx_stub must not take the frontend mutex itself")

    unlocked = {
        "void pump_audio()": "lambo::audio::pump()",
        "void collect_events()": "SDL_PollEvent(",
        "void show_advisory()": "SDL_ShowSimpleMessageBox(",
    }
    for signature, call in unlocked.items():
        require(call in function_body(source, signature),
                f"{call} must run in {signature}, outside the frontend mutex")
        require(source.count(call) == 1,
                f"{call} must appear only in {signature}")


if __name__ == "__main__":
    main()

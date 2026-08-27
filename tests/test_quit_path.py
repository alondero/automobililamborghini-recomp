#!/usr/bin/env python3

import sys
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening_brace = source.index("{", start)
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening_brace + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def main() -> None:
    source = (Path(sys.argv[1]).resolve() / "src" / "main.cpp").read_text(encoding="utf-8")
    exit_body = function_body(source, "static void application_exit_success()")

    # This function runs on librecomp's primary thread while RT64 can still invoke
    # draw hooks from its presentation thread. It must not touch RmlUi or RT64 before
    # process termination (issue #163).
    require("std::_Exit(0)" in exit_body,
            "the window-close path must terminate without falling into runtime teardown")
    require("lambo::ui::shutdown" not in exit_body,
            "the window-close path must not destroy UI state while RT64 may render it")

    event_loop = function_body(source, "static void update_gfx_stub(void*")
    require("event.type == SDL_QUIT" in event_loop and
            "SDL_WINDOWEVENT_CLOSE" in event_loop and
            "application_exit_success();" in event_loop,
            "SDL quit and window-close events must use the immediate safe exit path")


if __name__ == "__main__":
    main()

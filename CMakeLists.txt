cmake_minimum_required(VERSION 3.20)

# Automobili Lamborghini: Recompiled — a static-recompilation PC port built on
# N64ModernRuntime (ultramodern + librecomp) with the RT64 renderer.
#
# See BUILDING.md for full instructions. In short, from the repo root:
#
#   # 1. Recompile the game code from YOUR ROM (produces RecompiledFuncs/):
#   python3 scripts/gen_syms_toml.py
#   ./lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp lamborghini.us.toml
#
#   # 2. Configure + build (Linux example; MinGW/Windows details in BUILDING.md):
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --target lamborghini_modern -j
#
# Run from the repo root: the ROM path in lamborghini.us.toml is cwd-relative.
# The RT64/plume MinGW patches (patches/0004+0005) are applied to the submodule
# working tree on Windows; DirectX-Headers v1.614.1 is fetched at configure time.
# RecompiledFuncs/ is gitignored — it is regenerated from your ROM (step 1).

project(lamborghini_modern VERSION 0.5.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(N64MR ${CMAKE_CURRENT_SOURCE_DIR}/lib/N64ModernRuntime)

# --- RT64 renderer (#58: DEFAULT presenter, hard build dependency since 2026-07-02) ---
# recomp/lib/rt64 is a pinned git submodule; RT64 is always compiled in and is the
# default presenter at runtime (LAMBO_HEADLESS=1 is the harness knob that keeps the
# headless swrender, the RDRAM-framebuffer measurement instrument for the port-vs-ares
# harness). Wiring mirrors Zelda64Recomp's CMakeLists.txt (the de-facto template).
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/CMakeLists.txt)
    message(FATAL_ERROR "lib/rt64 submodule is missing. Initialise it first:\n"
        "  git submodule update --init --recursive")
endif()

if(BUILD_TESTING)
    find_program(LAMBO_PYTHON_EXECUTABLE NAMES python python3)

    add_executable(lambo_config_tests
        tests/test_live_config.cpp
        src/lambo_config.cpp
        src/lambo_player_name.cpp
        src/lambo_log.cpp
    )
    target_include_directories(lambo_config_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${N64MR}/ultramodern/include
        ${N64MR}/N64Recomp/include
        ${N64MR}/thirdparty
    )
    add_test(NAME lambo_config_live_updates COMMAND lambo_config_tests)

    add_executable(lambo_camera_projection_tests
        tests/test_camera_projection.cpp
    )
    target_include_directories(lambo_camera_projection_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME lambo_camera_projection_policy COMMAND lambo_camera_projection_tests)

    add_executable(lambo_controller_pak_tests
        tests/test_controller_pak.c
        src/libultra_stubs.c
    )
    target_include_directories(lambo_controller_pak_tests PRIVATE
        ${N64MR}/N64Recomp/include
    )
    add_test(NAME lambo_controller_pak_identity COMMAND lambo_controller_pak_tests)

    add_executable(lambo_startup_tests
        tests/test_startup.cpp
        src/lambo_startup.cpp
    )
    target_include_directories(lambo_startup_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME lambo_startup_state_machine COMMAND lambo_startup_tests)

    if(WIN32)
        add_executable(lambo_rdram_allocation_tests
            tests/test_rdram_allocation.cpp
            ${N64MR}/librecomp/src/rdram_memory.cpp
        )
        target_include_directories(lambo_rdram_allocation_tests PRIVATE
            ${N64MR}/librecomp/include
        )
        target_compile_definitions(lambo_rdram_allocation_tests PRIVATE NOMINMAX)
        add_test(NAME lambo_windows_rdram_reservation COMMAND lambo_rdram_allocation_tests)
    endif()

    add_executable(lambo_input_gate_tests
        tests/test_input_gate.cpp
        src/lambo_input_gate.cpp
    )
    target_include_directories(lambo_input_gate_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME lambo_input_capture_gate COMMAND lambo_input_gate_tests)

    add_executable(lambo_controls_tests
        tests/test_controls.cpp
        src/controls/lambo_controls.cpp
        src/lambo_config.cpp
        src/lambo_log.cpp
    )
    target_include_directories(lambo_controls_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${N64MR}/ultramodern/include
        ${N64MR}/thirdparty
    )
    add_test(NAME lambo_controls_domain_and_persistence COMMAND lambo_controls_tests)

    add_executable(lambo_controls_capture_tests
        tests/test_controls_capture.cpp
        src/controls/lambo_controls.cpp
        src/lambo_config.cpp
        src/lambo_log.cpp
    )
    target_include_directories(lambo_controls_capture_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${N64MR}/ultramodern/include
        ${N64MR}/thirdparty
    )
    add_test(NAME lambo_controls_capture_state_machine COMMAND lambo_controls_capture_tests)

    add_executable(lambo_ui_controls_tests
        tests/test_ui_controls.cpp
        src/ui/lambo_ui_controls.cpp
        src/controls/lambo_controls.cpp
        src/lambo_config.cpp
        src/lambo_log.cpp
    )
    target_include_directories(lambo_ui_controls_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${N64MR}/ultramodern/include
        ${N64MR}/thirdparty
    )
    add_test(NAME lambo_ui_controls_actions COMMAND lambo_ui_controls_tests)

    add_executable(lambo_analog_throttle_tests
        tests/test_analog_throttle.cpp
        src/lambo_analog_throttle.cpp
    )
    target_include_directories(lambo_analog_throttle_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${N64MR}/N64Recomp/include
    )
    add_test(NAME lambo_analog_throttle_bridge COMMAND lambo_analog_throttle_tests)

    add_executable(lambo_analog_brake_tests
        tests/test_analog_brake.cpp
        src/lambo_analog_brake.cpp
    )
    target_include_directories(lambo_analog_brake_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${N64MR}/N64Recomp/include
    )
    add_test(NAME lambo_analog_brake_bridge COMMAND lambo_analog_brake_tests)

    add_executable(lambo_ui_input_tests
        tests/test_ui_input.cpp
        src/ui/lambo_ui_input.cpp
    )
    target_include_directories(lambo_ui_input_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME lambo_ui_controller_navigation COMMAND lambo_ui_input_tests)

    add_executable(lambo_ui_settings_tests
        tests/test_ui_settings.cpp
        src/ui/lambo_ui_settings.cpp
        src/lambo_config.cpp
        src/lambo_log.cpp
    )
    target_include_directories(lambo_ui_settings_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${N64MR}/ultramodern/include
        ${N64MR}/thirdparty
    )
    add_test(NAME lambo_ui_settings_bindings COMMAND lambo_ui_settings_tests)

    if(LAMBO_PYTHON_EXECUTABLE)
        add_test(NAME lambo_ui_asset_references
            COMMAND ${LAMBO_PYTHON_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ui_assets.py
                ${CMAKE_CURRENT_SOURCE_DIR}
        )
        add_test(NAME lambo_quit_path
            COMMAND ${LAMBO_PYTHON_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_quit_path.py
                ${CMAKE_CURRENT_SOURCE_DIR}
        )
    else()
        message(WARNING "Python not found; Python-based tests will not be registered")
    endif()
endif()

# RmlUi is the only UI toolkit used by the launcher. Keep the dependency pinned
# as a submodule and build only the core/debugger libraries; the SDL backend is
# compiled into the game target below so it shares the game's SDL instance.
set(BUILD_SHARED_LIBS OFF)
set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
if(WIN32)
    # Reuse RT64's pinned FreeType build for RmlUi text rendering. The Windows
    # SDK bundle does not necessarily provide a CMake package of its own.
    set(LAMBO_FREETYPE_ROOT
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/mupen64plus-win32-deps/freetype-2.13.0)
    set(FREETYPE_INCLUDE_DIR_ft2build ${LAMBO_FREETYPE_ROOT}/include CACHE PATH "" FORCE)
    set(FREETYPE_INCLUDE_DIR_freetype2 ${LAMBO_FREETYPE_ROOT}/include CACHE PATH "" FORCE)
    set(FREETYPE_LIBRARY ${LAMBO_FREETYPE_ROOT}/lib/x64/freetype.lib CACHE FILEPATH "" FORCE)
    set(FREETYPE_LIBRARY_RELEASE ${LAMBO_FREETYPE_ROOT}/lib/x64/freetype.lib CACHE FILEPATH "" FORCE)
endif()
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/lib/RmlUi/CMakeLists.txt)
    message(FATAL_ERROR "lib/RmlUi submodule is missing. Initialise it first:\n"
        "  git submodule update --init --recursive")
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/RmlUi ${CMAKE_BINARY_DIR}/RmlUi)
# GCC 16+ stopped pulling <cstdint> into <limits>/<algorithm>/<cstdlib>
# transitively; RmlUi's vendored robin_hood.h relies on that and fails with
# "Unsupported bitness" at the SIZE_MAX check (see release flow notes).
# Force-include the standard integer header for every C++ translation unit
# that includes an RmlUi header. Apply per-target so it covers:
#   * rmlui_core / rmlui_debugger (STATIC libraries built by RmlUi's own CMakeLists
#     whose target_compile_options already lock in -Wall -Wextra -pedantic — adding
#     CMAKE_CXX_FLAGS at the parent level doesn't reach these targets because
#     they're created before our parent CMakeLists resumes)
#   * the SDL backend file compiled DIRECTLY into lamborghini_modern (we flag
#     lamborghini_modern below in the target's own target_compile_options)
foreach(_rmlui_target rmlui_core rmlui_debugger)
    if(TARGET ${_rmlui_target})
        target_compile_options(${_rmlui_target} PRIVATE -include cstdint)
    endif()
endforeach()

set(RT64_STATIC TRUE)
if(WIN32)
    # Windows (#68 native build): RT64 uses its D3D12 backend + a plain Win32 window
    # (HWND handed over via SDL_GetWindowWMInfo in create_window_stub), so no SDL
    # Vulkan surface. SDL2 comes from rt64's own bundled Windows deps — the same
    # tree rt64's CMake points itself at (mupen64plus-win32-deps/SDL2-2.26.3).
    set(SDL2_WIN32_DEPS ${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3)
    set(SDL2_INCLUDE_DIRS ${SDL2_WIN32_DEPS}/include)
    set(SDL2_LIBRARIES SDL2 SDL2main)
    link_directories(${SDL2_WIN32_DEPS}/lib/x64)
    # MinGW's bundled d3d12.h is years behind the D3D12 SDK: plume needs
    # D3D12_HEAP_TYPE_GPU_UPLOAD and D3D12MemAlloc needs <dxguids.h> (its own
    # MinGW support path, D3D12MA #71). Microsoft's DirectX-Headers package is
    # the official fix and supports MinGW; put its d3d12.h AHEAD of the MinGW
    # system header and link the GUID definitions library everywhere.
    include(FetchContent)
    FetchContent_Declare(DirectX-Headers
        GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers
        GIT_TAG v1.614.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(DirectX-Headers)
    include_directories(BEFORE
        ${directx-headers_SOURCE_DIR}/include
        ${directx-headers_SOURCE_DIR}/include/directx
        ${directx-headers_SOURCE_DIR}/include/dxguids)
else()
    set(RT64_SDL_WINDOW_VULKAN TRUE) # non-Windows: RT64 expects an SDL window + Vulkan surface
    # rt64's vendored nativefiledialog-extended defaults to the GTK3 backend; this
    # box (WSL Ubuntu 24.04) has no libgtk-3-dev but does have dbus-1, so use the
    # xdg-desktop-portal backend instead.
    set(NFD_PORTAL ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64 ${CMAKE_BINARY_DIR}/rt64)
# Generated shader headers land in the rt64 binary dir (Zelda64Recomp does the same).
target_include_directories(rt64 PRIVATE ${CMAKE_BINARY_DIR}/rt64/src)
if(WIN32)
    # GUID definitions for GCC's __uuidof emulation (see the DirectX-Headers
    # block above). Scoped per-target: a global link_libraries() leaks into
    # zstd's install-export set and breaks the CMake generate step.
    target_link_libraries(plume PRIVATE DirectX-Guids)
    # dwmapi (imgui_impl_win32) + d3dcompiler (imgui_impl_dx12's D3DCompile):
    # both are pulled in via #pragma comment(lib,...) which only MSVC honours;
    # MinGW needs the explicit link.
    target_link_libraries(rt64 DirectX-Guids dwmapi d3dcompiler)
    if(NOT MSVC)
        # MinGW: force-include the __mingw_uuidof compat header into every C++
        # TU of the D3D12-touching targets (why: see the header itself).
        set(DX12_UUID_COMPAT ${CMAKE_CURRENT_SOURCE_DIR}/src/mingw_dx12_uuid_compat.h)
        foreach(dx12_target plume rt64)
            target_compile_options(${dx12_target} PRIVATE
                $<$<COMPILE_LANGUAGE:CXX>:-include${DX12_UUID_COMPAT}>)
        endforeach()
    endif()
endif()
if(NOT WIN32)
    find_package(SDL2 REQUIRED)
endif()

if(BUILD_TESTING)
    add_executable(lambo_controls_sdl_tests
        tests/test_controls_sdl.cpp
        src/controls/lambo_controls.cpp
        src/controls/lambo_controls_sdl.cpp
        src/lambo_config.cpp
        src/lambo_log.cpp
    )
    target_include_directories(lambo_controls_sdl_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${N64MR}/ultramodern/include
        ${N64MR}/thirdparty
        ${SDL2_INCLUDE_DIRS}
    )
    if(WIN32)
        target_compile_definitions(lambo_controls_sdl_tests PRIVATE SDL_MAIN_HANDLED)
        target_link_libraries(lambo_controls_sdl_tests PRIVATE SDL2)
    else()
        target_link_libraries(lambo_controls_sdl_tests PRIVATE ${SDL2_LIBRARIES})
    endif()
    add_test(NAME lambo_controls_sdl_registry COMMAND lambo_controls_sdl_tests)
endif()

# ultramodern + librecomp, which cascades N64Recomp / LiveRecomp / miniz.
# The runtime root CMake (lib/N64ModernRuntime/CMakeLists.txt) deliberately
# omits RT64 — it is pulled in only by a game's own CMake.
add_subdirectory(${N64MR})

# --- RecompiledFuncs: the whole-ROM recompiled image as a static library -----
# The generated funcs #include "recomp.h" (N64Recomp/include), "funcs.h"
# (resolved relative to each TU in RecompiledFuncs/), and lookup.cpp is picked
# up by the *.cpp glob. recomp_overlays.inl is an include (NOT a TU) and is
# intentionally excluded by the *.c/*.cpp globs.
#
# Skipped when empty so the first configure (which builds the recompiler
# tools) succeeds before N64Recomp populates this directory.
file(GLOB FUNC_C_SOURCES   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs/*.c)
file(GLOB FUNC_CXX_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs/*.cpp)
if(FUNC_C_SOURCES OR FUNC_CXX_SOURCES)
    add_library(RecompiledFuncs STATIC)
    target_sources(RecompiledFuncs PRIVATE ${FUNC_C_SOURCES} ${FUNC_CXX_SOURCES})

    target_compile_options(RecompiledFuncs PRIVATE
        -fno-strict-aliasing
        -ggdb
        -Wno-unused-variable
        # implicit-function-declaration is a C-only diagnostic; lookup.cpp is C++.
        $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>
    )

    target_include_directories(RecompiledFuncs PRIVATE
        ${N64MR}/ultramodern/include
        ${N64MR}/librecomp/include
        ${N64MR}/N64Recomp/include
    )
endif()

# --- Headless harness (#57): added once src/ exists ---------------------------
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
    # src/aspMain.cpp is RSPRecomp output (see BUILDING.md step 3); appended
    # only when present so the first configure (which builds the recompilers)
    # can succeed before RSPRecomp runs.
    set(LAMBO_SOURCES
        src/main.cpp
        src/register_overlays.cpp
        src/stub_renderer.cpp
        src/lambo_audio.cpp    # SDL2 push-audio sink for the AI buffer (audio epic #53, PR 1 of 3)
        src/lambo_config.cpp   # persistent graphics.json -> ultramodern GraphicsConfig (enhancements #1/#2)
        src/lambo_player_name.cpp # remember player one's confirmed name for later name-entry screens
        src/lambo_menu.cpp     # lightweight native Win32 menu bar; live config actions + persistence
        src/lambo_startup.cpp  # runtime-ready -> Play startup state machine (issue #126)
        src/lambo_input_gate.cpp # controller capture/neutral-release barrier (issue #126)
        src/lambo_analog_throttle.cpp # issue #128: atomic native-port throttle bridge + race hook
        src/lambo_analog_brake.cpp    # issue #152: atomic native-port brake bridge + race hook
        src/controls/lambo_controls.cpp
        src/controls/lambo_controls_sdl.cpp
        src/ui/lambo_ui.cpp
        src/ui/lambo_ui_input.cpp
        src/ui/lambo_ui_controls.cpp
        src/ui/lambo_ui_render_interface.cpp
        src/ui/lambo_ui_settings.cpp
        lib/RmlUi/Backends/RmlUi_Platform_SDL.cpp
        src/lambo_crash.cpp    # native crash backtrace (issue #13 / A14): Win32 EXCEPTION_POINTERS +
                               # POSIX sigaction handler printing KSEG0/RDRAM offset + a 32-entry
                               # function-lookup trace ring. No deps beyond libc + DbgHelp (Win) /
                               # libc backtrace(3) (POSIX).
        src/libultra_stubs.c   # no-op symbols for ignored libultra CP0 helpers (e.g. __osSetSR)
        src/lambo_hud_widescreen.c  # issue #2: gEXSetRectAlign brackets around the race-HUD dials
        src/lambo_flare_widescreen.c  # issue #40: gEXSetRectAspect(STRETCH) bracket around the sun lens-flare ghosts
        src/lambo_fog_widescreen.cpp  # issue #83: widen dense 3P/4P fog to the 1P window/colour (DL rewrite, players>=3)
        src/lambo_sky_widescreen.cpp  # issue #84: draw the 1P/2P sky panorama in 3P/4P (patches.hook branch guard)
        src/lambo_no_lod.cpp   # issues #87/#91: emit the per-segment scenery layer in 2P-4P like 1P (patches.hook branch guards)
        src/lambo_camera.cpp   # sense of speed: chase-camera distance/height + FOV delta float-bit shims for the hooks
        src/lambo_gpu_advisory.cpp  # issue #109: outdated-Intel-driver detection + user-facing fix advisory
        src/lambo_warp.c       # issue #12: developer warp menu (F1-F6 / LAMBO_WARP race-track warp)
        src/lambo_savestate.c  # issue #22: developer save-state (F7/F8 / LAMBO_STATE_LOAD RDRAM snapshot)
        src/lambo_log.cpp      # first-party debug-logging gate (--lambo-debug CLI flag)
    )
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/aspMain.cpp)
        list(APPEND LAMBO_SOURCES src/aspMain.cpp)
    endif()

    add_executable(lamborghini_modern ${LAMBO_SOURCES})
    target_compile_definitions(lamborghini_modern PRIVATE
        HLSL_CPU
        LAMBO_VERSION="${PROJECT_VERSION}"
        LAMBO_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        LAMBO_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        LAMBO_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    )
    if(NOT WIN32 AND NOT APPLE)
        target_compile_definitions(lamborghini_modern PRIVATE
            PLUME_SDL_VULKAN_ENABLED
            RT64_SDL_WINDOW_VULKAN
        )
    endif()
    target_compile_options(lamborghini_modern PRIVATE
        -fno-strict-aliasing
        -fms-extensions
        -ggdb
        # GCC 16+ stopped pulling <cstdint> into <limits>/<algorithm>/<cstdlib>
        # transitively; RmlUi's vendored robin_hood.h relies on that and fails
        # with "Unsupported bitness" at the SIZE_MAX check. RmlUi_Platform_SDL.cpp
        # (compiled directly into lamborghini_modern, C++) pulls RmlUi headers.
        # The flag is scoped to CXX via two-argument generator expressions
        # because `-include` is a single compiler flag followed by a header
        # name, not a `-flag,value` shorthand — passing both as bare strings
        # inside $<COMPILE_LANGUAGE:CXX>:...> makes CMake emit them as two
        # separate tokens for C++ TUs and empty strings for C TUs.
        "$<$<COMPILE_LANGUAGE:CXX>:-include>" "$<$<COMPILE_LANGUAGE:CXX>:cstdint>"
    )
    if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        target_compile_options(lamborghini_modern PRIVATE -march=nehalem)
    endif()
    target_include_directories(lamborghini_modern PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${N64MR}/ultramodern/include
        ${N64MR}/librecomp/include
        ${N64MR}/N64Recomp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/RmlUi/Include
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/RmlUi/Backends
        ${N64MR}/thirdparty/concurrentqueue
    )
    target_link_libraries(lamborghini_modern PRIVATE
        librecomp
        ultramodern
    )
    if(TARGET RecompiledFuncs)
        target_link_libraries(lamborghini_modern PRIVATE RecompiledFuncs)
    endif()

    # RT64 renderer (#58: default presenter; see the RT64 block above).
    target_sources(lamborghini_modern PRIVATE src/rt64_renderer.cpp)
    # rt64's build_pixel_shader/build_vertex_shader read ${DXC} and the DXC_*_OPTS
    # variables from the CALLER's scope (CMake evaluates unreferenced variables at call
    # time). Those variables are set inside rt64/CMakeLists.txt's own project() scope, so
    # invoking it from THIS file finds them unset and the generated build command is
    # missing the dxc.exe path AND the compiler args (-T ps_6_3, -spirv, ...). Pin the
    # full set of variables here so the shader build commands resolve end-to-end.
    set(LAMBO_DXC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/dxc")
    if(WIN32)
        set(DXC "${LAMBO_DXC_ROOT}/bin/x64/dxc.exe")
    elseif(APPLE)
        find_library(LAMBO_ZLIB_LIBRARY NAMES z)
        get_filename_component(LAMBO_ZLIB_PATH ${LAMBO_ZLIB_LIBRARY} DIRECTORY)
        if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
            set(DXC "DYLD_LIBRARY_PATH=${LAMBO_DXC_ROOT}/lib/x64:${LAMBO_ZLIB_PATH}"
                "${LAMBO_DXC_ROOT}/bin/x64/dxc-macos")
        else()
            set(DXC "DYLD_LIBRARY_PATH=${LAMBO_DXC_ROOT}/lib/arm64:${LAMBO_ZLIB_PATH}"
                "${LAMBO_DXC_ROOT}/bin/arm64/dxc-macos")
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(DXC "LD_LIBRARY_PATH=${LAMBO_DXC_ROOT}/lib/x64"
            "${LAMBO_DXC_ROOT}/bin/x64/dxc-linux")
    else()
        set(DXC "LD_LIBRARY_PATH=${LAMBO_DXC_ROOT}/lib/arm64"
            "${LAMBO_DXC_ROOT}/bin/arm64/dxc-linux")
    endif()
    set(DXC_COMMON_OPTS "-I${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src")
    set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
    set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
    set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_3")
    set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T vs_6_3" "-fvk-invert-y")
    build_vertex_shader(lamborghini_modern "shaders/InterfaceVS.hlsl" "shaders/InterfaceVS.hlsl")
    build_pixel_shader(lamborghini_modern "shaders/InterfacePS.hlsl" "shaders/InterfacePS.hlsl")
    # rt64's include_directories() are directory-scoped and do not propagate;
    # mirror Zelda64Recomp's include list for the game target.
    set(RT64_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64)
    target_include_directories(lamborghini_modern PRIVATE
        ${RT64_DIR}/include
        ${RT64_DIR}/src
        ${RT64_DIR}/src/rhi
        ${RT64_DIR}/src/render
        ${RT64_DIR}/src/contrib
        ${RT64_DIR}/src/contrib/plume
        ${RT64_DIR}/src/contrib/imgui
        ${RT64_DIR}/src/contrib/hlslpp/include
        ${RT64_DIR}/src/contrib/nativefiledialog-extended/src/include
        ${SDL2_INCLUDE_DIRS}
        ${CMAKE_BINARY_DIR}/shaders
    )
    target_link_libraries(lamborghini_modern PRIVATE
        rt64
        RmlUi::Core
        RmlUi::Debugger
        ${SDL2_LIBRARIES}
    )

    add_custom_command(TARGET lamborghini_modern POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/ui
            $<TARGET_FILE_DIR:lamborghini_modern>/assets/ui
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/RmlUi/Samples/assets/LatoLatin-Regular.ttf
            $<TARGET_FILE_DIR:lamborghini_modern>/assets/ui/fonts/LatoLatin-Regular.ttf
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/lib/RmlUi/Samples/assets/LatoLatin-Bold.ttf
            $<TARGET_FILE_DIR:lamborghini_modern>/assets/ui/fonts/LatoLatin-Bold.ttf
    )

    if(WIN32)
        # Per-Monitor V2 DPI awareness (issue #161): embed src/app.manifest so the
        # process gets true physical pixel dimensions at >100% display scaling --
        # without it DWM bitmap-stretches the window by the scale factor and a
        # 1600x900 window overflows 1080p screens at 175% scaling. MinGW ld has no
        # /MANIFEST switch, so compile the .rc (which references the manifest) with
        # windres and link the object.
        find_program(LAMBO_WINDRES_EXECUTABLE NAMES windres REQUIRED)
        set(LAMBO_MANIFEST_OBJ ${CMAKE_CURRENT_BINARY_DIR}/lambo_manifest.rc.o)
        add_custom_command(OUTPUT ${LAMBO_MANIFEST_OBJ}
            COMMAND ${LAMBO_WINDRES_EXECUTABLE}
                -I ${CMAKE_CURRENT_SOURCE_DIR}/src
                -O coff -o ${LAMBO_MANIFEST_OBJ}
                ${CMAKE_CURRENT_SOURCE_DIR}/src/lambo_manifest.rc
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/lambo_manifest.rc
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest
            COMMENT "Embedding DPI-awareness manifest (windres)")
        target_sources(lamborghini_modern PRIVATE ${LAMBO_MANIFEST_OBJ})
        # Native crash backtrace (#13 / A14): CaptureStackBackTrace lives in
        # dbghelp.dll. MinGW64 ships the import lib (libdbghelp.a) so an explicit
        # target_link_libraries entry is enough; we don't ship the DLL — the
        # system one is fine.
        target_link_libraries(lamborghini_modern PRIVATE dbghelp)
        # Self-contained exe dir: RT64 dlopen()s dxcompiler/dxil at runtime and SDL2 is
        # a dynamic dep; MinGW's own runtime is linked statically so the exe runs
        # outside an MSYS/MinGW shell.
        target_link_options(lamborghini_modern PRIVATE -static-libgcc -static-libstdc++
            -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
        set(RT64_DXC_BIN ${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64)
        add_custom_command(TARGET lamborghini_modern POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SDL2_WIN32_DEPS}/lib/x64/SDL2.dll
                ${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/mupen64plus-win32-deps/freetype-2.13.0/lib/x64/freetype.dll
                ${RT64_DXC_BIN}/dxcompiler.dll
                ${RT64_DXC_BIN}/dxil.dll
                $<TARGET_FILE_DIR:lamborghini_modern>)
    endif()
endif()

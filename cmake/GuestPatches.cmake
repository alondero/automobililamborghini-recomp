# Guest C is built by host Clang/LLD even when the port targets Android.
set(LAMBO_GUEST_CLANG "clang" CACHE STRING "Host Clang with MIPS target support")
set(LAMBO_GUEST_LINKER "ld.lld" CACHE STRING "Host LLVM linker")
set(LAMBO_GUEST_WSL_DISTRO "" CACHE STRING "Optional WSL distro for guest Clang/LLD")
set(LAMBO_GUEST_ZIG "$ENV{LAMBO_GUEST_ZIG}" CACHE STRING "Optional host Zig executable providing Clang/LLD")
set(LAMBO_GUEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/guest")
set(LAMBO_GUEST_TOOL_ARGS --clang "${LAMBO_GUEST_CLANG}" --linker "${LAMBO_GUEST_LINKER}")
if(LAMBO_GUEST_WSL_DISTRO)
    list(APPEND LAMBO_GUEST_TOOL_ARGS --wsl-distro "${LAMBO_GUEST_WSL_DISTRO}")
endif()
if(LAMBO_GUEST_ZIG)
    list(APPEND LAMBO_GUEST_TOOL_ARGS --zig "${LAMBO_GUEST_ZIG}")
endif()
if(ANDROID)
    set(LAMBO_GUEST_RECOMPILER "${LAMBO_HOST_TOOLS}/N64Recomp${CMAKE_HOST_EXECUTABLE_SUFFIX}")
else()
    set(LAMBO_GUEST_RECOMPILER "$<TARGET_FILE:N64RecompCLI>")
endif()
add_custom_command(
    OUTPUT "${LAMBO_GUEST_DIR}/guest_patches.c" "${LAMBO_GUEST_DIR}/guest_patch_data.inc"
        "${LAMBO_GUEST_DIR}/recomp_overlays.inl" "${LAMBO_GUEST_DIR}/funcs.h"
    COMMAND ${LAMBO_PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_guest_patches.py"
        --output "${LAMBO_GUEST_DIR}" --recompiler "${LAMBO_GUEST_RECOMPILER}" ${LAMBO_GUEST_TOOL_ARGS}
    DEPENDS scripts/build_guest_patches.py guest/pit_stop.c guest/pit_stop.h guest/vehicle_select.c
        guest/menu_input.c guest/menu_input.h
        guest/vehicle_update.c guest/vehicle_update.h
        guest/patches.ld guest/symbols.ld lamborghini.syms.toml "${LAMBO_GUEST_RECOMPILER}"
    VERBATIM)
add_library(GuestPatches STATIC "${LAMBO_GUEST_DIR}/guest_patches.c")
target_include_directories(GuestPatches PRIVATE src "${LAMBO_GUEST_DIR}" "${N64MR}/N64Recomp/include")
target_compile_options(GuestPatches PRIVATE -fno-strict-aliasing -Wno-unused-variable
    -fpatchable-function-entry=16 -falign-functions=16 $<$<C_COMPILER_ID:GNU>:-fno-ipa-icf>)

function(lambo_test_guest_functions target)
    set(output "${LAMBO_GUEST_DIR}/${target}.c")
    add_custom_command(OUTPUT "${output}"
        COMMAND ${LAMBO_PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/extract_guest_test_funcs.py"
            "${LAMBO_GUEST_DIR}/guest_patches.c" "${output}" ${ARGN}
        DEPENDS tests/extract_guest_test_funcs.py "${LAMBO_GUEST_DIR}/guest_patches.c"
        VERBATIM)
    target_sources(${target} PRIVATE "${output}")
    target_include_directories(${target} PRIVATE "${LAMBO_GUEST_DIR}")
    target_compile_options(${target} PRIVATE -fno-strict-aliasing)
endfunction()

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Automobili Lamborghini (USA).z64")
    add_executable(lambo_guest_settings_tests tests/test_guest_settings.cpp src/lambo_guest_settings.cpp)
    target_include_directories(lambo_guest_settings_tests PRIVATE src "${N64MR}/N64Recomp/include")
    add_test(NAME lambo_guest_settings_abi COMMAND lambo_guest_settings_tests)
    set(VEHICLE_REFERENCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/vehicle-reference")
    add_custom_command(OUTPUT "${VEHICLE_REFERENCE_DIR}/vehicle_reference.c"
        COMMAND ${LAMBO_PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/build_vehicle_reference.py"
            "${LAMBO_GUEST_RECOMPILER}" "${VEHICLE_REFERENCE_DIR}"
        DEPENDS tests/build_vehicle_reference.py scripts/race_field_patches.py
            "Automobili Lamborghini (USA).z64" lamborghini.syms.toml "${LAMBO_GUEST_RECOMPILER}"
        VERBATIM)
    add_executable(lambo_vehicle_parity_tests tests/test_vehicle_update_parity.cpp
        "${VEHICLE_REFERENCE_DIR}/vehicle_reference.c"
        tests/vehicle_reference/lambo_analog_throttle.cpp tests/vehicle_reference/lambo_analog_brake.cpp
        tests/vehicle_reference/lambo_steering.cpp)
    target_include_directories(lambo_vehicle_parity_tests PRIVATE src "${LAMBO_GUEST_DIR}"
        "${VEHICLE_REFERENCE_DIR}" "${N64MR}/N64Recomp/include")
    lambo_test_guest_functions(lambo_vehicle_parity_tests func_80019D20)
    add_test(NAME lambo_vehicle_update_parity COMMAND lambo_vehicle_parity_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/Automobili Lamborghini (USA).z64")
    set(MENU_REFERENCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/menu-reference")
    add_custom_command(OUTPUT "${MENU_REFERENCE_DIR}/menu_reference.c"
        COMMAND ${LAMBO_PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/build_menu_reference.py"
            "${LAMBO_GUEST_RECOMPILER}" "${MENU_REFERENCE_DIR}"
        DEPENDS tests/build_menu_reference.py "Automobili Lamborghini (USA).z64"
            lamborghini.syms.toml "${LAMBO_GUEST_RECOMPILER}"
        VERBATIM)
    add_executable(lambo_menu_parity_tests tests/test_menu_input_parity.cpp "${MENU_REFERENCE_DIR}/menu_reference.c")
    target_include_directories(lambo_menu_parity_tests PRIVATE src "${LAMBO_GUEST_DIR}"
        "${MENU_REFERENCE_DIR}" "${N64MR}/N64Recomp/include")
    lambo_test_guest_functions(lambo_menu_parity_tests func_800427D4)
    add_test(NAME lambo_menu_input_parity COMMAND lambo_menu_parity_tests)
    set(SELECTOR_REFERENCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/selector-reference")
    add_custom_command(OUTPUT "${SELECTOR_REFERENCE_DIR}/selector_reference.c"
        "${SELECTOR_REFERENCE_DIR}/selector_categories.inc"
        COMMAND ${LAMBO_PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/build_vehicle_select_reference.py"
            "${LAMBO_GUEST_RECOMPILER}" "${SELECTOR_REFERENCE_DIR}"
        DEPENDS tests/build_vehicle_select_reference.py "Automobili Lamborghini (USA).z64"
            "${LAMBO_GUEST_RECOMPILER}"
        VERBATIM)
    add_executable(lambo_selector_parity_tests tests/test_vehicle_select_parity.cpp
        "${SELECTOR_REFERENCE_DIR}/selector_reference.c")
    target_include_directories(lambo_selector_parity_tests PRIVATE src "${LAMBO_GUEST_DIR}"
        "${SELECTOR_REFERENCE_DIR}" "${N64MR}/N64Recomp/include")
    lambo_test_guest_functions(lambo_selector_parity_tests func_8003F40C func_8003F56C)
    add_test(NAME lambo_vehicle_select_parity COMMAND lambo_selector_parity_tests)
    if(TARGET lambo_cheat_tests)
        lambo_test_guest_functions(lambo_cheat_tests func_8003F40C func_8003F56C)
    endif()
    set(PIT_REFERENCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/pit-reference")
    add_custom_command(OUTPUT "${PIT_REFERENCE_DIR}/pit_reference.c"
        COMMAND ${LAMBO_PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/build_pit_reference.py"
            "${LAMBO_GUEST_RECOMPILER}" "${PIT_REFERENCE_DIR}"
        DEPENDS tests/build_pit_reference.py scripts/race_field_patches.py lamborghini.syms.toml
            "Automobili Lamborghini (USA).z64" "${LAMBO_GUEST_RECOMPILER}"
        VERBATIM)
    add_executable(lambo_pit_parity_tests tests/test_pit_stop_parity.cpp "${PIT_REFERENCE_DIR}/pit_reference.c")
    target_include_directories(lambo_pit_parity_tests PRIVATE src "${LAMBO_GUEST_DIR}" "${N64MR}/N64Recomp/include")
    lambo_test_guest_functions(lambo_pit_parity_tests func_8006B470)
    add_test(NAME lambo_pit_stop_parity COMMAND lambo_pit_parity_tests)
    add_executable(lambo_pit_stop_tests tests/test_automatic_pit_stops.cpp src/lambo_automatic_pit_stops.cpp)
    target_include_directories(lambo_pit_stop_tests PRIVATE src "${LAMBO_GUEST_DIR}"
        "${N64MR}/N64Recomp/include" "${N64MR}/ultramodern/include" "${N64MR}/thirdparty")
    lambo_test_guest_functions(lambo_pit_stop_tests func_8006B470)
    add_test(NAME lambo_automatic_pit_stops COMMAND lambo_pit_stop_tests)
endif()

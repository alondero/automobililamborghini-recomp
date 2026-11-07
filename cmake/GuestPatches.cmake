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
    DEPENDS scripts/build_guest_patches.py guest/pit_stop.c guest/pit_stop.h
        guest/patches.ld guest/symbols.ld lamborghini.syms.toml "${LAMBO_GUEST_RECOMPILER}"
    VERBATIM)
add_library(GuestPatches STATIC "${LAMBO_GUEST_DIR}/guest_patches.c")
target_include_directories(GuestPatches PRIVATE src "${LAMBO_GUEST_DIR}" "${N64MR}/N64Recomp/include")
target_compile_options(GuestPatches PRIVATE -fno-strict-aliasing -Wno-unused-variable
    -fpatchable-function-entry=16 -falign-functions=16 $<$<C_COMPILER_ID:GNU>:-fno-ipa-icf>)

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Automobili Lamborghini (USA).z64")
    set(PIT_REFERENCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/pit-reference")
    add_custom_command(OUTPUT "${PIT_REFERENCE_DIR}/pit_reference.c"
        COMMAND ${LAMBO_PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/build_pit_reference.py"
            "${LAMBO_GUEST_RECOMPILER}" "${PIT_REFERENCE_DIR}"
        DEPENDS tests/build_pit_reference.py scripts/race_field_patches.py lamborghini.syms.toml
            "Automobili Lamborghini (USA).z64" "${LAMBO_GUEST_RECOMPILER}"
        VERBATIM)
    add_executable(lambo_pit_parity_tests tests/test_pit_stop_parity.cpp "${PIT_REFERENCE_DIR}/pit_reference.c")
    target_include_directories(lambo_pit_parity_tests PRIVATE src "${LAMBO_GUEST_DIR}" "${N64MR}/N64Recomp/include")
    target_link_libraries(lambo_pit_parity_tests PRIVATE GuestPatches)
    add_test(NAME lambo_pit_stop_parity COMMAND lambo_pit_parity_tests)
    add_executable(lambo_pit_stop_tests tests/test_automatic_pit_stops.cpp src/lambo_automatic_pit_stops.cpp)
    target_include_directories(lambo_pit_stop_tests PRIVATE src "${LAMBO_GUEST_DIR}"
        "${N64MR}/N64Recomp/include" "${N64MR}/ultramodern/include" "${N64MR}/thirdparty")
    target_link_libraries(lambo_pit_stop_tests PRIVATE GuestPatches)
    add_test(NAME lambo_automatic_pit_stops COMMAND lambo_pit_stop_tests)
endif()

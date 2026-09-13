cmake_minimum_required(VERSION 3.24)
project(lambo_frontend_preview LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT WIN32 OR NOT MINGW)
    message(FATAL_ERROR "This experimental host currently reuses a Windows MinGW RT64 build.")
endif()
set(FRONTEND "" CACHE PATH "RecompFrontend checkout")
set(RUNTIME "" CACHE PATH "New N64ModernRuntime checkout")
set(RT64_SOURCE "" CACHE PATH "Patched Lamborghini RT64 checkout")
set(RT64_BUILD "" CACHE PATH "Existing Lamborghini build directory")
set(PREVIEW_ASSETS "" CACHE PATH "Peer frontend assets directory")
foreach(required FRONTEND RUNTIME RT64_SOURCE RT64_BUILD PREVIEW_ASSETS)
    if(NOT IS_DIRECTORY "${${required}}")
        message(FATAL_ERROR "Set ${required} to an existing directory")
    endif()
endforeach()
add_compile_definitions(NOMINMAX SDL_MAIN_HANDLED)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-include> $<$<COMPILE_LANGUAGE:CXX>:cstdint>)
set(BUILD_SHARED_LIBS OFF)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
set(sdl2_SOURCE_DIR "${RT64_SOURCE}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")
include_directories("${sdl2_SOURCE_DIR}/include")
link_directories("${sdl2_SOURCE_DIR}/lib/x64")
add_subdirectory("${RUNTIME}" runtime)

# Build the actual upstream shaders using the existing RT64 host tools.
function(preview_shader target shader stage)
    get_filename_component(name "${shader}" NAME_WE)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")
    foreach(format dxil spirv)
        set(extra -Wno-ignored-attributes)
        if(format STREQUAL "spirv")
            list(APPEND extra -spirv -fspv-target-env=vulkan1.0 -fvk-use-dx-layout)
            if(stage STREQUAL "vs")
                list(APPEND extra -fvk-invert-y)
            endif()
            set(symbol "${name}BlobSPIRV")
        else()
            set(symbol "${name}BlobDXIL")
        endif()
        string(TOUPPER "${stage}" entry)
        set(output "${CMAKE_BINARY_DIR}/${shader}.${format}")
        add_custom_command(OUTPUT "${output}.c" "${output}.h"
            COMMAND "${RT64_SOURCE}/src/contrib/dxc/bin/x64/dxc.exe"
                -I "${RT64_SOURCE}/src" -E "${entry}Main" -T "${stage}_6_3"
                ${extra} "${CMAKE_CURRENT_SOURCE_DIR}/${shader}" -Fo "${output}"
            COMMAND "${RT64_BUILD}/rt64/src/tools/file_to_c/file_to_c.exe"
                "${output}" "${symbol}" "${output}.c" "${output}.h"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}" VERBATIM)
        target_sources(${target} PRIVATE "${output}.c")
    endforeach()
endfunction()
function(build_vertex_shader target shader)
    preview_shader(${target} "${shader}" vs)
endfunction()
function(build_pixel_shader target shader)
    preview_shader(${target} "${shader}" ps)
endfunction()
set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH "${RUNTIME}" CACHE PATH "" FORCE)
set(RECOMP_FRONTEND_RT64_PATH "${RT64_SOURCE}" CACHE PATH "" FORCE)
add_subdirectory("${FRONTEND}" frontend)

add_executable(lambo_frontend_preview main.cpp "${RUNTIME}/librecomp/src/rdram_memory.cpp")
target_include_directories(lambo_frontend_preview PRIVATE "${RUNTIME}/librecomp/include")
target_link_libraries(lambo_frontend_preview PRIVATE
    "$<LINK_GROUP:RESCAN,recompui,recompinput,librecomp,ultramodern>"
    "${RT64_BUILD}/rt64/rt64.a"
    "${RT64_BUILD}/rt64/src/contrib/re-spirv/libre-spirv.a"
    "${RT64_BUILD}/rt64/src/contrib/nativefiledialog-extended/src/libnfd.a"
    "${RT64_BUILD}/rt64/src/contrib/zstd/build/cmake/lib/libzstd.a"
    "${RT64_BUILD}/rt64/src/contrib/plume/libplume.a"
    "${RT64_BUILD}/_deps/directx-headers-build/libDirectX-Guids.a"
    "${RT64_BUILD}/_deps/directx-headers-build/libDirectX-Headers.a"
    "${RT64_SOURCE}/src/contrib/dxc/lib/x64/dxcompiler.lib"
    SDL2 delayimp D3D12 DXGI Shcore dwmapi d3dcompiler dbghelp shell32)
add_custom_command(TARGET lambo_frontend_preview POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PREVIEW_ASSETS}" "${CMAKE_BINARY_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/recomp.rcss" "${CMAKE_BINARY_DIR}/assets/recomp.rcss"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/icons" "${CMAKE_BINARY_DIR}/assets/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll"
        "${RT64_SOURCE}/src/contrib/dxc/bin/x64/dxcompiler.dll"
        "${RT64_SOURCE}/src/contrib/dxc/bin/x64/dxil.dll"
        "${FRONTEND}/recompui/lib/freetype-windows-binaries/release dll/win64/freetype.dll"
        "${CMAKE_BINARY_DIR}" VERBATIM)

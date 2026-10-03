# GPU probes are opt-in: ordinary builds/backends do not compile new SM6.5
# pipelines or allocate shadow resources. Game replacement remains gated.
option(LAMBO_RT_GPU_TESTS "Build D3D12 sunlight shadow GPU probes" OFF)
if(LAMBO_RT_GPU_TESTS)
    if(NOT WIN32 OR NOT BUILD_TESTING)
        message(FATAL_ERROR "LAMBO_RT_GPU_TESTS requires Windows and BUILD_TESTING")
    endif()
    set(LAMBO_RT_DXC "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc.exe"
        CACHE FILEPATH "DXC with matching dxcompiler.dll/dxil.dll beside it (SM6.5)")
    set(sun_shader "${CMAKE_CURRENT_BINARY_DIR}/sun-shadow-probe.dxil")
    add_custom_command(OUTPUT "${sun_shader}"
        COMMAND "${LAMBO_RT_DXC}" -E CSMain -T cs_6_5 -WX
            "-I${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/gpu/sun_shadow_probe.hlsl" -Fo "${sun_shader}"
        DEPENDS tests/gpu/sun_shadow_probe.hlsl
            lib/rt64/src/shaders/SunShadow.hlsli lib/rt64/src/shared/rt64_sun_shadow.h
        VERBATIM)
    set(material_shader "${CMAKE_CURRENT_BINARY_DIR}/native-material-probe.dxil")
    add_custom_command(OUTPUT "${material_shader}"
        COMMAND "${LAMBO_RT_DXC}" -E CSMain -T cs_6_5 -WX
            "-I${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/gpu/native_material_probe.hlsl" -Fo "${material_shader}"
        DEPENDS tests/gpu/native_material_probe.hlsl
            lib/rt64/src/shaders/NativeMaterial.hlsli
            lib/rt64/src/shaders/NativeRayHit.hlsli
            lib/rt64/src/shaders/SunShadow.hlsli
            lib/rt64/src/shaders/TextureSampler.hlsli
            lib/rt64/src/shaders/TextureDecoder.hlsli
            lib/rt64/src/shaders/Random.hlsli
            lib/rt64/src/shared/rt64_color_combiner.h
        VERBATIM)
    set(native_vs_shader "${CMAKE_CURRENT_BINARY_DIR}/native-material-vs.dxil")
    set(native_ps_shader "${CMAKE_CURRENT_BINARY_DIR}/native-material-ps.dxil")
    add_custom_command(OUTPUT "${native_vs_shader}"
        COMMAND "${LAMBO_RT_DXC}" -E VSMain -T vs_6_3 -WX -Wno-ignored-attributes -D DYNAMIC_RENDER_PARAMS
            "-I${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/shaders/RasterVS.hlsl" -Fo "${native_vs_shader}"
        DEPENDS lib/rt64/src/shaders/RasterVS.hlsl
        VERBATIM)
    add_custom_command(OUTPUT "${native_ps_shader}"
        COMMAND "${LAMBO_RT_DXC}" -E PSMain -T ps_6_3 -WX -Wno-ignored-attributes -D DYNAMIC_RENDER_PARAMS -D OWNER_BUFFER
            "-I${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/rt64/src/shaders/RasterPS.hlsl" -Fo "${native_ps_shader}"
        DEPENDS lib/rt64/src/shaders/RasterPS.hlsl lib/rt64/src/shaders/NativeMaterial.hlsli
            lib/rt64/src/shaders/TextureSampler.hlsli lib/rt64/src/shaders/TextureDecoder.hlsli
            lib/rt64/src/shared/rt64_color_combiner.h
        VERBATIM)
    add_executable(lambo_rt_shadow_gpu tests/gpu/sun_shadow_probe.cpp
        tests/gpu/native_material_probe.cpp "${sun_shader}" "${material_shader}"
        "${native_vs_shader}" "${native_ps_shader}")
    target_compile_features(lambo_rt_shadow_gpu PRIVATE cxx_std_20)
    target_compile_definitions(lambo_rt_shadow_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(lambo_rt_shadow_gpu PRIVATE lib/rt64/src lib/rt64/src/contrib/plume
        lib/rt64/src/contrib/hlslpp/include)
    target_link_libraries(lambo_rt_shadow_gpu PRIVATE rt64 plume)
    if(NOT MSVC)
        target_compile_options(lambo_rt_shadow_gpu PRIVATE
            "-include${CMAKE_CURRENT_SOURCE_DIR}/src/mingw_dx12_uuid_compat.h")
    endif()
    add_test(NAME lambo_rt_shadow_gpu COMMAND lambo_rt_shadow_gpu "${sun_shader}" "${material_shader}"
        "${native_vs_shader}" "${native_ps_shader}")
    set_tests_properties(lambo_rt_shadow_gpu PROPERTIES
        LABELS "gpu;d3d12;raytracing" TIMEOUT 60 SKIP_RETURN_CODE 77)
endif()

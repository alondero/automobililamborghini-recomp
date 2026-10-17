# Downstream compatibility patches belong to this repo, not an unrecorded
# dirty submodule. Both configure and the existing build scripts can run this
# repeatedly. Refuse unrelated/conflicting edits instead of resetting them.
find_package(Git REQUIRED)
function(lambo_frontend_patch directory patch)
    set(patch_path "${CMAKE_CURRENT_SOURCE_DIR}/patches/${patch}")
    execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check "${patch_path}"
        WORKING_DIRECTORY "${directory}" RESULT_VARIABLE applied OUTPUT_QUIET ERROR_QUIET)
    if(applied EQUAL 0)
        return()
    endif()
    execute_process(COMMAND ${GIT_EXECUTABLE} apply --check "${patch_path}"
        WORKING_DIRECTORY "${directory}" RESULT_VARIABLE clean OUTPUT_QUIET ERROR_VARIABLE reason)
    if(NOT clean EQUAL 0)
        message(FATAL_ERROR "Cannot apply ${patch}; preserve/resolve submodule changes first:\n${reason}")
    endif()
    execute_process(COMMAND ${GIT_EXECUTABLE} apply "${patch_path}"
        WORKING_DIRECTORY "${directory}" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Applying ${patch} failed")
    endif()
endfunction()
lambo_frontend_patch("${N64MR}" 0016-runtime-host-config-storage.patch)
lambo_frontend_patch("${N64MR}" 0018-runtime-game-presentation.patch)
lambo_frontend_patch("${N64MR}" 0035-runtime-live-mod-discovery.patch)
# 0034 changes a control-state line from 0017, so recognize it before checking
# the older patch's reverse context on repeated configure runs.
execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check
    "${CMAKE_CURRENT_SOURCE_DIR}/patches/0034-recompfrontend-live-mod-refresh.patch"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend"
    RESULT_VARIABLE mod_refresh_patch_applied OUTPUT_QUIET ERROR_QUIET)
if(NOT mod_refresh_patch_applied EQUAL 0)
    lambo_frontend_patch("${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend" 0017-recompfrontend-lamborghini-integration.patch)
endif()
lambo_frontend_patch("${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend" 0019-recompfrontend-prompt-back-cancels.patch)
lambo_frontend_patch("${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend" 0020-recompfrontend-enum-wrap.patch)
lambo_frontend_patch("${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend" 0031-recompfrontend-document-source-url.patch)
lambo_frontend_patch("${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend" 0032-recompfrontend-settings-header-wrap.patch)
lambo_frontend_patch("${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend" 0034-recompfrontend-live-mod-refresh.patch)

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
lambo_frontend_patch("${CMAKE_CURRENT_SOURCE_DIR}/lib/RecompFrontend" 0017-recompfrontend-lamborghini-integration.patch)

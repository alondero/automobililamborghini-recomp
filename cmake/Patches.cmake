# Dependency patches are build input, not an unrecorded dirty submodule.
# patches/series.txt lists them; tools/apply_patches.py brings each dependency
# to the end of its series. Repeated configures are no-ops, and local edits
# that conflict with the series stop the configure instead of being reset.
find_package(Git REQUIRED)
if(ANDROID)
    set(LAMBO_PATCH_PLATFORM android)
elseif(WIN32)
    set(LAMBO_PATCH_PLATFORM windows)
else()
    set(LAMBO_PATCH_PLATFORM linux)
endif()
execute_process(
    COMMAND ${LAMBO_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/apply_patches.py
        --platform ${LAMBO_PATCH_PLATFORM} --git ${GIT_EXECUTABLE}
    RESULT_VARIABLE lambo_patch_result)
if(NOT lambo_patch_result EQUAL 0)
    message(FATAL_ERROR "Dependency patches were not applied; see the error above.")
endif()
# Reconfigure when the series changes, so an edited patch is either recognized
# or reported instead of silently building the previously patched sources.
file(GLOB lambo_patch_files ${CMAKE_CURRENT_SOURCE_DIR}/patches/*.patch)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/patches/series.txt ${lambo_patch_files})

if(NOT TARGET Freetype::Freetype)
    message(FATAL_ERROR "Android FreeType must be built before RmlUi")
endif()
set(FREETYPE_FOUND TRUE)

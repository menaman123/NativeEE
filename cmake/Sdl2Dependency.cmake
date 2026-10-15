# cmake/Sdl2Dependency.cmake -- SDL2 found or fetched, ONE way for every host.
#
#   find_package(SDL2 CONFIG)        a system / Homebrew / vcpkg SDL2 (the default)
#   PS2R_FETCH_SDL2=ON               FetchContent of the pinned SDL2 release when the
#                                    package is absent (a repo-clone build on a bare
#                                    machine; CI's Linux lanes install libsdl2-dev
#                                    instead and never hit this path)
#
# Both paths end with the target SDL2::SDL2 that every target_link_libraries in
# the tree names. Included from EACH directory that links SDL2 (imported
# targets are directory-scoped, so the find_package repeats; the fetch runs
# once, guarded by a global property, and its targets are global).
option(PS2R_FETCH_SDL2 "Fetch and build the pinned SDL2 release when find_package(SDL2) fails" OFF)
set(PS2R_SDL2_FETCH_TAG "release-2.30.9" CACHE STRING "SDL2 git tag used by PS2R_FETCH_SDL2")

if(NOT TARGET SDL2::SDL2)
    find_package(SDL2 CONFIG QUIET)
    if(TARGET SDL2::SDL2-static AND NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 ALIAS SDL2::SDL2-static)
    endif()
endif()

if(TARGET SDL2::SDL2)
    get_property(_ps2r_sdl2_reported GLOBAL PROPERTY PS2R_SDL2_REPORTED)
    if(NOT _ps2r_sdl2_reported)
        set_property(GLOBAL PROPERTY PS2R_SDL2_REPORTED TRUE)
        message(STATUS "nativeee: SDL2 ${SDL2_VERSION} found (${SDL2_DIR})")
    endif()
    return()
endif()

if(NOT PS2R_FETCH_SDL2)
    message(FATAL_ERROR
        "SDL2 not found. Install it (macOS: brew install sdl2; Debian/Ubuntu: apt install libsdl2-dev; "
        "Fedora: dnf install SDL2-devel) or configure with -DPS2R_FETCH_SDL2=ON to build the pinned "
        "${PS2R_SDL2_FETCH_TAG} from source.")
endif()

get_property(_ps2r_sdl2_fetched GLOBAL PROPERTY PS2R_SDL2_FETCHED)
if(NOT _ps2r_sdl2_fetched)
    set_property(GLOBAL PROPERTY PS2R_SDL2_FETCHED TRUE)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(sdl2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG ${PS2R_SDL2_FETCH_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(sdl2)
    # Third-party code: its own warning policy (macOS 15 SDK deprecations in the Cocoa /
    # GameController back ends). The tree's 0-warning rule is for the tree's own TUs.
    foreach(_sdl_target SDL2-static SDL2main sdl_headers_copy)
        if(TARGET ${_sdl_target})
            get_target_property(_sdl_type ${_sdl_target} TYPE)
            if(NOT _sdl_type STREQUAL "INTERFACE_LIBRARY" AND NOT _sdl_type STREQUAL "UTILITY")
                target_compile_options(${_sdl_target} PRIVATE -w)
            endif()
        endif()
    endforeach()
    message(STATUS "nativeee: SDL2 fetched and built from ${PS2R_SDL2_FETCH_TAG} (static)")
endif()
if(TARGET SDL2-static AND NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 ALIAS SDL2-static)   # a real target: global, visible to every directory
endif()
if(NOT TARGET SDL2::SDL2)
    message(FATAL_ERROR "nativeee: the fetched SDL2 did not produce SDL2-static")
endif()

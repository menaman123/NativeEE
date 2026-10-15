# engine/platform/sdl -- SDL2 window + OpenGL 4.1 context (Phase 6). The audio sink
# and input backends of the same directory are Phase 7's and get their own target.
include(${CMAKE_SOURCE_DIR}/cmake/Sdl2Dependency.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/OpenGlDependency.cmake)

add_library(ps2r_platform_sdl STATIC
    window.cpp
    gl_context.cpp)
target_include_directories(ps2r_platform_sdl PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_platform_sdl PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_platform_sdl PUBLIC SDL2::SDL2 OpenGL::GL)

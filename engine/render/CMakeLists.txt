# engine/render -- the backend contract (header-only: backend.h, surface_format.h)
# and the API adapters beneath it.
add_library(ps2r_render INTERFACE)
target_include_directories(ps2r_render INTERFACE ${CMAKE_SOURCE_DIR})

add_subdirectory(backends/gl)
add_subdirectory(review)

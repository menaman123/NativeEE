# tools/perf -- the host ROOFLINE microbenchmarks: the peaks every engine unit's
# achieved rate is judged against. perf_roofline = the CPU side (portable: C++17 threads;
# NEON under PS2R_SIMD as the engine's kernels are, scalar twins elsewhere). The GPU side is
# the Metal arm's metal_microbench (engine/render/backends/metal, Apple only). Both read no
# environment; every knob is a flag; rows print as key=value for the notes to quote.
add_executable(perf_roofline perf_roofline.cpp)
target_include_directories(perf_roofline PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(perf_roofline PRIVATE -Wall -Wextra -Werror -O2)
if(PS2R_SIMD)
    target_compile_definitions(perf_roofline PRIVATE PS2R_SIMD=1)
endif()
find_package(Threads REQUIRED)
target_link_libraries(perf_roofline PRIVATE Threads::Threads)

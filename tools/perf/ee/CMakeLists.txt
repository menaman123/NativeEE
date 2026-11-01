# tools/perf/ee: the EE-thread codegen roofline microbenchmark (STAGE PERF-1, track ee). Portable; links the
# engine's EE memory model so the measured access path is the one the generated code runs.
add_executable(bench_ee_codegen bench_ee_codegen.cpp)
target_include_directories(bench_ee_codegen PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(bench_ee_codegen PRIVATE ps2r_ee)
target_compile_options(bench_ee_codegen PRIVATE $<$<NOT:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>:-O2 -fno-strict-aliasing -fwrapv>)

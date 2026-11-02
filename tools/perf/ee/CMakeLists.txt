# tools/perf/ee: the EE-thread codegen roofline microbenchmark. Portable; links the
# engine's EE memory model so the measured access path is the one the generated code runs.
add_executable(bench_ee_codegen bench_ee_codegen.cpp)
target_include_directories(bench_ee_codegen PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(bench_ee_codegen PRIVATE ps2r_ee)
target_compile_options(bench_ee_codegen PRIVATE -O2 -fno-strict-aliasing -fwrapv)   # the shim spells these for clang-cl (toolchain_cross_macos.cmake)

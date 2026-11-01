# tools/perf/io -- the io track's roofline microbenchmarks (portable; no game data).
#   bench_spu_mixer: SpuCore::MixInto at the title's pump shape (800 frames, 48 voices) + the output hash oracle.
add_executable(bench_spu_mixer bench_spu_mixer.cpp)
target_link_libraries(bench_spu_mixer PRIVATE ps2r_sound ps2r_core ps2r_platform)
target_compile_options(bench_spu_mixer PRIVATE -Wall -Wextra -Werror)

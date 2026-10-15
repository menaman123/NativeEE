# engine/core -- platform-independent primitives (PHASE1_PLAN section 2).
# ps2r_core is always built; ps2r_diag only under PS2R_DIAGNOSTICS.

add_library(ps2r_core STATIC
    hash/hash.cpp
    hash/sha256.cpp
    image/png.cpp
    image/png_inflate.cpp
    log/ring_tags.cpp
    log/ring_decoder.cpp
    log/ring_log.cpp
    log/logger.cpp
    log/log_budget.cpp
    threads/park.cpp
    config/config.cpp
    config/config_loader.cpp
    config/settings_file.cpp
    config/config_validator.cpp
    config/deterministic_profile.cpp
    savestate/savestate_format.cpp
    savestate/payload_reader.cpp
    savestate/section_registry.cpp
    savestate/savestate_writer.cpp
    savestate/quiescence.cpp
    verify/frame_hasher.cpp
    verify/audio_hasher.cpp
    verify/memory_checkpointer.cpp
    verify/run_meta.cpp
    verify/frame_cap.cpp
    verify/verify_host.cpp
    verify/verify_input_segment.cpp
    verify/verify_output_segment.cpp
    triage/input_log.cpp
    triage/triage_bundle.cpp)
target_include_directories(ps2r_core PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_core PRIVATE -Wall -Wextra -Werror)
find_package(Threads REQUIRED)
# The OS surface core needs (fd writes in the signal-safe ring dumps, the triage
# backtrace, case-insensitive compares) comes through the ONE host shim header,
# engine/platform/host/platform.h, whose arm is ps2r_host (below ps2r_platform).
target_link_libraries(ps2r_core PUBLIC ps2r_host Threads::Threads)

if(PS2R_DIAGNOSTICS)
    add_library(ps2r_diag STATIC
        diag/diag_registry.cpp
        diag/perf_profiler.cpp
        diag/bench_report.cpp
        diag/bench.cpp
        diag/iopsched_trace.cpp
        diag/drawlist_recorder.cpp
        diag/word_watch.cpp)
    target_include_directories(ps2r_diag PUBLIC ${CMAKE_SOURCE_DIR})
    target_compile_options(ps2r_diag PRIVATE -Wall -Wextra -Werror)
    target_compile_definitions(ps2r_diag PUBLIC PS2R_DIAGNOSTICS=1)
    # The .gsdl drawlist recorder is a DrawIrSink over the GS DrawIR types (ps2r_gs_ir).
    target_link_libraries(ps2r_diag PUBLIC ps2r_core ps2r_gs_ir)
endif()

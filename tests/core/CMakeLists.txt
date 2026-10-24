# tests/core -- Phase 1 core suites (PHASE1_PLAN section 2.9).
set(PS2R_TEST_SCRATCH "${CMAKE_BINARY_DIR}/test_scratch/core")
file(MAKE_DIRECTORY "${PS2R_TEST_SCRATCH}")

function(ps2r_core_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ps2r_core)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME Core${name} COMMAND ${name} ${PS2R_TEST_SCRATCH})
endfunction()

ps2r_core_test(test_hash)
ps2r_core_test(test_png)
ps2r_core_test(test_threads_log)
ps2r_core_test(test_ring_log)
ps2r_core_test(test_verify_units)

# Phase 2A: PsmLayout (gs/format) -- descriptor table, bijection, arithmetic, units, 4 MB wrap.
add_executable(test_psm_layout test_psm_layout.cpp)
target_link_libraries(test_psm_layout PRIVATE ps2r_gs_format)
target_compile_options(test_psm_layout PRIVATE -Wall -Wextra -Werror)
add_test(NAME Coretest_psm_layout COMMAND test_psm_layout)

# Config: argv[2] is the generated settings table -- the deterministic golden
# fingerprint is READ from its header (single source, CONSTRUCTION_PLAN R14).
set(PS2R_SETTINGS_TABLE "${CMAKE_SOURCE_DIR}/docs/filemap/settings_table.tsv")
add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE ps2r_core)
target_compile_options(test_config PRIVATE -Wall -Wextra -Werror)
add_test(NAME Coretest_config COMMAND test_config ${PS2R_TEST_SCRATCH} ${PS2R_SETTINGS_TABLE})

# SettingsFile: the settings writer the in-game options persist through
# (replace in place, append, preserve, refuse non-schema keys, round-trip
# through ConfigLoader::ApplyIniText).
add_executable(test_settings_file test_settings_file.cpp)
target_link_libraries(test_settings_file PRIVATE ps2r_core)
target_compile_options(test_settings_file PRIVATE -Wall -Wextra -Werror)
add_test(NAME Coretest_settings_file COMMAND test_settings_file ${PS2R_TEST_SCRATCH})
set_tests_properties(Coretest_settings_file PROPERTIES TIMEOUT 60)

# ConfigStore: the ONE settings file (nativeee.ini) -- resolution order, the legacy
# ps2r.ini / display.ini migration, the process-wide active store the writers use.
add_executable(test_config_store test_config_store.cpp)
target_link_libraries(test_config_store PRIVATE ps2r_core)
target_compile_options(test_config_store PRIVATE -Wall -Wextra -Werror)
add_test(NAME Coretest_config_store COMMAND test_config_store ${PS2R_TEST_SCRATCH})
set_tests_properties(Coretest_config_store PROPERTIES TIMEOUT 60)

# The schema table enumerated against the compiled Config: bijection with
# Config::Keys(), peak + deterministic columns, value domains, fingerprint.
add_executable(test_config_schema test_config_schema.cpp)
target_link_libraries(test_config_schema PRIVATE ps2r_core)
target_compile_options(test_config_schema PRIVATE -Wall -Wextra -Werror)
add_test(NAME Coretest_config_schema COMMAND test_config_schema ${PS2R_SETTINGS_TABLE})

# The bounded-shutdown pure policy (app/host; the original test_shutdown_policy
# tables). Compiles the .cpp directly: the ladder is app code, not a library.
add_executable(test_shutdown_ladder test_shutdown_ladder.cpp ${CMAKE_SOURCE_DIR}/app/host/shutdown_ladder.cpp)
target_include_directories(test_shutdown_ladder PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(test_shutdown_ladder PRIVATE -Wall -Wextra -Werror)
add_test(NAME Coretest_shutdown_ladder COMMAND test_shutdown_ladder)

# PS2SAVE1 envelope + the 12 OLD anchor states re-serialized byte-identical
# (read-only; SKIPs when OLD is absent).
add_executable(test_savestate_format test_savestate_format.cpp)
target_link_libraries(test_savestate_format PRIVATE ps2r_core)
target_compile_options(test_savestate_format PRIVATE -Wall -Wextra -Werror)
add_test(NAME Coretest_savestate_format COMMAND test_savestate_format ${PS2R_TEST_SCRATCH} "${PS2R_OG_ROOT}")

# The save-point refusal predicate (INTEGRATION_PLAN row 7.6: the FMV-active
# signal refuses a savestate; named predicates, first refusal counted).
add_executable(test_quiescence test_quiescence.cpp)
target_link_libraries(test_quiescence PRIVATE ps2r_core)
target_compile_options(test_quiescence PRIVATE -Wall -Wextra -Werror)
add_test(NAME SavestateQuiescence COMMAND test_quiescence)

# DP-13 (a)+(b) pins: EmuDomain's Deferred tick == OnRawTick under the deterministic
# policy (same absorbed counts), rides 1:1 with the pacer release on; the sequencer's
# TickAdvance slot unchanged unbound, wall-locked (park / late / bounded stopped-pacer
# fallback) once a pacer is bound under Play.
add_executable(test_pacer_release test_pacer_release.cpp)
target_link_libraries(test_pacer_release PRIVATE ps2r_ee)
target_compile_options(test_pacer_release PRIVATE -Wall -Wextra -Werror)
add_test(NAME PacerReleaseDomain COMMAND test_pacer_release domain)
add_test(NAME PacerReleaseSequencer COMMAND test_pacer_release sequencer)
set_tests_properties(PacerReleaseDomain PacerReleaseSequencer PROPERTIES TIMEOUT 60)

# Row 6.7: the present policy's decision table (live / hold / none; fps_cap share;
# the first image always shown; FMV boundaries counted).
add_executable(test_present_policy test_present_policy.cpp)
target_link_libraries(test_present_policy PRIVATE ps2r_gs_hle_present)
target_compile_options(test_present_policy PRIVATE -Wall -Wextra -Werror)
add_test(NAME PresentPolicyTable COMMAND test_present_policy)
set_tests_properties(PresentPolicyTable PROPERTIES TIMEOUT 60)

# The ring text form against the frozen original's decoder (read-only oracle;
# SKIPs when OLD is absent).
add_test(NAME CoreRingDecodeOracle
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/ring_decode_oracle.py
            --test-binary $<TARGET_FILE:test_ring_log>
            --scratch ${PS2R_TEST_SCRATCH}/oracle
            --og-root "${PS2R_OG_ROOT}"
            --bounded ${CMAKE_SOURCE_DIR}/verify/run_bounded.sh)

# Exactly ONE getenv site in the tree (ConfigLoader's dev override).
add_test(NAME CoreSingleGetenvSite
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/core/check_getenv_sites.py --root ${CMAKE_SOURCE_DIR})
# The generated TUs' exempt arms are counted where the build generated them (TitleInputs).
set_tests_properties(CoreSingleGetenvSite PROPERTIES ENVIRONMENT "${PS2R_RECOMP_TEST_ENV}")

if(PS2R_DIAGNOSTICS)
    add_executable(test_bench_report test_bench_report.cpp)
    target_link_libraries(test_bench_report PRIVATE ps2r_diag)
    target_compile_options(test_bench_report PRIVATE -Wall -Wextra -Werror)
    add_test(NAME CoreBenchReportFormat COMMAND test_bench_report)

    # The .gsdl drawlist recorder (DP-10 (b)): record -> GsdlReader round trip, counts
    # equal to the counting sink's. Diagnostics builds only; the dev build SKIPs (77).
    add_executable(test_drawlist_recorder test_drawlist_recorder.cpp)
    target_link_libraries(test_drawlist_recorder PRIVATE ps2r_diag ps2r_gsdl_reader)
    target_compile_options(test_drawlist_recorder PRIVATE -Wall -Wextra -Werror)
    add_test(NAME CoreDrawlistRecorder COMMAND test_drawlist_recorder ${PS2R_TEST_SCRATCH})
else()
    add_test(NAME CoreDrawlistRecorder
        COMMAND ${Python3_EXECUTABLE} -c "import sys; print('SKIP: diagnostics build only'); sys.exit(77)")
endif()
set_tests_properties(CoreDrawlistRecorder PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

# The crash path's triage bundle (ALL-ON, NO EXCEPTIONS): a synthetic fault +
# a DrawlistRing that saw 450 frames write <scratch>/triage/<UTC>/ with every
# file present and non-empty; the ring is bounded (200 frames / 32768 draws).
add_executable(test_triage_bundle test_triage_bundle.cpp)
target_link_libraries(test_triage_bundle PRIVATE ps2r_core ps2r_gs_ir)
target_compile_options(test_triage_bundle PRIVATE -Wall -Wextra -Werror)
add_test(NAME CoreTriageBundle COMMAND test_triage_bundle ${PS2R_TEST_SCRATCH})
set_tests_properties(CoreTriageBundle PROPERTIES TIMEOUT 60)

# The --watch-word harness knob (ConfigLoader, every build) and the WordWatch
# census (diagnostics builds; the dev build runs the knob pins and prints SKIP
# for the census half -- exit 0, not 77, so the knob stays pinned everywhere).
add_executable(test_word_watch test_word_watch.cpp)
if(PS2R_DIAGNOSTICS)
    target_link_libraries(test_word_watch PRIVATE ps2r_diag)
else()
    target_link_libraries(test_word_watch PRIVATE ps2r_core)
endif()
target_compile_options(test_word_watch PRIVATE -Wall -Wextra -Werror)
add_test(NAME CoreWordWatch COMMAND test_word_watch ${PS2R_TEST_SCRATCH})
set_tests_properties(CoreWordWatch PROPERTIES TIMEOUT 60)

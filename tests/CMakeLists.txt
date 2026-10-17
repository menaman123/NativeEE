# tests — ctest registrations. Phase 0: the two CI lints (ARCH section 13) plus
# a smoke test of the empty binary. Per-module suites are added by their phases
# as add_subdirectory(<module>) below.

# LintOneClass: one primary type per .h/.cpp pair, no .inc includes, headers
# declarations-only, no session tags, file name = snake_case(type).
add_test(NAME LintOneClass
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_one_class.py --root ${CMAKE_SOURCE_DIR})
add_test(NAME LintOneClassSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_one_class.py --selftest)

# LintEnginePurity: zero game tokens under engine/ and verify/ (A6 section 1.2 set).
add_test(NAME LintEnginePurity
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_engine_purity.py --root ${CMAKE_SOURCE_DIR})
add_test(NAME LintEnginePuritySelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_engine_purity.py --selftest)

# LintSdkBinding* read a title's hle/sdk_bindings.tsv, so they exist only with a title
# configured. The selftest below is title-neutral and always runs.
if(PS2R_TITLE AND EXISTS "${PS2R_GAMES_DIR}/${PS2R_TITLE}/hle/sdk_bindings.tsv")
    add_test(NAME LintSdkBindingRowCount
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_sdk_bindings.py --check row-count
                --tsv ${PS2R_GAMES_DIR}/${PS2R_TITLE}/hle/sdk_bindings.tsv)
    set_tests_properties(LintSdkBindingRowCount PROPERTIES TIMEOUT 60)
    if(TARGET ps2r_title_recomp)
        add_test(NAME LintSdkBindingNoGameAddress
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_sdk_bindings.py --check no-game-address
                    --tsv ${PS2R_GAMES_DIR}/${PS2R_TITLE}/hle/sdk_bindings.tsv
                    --tu ${PS2R_RECOMP_GEN_DIR}/recompiled.cpp)
        set_tests_properties(LintSdkBindingNoGameAddress PROPERTIES TIMEOUT 60)
    endif()
endif()
add_test(NAME LintSdkBindingsSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_sdk_bindings.py --selftest)
set_tests_properties(LintSdkBindingsSelftest PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 ENVIRONMENT "${PS2R_RECOMP_TEST_ENV}")   # the TU cases need the generated TU

# ARCHITECTURE section 11 size budgets / header globals / session tags (P8.36-L), pure python, no build:
#   LintSizeBudget    file 1,000/1,500/2,000 lines, function 150/300/400, generated shard 3/4 MB;
#                     FAILs only on an unallowlisted hard-fail (WARNs = the refactor queue)
#   LintHeaderGlobals zero mutable variable DEFINITIONS in engine/app/games headers
#   LintSessionTags   zero [TAG] comments / session namespaces / session-numbered literals in the hand-written tree
# All three share tests/lint/budget_allowlist.txt (<relpath>:<kind>:<measured>, dated reason above each entry;
# an allowlisted offender that grows fails again).
add_test(NAME LintSizeBudget
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_size_budget.py --root ${CMAKE_SOURCE_DIR} --quiet-warns)
set_tests_properties(LintSizeBudget PROPERTIES TIMEOUT 120)
add_test(NAME LintSizeBudgetSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_size_budget.py --selftest)
set_tests_properties(LintSizeBudgetSelftest PROPERTIES TIMEOUT 60)
add_test(NAME LintHeaderGlobals
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_header_globals.py --root ${CMAKE_SOURCE_DIR})
set_tests_properties(LintHeaderGlobals PROPERTIES TIMEOUT 60)
add_test(NAME LintHeaderGlobalsSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_header_globals.py --selftest)
set_tests_properties(LintHeaderGlobalsSelftest PROPERTIES TIMEOUT 60)
add_test(NAME LintSessionTags
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_session_tags.py --root ${CMAKE_SOURCE_DIR})
set_tests_properties(LintSessionTags PROPERTIES TIMEOUT 60)
add_test(NAME LintSessionTagsSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_session_tags.py --selftest)
set_tests_properties(LintSessionTagsSelftest PROPERTIES TIMEOUT 60)

# SmokeEmptyBinary: the skeleton binary runs and exits 0, printing its build id.
# (The binaries exist only when the title's generated code is being built -- TitleInputs.)
if(TARGET nativeee)
    add_test(NAME SmokeEmptyBinary COMMAND nativeee --version)
    set_tests_properties(SmokeEmptyBinary PROPERTIES PASS_REGULAR_EXPRESSION "NATIVEEE-BUILD-ID")
endif()
if(TARGET nativeee_oracle)
    add_test(NAME SmokeOracleBinary COMMAND nativeee_oracle --version)
    set_tests_properties(SmokeOracleBinary PROPERTIES PASS_REGULAR_EXPRESSION "NATIVEEE-BUILD-ID")
endif()

# LintPlayBinaryNoIop (M12, TRACK 3b D2/D3): the PLAY binary's symbol table names no
# IOP kernel / module / libsd model (`ps2r::iop::`), no recompiled STREAM.IRX or plugin
# (`stream_irx::`, `iop_stubs::`), no SIF DMA model (`EeRpcServer`, `MainMemoryEeRamSink`,
# `SifRpcBridge`); the ORACLE binary carries them (the pattern is not vacuous).
if(TARGET nativeee AND TARGET nativeee_oracle)
    add_test(NAME LintPlayBinaryNoIop
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_play_binary_no_iop.py
                --play $<TARGET_FILE:nativeee> --oracle $<TARGET_FILE:nativeee_oracle>)
    set_tests_properties(LintPlayBinaryNoIop PROPERTIES TIMEOUT 300)
endif()
add_test(NAME LintPlayBinaryNoIopSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_play_binary_no_iop.py --selftest)

# LintArtifactLicenses (Charter 2 C1): the artifact's licence set -- tools/dist/licenses/ pinned
# by its PROVENANCE.md (SDL2 zlib, FFmpeg LGPL v2.1 + LICENSE.md, Dear ImGui MIT, the gl3w loader's
# public-domain dedication, GPLv3 + the GCC Runtime Library Exception for Linux's static libstdc++),
# the FFmpeg recipe (tools/ci/ffmpeg_static.sh) enabling no GPL / non-free component, build.sh /
# build.ps1 / README.md / the *-dist presets agreeing with it; and, when dist/<platform>/ has been
# staged, that folder's licenses/ + THIRD_PARTY_NOTICES.md (the stagers run the same check).
add_test(NAME LintArtifactLicenses
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_artifact_licenses.py
            --root ${CMAKE_SOURCE_DIR} --dist ${CMAKE_SOURCE_DIR}/dist/$<IF:$<PLATFORM_ID:Darwin>,macos,linux-${CMAKE_SYSTEM_PROCESSOR}>)
set_tests_properties(LintArtifactLicenses PROPERTIES TIMEOUT 60)
add_test(NAME LintArtifactLicensesSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/lint/lint_artifact_licenses.py --selftest)
set_tests_properties(LintArtifactLicensesSelftest PROPERTIES TIMEOUT 60)

# Harness self-checks (verify/ is ours): diff_legs unit checks.
add_test(NAME VerifyDiffLegsSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/diff_legs.py --selftest)

# tools/frame_diff.py (the frames lane's pixel attribution: PPM/BMP readers, the lane hash,
# bbox / histogram / per-region report) unit checks.
add_test(NAME ToolsFrameDiffSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/frame_diff.py --selftest)
set_tests_properties(ToolsFrameDiffSelftest PROPERTIES TIMEOUT 60)

# The PCSX2 GS-dump census tools (tools/gsdump): the dump reader / draw census, the
# .gsdl <-> dump N-way comparer, and the streaming .gsdl frame slicer.
add_test(NAME ToolsGsdumpCensusSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gsdump/gsdump_census.py --selftest)
set_tests_properties(ToolsGsdumpCensusSelftest PROPERTIES TIMEOUT 60)
add_test(NAME ToolsDrawCensusCompareSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gsdump/draw_census_compare.py --selftest)
set_tests_properties(ToolsDrawCensusCompareSelftest PROPERTIES TIMEOUT 60)
add_test(NAME ToolsGsdlSliceSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gsdump/gsdl_slice.py --selftest)
set_tests_properties(ToolsGsdlSliceSelftest PROPERTIES TIMEOUT 60)

# tools/footprint_watch.py (the leg footprint sampler: vmmap --summary parsing, unit
# normalisation) unit checks (BUG-083's instrument).
add_test(NAME ToolsFootprintWatchSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/footprint_watch.py --selftest)
set_tests_properties(ToolsFootprintWatchSelftest PROPERTIES TIMEOUT 60)

# FlagPhaseCountsReproducible: the per-phase flag tally quoted by CONSTRUCTION_PLAN section 3
# is regenerated from FLAG_LEDGER.md by tools/flag_phase_counts.py and must equal the
# checked-in docs/filemap/FLAG_PHASE_COUNTS.txt (plan numbers are reproducible, not typed).
add_test(NAME FlagPhaseCountsReproducible
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/flag_phase_counts.py
            --check ${CMAKE_SOURCE_DIR}/docs/filemap/FLAG_PHASE_COUNTS.txt)

# SettingsTableReproducible: docs/filemap/settings_table.tsv (the Config schema in machine
# form + the deterministic golden fingerprint, CONSTRUCTION_PLAN R14) is regenerated by
# tools/gen_settings_table.py -- which re-verifies it against FLAG_LEDGER.md and
# PHASE1_PLAN.md section 3 -- and must equal the checked-in file.
add_test(NAME SettingsTableReproducible
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gen_settings_table.py
            --check ${CMAKE_SOURCE_DIR}/docs/filemap/settings_table.tsv)

# Phase 1 core suites (+ the Phase 2 PsmLayout unit test).
# The saved PCSX2 console EE images (pcsx2_plus90_ee.bin, pcsx2_rec159_ee.bin: the Default.rm2
# stream ring's console timeline, P8.28-A) -- optional evidence for the ring-rule pins
# (tests/cdvd LoaderRingCache, tests/app RootLoaderRingCache; SKIP / not-run when absent).
set(PS2R_CONSOLE_IMAGES_DIR "${CMAKE_SOURCE_DIR}/../int_work/p828a" CACHE PATH
    "Directory holding the saved PCSX2 EE RAM images the ring-rule pins compare against")

add_subdirectory(core)

add_subdirectory(lib)  # Phase 6: test/tool support libs (ps2r_gsdl_reader) -- before their consumers

# Phase 2B+ gs/format property tests (Swizzle, ClutLayout, ...).
add_subdirectory(gs)

# Phase 2 OG-oracle differential tests (OLD read-only; SKIP when absent).
add_subdirectory(oracle)

# Phase 3 EE suites (cpu state layout, ps2 float, fastmem, address map, MMIO sink, ELF loader).
add_subdirectory(ee)

# Phase 4 IOP suites (kernel semantics + FRAME determinism, sysmem heap, frame clock).
add_subdirectory(iop)

# Phase 4C: the SIF register block and the DMAC register page.
add_subdirectory(sif)
# Phase 4D: the disc (IsoFs + ReadEngine over the frozen image; libcdvd rows).
add_subdirectory(cdvd)
add_subdirectory(mc)
add_subdirectory(pad)
add_subdirectory(sound)
add_subdirectory(gif)     # Phase 5: GIF suites (register file, recording sink, parser equivalence)
add_subdirectory(vu)     # Phase 5: VU suites
add_subdirectory(vif)     # Phase 5: VIF suites
add_subdirectory(dmac)     # Phase 5: DMAC suites
add_subdirectory(render)  # Phase 6

# Phase 7 host-half suites: the audio pull ring + SDL audio sink (dummy driver),
# and the input mapper / padlog replay / bindings round trip (stage 7B).
add_subdirectory(platform)   # the host shim header contract (engine/platform/host)
add_subdirectory(platform_sdl)
add_subdirectory(input)
# Stage 7C: the IPU register model + MpegDecode arithmetic.
add_subdirectory(ipu)

# Integration: process-level pins of the composition root (the built binary, bounded).
add_subdirectory(app)

# tests/recomp, tests/services and tests/title were the title-bound suites: they exercised
# ONE title's recompiled output, service oracles and HLE. A title module brings its own
# suites now (games/<serial>_<slug>/, registered by its own CMakeLists).

# Harness tool self-checks (verify/score.py, battery.py, run_leg.py; no binary).
add_subdirectory(verify)

# Every ctest carries a TIMEOUT so a hung test can never block a stage (the
# 2026-10-01 HeadroomQueryRecompiled hang). Tests that set their own TIMEOUT
# keep it; every other test gets the default below. The test preset also
# passes `--timeout` as a backstop for tests registered outside this walk.
set(PS2R_TEST_DEFAULT_TIMEOUT 120)
function(ps2r_apply_default_test_timeout dir)
    get_directory_property(_tests DIRECTORY "${dir}" TESTS)
    foreach(t IN LISTS _tests)
        get_property(_has TEST "${t}" DIRECTORY "${dir}" PROPERTY TIMEOUT)
        if(NOT _has)
            set_property(TEST "${t}" DIRECTORY "${dir}" PROPERTY TIMEOUT ${PS2R_TEST_DEFAULT_TIMEOUT})
        endif()
    endforeach()
    get_directory_property(_subs DIRECTORY "${dir}" SUBDIRECTORIES)
    foreach(s IN LISTS _subs)
        ps2r_apply_default_test_timeout("${s}")
    endforeach()
endfunction()
# A title's own judges as ctest rows: games/<title>/verify/CMakeLists.txt, added here (after
# enable_testing(); the title's hle/ directory is added by the root before it, for the libraries).
# Title-neutral: any title may ship one; a title without it registers nothing.
if(PS2R_TITLE AND EXISTS "${PS2R_GAMES_DIR}/${PS2R_TITLE}/verify/CMakeLists.txt")
    add_subdirectory("${PS2R_GAMES_DIR}/${PS2R_TITLE}/verify" "${CMAKE_BINARY_DIR}/games/${PS2R_TITLE}/verify")
endif()

ps2r_apply_default_test_timeout("${CMAKE_CURRENT_SOURCE_DIR}")

# tests/ee -- Phase 3 EE suites (PHASE3_PLAN section 4.1). Stage 3A: cpu state
# layout, ps2 float semantics, fastmem fast/slow equivalence, address map +
# MMIO sink idle table, ELF loader + the RAM-image probe (OLD read-only; SKIPs
# when the frozen ELF is absent).
function(ps2r_ee_test name ctest_name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ps2r_ee)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${ctest_name} COMMAND ${name} ${ARGN})
endfunction()

ps2r_ee_test(test_cpu_state_layout EeCpuStateLayout)
# Same binary, anchor mode: re-reads the THRD ctx_size guard from one frozen
# anchor (OLD read-only) and requires it == sizeof(CpuContext). SKIP when absent.
add_test(NAME EeCpuStateLayoutAnchor
    COMMAND test_cpu_state_layout "${PS2R_OG_ROOT}/save_states/ps2state_slot0_f36935.ps2st")
set_tests_properties(EeCpuStateLayoutAnchor PROPERTIES SKIP_RETURN_CODE 77)
ps2r_ee_test(test_ps2_float Ps2FloatSemantics)
# The guest thread rounding mode (FE_TOWARDZERO per thread; div/sqrt/rsqrt nearest).
ps2r_ee_test(test_float_rounding Ps2FloatRounding)
set_tests_properties(Ps2FloatRounding PROPERTIES TIMEOUT 60)
ps2r_ee_test(test_fastmem FastMemDiff)
ps2r_ee_test(test_address_map AddressMapAndMmioSinkIdleValues)
# The fatal_no_jump park record: jump-source candidates + shadow chain + GPRs
# (the shadow-stack accessors are stubbed in the test TU; no title link).
ps2r_ee_test(test_park_site_dump EeParkSiteDump)
set_tests_properties(EeParkSiteDump PROPERTIES TIMEOUT 60)

# The RAM-image probe needs the frozen ELF (PS2R_OG_ROOT) and the title's .data word dump, a
# PRIVATE fixture ({PRIVATE_FIXTURES} = $NATIVEEE_PRIVATE_FIXTURES; cmake/Ps2rPrivateFixtures.cmake):
# SKIP 77 without either. The three post-load patch words stay in the tree.
set(PS2R_TITLE_DATA "${PS2R_GAMES_DIR}/${PS2R_TITLE}/data")
add_executable(test_elf_loader test_elf_loader.cpp)
target_link_libraries(test_elf_loader PRIVATE ps2r_ee)
target_compile_options(test_elf_loader PRIVATE -Wall -Wextra -Werror)
ps2r_add_private_fixture_test(NAME ElfLoaderTitle
    FIXTURES games/${PS2R_TITLE}/data/ram_data_map_v50.txt
    COMMAND test_elf_loader
    "${PS2R_OG_ROOT}/game_files/example_title.rep/SLUS_123.45"
    "{PRIVATE_FIXTURES}/games/${PS2R_TITLE}/data/ram_data_map_v50.txt"
    "${PS2R_TITLE_DATA}/post_load_patches.txt")

# Stage 3B: the generated code links with the seam providers; the dispatch surface is
# touched through the engine wrappers (no boot). Census: roster + unlisted-extern scan.
# Title tests: they link / read the GENERATED code (${PS2R_RECOMP_GEN_DIR}), so they exist only
# when a title ELF was given (TitleInputs) -- skipped with a STATUS line otherwise.
if(TARGET ps2r_title_recomp)
    add_executable(test_dispatch_table test_dispatch_table.cpp)
    target_link_libraries(test_dispatch_table PRIVATE ps2r_title_recomp ps2r_vu ps2r_gs_sync ps2r_ee)   # ps2r_vu: the row-27 seam bodies (VuMacroBridge)
    target_compile_options(test_dispatch_table PRIVATE -Wall -Wextra -Werror)
    add_test(NAME RecompDispatchTableLink COMMAND test_dispatch_table)
    # The census reads the generated TUs and the committed continuations.h side by side: the generated
    # directory holds its own (manifest-checked) copy of continuations.h, so --dir is the generated one.
    add_test(NAME RecompSymbolCensus
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/recomp_symbol_census.py
                --dir ${PS2R_RECOMP_GEN_DIR} --check)
    add_test(NAME RecompManifestIntact COMMAND ${CMAKE_SOURCE_DIR}/tools/regen_from_frozen.sh --check ${PS2R_RECOMP_GEN_DIR})
    # DP-8 (1): the headroom query FUN_00191bd0 runs its recompiled body over the dispatch
    # install primitive and advances no tick (HeadroomQueryGlue; the emitted hook fired one).
    # BUG-072: the retail function 0x178470 (prologue in its branch delay slot) returns with the
    # sp it was entered with on both paths -- red on a generation that split it at the delay slot.
    add_executable(test_bug072_sp_balance test_bug072_sp_balance.cpp)
    target_link_libraries(test_bug072_sp_balance PRIVATE ps2r_title_recomp ps2r_title_hle ps2r_vu ps2r_gs_sync ps2r_ee)
    target_compile_options(test_bug072_sp_balance PRIVATE -Wall -Wextra -Werror)
    add_test(NAME Bug072SpBalance COMMAND test_bug072_sp_balance)
    set_tests_properties(Bug072SpBalance PROPERTIES TIMEOUT 60)
    # The symbol census walks the whole recompiled TU (~100 s idle; measured 158-276 s and once
    # past 300 s under the full suite's -j8 load beside the 1500-frame root legs): the own
    # TIMEOUT bounds it at 600 s -- a real wait on the host's CPU, not a guest wait.
    set_tests_properties(RecompSymbolCensus PROPERTIES TIMEOUT 600)
else()
    message(STATUS "tests/ee: RecompDispatchTableLink / RecompSymbolCensus / RecompManifestIntact / HeadroomQueryRecompiled / Bug072SpBalance SKIPPED (no title ELF)")
endif()

# Stage 3C1: kernel objects + run token + event queue (PHASE3_PLAN section 7 row 3C1).
ps2r_ee_test(test_thread_manager ThreadManagerThbase)
ps2r_ee_test(test_ee_sched EeRunTokenAndEventQueue)
ps2r_ee_test(test_relaunch_order RelaunchOrder)
set_tests_properties(ThreadManagerThbase EeRunTokenAndEventQueue RelaunchOrder PROPERTIES TIMEOUT 120)
# The back-edge yield seam (--yield-backedges generated code -> EeYieldPoint): a call-free
# spin trips every budget back-edges; the trip advances a field, runs the pending ISRs on the
# spinner, and hands the CPU to an ISR-readied HIGHER-priority thread (thbase R2), never to an
# equal one (R1); nested / in-dispatch / non-holder trips are refused; unbound = reload only.
ps2r_ee_test(test_ee_yield_point EeYieldPointBackedge)
set_tests_properties(EeYieldPointBackedge PROPERTIES TIMEOUT 120)

# Stage 3C2: syscall table + boot env + libc + SDK binding manifest + kernl parks (one binary,
# section per ctest name; the manifest section reads the title's sdk_bindings.tsv).
add_executable(test_syscall_table test_syscall_table.cpp)
target_link_libraries(test_syscall_table PRIVATE ps2r_title_api ps2r_title_hle ps2r_sif ps2r_ee)
target_compile_options(test_syscall_table PRIVATE -Wall -Wextra -Werror)
set(PS2R_TITLE_HLE "${PS2R_GAMES_DIR}/${PS2R_TITLE}/hle")
add_test(NAME SyscallTableCoverage COMMAND test_syscall_table coverage)
add_test(NAME BootEnvRetailArgv COMMAND test_syscall_table bootenv)
add_test(NAME LibcHleDiff COMMAND test_syscall_table libc)
add_test(NAME SdkBindingManifestResolves COMMAND test_syscall_table manifest "${PS2R_TITLE_HLE}/sdk_bindings.tsv")
add_test(NAME KernlParksTickAndSif COMMAND test_syscall_table parks)
set_tests_properties(SyscallTableCoverage BootEnvRetailArgv LibcHleDiff SdkBindingManifestResolves KernlParksTickAndSif PROPERTIES TIMEOUT 120)

# Stage 3D1: the timing domain (one binary, section per ctest name): regime detector +
# boundary advance + EmuDomain quantum/carry/idle/re-anchor; budget arithmetic pins + fold
# closed==literal + phase classifier truth table; the nine derived clocks as pure
# functions of EmuDomain (identical runs, wall stub irrelevant), kernel clock install.
add_executable(test_timing test_timing.cpp)
target_link_libraries(test_timing PRIVATE ps2r_ee)
target_compile_options(test_timing PRIVATE -Wall -Wextra -Werror)
add_test(NAME EmuDomainAndRegime COMMAND test_timing domain)
add_test(NAME BudgetClock COMMAND test_timing budget)
add_test(NAME DerivedClocksPurity COMMAND test_timing clocks)
set_tests_properties(EmuDomainAndRegime BudgetClock DerivedClocksPurity PROPERTIES TIMEOUT 120)

# Stage 3D2: the boundary (one binary, section per ctest name): sequencer order + one tick
# per boundary + rendezvous + foreign INTC raise queued/applied in order + determinism of
# two identical boundary sequences; the boundary protocol (pace predicate, demand wait);
# EE timers registers on the MMIO router + the TIMR codec.
add_executable(test_boundary test_boundary.cpp)
target_link_libraries(test_boundary PRIVATE ps2r_gs_sync ps2r_ee)
target_compile_options(test_boundary PRIVATE -Wall -Wextra -Werror)
add_test(NAME TickSyncBoundaryOrder COMMAND test_boundary sequence)
add_test(NAME BoundaryProtocolPace COMMAND test_boundary protocol)
add_test(NAME EeTimersTimr COMMAND test_boundary timers)
set_tests_properties(TickSyncBoundaryOrder BoundaryProtocolPace EeTimersTimr PROPERTIES TIMEOUT 120)

# Stage 3E: the restore.mhash gate -- 11 frozen anchors (OLD read-only) restored into NEW
# memory through StateRestorer; hash == verify/state_hash.py's table; re-serialize identical.

# BUG-085: the HEAP section owner (the original prefix grammar + the META arena tail).

# Stage 4D'': the libscf RTC row over guest memory (six binary bytes from the fixed
# RTC, null -> no write, a scratchpad pointer written through the address map).
ps2r_ee_test(test_libscf_bindings LibscfRtcRow)
set_tests_properties(LibscfRtcRow PROPERTIES TIMEOUT 60)

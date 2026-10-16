# tests/iop -- Phase 4 IOP suites (PHASE4_PLAN section 4.1). Stage 4A: the kernel
# semantics + FRAME determinism port, the sysmem heap vectors, the frame clock.
# Stage 4A': the fiber guard page, the module table pumps, the RPC bridge, the
# IOPM section codec (+ the OLD anchor states as a read-only layout oracle).
function(ps2r_iop_test name ctest_name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ps2r_iop)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${ctest_name} COMMAND ${name} ${ARGN})
    set_tests_properties(${ctest_name} PROPERTIES TIMEOUT 120)
endfunction()

ps2r_iop_test(test_iop_kernel IopKernelUnits)
ps2r_iop_test(test_iop_shortcuts IopShortcutSemantics)
ps2r_iop_test(test_sysmem_heap IopSysmemHeap)
ps2r_iop_test(test_iop_clock IopClockFrame)
# BUG-066: the FRAME clock's frame base is rebased to the restored image's frame count
# at a savestate restore, so a hard timer re-armed at the restored now (the STREAM.IRX
# heartbeat) beats on the following boundaries instead of waiting ~F frames.
ps2r_iop_test(test_iop_clock_rebase IopClockRebase)

# 4A': the guest-stack guard page of the platform fiber layer (forked probes).
ps2r_iop_test(test_fiber_guard FiberGuardPage)
# A coroutine parked on one host thread and resumed from another (the IOP is pumped from the EE
# main thread at the boundary and from whichever EE guest thread trips the yield point): each host
# thread's current-context record stays its own across the migration.
ps2r_iop_test(test_fiber_migrate FiberHostMigration)
ps2r_iop_test(test_module_table IopModuleTable)
ps2r_iop_test(test_sif_rpc_bridge SifRpcBridge)
# Title tests (the recompiled STREAM.IRX is GENERATED at build time from the user's disc,
# TitleInputs): registered only when ps2r_title_stream_irx exists; a STATUS line otherwise.
if(TARGET ps2r_title_stream_irx)
    ps2r_iop_test(test_iopm_section IopmSection "${PS2R_OG_ROOT}")
    # The anchor re-park oracle boots the real STREAM.IRX through the plugin.
    target_link_libraries(test_iopm_section PRIVATE ps2r_title_stream_irx)
    # 4B: the STREAM.IRX regen manifest over the GENERATED output (tools/regen_from_frozen.sh --check --iop DIR).
    add_test(NAME IopRecompManifestIntact COMMAND ${CMAKE_SOURCE_DIR}/tools/regen_from_frozen.sh --check --iop ${PS2R_IOP_GEN_DIR})
else()
    message(STATUS "tests/iop: IopmSection / IopRecompManifestIntact / StreamIrx* / IopmBootSymmetricRepark SKIPPED (no STREAM.IRX)")
endif()

# 4B: the sysclib ordinal map of the kernel-facing HLE modules.
ps2r_iop_test(test_sysclib_map IopSysclibMap)

# 4B': the IRX loader (OLD's STREAM.IRX bytes read-only from game_files/), the kernel-facing
# module routing (thbase/thsemap/sifcmd/timrman/ioman) and the 74-name stub-surface census +
# artifact pin over the installed generated TU. The test references iop_recompiled_functions so
# the archive member is really linked in (an unlisted extern then fails the link).
ps2r_iop_test(test_iop_loader IopLoader "${PS2R_OG_ROOT}")
ps2r_iop_test(test_kernel_facing_modules IopKernelFacingModules)
# sifman's sceSifDmaStat answers the SDK's complete (-1) for the synchronous SetDma, so the
# recompiled STREAM.IRX CD worker's `bgez v0 -> loop` poll exits (BUG-043).
add_executable(test_sifman_module test_sifman_module.cpp vector_ee_ram_sink.cpp)
target_link_libraries(test_sifman_module PRIVATE ps2r_iop)
target_compile_options(test_sifman_module PRIVATE -Wall -Wextra -Werror)
add_test(NAME SifmanDmaStatComplete COMMAND test_sifman_module)
set_tests_properties(SifmanDmaStatComplete PROPERTIES TIMEOUT 60)
# The recompiled-IOP-module suites (stub surface, load/park, first RPC) exercised ONE title's
# STREAM.IRX decompilation; they move with the title module.

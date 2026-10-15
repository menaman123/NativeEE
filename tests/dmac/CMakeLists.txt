# tests/dmac -- Phase 5 DMAC suites. NEW-only unit tests against ps2r_dmac over
# a MainMemory + Scratchpad + VIF/GIF rig (the OLD tree has no DMAC tests).

function(ps2r_dmac_test name ctest_name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ps2r_dmac ps2r_gs_ir)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -O2)
    add_test(NAME ${ctest_name} COMMAND ${name} ${ARGN})
    set_tests_properties(${ctest_name} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endfunction()

# DmacRegs: the register map (channel pages for reads by range / writes by
# 256-byte page, the dropped globals, PCR, D_ENABLER/W), sub-word widening,
# CHCR.STR -> StartChannel -> CompleteChannel (STR clear, CIS, QWC 0), the
# D_STAT status acknowledgement and mask toggles, dispatch through the handler
# registry once a mask is set, SPR copies, the deferred VIF1 kick's
# register-file observation points, and the TAG_DMAC codec round trip.
ps2r_dmac_test(test_dmac_regs DmacRegs)

# DmacChainWalk: synthetic DMAtag chains in a 32 MB RAM + scratchpad fixture
# for every tag id (REFE/CNT/NEXT/REF/REFS/CALL/RET/END, ASR depth 2), the two
# terminator rules, TTE, IRQ+TIE, QWC 0, SPR-bit sources, kMaxTags, VIF normal
# mode and early returns, the GIF chain as ONE PATH3 submit, GIF normal mode.
ps2r_dmac_test(test_dmac_chain_walk DmacChainWalk)

# DmacGifPath3: a synthetic GIF chain -> CHCR kick -> ChainWalker -> ONE PATH3
# submit -> drain -> GifParser -> RegisterOpStream: op sequence + hash vs the
# same bytes fed directly, the IMAGE packet (arbiter is_path3_image rule, the
# mid-IMAGE FinishPath3 hold, the PATH3 stale reset), the M3R-masked case (the
# DMAC completes; the parser holds), PATH2-before-PATH3 across a parked VIF1.
ps2r_dmac_test(test_dmac_gif_path3 DmacGifPath3)

# DmacVifChain: VIF1/VIF0 chains end to end -- TTE tag words (STCYCL) before
# the payload, UNPACK V4-32 into VU1 memory, MSCAL running a micro through
# VuUnit, REF from the scratchpad, DIRECT -> PATH2 ops at the flush point.
ps2r_dmac_test(test_dmac_vif_chain DmacVifChain)

# DmacDeferredVif1: park, append-before-flush, the observation-point matrix,
# re-kick order, the kick-time snapshot, the END-slot tail chase (accept /
# reject tables, batch consumption, dedup, self-loop termination), frame sync.
ps2r_dmac_test(test_dmac_deferred_vif1 DmacDeferredVif1)

# DmacSpr: real scratchpad copies replace the obsolete no-copy baseline pin.
# Both directions, wrap, MMIO, source chains, TTE/TIE, interrupts, faults,
# unsupported requests, deferred VIF isolation, and census/savestate isolation.
ps2r_dmac_test(test_dmac_spr DmacSpr)

# DmacSectionRoundTrip: TAG_DMAC byte layout (324 + 25 n), Encode -> Decode ->
# Encode identity, validate rejections, restore resets the transient state.
ps2r_dmac_test(test_dmac_section DmacSectionRoundTrip)

# DmacLibdmaRows: the libdma.a bodies + the sceDmaSync / sceGifSyncDma seams
# (driven directly and through their manifest rows: a parked VIF1 kick flushes, v0 = 0).
ps2r_dmac_test(test_dmac_libdma DmacLibdmaRows)

# DmacTailChaseAtBoundary: GsSync::SyncV / WaitForVblank run the bound
# Dmac::ChaseVif1Tail BEFORE RunBoundary (probes in slots 0/1 see the kick
# landed), a rewritten END slot resumes at the boundary, the kick stays parked
# with no hook bound, GsSync::SyncPath flushes before reporting idle.
ps2r_dmac_test(test_dmac_tail_chase_boundary DmacTailChaseAtBoundary)
target_link_libraries(test_dmac_tail_chase_boundary PRIVATE ps2r_gs_sync ps2r_gs_ir)

# ---- MTVU staged feed (5G part 2) ----
# DmacMtvuFeed: the TTE UNPACK + MSCAL VIF1 chain walked inline (ChainWalker ->
# Vif) and through DmacMtvuFeed -> MtvuWorker -> Vif (one staged Batch per
# walk, the latch reset as a segment) lands identical VU1 data + registers on
# the worker's unit; Uninstall / destruction restore the inline feed. Starts
# the worker thread: serial.
ps2r_dmac_test(test_dmac_mtvu_feed DmacMtvuFeed)
set_tests_properties(DmacMtvuFeed PROPERTIES RUN_SERIAL TRUE)

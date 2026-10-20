# tests/gif -- Phase 5 GIF suites. NEW-only unit and property tests against
# ps2r_gif; the sink under test is the recording RegisterOpStream or the
# per-method CountingGsSink (counting_gs_sink.h/.cpp, linked into every suite).

function(ps2r_gif_test name ctest_name)
    add_executable(${name} ${name}.cpp counting_gs_sink.cpp)
    target_link_libraries(${name} PRIVATE ps2r_gif ps2r_gs_ir)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -O2)
    add_test(NAME ${ctest_name} COMMAND ${name} ${ARGN})
    set_tests_properties(${ctest_name} PROPERTIES TIMEOUT 120)
endfunction()

# GifRegsStat: STAT bit composition (M3R/M3P/IMT/PSE/OPH/APATH/FQC), TAG0-3
# readback of the in-flight tag, CNT, CTRL RST+PSE ordering, MODE & 7, read-only
# registers dropping writes; plus the GifTag / RegisterOp layout pins and the
# RegisterOpStream encoding + hash chaining + CountingGsSink self-check.
ps2r_gif_test(test_gif_regs GifRegsStat)

# GifsSection: the GIFS savestate section -- 13 bytes (ctrl, mode, stat = the
# reference's never-written mirror = 0, path3_masked), round trip byte-equal,
# restore clears the in-flight transfer + FIFO count while CTRL / MODE / the VIF
# mask take the saved words and STAT recomposes, the all-zero reference image,
# wrong lengths refused, REQUIRED through the registry, the restored callback.
ps2r_gif_test(test_gifs_section GifsSection)

# GifParserEquivalence: the synthetic packets of OLD tests/test_gif_fast_equivalence.cpp
# fed through two GifParser configurations (bulk lanes on / off) into two
# RegisterOpStreams -> identical op vectors and hashes; plus the pinned rules
# (REGLIST odd pad, NLOOP=0 EOP, IMAGE halves, TEXCLUT dropped, REGLIST XYZ2
# ADC ignored, PRE=1 PRIM, A+D, PATH3 stale reset, M3R hold, FinishPath3).
ps2r_gif_test(test_gif_parser_equivalence GifParserEquivalence)

# GifArbiterOrder: submissions drain in submission order regardless of path;
# FinishPath3 once per pass that delivered PATH3; sub-tag payloads dropped;
# buffer pool recycle bounds; pending-bytes ledger; clear().
ps2r_gif_test(test_gif_arbiter_order GifArbiterOrder)

# GifDrainThread: the drain thread owns the drain while it runs -- producer
# thread submissions (plain slots + worker-style markers through a fake
# GifMarkerOwner) delivered in order on the drain thread, boundary slot once
# in stream position, DrainFence drained-to-mark, idle generation, foreign
# drain() == kick, Stop with an unsealed marker joins without delivering
# behind it, restart; the EE's idle view (DrainInFlight / WaitDrainPassAfter /
# the busy ledger) and a pass fault rethrown on the producer's drain().
# Order/count/thread-identity checks only.
ps2r_gif_test(test_gif_drain_thread GifDrainThread)

# GifEarlyFinish (E425): GifFinishScan counts exactly the parser's A+D FINISH
# writes (PACKED found; REGLIST / IMAGE / FLG 3 / NLOOP 0 not; split anywhere);
# VifFinishScan agrees with the real Vif + parser on 120 randomized VIF1 streams
# (every UNPACK format / CL-WL mode, MPG, STMASK/STROW/STCOL, DIRECT/DIRECTHL,
# TTE quadwords, latch resets); the arbiter pre-raises a PATH3 FINISH once on the
# EE thread while the worker runs, delivers it in stream order behind the
# unsealed marker (GifArbiterOrder with a pre-raised FINISH in flight), the
# consume rule, inert without a worker / raiser, clear(). Links ps2r_vif for the Vif.
add_executable(test_gif_early_finish test_gif_early_finish.cpp counting_gs_sink.cpp)
target_link_libraries(test_gif_early_finish PRIVATE ps2r_vif ps2r_gif ps2r_gs_ir)
target_compile_options(test_gif_early_finish PRIVATE -Wall -Wextra -Werror -O2)
add_test(NAME GifEarlyFinish COMMAND test_gif_early_finish)
set_tests_properties(GifEarlyFinish PROPERTIES TIMEOUT 120)

# GifDeferBoundaryOrder: frame boundaries as ordered command slots under
# inline drains -- the directed interleaving matrix (sealed before/after the
# enqueue, stop at the first unsealed marker, two boundaries in flight, EE
# packet after a boundary, the feed-note tripwire, seq 0 -> 1, clear()
# detach + ledger), inline vs deferred (drain thread) identical
# RegisterOpStream + hook order, and a 10k randomized soak with a real
# worker thread.
ps2r_gif_test(test_gif_defer_boundary_order GifDeferBoundaryOrder)

# Vu1GsopsSeed: the corpus half of the parser equivalence -- every recorded VU1
# case's kicked GIF stream (interpreter leg, through GifArbiter -> GifParser ->
# RegisterOpStream) must reproduce the op count + hash recorded in
# fixtures/gsops_seed.txt (126 lines, generated once at stage 5C). SKIP 77 when
# the frozen corpus is absent.
if(TARGET vu1_replay)   # the corpus tool is optional in this tree (tools/vu1_replay may define no executable)
add_test(NAME Vu1GsopsSeed
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/gif/check_gsops_seed.py
            --tool $<TARGET_FILE:vu1_replay>
            --cases ${PS2R_OG_ROOT}/vutrace_cases2
            --seed ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gsops_seed.txt)
set_tests_properties(Vu1GsopsSeed PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)
endif()

# GsStateSink / GsSinkFanout (integration item 6.4): synthetic GIF streams through
# GifParser(fanout{GsStateSink, RegisterOpStream}) -- A+D writes decoded in the
# façade's register file with recorder Reg ops == façade register writes and the
# GIFtag Q reset; PACKED RGBAQ/ST/UV/FOG re-packed (Q non-zero rule, UV latch,
# FOG -> PRIM.FGE) and a strip3 triangle's vertices; an IMAGE upload's pixels in
# VRAM with one upload epoch; EOP = the one GifEop flush (strip parity kept
# across packets, empty EOP flushes nothing); fan-out order / null members / late
# add; FinishPath3 = state-change flush of a non-empty draw buffer only; the
# identity stamp (invariant 28) carried across the arbiter queue per PATH1 slot.
add_executable(test_gs_state_sink test_gs_state_sink.cpp)
target_link_libraries(test_gs_state_sink PRIVATE ps2r_gif_hle)
target_compile_options(test_gs_state_sink PRIVATE -Wall -Wextra -Werror -O2)
foreach(pair "GsStateSinkAdWrites;ad" "GsStateSinkPackedItems;packed" "GsStateSinkPackedFlat;packedflat"
        "GsStateSinkImageUpload;image"
        "GsStateSinkEopFlush;eop" "GsSinkFanoutForwards;fanout" "GsStateSinkFinishPath3;path3"
        "GsStateSinkIdentityStamp;identity" "GsStateSinkAttribution;attribution")
    list(GET pair 0 tname)
    list(GET pair 1 arg)
    add_test(NAME ${tname} COMMAND test_gs_state_sink ${arg})
    set_tests_properties(${tname} PROPERTIES TIMEOUT 120)
endforeach()

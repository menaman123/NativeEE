# tests/gs -- gs/format suites (Phase 2B+). NEW-only unit and property tests;
# the differential oracles against the frozen original live in tests/oracle.

function(ps2r_gs_test name)
    add_executable(${name} format/${name}.cpp)
    target_link_libraries(${name} PRIVATE ps2r_gs_format)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -O2)
    add_test(NAME Gsformat_${name} COMMAND ${name})
    set_tests_properties(Gsformat_${name} PROPERTIES TIMEOUT 120)
endfunction()

# Swizzle: fast path == generic (byte-identical, all PSMs, masks, 4 MB wrap),
# LinearToVram / VramToLinear bijection per PSM over sampled rects.
ps2r_gs_test(test_swizzle)

# ClutLayout: Csm1Index involution, 16-entry identity, the section 2.7.3 figure,
# CSM1 rectangle contiguity, CT16 IDTEX4 order.
ps2r_gs_test(test_clut_layout)

# TexaExpand: 5-bit replicate <-> truncate round trip, Expand16To32 over all 65536
# halfwords, the section 3.4.6 TEXA truth table, register field decode.
ps2r_gs_test(test_texa_expand)

# PixelConvert: pack/unpack identity per PSM (colour, Z, index RMW), element and
# stream widths from the descriptor table, the alpha-domain scale vectors.
ps2r_gs_test(test_pixel_convert)

# BlockDecode: generated page LUT == PsmLayout entry by entry, LUT path == per-pixel
# reference, Decode == DecodeScalar (the SIMD proof under PS2R_SIMD), value rules,
# ReadPalette, footprint / row-band page arithmetic vs brute force.
ps2r_gs_test(test_block_decode)

# MipChain: level halving to 1, MIPTBP field positions (TBW 0 -> 1), the manual's
# section 3.4.11 example table, MXL/MTBA decode, pack/unpack round trip.
ps2r_gs_test(test_mip_chain)

# The two non-boundary libgraph rows (sceGsResetGraph, sceGsSyncVCallback) over
# the GS privileged page and the ps2r::gs::GsPrivRegs shadow, through guest memory.
add_executable(test_libgraph_bindings test_libgraph_bindings.cpp)
target_link_libraries(test_libgraph_bindings PRIVATE ps2r_gs_sync ps2r_gs_state ps2r_ee)
target_compile_options(test_libgraph_bindings PRIVATE -Wall -Wextra -Werror)
add_test(NAME LibgraphBindings COMMAND test_libgraph_bindings)
set_tests_properties(LibgraphBindings PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# Phase 6 (stage 6A): gs/hle/ir suites over ps2r_gs_ir + the tests/lib reader.
# Corpus-reading tests exit 77 (SKIP) when the frozen OLD tree is absent.
set(PS2R_GS_CORPUS "${PS2R_OG_ROOT}/gs_corpus")

function(ps2r_gs_ir_test name)
    add_executable(${name} ir/${name}.cpp)
    target_link_libraries(${name} PRIVATE ps2r_gsdl_reader)  # ps2r_gsdl_reader links ps2r_gs_ir PUBLIC
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -O2)
endfunction()

ps2r_gs_ir_test(test_gsdl_format)
ps2r_gs_ir_test(test_gsdl_reader)
ps2r_gs_ir_test(test_draw_ir)

# AlphaFailMask: the TEST register's static alpha-test arm as a whole-draw write mask
# (ATST NEVER + AFAIL ZB_ONLY = the Z-clear idiom leaves the frame buffer untouched).
ps2r_gs_ir_test(test_alpha_fail_mask)
add_test(NAME GsAlphaFailMask COMMAND test_alpha_fail_mask)
set_tests_properties(GsAlphaFailMask PROPERTIES TIMEOUT 60)

# HalfwordShuffle: Detect accepts exactly the 16-bit sprite column pass over a 32-bit
# buffer (both parities, self / sibling source, DECAL / identity MODULATE, the uniform
# alpha-test arms) and rejects every gate alone (blend term, shape, DATE, per-pixel Z,
# scissor, FBA, TCC = 0, DTHE); Fbmsk16From32 folds RGBA8 onto 5551.
ps2r_gs_ir_test(test_halfword_shuffle)
add_test(NAME GsHalfwordShuffle COMMAND test_halfword_shuffle)
set_tests_properties(GsHalfwordShuffle PROPERTIES TIMEOUT 60)

# G1: every .gsdl struct size / field offset (compile-time and run-time), the hash
# primitives, and the FileHeader decode of a real capture's first 4 KiB.
add_test(NAME GsdlFormatOffsets COMMAND test_gsdl_format ${PS2R_GS_CORPUS}/boot_menu.gsdl)
set_tests_properties(GsdlFormatOffsets PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# G1: synthetic v2 (+ v1) recordings written with the NEW structs read back exactly.
add_test(NAME GsdlReaderRoundTrip COMMAND test_gsdl_reader roundtrip ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(GsdlReaderRoundTrip PROPERTIES TIMEOUT 120)

# G1: the reader's frame/draw/job census of load_transition equals the OLD replay
# tool's CENSUS / PRIM / FLAGS / JOBS lines in the frozen baseline.
add_test(NAME GsdlReaderCensusMatchesOld
    COMMAND test_gsdl_reader census ${PS2R_GS_CORPUS}/load_transition.gsdl
            ${PS2R_GS_CORPUS}/baseline/load_transition.replay.txt)
set_tests_properties(GsdlReaderCensusMatchesOld PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)

# G4 / invariant 27: DrawIrLift == DirectOps under the DrawIrVerifier canonicaliser.
add_test(NAME Gs_Inv27_DrawIrEquivalence COMMAND test_draw_ir equivalence)
set_tests_properties(Gs_Inv27_DrawIrEquivalence PROPERTIES TIMEOUT 300)

# DrawRec -> DrawIr -> DrawRec byte-identical (synthetic + every capture draw).
add_test(NAME GsDrawIrRoundTrip COMMAND test_draw_ir roundtrip ${PS2R_GS_CORPUS}/load_transition.gsdl)
set_tests_properties(GsDrawIrRoundTrip PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)

# G2: drawlist_tool diff <cap> <cap> prints the pinned clean verdict (bounded run).
add_test(NAME GsDrawlistToolDiffClean
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/ir/drawlist_tool_diff_clean.sh
            ${CMAKE_SOURCE_DIR}/verify/run_bounded.sh $<TARGET_FILE:drawlist_tool>
            ${PS2R_GS_CORPUS}/load_transition.gsdl ${CMAKE_CURRENT_BINARY_DIR}/drawlist_tool_diff_clean.log)
set_tests_properties(GsDrawlistToolDiffClean PROPERTIES TIMEOUT 400 SKIP_RETURN_CODE 77)

# ---------------------------------------------------------------------------
# Phase 6 (stage 6B): gs/state, gs/xfer, gs/vram suites over ps2r_gs_state.
function(ps2r_gs_state_test subdir name)
    add_executable(${name} ${subdir}/${name}.cpp)
    target_link_libraries(${name} PRIVATE ps2r_gs_state)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -O2)
endfunction()
ps2r_gs_state_test(vram test_vram_image)
ps2r_gs_state_test(xfer test_transfer_engine)
ps2r_gs_state_test(state test_gs_state)
ps2r_gs_state_test(state test_gs_mmio_handler)

# G3: GsState units (register decode vectors, kick sequences, flush, façade, priv regs).
foreach(pair
        "Gs_Inv01_FirstKickTmeSnapshot;inv01" "Gs_Inv02_SetterFlushesPending;inv02"
        "Gs_Inv28_IdentityStamp;inv28" "Gs_Inv32_DitherInMergeKey;inv32"
        "Gs_Inv38_RoundSpriteNormalise;inv38" "Gs_Inv40_Tex1KSignExtended;inv40" "GsRegistersDecodeVectors;decode"
        "GsVertexKickSequences;kick" "GsVertexKickFlatShading;flat" "GsStateFacadeEmitsDrawIr;facade"
        "GsPrivRegsJobsAndReset;priv")
    list(GET pair 0 tname)
    list(GET pair 1 arg)
    add_test(NAME ${tname} COMMAND test_gs_state ${arg})
    set_tests_properties(${tname} PROPERTIES TIMEOUT 120)
endforeach()

# Integration item 6.2: GsMmioHandler -- the EE's view of the GS privileged page
# over GsState (64-bit CSR RESET -> kCsrAfterReset readable, FINISH + 32-bit W1C;
# 32-bit halves merge either order; DISPFB/DISPLAY/BGCOLOR through the handler
# emit the presenter jobs once per changed value; 16/8-bit lanes widen).
foreach(pair "GsMmioHandlerCsr;csr" "GsMmioHandlerHalves;halves" "GsMmioHandlerJobs;jobs" "GsMmioHandlerWiden;widen")
    list(GET pair 0 tname)
    list(GET pair 1 arg)
    add_test(NAME ${tname} COMMAND test_gs_mmio_handler ${arg})
    set_tests_properties(${tname} PROPERTIES TIMEOUT 120)
endforeach()

# G3: transfer engine (TRXDIR latch, IMAGE re-arm stamp, local->local, cursor).
foreach(pair
        "Gs_Inv03_TrxdirLatch;inv03" "Gs_Inv21_LocalLocalOverGpuPages;inv21"
        "Gs_Inv31_ImageRearmRestamp;inv31" "GsTransferCursor;cursor" "GsTransferStagedStore;staged")
    list(GET pair 0 tname)
    list(GET pair 1 arg)
    add_test(NAME ${tname} COMMAND test_transfer_engine ${arg})
    set_tests_properties(${tname} PROPERTIES TIMEOUT 120)
endforeach()

# G5: VramImage / PageRecord.
foreach(pair
        "Gs_Inv04_SnapshotObservesBytes;inv04" "Gs_Inv05_NewerWriterNotClobbered;inv05"
        "Gs_Inv06_DepositEpochSeparate;inv06" "Gs_Inv07_NoReuseMidDeposit;inv07"
        "Gs_Inv08_SingleWriterMirror;inv08" "Gs_Inv16_ReadFootprintPreciseBits;inv16"
        "Gs_Inv22_ZeroStaleAtSave;inv22" "Gs_Inv36_AliasRefreshPageClear;inv36"
        "Gs_Inv39_ViewHonoursNewerWriter;inv39" "Gs_VramViewSignature;viewsig"
        "Gs_VramOpLogReplay;oplog" "Gs_VramPageOccupancy;pageocc")
    list(GET pair 0 tname)
    list(GET pair 1 arg)
    add_test(NAME ${tname} COMMAND test_vram_image ${arg})
    set_tests_properties(${tname} PROPERTIES TIMEOUT 120)
endforeach()

# ---------------------------------------------------------------------------
# Phase 6 (stage 6D): gs/hle/targets + gs/hle/present suites over the CPU FakeBackend
# (tests/lib/fake_backend.h) -- GL-free; the title profile's VramLayout data.
add_executable(test_targets targets/test_targets.cpp)
target_link_libraries(test_targets PRIVATE ps2r_gs_hle_targets ps2r_fake_backend)
target_compile_options(test_targets PRIVATE -Wall -Wextra -Werror -O2)
add_executable(test_present present/test_present.cpp)
target_link_libraries(test_present PRIVATE ps2r_gs_hle_present ps2r_fake_backend)
target_compile_options(test_present PRIVATE -Wall -Wextra -Werror -O2)

# G7: targets invariants + the uploaded-texture map + the empty-layout contract.
foreach(pair
        "Gs_Inv09_DrawnSinceSyncClamp;inv09" "Gs_Inv10_ZDrawnSinceSyncClamp;inv10"
        "Gs_Inv11_Alt16Bifurcation;inv11" "Gs_Inv12_Alt16CoherenceRect;inv12"
        "Gs_Inv13_GrowOnlyRetagKeepsContent;inv13" "Gs_Inv14_ClassTagsSurviveGrow;inv14"
        "Gs_Inv20_ZAddressingUsesZLayout;inv20" "Gs_Inv23_ScaleOneIdentity;inv23"
        "Gs_Inv24_NativePins;inv24" "Gs_Inv35_MaskedWritebackKeepsBits;inv35"
        "Gs_Inv37_ClearKeyedOnComposite;inv37" "Gs_Inv38_ClearOnlyOnFrameOpeningDraw;inv38"
        "Targets_DrawnRectsByRoute;rects"
        "Targets_PerViewAuthorSeqs;authors" "Targets_ZSaturate;zsat"
        "Targets_SyncAllJob;syncall" "Targets_EncodeVetoes;vetoes"
        "Targets_DepositSpan;span" "Targets_UploadedTextureMap;uploaded"
        "Targets_EvictUnderDraw;evict"
        "Targets_RefreshFromVram;refresh" "Targets_VramLayoutQueries;layout"
        "Targets_RefreshArms;arms" "Targets_RefreshBandStride;stride" "Targets_AliasPreDraw;alias" "Targets_PendingLedger;ledger"
        "Targets_AlphaMergeRoutes;alphamerge"
        "Targets_TargetDims;dims")
    list(GET pair 0 tname)
    list(GET pair 1 arg)
    add_test(NAME ${tname} COMMAND test_targets ${arg})
    set_tests_properties(${tname} PROPERTIES TIMEOUT 120)
endforeach()

# G7: present.
add_test(NAME Present_ViewportMath COMMAND test_present viewport)
add_test(NAME Present_CrtcClassifier COMMAND test_present crtc)
add_test(NAME Present_GrabDisplayFallback COMMAND test_present grab)
add_test(NAME Present_GrabLocalMemory COMMAND test_present grab_local)
add_test(NAME Present_DrawScanoutSource COMMAND test_present drawscanout)
set_tests_properties(Present_ViewportMath Present_CrtcClassifier Present_GrabDisplayFallback
                     Present_GrabLocalMemory Present_DrawScanoutSource PROPERTIES TIMEOUT 120)

# The title profile fills the layout; an empty layout is false everywhere.

# engine/gs/hle/{targets,texsrc,present} include only the backend contract + gs siblings.
add_test(NAME Gs_HleIncludeDepth
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/hle_include_depth.py ${CMAKE_SOURCE_DIR})
set_tests_properties(Gs_HleIncludeDepth PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# Phase 6 (stage 6D'): gs/hle/texsrc over the FakeBackend -- GL-free.
add_executable(test_texsrc texsrc/test_texsrc.cpp)
target_link_libraries(test_texsrc PRIVATE ps2r_gs_hle_texsrc ps2r_fake_backend)
target_compile_options(test_texsrc PRIVATE -Wall -Wextra -Werror -O2)
foreach(pair
        "Gs_Inv15_NeverStaleRtRead;inv15" "Gs_Inv17_PerCallResultsReset;inv17"
        "Gs_Inv18_ContentKeyAddressFree;inv18" "Gs_Inv19_DecodeRectIsCrop;inv19"
        "Gs_Inv33_FogLaneLive;inv33" "Present_DisplayMagnification;magnif"
        "Texsrc_ClutSource;clut" "Texsrc_WindowInTarget;inrt" "Texsrc_Idx16Lane;idx16"
        "Texsrc_L2Entries;l2" "Texsrc_S9ShadowPredicates;s9" "Texsrc_ClutHeal;heal"
        "Texsrc_UvBbox;uvbbox" "Texsrc_ResolveTiers;tiers" "Texsrc_UvRegion;uvregion"
        "Texsrc_DecodeCacheOnMiss;cache_miss" "Texsrc_DecodeFallbackPrepBytes;prep_bytes")
    list(GET pair 0 tname)
    list(GET pair 1 arg)
    add_test(NAME ${tname} COMMAND test_texsrc ${arg})
    set_tests_properties(${tname} PROPERTIES TIMEOUT 120)
endforeach()

# Track 1c-2: the owned texture slots (a slot bound without bytes, served from the
# owner's converted image) and the resolver's OwnedSlot tier.
add_executable(test_owned_texture_slots texsrc/test_owned_texture_slots.cpp)
target_link_libraries(test_owned_texture_slots PRIVATE ps2r_gs_hle_texsrc ps2r_fake_backend)
target_compile_options(test_owned_texture_slots PRIVATE -Wall -Wextra -Werror -O2)
foreach(pair "Texsrc_OwnedSlotsServe;serve" "Texsrc_OwnedSlotsTier;tier" "Texsrc_OwnedSlotsLifecycle;lifecycle")
    list(GET pair 0 tname)
    list(GET pair 1 arg)
    add_test(NAME ${tname} COMMAND test_owned_texture_slots ${arg})
    set_tests_properties(${tname} PROPERTIES TIMEOUT 120)
endforeach()

# The texture-oracle observer (TextureDumpSink): Oracle B -- the fast tier == the per-pixel
# reference over every PSM x CLUT (CPSM / CSM / CSA) x TBW != TW/8 x block-granular bases x
# TEXA x sub-rects, the corrupted-texel control; and the file contract (one write per distinct
# texture, dedup, serves.tsv).
add_executable(test_texture_dump_sink texsrc/test_texture_dump_sink.cpp)
target_link_libraries(test_texture_dump_sink PRIVATE ps2r_gs_hle_texsrc)
target_compile_options(test_texture_dump_sink PRIVATE -Wall -Wextra -Werror -O2)
foreach(pair "Texsrc_DumpSinkOracleB;oracle_b" "Texsrc_DumpSinkFiles;files")
    list(GET pair 0 tname)
    list(GET pair 1 arg)
    add_test(NAME ${tname} COMMAND test_texture_dump_sink ${arg})
    set_tests_properties(${tname} PROPERTIES TIMEOUT 120)
endforeach()

# Phase 8 (P8.21-A): the title logo atlas from the PCSX2 dump through TransferEngine + TextureDecoder.
add_executable(test_title_atlas_decode texsrc/test_title_atlas_decode.cpp)
target_link_libraries(test_title_atlas_decode PRIVATE ps2r_gs_hle_texsrc ps2r_gs_xfer)
target_compile_options(test_title_atlas_decode PRIVATE -Wall -Wextra -Werror -O2)
# The atlas page is a private fixture (game texture bytes): SKIP 77 without $NATIVEEE_PRIVATE_FIXTURES.
ps2r_add_private_fixture_test(NAME Texsrc_TitleAtlasDecode
    FIXTURES games/<title>/verify/baseline/title_screen/logo_atlas_page_f1_xfer243.bin
    COMMAND test_title_atlas_decode
    {PRIVATE_FIXTURES}/games/<title>/verify/baseline/title_screen/logo_atlas_page_f1_xfer243.bin)
set_tests_properties(Texsrc_TitleAtlasDecode PROPERTIES TIMEOUT 120)

# Phase 6 (stage 6E): gs/hle/classrdr over the FakeBackend -- GL-free.
# 6E': the sequence hooks through the pipeline -- the live leaving-capture predicate, hook-before-guards,
# the resolver / cache touch hooks, the ledger hold, the colour-authored Z route.
# 6E'': the stencil-composite renderer -- CLEAR prime, ARM, the three-pass ladder per strip (FakeBackend
# stencil_calls), the ledger record + hold, materialise quads + stamps, the invert predicate + declines,
# the emit grammar; the world-strip steer (colour arm without the hook, disabled slot, alias abort).
# 6E'': the load_transition-shaped synthetic run -- 8 CLEAR primes, 8 hook demotes, 8 CLUT declines -- prints
# the two [GS-S9RDR] lines 6F expects verbatim, in the profile's emitter order.
# Review 6E'' item 1: the profile-owned renderer never calls a backend it does not own from its destructor;
# ReleaseBackendResources is the explicit path; the profile may outlive and be re-registered across Worlds.

# ---------------------------------------------------------------------------
# Phase 6 (stage 6F): tools/gs_replay against the frozen OLD corpus baselines.
# CPU-only census / classdiff gates and the bounded GL replay legs (SKIP 77 when the
# corpus or a GL context is absent).
set(PS2R_REPLAY_WORK ${CMAKE_CURRENT_BINARY_DIR}/replay_work)
# The replay tool is optional in this tree (tools/gs_replay defines no executable when its
# driver sources are absent): the rows that run it are registered only when it exists.
if(TARGET gs_replay)
add_test(NAME GsReplayCensus
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/replay/gs_replay_census.sh $<TARGET_FILE:gs_replay>
            ${PS2R_GS_CORPUS} load_transition ${PS2R_REPLAY_WORK})
set_tests_properties(GsReplayCensus PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
add_test(NAME GsClassdiffMatchesOld
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/replay/gs_classdiff_matches_old.sh $<TARGET_FILE:gs_replay>
            ${PS2R_GS_CORPUS} load_transition ${PS2R_REPLAY_WORK})
set_tests_properties(GsClassdiffMatchesOld PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
add_test(NAME GsReplayDeterminism
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/replay/gs_replay_determinism.sh ${CMAKE_SOURCE_DIR}/verify/run_bounded.sh
            $<TARGET_FILE:gs_replay> ${PS2R_GS_CORPUS} load_transition 600 ${PS2R_REPLAY_WORK})
set_tests_properties(GsReplayDeterminism PROPERTIES TIMEOUT 1300 SKIP_RETURN_CODE 77)
# The 19 corpus legs (G9). A leg is DISABLED until its REPLAY_HASH is baseline-equal
# (PHASE6_IMPL 6F records which are); run a disabled leg by hand through gs_corpus_leg.sh.
set(PS2R_GS_CORPUS_CAPS bad_shadows boot_menu cavern_checker cavern_f13611 cavern_f19112 cavern_f20442
    cavern_flicker_f37152 cavern_fog cavern_gas cavern_mipmap cavern_streaks cavern_wallnoise_f39807 crystal
    fmv_attract island_slot7 load_transition s57_walk save_select slowspot)
# 6F': 18 / 19 hash-equal + .gsrep byte-identical (PHASE6_IMPL 6F' sweep table). slowspot
# stays DISABLED: NEW reproduces the ORIGINAL Phase-A hash 0x7a99f39ddbf9cc18
# (BASELINE.md:38), the frozen file carries the GB2 warmup-frame value 0x99b108280b0c4799
# (P6-SLOWSPOT-GB2-WARMUP, open).
set(PS2R_GS_CORPUS_GREEN bad_shadows boot_menu cavern_checker cavern_f13611 cavern_f19112 cavern_f20442
    cavern_flicker_f37152 cavern_fog cavern_gas cavern_mipmap cavern_streaks cavern_wallnoise_f39807 crystal
    fmv_attract island_slot7 load_transition s57_walk save_select)
foreach(cap ${PS2R_GS_CORPUS_CAPS})
    set(bound 900)
    if(cap STREQUAL "load_transition")
        set(bound 600)
    elseif(cap MATCHES "^(crystal|cavern_f19112|cavern_f20442)$")
        set(bound 1200)   # the 2.3-2.5 GB captures
    endif()
    add_test(NAME GsCorpus_${cap}
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/replay/gs_corpus_leg.sh ${CMAKE_SOURCE_DIR}/verify/run_bounded.sh
                $<TARGET_FILE:gs_replay> ${PS2R_GS_CORPUS} ${cap} ${bound} ${PS2R_REPLAY_WORK})
    math(EXPR leg_timeout "${bound} + 100")
    set_tests_properties(GsCorpus_${cap} PROPERTIES TIMEOUT ${leg_timeout} SKIP_RETURN_CODE 77)
    if(NOT cap IN_LIST PS2R_GS_CORPUS_GREEN)
        set_tests_properties(GsCorpus_${cap} PROPERTIES DISABLED TRUE)
    endif()
endforeach()
endif()   # TARGET gs_replay

# ---------------------------------------------------------------------------
# Phase 6 (stage 6F"): G10 size budgets + the G11 SKIP stubs for the three invariants whose
# owning module is outside Phase 6 (tests/gs/invariants/README.md: exit 77 = honest SKIP,
# never Passed). 35 real Gs_InvNN_* + these 3 = 38 named (ctest -N | grep -c Gs_Inv).
add_test(NAME Phase6SizeBudgets
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_sizes.py ${CMAKE_SOURCE_DIR})
set_tests_properties(Phase6SizeBudgets PROPERTIES TIMEOUT 120)
set(PS2R_GS_INV_STUB bash ${CMAKE_CURRENT_SOURCE_DIR}/invariants/skip_stub.sh)
add_test(NAME Gs_Inv25_InterpRingRotateBeforeFire
    COMMAND ${PS2R_GS_INV_STUB} Gs_Inv25_InterpRingRotateBeforeFire engine/gs/hle/interp/frame_interpolator
            "frame interpolator deferred to integration / Phase 8 (P6-INTERP-DEFERRED); video.interp is OFF in the deterministic profile and every corpus baseline")
add_test(NAME Gs_Inv26_InterpIdentityStampedOnly
    COMMAND ${PS2R_GS_INV_STUB} Gs_Inv26_InterpIdentityStampedOnly engine/gs/hle/interp/frame_interpolator
            "frame interpolator deferred to integration / Phase 8 (P6-INTERP-DEFERRED); OLD pin test_gs_interp.cpp")
add_test(NAME Gs_Inv30_OwnedByPhase5
    COMMAND ${PS2R_GS_INV_STUB} Gs_Inv30_OwnedByPhase5 engine/gif
            "GIF PATH priority P1 > P2 > P3 is the Phase-5 arbiter's; Phase 6 consumes the arbitrated register stream")
set_tests_properties(Gs_Inv25_InterpRingRotateBeforeFire Gs_Inv26_InterpIdentityStampedOnly Gs_Inv30_OwnedByPhase5
    PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)

# ---------------------------------------------------------------------------
# Integration (I6.3-A): gs/hle/queue -- the DrawIR consumer over the FakeBackend (GL-free):
# every JobKind reaches its DrawPipeline entry in stream order, the upload payload lands at
# the next event before the draw that follows, OnFrame -> OnFrameEnd (grab, present skipped
# until the first DISPFB job), the record stamps and the hooks.

# gs/hle/queue -- DrawIrThreadQueue: the cross-thread DrawIR ring (push order kept, the
# upload payload captured at the producer's next event, the CRTC snapshot with the frame
# record, Sync/WaitFrame, lockstep depth 1, Stop drains). No real waits: every wait is on
# a condition the consumer thread satisfies at once.
add_executable(test_draw_ir_thread_queue queue/test_draw_ir_thread_queue.cpp)
target_link_libraries(test_draw_ir_thread_queue PRIVATE ps2r_gs_hle_queue)
target_compile_options(test_draw_ir_thread_queue PRIVATE -Wall -Wextra -Werror -O2)
add_test(NAME DrawIrThreadQueueLockstep COMMAND test_draw_ir_thread_queue lockstep)
add_test(NAME DrawIrThreadQueuePipelined COMMAND test_draw_ir_thread_queue pipelined)
add_test(NAME DrawIrThreadQueueNoPayload COMMAND test_draw_ir_thread_queue nopayload)
add_test(NAME DrawIrThreadQueueConsumerFault COMMAND test_draw_ir_thread_queue fault)
add_test(NAME DrawIrThreadQueueSplitImage COMMAND test_draw_ir_thread_queue splitimage)
# The clock contract, rule 2: one pending frame, the older dropped; pushes never wait.
add_test(NAME DrawIrThreadQueueFrameSkip COMMAND test_draw_ir_thread_queue frameskip)
# Rule 2, review loop 1: the play-mode local->local source served from the EE image /
# the completed-frame snapshot, never a wait.
add_test(NAME DrawIrThreadQueueLocalToLocalSource COMMAND test_draw_ir_thread_queue l2lsource)
set_tests_properties(DrawIrThreadQueueLockstep DrawIrThreadQueuePipelined DrawIrThreadQueueNoPayload
    DrawIrThreadQueueConsumerFault DrawIrThreadQueueSplitImage DrawIrThreadQueueFrameSkip
    DrawIrThreadQueueLocalToLocalSource PROPERTIES TIMEOUT 60)

# P8.13-A: GsSections -- the VRAM + GSPR + GSST savestate sections over GsState
# (round trip byte-equal, restore emits the presenter jobs, wrong lengths /
# versions refused, all three REQUIRED through the registry).
ps2r_gs_state_test(savestate test_gs_sections)
foreach(pair "GsSectionsRoundtrip;roundtrip" "GsSectionsJobs;jobs" "GsSectionsRefuse;refuse" "GsSectionsRegistry;registry" "GsSectionsGsst;gsst")
    list(GET pair 0 tname)
    list(GET pair 1 arg)
    add_test(NAME ${tname} COMMAND test_gs_sections ${arg})
    set_tests_properties(${tname} PROPERTIES TIMEOUT 120)
endforeach()

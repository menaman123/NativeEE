# tests/vu -- Phase 5 VU suites. NEW-only unit and property tests against
# ps2r_vu (decode tables, float rules, the frozen Vu1Case record) plus the
# ported OLD tests as the tiers land. Real corpus files are read from the
# frozen OLD tree (PS2R_OG_ROOT, read-only) and SKIP when it is absent.

function(ps2r_vu_test name ctest_name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ps2r_vu ps2r_gs_ir)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -O2)
    add_test(NAME ${ctest_name} COMMAND ${name} ${ARGN})
    set_tests_properties(${ctest_name} PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endfunction()

# VuDecodeTable: every opcode family of the ONE decode on hand-encoded words --
# operand fields, kinds, VF read/write sets and lane masks, VI write/branch-read
# sets, pipe markers, same-bundle discard/hide hazards, the hard-wired
# predicates, IsBranchKind, and the 60-byte layout pin; plus the SpanClassifier
# passes on a small program.
ps2r_vu_test(test_vu_decode VuDecodeTable)

# VuFloatRules: ClampBits1 table (+/-0, denormals, +/-Inf, NaN, MAX), MAC flag
# nibbles from raw bits (S on -0, Z+U on denormals, O on Inf/NaN), the lane
# select table, WriteMaskClamp/ClampStoreACC, CommitQStatus bit ownership,
# FpMax/FpMin integer ordering, VuDiv by-zero signs, FTOI saturation edges and
# truncation, ITOF, the round-toward-zero guard.
ps2r_vu_test(test_vu_float VuFloatRules)

# Vu1CaseRoundTrip: layout/offset pins, WriteCase -> ReadCase identity on a
# synthetic record, magic/version rejection, and one real corpus case from the
# frozen OLD tree (SKIP when absent) with its image hash pinned to the OLD
# catalog row.
ps2r_vu_test(test_vu1_case Vu1CaseRoundTrip ${PS2R_OG_ROOT}/vutrace_cases2/vu1_case_0000_1748.bin)

# VuFlagPipeline: CLIP ring push/visible at +4, overflow, FCSET drop; MAC ring
# (eager and lazy) push/visible at +4, sticky STATUS bits, dest-lane judging,
# overflow, flush; Q latch at 7/13 with status commit, WAITQ clock jump,
# back-to-back FDIV; P latch retire, early release at retire-1, full-latency
# flush; the EFU latency table; the case-record latch accessors.
ps2r_vu_test(test_vu_flag_pipeline VuFlagPipeline)

# VuLazyFlagsAlgebra: part 1 -- random push/advance/read/Q-commit/flush action
# sequences replayed on a fresh eager and a fresh lazy FlagPipeline -> identical
# read logs, ring views, final registers and clock; part 2 -- random
# straight-line FMAC/DIV/FSSET/consumer programs through the interpreter tier
# on an eager and a lazy unit -> identical registers, data memory and clock.
# Both representations are compiled in every build (the fold is NEON under
# PS2R_SIMD, scalar otherwise). Args: sequences, programs.
ps2r_vu_test(test_lazyflags_algebra VuLazyFlagsAlgebra 250 120)

# VuStallModel: FMAC read-after-write stalls to +4, lane-disjoint reads, VF0,
# ILW -> branch stalls, I-bit bundles, FDIV/EFU busy-waits committing Q / P,
# ring overflow (soonest-retiring dropped), flushes, Reset, and the classed /
# spec entry points against the dynamic body.
ps2r_vu_test(test_vu_stall_model VuStallModel)

# VuSpecial2QRows: the special-2 table's Q column (func 0x3C) in rows 7/8/9 --
# MULAq / ADDAq / SUBAq on hand-computed operands through the interpreter tier
# (dest mask honoured), with the 0x3D column (ABS / MADDAq / MSUBAq) unchanged.
ps2r_vu_test(test_vu_special2_q_rows VuSpecial2QRows)

# VuXgKick: PacketSize over synthetic GIFtags (PACKED/REGLIST/IMAGE/DISABLED,
# NREG 0, chain to EOP, wrap, 0x4000 cancel), the 32-cycle pacing schedule,
# drain-time copy, second-kick stall of remaining_qw*2, program-end flush,
# wrap, cancel, runaway drop, Reset, and both drain arms identical through
# GifArbiter -> GifParser -> RegisterOpStream.
ps2r_vu_test(test_xg_kick VuXgKick)

# VuUnitSurface: VU0/VU1 memory masks on the byte-loop writers/readers, the
# micro epoch, Reset scope, VIF tops, Vu1Case LoadCase/SaveCase round trip.
ps2r_vu_test(test_vu_unit VuUnitSurface)

# ---- interpreter tier (5B part 3) ----
# The retail VU0 fixture images (raw 4 KB code/data captures of the title's microprograms) are
# PRIVATE fixtures (cmake/Ps2rPrivateFixtures.cmake): the tests below resolve
# tests/vu/fixtures/<name> under $NATIVEEE_PRIVATE_FIXTURES through PrivateFixtures and SKIP (77)
# when the captures are absent. The directory here holds only the README.

# Vu1TransformGroundTruth: the retail transform chain whose bit-exact outputs
# discriminate round-toward-zero + fused multiply-add (MULAw/MADDAy/MADDAz/
# MADDx, then DIV/WAITQ/MULq/FTOI0).
ps2r_vu_test(test_vu1_transform_gt Vu1TransformGroundTruth)

# Vu0ShadowReplay: retail VU0 shadow micro, entries 0xA08/0xB90, five register/
# data variants fresh + a 12-step persistent sequence; VI02 == 0 on the captured
# PASS vector, verdict variation, E-bit termination; GT dump printed.
ps2r_vu_test(test_vu0_shadow_replay Vu0ShadowReplay)
target_link_libraries(test_vu0_shadow_replay PRIVATE ps2r_private_fixtures)

# Vu0VisMicro: retail VU0 visibility micro (entry 0x1C0) -- mid-program cut
# (FMAC read stall re-timing an FTOI0 -> MTIR pair) and full-program output
# rows pinned to PCSX2 ground truth.
ps2r_vu_test(test_vu0_vis_micro Vu0VisMicro)
target_link_libraries(test_vu0_vis_micro PRIVATE ps2r_private_fixtures)

# Vu0DecalEval: retail VU0 decal micro (entry 0x1C0) -- VF28/VF30/VF31 and Q
# pinned to retail.
ps2r_vu_test(test_vu0_decal_eval Vu0DecalEval)
target_link_libraries(test_vu0_decal_eval PRIVATE ps2r_private_fixtures)

# VuInterpSmoke: bundle-loop rules on hand-encoded programs (E-bit delay slot
# + TPC, branch delay slot + the ignored delay-slot branch, I-bit ordering,
# VF0/VI0 pins, discard_lower / hide_upper, NOP-sled stop, cycle cap, XTOP
# yield, VU0 self-spin park + ResumeIfWaiting, the VU0 4 KB edge), XGKICK
# issued from a microprogram through the GIF chain, and one real corpus case
# from the frozen OLD tree run end to end (SKIP when absent).
ps2r_vu_test(test_vu_interp_smoke VuInterpSmoke ${PS2R_OG_ROOT}/vutrace_cases2/vu1_case_0000_1748.bin)

# LibVu0SinCosPin: the SDK libvu0 sin/cos fingerprint + the host-identical SineCosine substitution
# (VF31, pins, TPC 0x1C0, no clock change), input clamping, non-matching
# images / entries / VU1 fall through to the microprogram.
ps2r_vu_test(test_lib_vu0_sin_cos LibVu0SinCosPin)

# VuMacroBridge: the EE COP2 <-> VuUnit hand-over behind the generated code's
# VCALLMS / CMSAR1 seams -- unbound park (vu_micro_absent + the Phase-3 detail),
# the SDK sin/cos program at 0xF0 through Vu0Macro::Execute (VF31 / TPC / the
# whole 608-byte block), the retail shadow micro through the seam == a direct
# unit run on five variants (VI02 == 0 on the PASS vector), ResumeIfWaiting on
# a non-suspended unit is a no-op, Vu1Window::Execute advances the VU1 clock.
ps2r_vu_test(test_vu_macro_bridge VuMacroBridge)
target_link_libraries(test_vu_macro_bridge PRIVATE ps2r_private_fixtures)

# ---- cached tier + tier dispatch (5C) ----
# VuCachedSuperblock: cached vs interp on hand-encoded programs (fresh unit per
# leg -> identical regs / data / clock / bundles / kicked op stream), the
# decoded table's stall / fold classes, run lengths and span_rem recomputed,
# the superblock rules (run entry, XTOP / E / I bit in a run, warmup, span
# close materialisation landing a stall, cap backstop mid-span at shifting
# offsets, pacing inside a run and a span), VU0 park + resume through the
# ladder, the epoch fast path + 64-entry eviction, and the dispatch hooks.
ps2r_vu_test(test_vu_cached_tier VuCachedSuperblock)

# VuBlockTier: the block tier against the interpreter on hand-encoded programs
# (block then interp on a fresh unit -> identical regs / data / clock / bundles /
# kicked op stream), each swept over EVERY cycle cap (TPC and the writeback
# ring at a cut), the static stall schedule vs the stall model on 3000 random
# straight-line sequences, the block structure (terminals, merged delay slot,
# static targets, lazily chained successors), stall kinds, the post-issue
# advance guard (a second XGKICK inside a static run), FDIV / WAITQ mid-block,
# XTOP yields in a body and in a delay slot, the NOP sled, the image-end wrap,
# a misaligned entry (interpreter leg), VU0 staying on the cached tier, the
# epoch fast path + 64-entry eviction.
ps2r_vu_test(test_vu_block_tier VuBlockTier)

# Vu1AotSpecializer: the AOT tier's (kind, dest)-specialised bodies against the
# dynamic bodies on every upper / lower encoding under every dest mask and
# several random register files -> identical registers, data, clock, branch
# verdict; every (kind, dest) pair exercised. Arg: random register files per encoding.
ps2r_vu_test(test_vu1_aot_specializer Vu1AotSpecializer 3)

# Vu1AotVerifier: the verification gate on fabricated corpus cases (identical /
# empty / mixed images, the corpus hash) and the tier's status machine
# (unverified -> fallback + observer, verified -> native on the specialised
# table, refused -> dynamic bodies, the interpreter fallback, assume_verified).
ps2r_vu_test(test_vu1_aot_verifier Vu1AotVerifier)

# VuCachedVsInterpCorpus: every recorded VU1 case under the frozen corpus
# directory through the cached, block and AOT tiers (the last with every image
# assumed verified) and the interpreter on fresh units -> identical post regs /
# data / clock / bundles / kicks (SKIP 77 when the corpus is absent), plus the
# three retail VU0 fixture images from their entry points through both tiers.
# The fixture directory is the private root's at run time ({PRIVATE_FIXTURES}/tests/vu/fixtures via the
# runner; no FIXTURES precondition -- the binary skips the images itself and still runs the corpus).
add_executable(test_vu_cached_vs_interp test_vu_cached_vs_interp.cpp)
target_link_libraries(test_vu_cached_vs_interp PRIVATE ps2r_vu ps2r_gs_ir)
target_compile_options(test_vu_cached_vs_interp PRIVATE -Wall -Wextra -Werror -O2)
ps2r_add_private_fixture_test(NAME VuCachedVsInterpCorpus
    COMMAND test_vu_cached_vs_interp ${PS2R_OG_ROOT}/vutrace_cases2 {PRIVATE_FIXTURES}/tests/vu/fixtures)
set_tests_properties(VuCachedVsInterpCorpus PROPERTIES TIMEOUT 600)

# ---- tools/vu1_replay (5C part 2) ----
# Vu1ReplaySelftest: the corpus tool's built-in check -- fabricated case
# round-trip through WriteCase/ReadCase, two interpreter replays identical,
# interp == cached on it (the COMPARE grammar), the transform ground truth.
if(TARGET vu1_replay)   # the corpus tool is optional in this tree (tools/vu1_replay may define no executable)
add_test(NAME Vu1ReplaySelftest COMMAND vu1_replay --selftest)
set_tests_properties(Vu1ReplaySelftest PROPERTIES TIMEOUT 120)

# Vu1ReplayCompareCorpus: the frozen OLD vu1_replay and this tree's tool run
# `--compare` over every corpus case (bounded, OLD cwd pinned under the build
# tree) and their lines must be identical; the first 12 cases additionally cmp
# the --mode interp / cached data + regs dumps byte for byte. SKIP 77 when the
# OLD tool or the corpus is absent.
add_test(NAME Vu1ReplayCompareCorpus
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/vu1_replay/compare_vs_old.py
            --old-bin ${PS2R_OG_ROOT}/build/tests/vu1_replay
            --new-bin $<TARGET_FILE:vu1_replay>
            --cases ${PS2R_OG_ROOT}/vutrace_cases2
            --out ${CMAKE_BINARY_DIR}/tests/vu/compare_vs_old
            --postdata 12)
set_tests_properties(Vu1ReplayCompareCorpus PROPERTIES TIMEOUT 1800 SKIP_RETURN_CODE 77)

# Vu1CollectSweep (5H, gate G5): the whole vutrace_collect corpus (3,293 cases) through
# tools/vu1_replay/sweep_vs_old.sh -- OLD vs NEW `--compare` in 200-case bounded chunks,
# resumable, then aggregate_sweep.py prints `SWEEP cases=3293 same=.. diff=..`. About two
# hours of wall time, so the row carries LABEL long and is DISABLED unless configured with
# -DPS2R_VU1_SWEEP=ON (CMakePresets.json carries no label filter and is not this
# directory's to edit); see tests/vu/README.md for the by-hand invocation. SKIP 77 when
# the OLD tool or the corpus is absent.
option(PS2R_VU1_SWEEP "enable the ~2 h Vu1CollectSweep ctest row (LABEL long)" OFF)
add_test(NAME Vu1CollectSweep
    COMMAND ${CMAKE_SOURCE_DIR}/tools/vu1_replay/sweep_vs_old.sh
            ${CMAKE_BINARY_DIR}/tests/vu/sweep_vs_old 200
            --old-root "${PS2R_OG_ROOT}"
            --new-bin $<TARGET_FILE:vu1_replay>
            --expect 3293)
set_tests_properties(Vu1CollectSweep PROPERTIES LABELS long TIMEOUT 18000 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
if(NOT PS2R_VU1_SWEEP)
  set_tests_properties(Vu1CollectSweep PROPERTIES DISABLED TRUE)
endif()
endif()   # TARGET vu1_replay

# ---- VU1 HLE mechanism + library (5D) ----
# VuGsPacketCodec: GifPacketBuilder -> GsPacketDiff::Decode -> Diff round trip
# (PACKED incl. A+D / ADC / XYZF2, REGLIST, PRE/PRIM, tolerance edges,
# normalisation, strict packets, truncation errors).
add_executable(test_gs_packet test_gs_packet.cpp)
target_link_libraries(test_gs_packet PRIVATE ps2r_vu1_replay_lib)
target_compile_options(test_gs_packet PRIVATE -Wall -Wextra -Werror -O2)
add_test(NAME VuGsPacketCodec COMMAND test_gs_packet)
set_tests_properties(VuGsPacketCodec PROPERTIES TIMEOUT 120)

# Vu1CaptureReplay: one Vu1Case recorded by a title composition's capture probe (a file OUTSIDE
# the tree -- no game bytes are checked in) replayed on a fresh unit through the interpreter tier:
# the kicked GIF stream decoded, the run summary, and an optional per-bundle register trace
# (--trace) for a reference evaluator to diff against. SKIP 77 with no argument / absent file;
# the ctest row carries no argument and always skips -- the tool is run by hand on a capture.
add_executable(test_vu1_capture_replay test_vu1_capture_replay.cpp)
target_link_libraries(test_vu1_capture_replay PRIVATE ps2r_vu1_replay_lib)
target_compile_options(test_vu1_capture_replay PRIVATE -Wall -Wextra -Werror -O2)
add_test(NAME Vu1CaptureReplay COMMAND test_vu1_capture_replay)
set_tests_properties(Vu1CaptureReplay PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# Vu1HleMechanism: Vu1Registry lookup rules, Vu1ImageAnalyzer on synthetic
# images, KickLowering's one-slot-per-kick raw tier, and Vu1HleRuntime::Hook
# through the tier dispatch on one corpus case (kicks / data / TPC equal the
# direct program call; MISS falls through; hle_enabled=false never consults).
# The second case is a material pass with its register block (`node.w=1`): the
# configured lowering delivers the A+D block and the geometry direct
# (unsupported=0) with the raw route's register-op stream.
# Every corpus case then joins the lowering parity sweep (direct vs raw
# register-op stream identical; the strip/material programs lower every kick).

# Vu1HleRuntime's identity observer (integration 6.4, DrawIR invariant 28): each
# HLE'd kick reports {handler name, image hash, entry pc, ordinal} before its
# submit, null after the last; no report on a kick-less run or a MISS.
add_executable(test_vu1_hle_identity test_vu1_hle_identity.cpp)
target_link_libraries(test_vu1_hle_identity PRIVATE ps2r_vu1_replay_lib)
target_compile_options(test_vu1_hle_identity PRIVATE -Wall -Wextra -Werror -O2)
add_test(NAME Vu1HleIdentityObserver COMMAND test_vu1_hle_identity)
set_tests_properties(Vu1HleIdentityObserver PROPERTIES TIMEOUT 120)

# Vu1LayoutKey: the loaded-program layout key -- the analyzer's catalog walk
# (same programs under different stale tails share layout_hash, variant_hash
# differs), lookup precedence image -> variant -> layout -> wildcard, and the
# runtime running a layout-keyed row where the variant key alone misses.
add_executable(test_vu1_layout_key test_vu1_layout_key.cpp)
target_link_libraries(test_vu1_layout_key PRIVATE ps2r_vu1_replay_lib)
target_compile_options(test_vu1_layout_key PRIVATE -Wall -Wextra -Werror -O2)
add_test(NAME Vu1LayoutKey COMMAND test_vu1_layout_key)
set_tests_properties(Vu1LayoutKey PROPERTIES TIMEOUT 120)

add_executable(test_kick_lowering test_kick_lowering.cpp)
target_link_libraries(test_kick_lowering PRIVATE ps2r_vu ps2r_gif_hle ps2r_gs_ir)
target_compile_options(test_kick_lowering PRIVATE -Wall -Wextra -Werror -O2)
add_test(NAME KickLowering COMMAND test_kick_lowering)
set_tests_properties(KickLowering PROPERTIES TIMEOUT 120)

if(TARGET vu1_replay)
# Vu1HleGroupA: the corpus tool's --hle verdict on every group-A case (the
# fam / ttx / pcs / lit programs) must be PASS; SKIP 77 when the corpus is absent.
add_test(NAME Vu1HleGroupA
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/vu1_replay/hle_gate.py
            --bin $<TARGET_FILE:vu1_replay> --cases ${PS2R_OG_ROOT}/vutrace_cases2
            --programs vs_family_tristrip,vs_textured_tristrip,vs_poscolor_strip,vs_lit_tristrip)
set_tests_properties(Vu1HleGroupA PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)

# Vu1HleGroupB: the same gate over the group-B programs (skn / idx / zk / spr,
# incl. the zero-kick continuations).
add_test(NAME Vu1HleGroupB
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/vu1_replay/hle_gate.py
            --bin $<TARGET_FILE:vu1_replay> --cases ${PS2R_OG_ROOT}/vutrace_cases2
            --programs vs_skinned_lit_strip,vs_indexed_displace,vs_zero_kick,vs_zero_kick_cont,vs_zero_kick_a_cont,vs_sprite_text)
set_tests_properties(Vu1HleGroupB PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)

# Vu1HleGroupC: the same gate over the crystal programs (the dual-material
# dispatcher image and the direct displace entries).
add_test(NAME Vu1HleGroupC
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/vu1_replay/hle_gate.py
            --bin $<TARGET_FILE:vu1_replay> --cases ${PS2R_OG_ROOT}/vutrace_cases2
            --programs vs_crys_dual,vs_crys_displace)
set_tests_properties(Vu1HleGroupC PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)

# Vu1HleBossMecha: the Mecha-Bandicoot boss capture (159 cases / 40 images the
# variant key MISSED on the --load-state resume: the known shader-type programs
# under new stale tails; --vu1-miss-cases on int_work/m_series/math/boss_vu1)
# -- every case must be registered (the layout rows) and PASS the interpreter
# oracle; SKIP 77 when the capture is absent.
add_test(NAME Vu1HleBossMecha
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/vu1_replay/hle_gate.py
            --bin $<TARGET_FILE:vu1_replay> --cases ${PS2R_OG_ROOT}/vutrace_boss_mecha --programs all --all --require-registered)
set_tests_properties(Vu1HleBossMecha PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77)

# Vu1ReplayHleCorpus (G3): every corpus case's `--hle --skip-unregistered`
# stdout + exit code identical to the frozen OLD tool's (124 PASS + 2 exit 77);
# the OLD tool is run bounded with cwd pinned under the build tree only for
# cases the reference log lacks. SKIP 77 when OLD or the corpus is absent.
add_test(NAME Vu1ReplayHleCorpus
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/vu1_replay/hle_vs_old.py
            --new-bin $<TARGET_FILE:vu1_replay>
            --cases ${PS2R_OG_ROOT}/vutrace_cases2
            --old-bin ${PS2R_OG_ROOT}/build/tests/vu1_replay
            --old-cwd ${CMAKE_BINARY_DIR}/tests/vu/hle_vs_old_oldcwd
            --out ${CMAKE_BINARY_DIR}/tests/vu/hle_vs_old.log
            --quiet)
set_tests_properties(Vu1ReplayHleCorpus PROPERTIES TIMEOUT 1800 SKIP_RETURN_CODE 77)

# Vu1CatalogIdentity (G4): `--catalog` over the corpus equals the frozen OLD
# tool's catalog line for line (the `[VU-MODE]` announce skipped). SKIP 77 when
# OLD or the corpus is absent.
add_test(NAME Vu1CatalogIdentity
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/vu1_replay/catalog_vs_old.py
            --new-bin $<TARGET_FILE:vu1_replay>
            --old-bin ${PS2R_OG_ROOT}/build/tests/vu1_replay
            --old-cwd ${CMAKE_BINARY_DIR}/tests/vu/catalog_vs_old_oldcwd
            --cases ${PS2R_OG_ROOT}/vutrace_cases2
            --out ${CMAKE_BINARY_DIR}/tests/vu/catalog_vs_old)
set_tests_properties(Vu1CatalogIdentity PROPERTIES TIMEOUT 1200 SKIP_RETURN_CODE 77)
endif()   # TARGET vu1_replay

# Vu1LibraryPurity (G8): the family signature literals, every vs_* stamp and
# every image / variant hash of the library appear nowhere under engine/ or tools/.
add_test(NAME Vu1LibraryPurity
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/vu/check_vu1_library_purity.py --root ${CMAKE_SOURCE_DIR})
set_tests_properties(Vu1LibraryPurity PROPERTIES TIMEOUT 120)

# Vu0RegistryCensus: the VU0 macro-program census mechanism on the title table.

# Vu1HleRef_<case>: the OLD tool's `--hle --hle-ref --exact` rows on the first
# 8 sorted corpus cases -- the library program under the EXACT tolerance with
# the interpreter appended as the wildcard reference. The tool exits 2 on an
# unreadable case, so the rows are registered only when the corpus is present.
if(TARGET vu1_replay AND EXISTS "${PS2R_OG_ROOT}/vutrace_cases2")
    foreach(i RANGE 0 7)
        add_test(NAME Vu1HleRef_vu1_case_000${i}_1748
            COMMAND vu1_replay --hle --hle-ref --exact --skip-unregistered
                    ${PS2R_OG_ROOT}/vutrace_cases2/vu1_case_000${i}_1748.bin)
        set_tests_properties(Vu1HleRef_vu1_case_000${i}_1748 PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
    endforeach()
endif()

# ---- MTVU (5G part 1) ----
# VuRunaheadGuard: the OLD test_runahead_guard asserts (depth clamp, over =
# depth > cap+1, 30-present window, one report per episode, recovery re-arm,
# 5-report cap, the cap-0 ratchet) plus EffMaxFrames 2 -> 3 under the injected
# low-band predicate only, Over(), and the 100 ms frame valve on a fake clock.
ps2r_vu_test(test_runahead_guard VuRunaheadGuard)

# VuMtvuBackpressure: the producer park loop on a fake pipeline + fake clock --
# fast path, byte high water with 7/8 hysteresis, the idle-worker stall valve,
# frame park released by present progress, the 100 ms frame valve, depth 0
# never parks, lifted cap, both arms together, shutdown flag, eager drain per
# iteration, sync-waiter balance, spin clamp / recency constants.
ps2r_vu_test(test_mtvu_backpressure VuMtvuBackpressure)


# ---- MTVU (5G part 2) ----
# VuMtvuWorker: the worker against the real VIF + VU1 + GIF arbiter -- a VIF1
# chain (MPG / UNPACK / MSCAL+XGKICK / DIRECT) fed inline, per span, staged and
# through the DMAC thunks gives identical VU1 memories, registers and arbiter
# RegisterOpStream; the TTE bracket; the reset-latch segment; the Batch record
# layout and the 32-record / 32 KiB flush thresholds; Stop drains and joins.
# Starts the worker thread: serial. Drives the real Vif (ps2r_vif, which
# carries ps2r_vu).
add_executable(test_mtvu_worker test_mtvu_worker.cpp)
target_link_libraries(test_mtvu_worker PRIVATE ps2r_vif ps2r_gs_ir)
target_compile_options(test_mtvu_worker PRIVATE -Wall -Wextra -Werror -O2)
add_test(NAME VuMtvuWorker COMMAND test_mtvu_worker)
set_tests_properties(VuMtvuWorker PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

# VuMtvuSync: the EE side -- Sync while the worker is held mid-span by a gated
# fake feed eager-drains the arbiter on the EE thread (the drain releases the
# gate), then idle; Sync + TryEagerDrain gives the inline RegisterOpStream;
# InstallInto redirects VuUnit::Execute on the inline unit to Sync(Vu1EeKick) +
# the worker's unit (identical to inline, inline unit untouched); the VIF1_STAT
# busy view only while in flight; site counters; DeferOn. Fake clock, predicate
# waits only. Starts the worker thread: serial.
add_executable(test_mtvu_sync test_mtvu_sync.cpp)
target_link_libraries(test_mtvu_sync PRIVATE ps2r_vif ps2r_gs_ir)
target_compile_options(test_mtvu_sync PRIVATE -Wall -Wextra -Werror -O2)
add_test(NAME VuMtvuSync COMMAND test_mtvu_sync)
set_tests_properties(VuMtvuSync PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

# VuSavestateSections: the VU0C/VU0D/VU1C/VU1D memory sections (VuMemorySection)
# and VU1R (Vu1RegisterSection) -- round trip into fresh units byte-equal, the
# exact lengths (4096/4096/16384/16384; 1605 with size 1600), wrong lengths /
# sizes refused, REQUIRED memories vs OPTIONAL VU1R through the registry, the
# micro-epoch bump + VU1 callback on restore, the frozen VU1R layout (running 0,
# VUregs at image offset 992).
ps2r_vu_test(test_vu_savestate_sections VuSavestateSections)

# VuVifGifSectionsAnchors: each of the 11 frozen anchors (OLD save_states,
# read-only; SKIP 77 when absent) -> the eight blobs VU0C VU0D VU1C VU1D DMAC
# VIFS GIFS VU1R present + REQUIRED with the exact reference lengths, validated
# and restored through this tree's codecs into a fresh rig in the root's
# registration order, then re-serialized byte-identical to the file's payloads.
add_executable(test_vu_vif_gif_sections_anchors test_vu_vif_gif_sections_anchors.cpp)
target_link_libraries(test_vu_vif_gif_sections_anchors PRIVATE ps2r_dmac ps2r_gs_ir)
target_compile_options(test_vu_vif_gif_sections_anchors PRIVATE -Wall -Wextra -Werror -O2)
add_test(NAME VuVifGifSectionsAnchors COMMAND test_vu_vif_gif_sections_anchors
    "${PS2R_GAMES_DIR}/${PS2R_TITLE}/verify/restore_mhash_expected.tsv"
    "${PS2R_OG_ROOT}/save_states")
set_tests_properties(VuVifGifSectionsAnchors PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# Vu0TraceCensus: the VU0 macro-mode operand census (the --vu0-trace diagnostics
# mechanism): Parse (bundle truncation, rejects), Wants, the bridge's Begin/End
# records of the COP2 block around an armed VCALLMS (16 VF lines each side, ACC,
# Q, I in), an unarmed pc records nothing, Summary per pc, Flush, SetTrace(nullptr).
ps2r_vu_test(test_vu0_trace Vu0TraceCensus)

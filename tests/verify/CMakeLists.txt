# tests/verify -- self-checks of the harness tools under verify/ (ours; no binary needed).
# Every row carries a TIMEOUT (tests/CMakeLists.txt's walk would otherwise set 120 s).

add_test(NAME VerifySubsystemReachabilitySelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/subsystem_reachability.py --selftest)
set_tests_properties(VerifySubsystemReachabilitySelftest PROPERTIES TIMEOUT 60)

# VerifyScoreSelftest (I8.13-T, DECISIONS_LOG DT-1): score.py's unit checks over synthetic
# inputs -- the outside-mask word rule (RAM0 + SPAD, META never), the missing-NEW-image rule
# (the OLD image's full word count), the battery table parser and the identity rule
# (ADMISSIBLE + IDENTICAL(frames,audio,mem); the 1-rep preview), the ctest pass-count
# parser, the first-divergent keys, --compare's lexicographic rule with the pins override,
# and an end-to-end tuple over a synthetic battery dir + probe pairs (precomputed too).
add_test(NAME VerifyScoreSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/score.py --selftest)
set_tests_properties(VerifyScoreSelftest PROPERTIES TIMEOUT 60)

# VerifyBatterySelftest: battery.py's argv construction -- --timeout and the repeatable
# --set key=val pass-through reach run_leg.py's argv, and only when given.
add_test(NAME VerifyBatterySelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/battery.py --selftest)
set_tests_properties(VerifyBatterySelftest PROPERTIES TIMEOUT 60)

# VerifyRunLegSelftest: run_leg.py's NEW argv -- `--set key=val` once per value, in order,
# after the harness arming; absent without --set; --load-state from --anchor; the stall
# watchdog rule (--stall-s wins over the inherited RB_STALL_S, which wins over the default).
add_test(NAME VerifyRunLegSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/run_leg.py --selftest)
set_tests_properties(VerifyRunLegSelftest PROPERTIES TIMEOUT 60)

# VerifyLegSpecSelftest (P8.22-T, DECISIONS_LOG DT-5): leg_spec.py -- the per-leg LEG.tsv run
# contract (frames, state_every, timeout_s, stall_s, probes B[@F], notes): absent file = None
# (the twelve legs keep today's behaviour), the integer columns, the probe tokens (a bare B
# defaults F to frames; a probe off the state_every cadence is an error), effective_timeout =
# max(caller, spec), stall_s, and every malformed file a LegSpecError (never a silent default).
add_test(NAME VerifyLegSpecSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/leg_spec.py)
set_tests_properties(VerifyLegSpecSelftest PROPERTIES TIMEOUT 60)

# VerifyFreezeSessionLegSelftest (PLAY_SESSIONS.md): freeze_session_leg.py -- a NEW-recorded play
# session becomes a `source=new` leg only when every replay rep is identical on frames/audio; the
# refusals (differing rep, overlong recording, existing leg) write nothing; acceptance writes the
# lanes, LEG.tsv session columns, mc_before, FREEZE.md, pads/<leg>.padlog and the anchors.tsv row.
add_test(NAME VerifyFreezeSessionLegSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/freeze_session_leg.py --selftest)
set_tests_properties(VerifyFreezeSessionLegSelftest PROPERTIES TIMEOUT 60)

# VerifyPlatformIdentitySelftest (Charter 2 Track B): platform_identity.py -- the cross-host lane
# compare pairs by key, gates the four CPU lanes, reports + attributes the pixel lane, refuses on a
# run.meta mismatch; the title's frozen macOS reference set parses and is self-identical.
add_test(NAME VerifyPlatformIdentitySelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/platform_identity.py --selftest)
set_tests_properties(VerifyPlatformIdentitySelftest PROPERTIES TIMEOUT 60
    ENVIRONMENT "PS2R_PLATFORM_REF=${PS2R_GAMES_DIR}/${PS2R_TITLE}/verify/platform_ref/s3")
# VerifyPlatformIdentity: the gate itself when a candidate leg of another host is handed in
# (-DPS2R_PLATFORM_CANDIDATE=<leg dir> at configure time); SKIP 77 without one. The candidate is
# produced by verify/run_leg.py on the other host (platform_ref/README.md); game files never enter CI.
set(PS2R_PLATFORM_CANDIDATE "" CACHE PATH "A leg dir replayed on another host for VerifyPlatformIdentity")
if(PS2R_PLATFORM_CANDIDATE)
    add_test(NAME VerifyPlatformIdentity
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/platform_identity.py
                --ref ${PS2R_GAMES_DIR}/${PS2R_TITLE}/verify/platform_ref/s3
                --candidate ${PS2R_PLATFORM_CANDIDATE} --report ${CMAKE_BINARY_DIR}/platform_identity.md)
else()
    add_test(NAME VerifyPlatformIdentity COMMAND ${CMAKE_COMMAND} -E env sh -c "echo 'VerifyPlatformIdentity: SKIP (no PS2R_PLATFORM_CANDIDATE)'; exit 77")
endif()
set_tests_properties(VerifyPlatformIdentity PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# CiAssetGuard* (Charter 2 Track B3): tools/ci/asset_guard.py -- game files NEVER in CI. The
# selftest covers the three classes (A disc/extracted files by name and magic; B game-derived
# oracle images; C pinned fixtures), the modes and the artifact folder shape. CiAssetGuardTree
# scans THIS checkout in --mode local: class A anywhere, or a pinned fixture whose bytes changed,
# or a new unpinned binary blob, fails here before CI sees it (class B is reported: the local
# tree carries the Root legs' images; CI's sparse checkout excludes them and runs --mode ci).
# On a git work tree --mode local scans the TRACKED set: the title's untracked play output
# beside the binary (memcards*/, savestates) is game-derived by construction and not in any
# checkout.
add_test(NAME CiAssetGuardSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/ci/asset_guard.py --selftest)
set_tests_properties(CiAssetGuardSelftest PROPERTIES TIMEOUT 60)
add_test(NAME CiAssetGuardTree
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/ci/asset_guard.py --root ${CMAKE_SOURCE_DIR} --mode local)
set_tests_properties(CiAssetGuardTree PROPERTIES TIMEOUT 300)

# ToolsSifStartupCallersSelftest (M11 startup audit, docs/design/M11_STARTUP_AUDIT.md): the static
# chain walk from a SIF SDK entry to its first title-code callers, bounded cycles, empty roots.
add_test(NAME ToolsSifStartupCallersSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/sif_startup_callers.py --selftest)
set_tests_properties(ToolsSifStartupCallersSelftest PROPERTIES TIMEOUT 60)

# VerifyProbeImagesSelftest (P8.22-T): probe_images.py's probe plans -- the built-in plans for a
# leg without LEG.tsv (anchors b2/b60, coldboot b60/b120), the spec-driven plans for a leg with one
# (OLD one run per probe at --state-every B-1 -> state_b<B-1> = boundary B; NEW one run at the leg's
# cadence keeping the probe boundaries), --timeout / --stall-s in the argv only when given, and the
# proof rule (mem.mhash line equality; no line and no sibling = UNPROVED).
add_test(NAME VerifyProbeImagesSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/probe_images.py --selftest)
set_tests_properties(VerifyProbeImagesSelftest PROPERTIES TIMEOUT 60)

# VerifyAttributeSelftest (P8.11-B): attribute_probe.py over synthetic images -- the rule order
# (exact ROW rule, writer-gated rule vs the census, MIXED region rule, the legs column, the mask
# skip, SPAD by guest address), the UNATTRIBUTED listing and the BLOCKED / IDENTICAL verdicts.
add_test(NAME VerifyAttributeSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/attribute_probe.py --selftest)
set_tests_properties(VerifyAttributeSelftest PROPERTIES TIMEOUT 60)

# VerifyAttributeRelocateSelftest (P8.11-B): relocate_compare.py over synthetic images -- the title
# heap walk against its header counts, the META-descriptor record-pool walk, record matching by
# (size, pointer-insensitive fingerprint) then unmoved then similarity, the OLD-only block named,
# the word classes (reloc-content for a swapped pair, reloc-pointer inside and outside the arenas,
# one genuine word, the heap-header words as `other`, the masked word skipped), self-compare = 0.
add_test(NAME VerifyAttributeRelocateSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/relocate_compare.py --selftest)
set_tests_properties(VerifyAttributeRelocateSelftest PROPERTIES TIMEOUT 60)

# VerifySlabWalkSelftest (P8.13-B): slab_walk.py over a synthetic page pool -- the walk against the
# allocator's own state (free-page stack vs the header count, class lists, class table, per-page free-chunk
# lists vs the descriptor counts), the word classes (a swapped pair of chunks is reloc-equal, one genuine
# word, a NEW-only object over a free page is alloc-differs, free residue in free chunks/pages, header /
# class-table / descriptor words), invariant violations are errors, self-compare = 0.
add_test(NAME VerifySlabWalkSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/slab_walk.py --selftest)
set_tests_properties(VerifySlabWalkSelftest PROPERTIES TIMEOUT 60)

# VerifySpadStackSelftest (P8.13-B): spad_stack.py over synthetic images with a THRD section -- the boundary
# thread's $sp read from the saved CpuContext, the 14-bit scratchpad mirror (0x70004220 -> 0x220), the live
# frames bounded by the crt0's fill word, the dead-below-sp / live-frame / decoder-work classes, the per-pair
# conditions C1-C3 (C3 over EVERY thread's chain against the libmpeg+libipu band, fail-closed without a DMAC
# section or a band), and an sp disagreement proving nothing (spad-sp-unproved).
add_test(NAME VerifySpadStackSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/spad_stack.py --selftest)
set_tests_properties(VerifySpadStackSelftest PROPERTIES TIMEOUT 60)

# VerifyAttributeAudioSelftest (P8.15-B): attribute_audio.py over synthetic audio.ahash lanes and SNDC/IOPM
# images -- the first divergent 4800-frame window and its boundary span, the NEW-rep agreement rule, the
# per-voice classes (dead-heartbeat / restore-phase inside W0 only / content / record-only / other), the
# evidence-pair choice (inside the window, else the first after it), the window verdicts, and the coldboot
# onset/content comparison (first non-silent window per side, aligned-by-the-edge and anywhere equality).
add_test(NAME VerifyAttributeAudioSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/attribute_audio.py --selftest)
set_tests_properties(VerifyAttributeAudioSelftest PROPERTIES TIMEOUT 60)

# VerifyRebaselineSelftest / VerifyApplyRebaselineSelftest (P8.17-B, DT-4 proposed): the re-baseline manifest
# grammar (`games/<title>/verify/baseline/<leg>/REBASELINE.tsv`: lane, window, rows, commit, proof, old/new line, image), the
# effective reference it defines (the lane line / probe image of a re-based window read from `rebased/`, the
# frozen files untouched), the verdict-line report text; and the ledger-gated writer: REFUSES a window whose
# ledger carries any OPEN / MIXED / UNATTRIBUTED word, an unfiled row id, a booked row missing from --rows, a
# ledger of another leg/boundary, a duplicate row -- and writes nothing on refusal; on acceptance writes the
# manifest row + rebased/ copies only (frozen digests equal before and after).
add_test(NAME VerifyRebaselineSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/rebaseline.py --selftest)
set_tests_properties(VerifyRebaselineSelftest PROPERTIES TIMEOUT 60)
add_test(NAME VerifyApplyRebaselineSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/apply_rebaseline.py --selftest)
set_tests_properties(VerifyApplyRebaselineSelftest PROPERTIES TIMEOUT 60)
# VerifyGateRunSelftest (P8.17-B): gate_run.py's re-based-window report lines (pass-through of the manifest).
add_test(NAME VerifyGateRunSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/gate_run.py --selftest)
set_tests_properties(VerifyGateRunSelftest PROPERTIES TIMEOUT 60)
# VerifySlabCensusSelftest (P8.17-B): slab_census.py over a synthetic page pool + census log -- pop/push/page-init
# decoding by site, the census classes (new-progress with boundary + site, old-held-new-freed, inherited, genuine),
# the window bound, the histograms.
add_test(NAME VerifySlabCensusSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/slab_census.py --selftest)
set_tests_properties(VerifySlabCensusSelftest PROPERTIES TIMEOUT 60)
# VerifyClockPairSelftest (P8.25-T, DECISIONS_LOG DT-6): clock_pair.py over synthetic images -- the census of the
# title clock word 0x323534 and the boot-SM word 0x98c140 (case, bit 63) per image, the census.tsv round-trip, the
# exact-clock match (reported, none expected inside one leg), the scene-edge pairing (old_b<B> -> new_b<B + offset>
# of the last edge <= B), the mapped image's scene check (SCENE-MISMATCH named), the wrong-header refusal, the
# 149/boundary residual (the +2048 jump reported), the pair_map text and the edge grammar.
add_test(NAME VerifyClockPairSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/clock_pair.py --selftest)
set_tests_properties(VerifyClockPairSelftest PROPERTIES TIMEOUT 60)

# VerifyAssetNamesSelftest (P8.26-B, DECISIONS_LOG LEDGER): asset_names.py over a synthetic archive index + archive --
# the extent walk checked against the archive size, short/ambiguous fingerprints flagged, a record named only when a
# whole file sits at its start (the asset-heap records of relocate_compare.py --names).
add_test(NAME VerifyAssetNamesSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/asset_names.py --selftest)
set_tests_properties(VerifyAssetNamesSelftest PROPERTIES TIMEOUT 60)

# VerifyConsoleCompareSelftest (P8.29-B, DECISIONS_LOG LEDGER "console arbitration"): console_compare.py over a
# synthetic raw EE image + two .ps2st images -- the one accessor resolving the record pool's descriptors in RAM
# (the console) or META (the trees), the live-counter check, the three-way content sets of heap blocks / slab
# objects / records (ONE pointer-arena list on every side, the META range in it with the span derived from the
# images' META section; whole-object fingerprints -- a shared
# META-range data word still matches, a record differing only beyond 4 KB does not match uncapped and does under
# --fp-limit 4096), the one-sided records' presence on the console, the reassigned-page verdict, the routed-node
# slab class flagged, plain words, the refusals (bad counter, short image).
add_test(NAME VerifyConsoleCompareSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/console_compare.py --selftest)
set_tests_properties(VerifyConsoleCompareSelftest PROPERTIES TIMEOUT 60)

# VerifyScenePairingSelftest (P8.30-T, DECISIONS_LOG DT-6 per run / DT-7): scene_pairing.py -- the per-run scene edge: find_edge
# over a synthetic census (first case 7 after a case 6; an unreadable image breaks adjacency), the PAIRING.tsv record round trip,
# the run's offset on every mapped probe with the frozen map as the reference (note + verdict text with the deviation), EDGE-NOT-FOUND,
# the malformed-record refusals, the census leg end to end against a synthetic image-writer process (every image read as it lands
# and deleted), derive() with the pass-2 check (VERIFIED / EDGE2-MISMATCH) and the one retry at 4x the margin.
add_test(NAME VerifyScenePairingSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/scene_pairing.py --selftest)
set_tests_properties(VerifyScenePairingSelftest PROPERTIES TIMEOUT 60)

# VerifyRegressionRefSelftest (P8.35-C, DECISIONS_LOG DT-8): regression_ref.py -- the standing regression gate over the
# 13-leg battery: the REGRESSION_REF.tsv round trip (per-leg frames/audio/mem first-divergent keys, raw/booked/unbooked
# words, the ALL row, the header's legs_identical/pins), and `score.py --regression`'s rule -- a frames/audio key that
# moved EARLIER fails the leg, LATER passes, IDENTICAL ranks above every key (0 is a real key), the mem lane and the
# per-leg words are reported only, a higher raw OR unbooked total fails, fewer identical legs fails, a missing /
# RUN-FAIL leg fails closed, a pins regression fails (pins not run is no claim), malformed references refuse.
add_test(NAME VerifyRegressionRefSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/regression_ref.py --selftest)
add_test(NAME VerifyRetirementGateSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/retirement_gate.py --selftest)
set_tests_properties(VerifyRegressionRefSelftest PROPERTIES TIMEOUT 60)

# TitleSymbolsTable (P8.36-D, CONSTRUCTION_PLAN 4.8 games/<t>/symbols): symbols.tsv is byte-identical to a fresh
# build from its sources (recomp/recompiled.h declarations + the install table, hle/sdk_bindings.tsv, the manual-body
# ledger, symbols/adjudicated_names.tsv), every address unique and inside the generated text, every name sourced.
# --check reads the GENERATED recompiled.h / recompiled.cpp (PS2R_RECOMP_DIR): a title test.
if(TARGET ps2r_title_recomp)
    add_test(NAME TitleSymbolsTable
        COMMAND ${Python3_EXECUTABLE} ${PS2R_GAMES_DIR}/${PS2R_TITLE}/symbols/build_symbols.py --check)
    set_tests_properties(TitleSymbolsTable PROPERTIES TIMEOUT 60 ENVIRONMENT "${PS2R_RECOMP_TEST_ENV}")
endif()
add_test(NAME TitleSymbolsTableSelftest
    COMMAND ${Python3_EXECUTABLE} ${PS2R_GAMES_DIR}/${PS2R_TITLE}/symbols/build_symbols.py --selftest)
set_tests_properties(TitleSymbolsTableSelftest PROPERTIES TIMEOUT 60)

# VerifyAnchorsTable (P8.36-D): games/<t>/verify/anchors.tsv (the 13-leg index) equals the frozen baseline tree --
# leg set, frames (LEG.tsv or the frames.fhash line count), probe boundaries, anchor path, pad script presence.
# The probe column needs the frozen RAM probes (private fixtures): SKIP 77 without $NATIVEEE_PRIVATE_FIXTURES.
add_test(NAME VerifyAnchorsTable
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/anchors_table.py)
set_tests_properties(VerifyAnchorsTable PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
add_test(NAME VerifyAnchorsTableSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/anchors_table.py --selftest)
set_tests_properties(VerifyAnchorsTableSelftest PROPERTIES TIMEOUT 60)

# VerifyComponentIdentitySelftest (Charter 2 Track B3): component_identity.py -- the identity CI
# checks without the disc: the math oracles' --digest channels collected into the `oracles` lane
# (<Oracle>/<channel>,sha256 + run.meta) and compared across hosts through platform_identity.py.
add_test(NAME VerifyComponentIdentitySelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/verify/component_identity.py --selftest)
set_tests_properties(VerifyComponentIdentitySelftest PROPERTIES TIMEOUT 60)

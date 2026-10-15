# FILEMAP — host_app shard 4 (37 files, 24,607 lines)

Sources: ARCHITECTURE.md §3/§11/§13/§14-§18, MIGRATION_PLAN.md §1, A2_CONFIG_DIAG.md
(env registry + dispositions), A6_GAME_SPECIFICITY.md, HOOK_RECLASSIFICATION.md.
Peak config (run_play.sh): PS2_GS_SCALE=2 PS2_GS_INTERP=1 PS2_FPS_CAP=60 PS2_MTVU=1
PS2_GL_FAST=1 PS2_GL_FAST2=1 PS2_VU1_HLE=1 PS2_VU1_HLE_DIRECT=1 — everything it turns
on becomes NORM; SCALE/INTERP/FPS_CAP become SETTINGs.

Outcome legend: NORM = winning arm becomes the only code path, losing arm deleted;
SETTING = Config field; DIAG = core/diag or core/triage, compile-time, default off;
TEST-ONLY = harness hook; DELETE = experiment concluded, both arms gone.

---

### host_app/gif.cpp (5,532 lines)

- **Purpose**: The GIF unit — parses GIFtags and dispatches PACKED/REGLIST/IMAGE data
  from all three paths (PATH1 XGKICK via arbiter, PATH2 VIF1 DIRECT, PATH3 GIF DMA)
  into `g_gs_state` register writes and vertex kicks. Owns the MMIO GIF register
  surface (Read/Write), the DMA buffer (ProcessBuffer → ParseSpan), the bulk-fast
  PACKED parse lane ([R158-GIFFAST]) with a SIMD vertex-staging kernel ([W10-GSKSIMD]),
  the decode-once packet-template cache ([M1T] TplTryServe), and the typed HLE apply
  paths (ApplyHleDirectOps / ApplyHleDrawIR) that consume VU1-HLE DIRECT op streams
  and DrawIR without re-parsing bytes.
- **Public surface**: `GIF g_gif` global; `GIF::{Reset,Read,Write,ReceiveData,
  FlushBatch,ProcessBuffer,ParseSpan,ProcessPacket,ProcessPacked,ProcessReglist,
  ProcessImage,ApplyHleDirectOps,ApplyHleDrawIR,FinishDMA,TplTryServe}`; bench riders
  `MidCensusEmitForBench`, `GifEmitR158CensusForBench`, `GifTplEmitForBench`,
  `ObjStripReportHook`; test hooks `GifFastOverrideForTest`, `GskSimdOverrideForTest`;
  census atomics `g_w10_gsksimd_{verts,runs}`. Called by dmac.cpp, vif.cpp,
  gif_arbiter, memory.cpp (MMIO), vu1_hle_runtime (typed payload delivery).
- **Classification**: MIXED (A6 §3.3 row C — generic GIF idiom machinery; 4 VRAM-keyed
  diagnostic hits at :1848/:1912/:2432/:2655 on 0x70000/0x150000). Subsystem: gif/gs.
- **Refactor destination** (MIGRATION §1.3 row): split —
  - `engine/gif/gif_parser.{h,cpp}` (class `GifParser`): ParseSpan canonical walk,
    tag decode, PACKED per-register decode bodies (GifPackedPRIM/RGBAQ/ST/UV/FOG,
    GifDecodeXYZ*, the single source of truth at :3211-3265), REGLIST, IMAGE routing.
    Legacy `ProcessPacked` switch + slow per-qword loop DELETED (GIF_FAST NORM).
  - `engine/gif/gif_regs.{h,cpp}` (class `GifRegs`): MMIO Read/Write, mode/PATH3 mask,
    FIFO state; registered with the ee/mem MMIO router.
  - `engine/gif/packet_template.{h,cpp}` (class `PacketTemplateCache`): TplWalk/
    TplTryServe/TplReport (pairs with gif_template.h from its own shard row).
  - `engine/gs/state/flush.*`: FlushBatch merges into the ONE flush (two entry
    points, §1.5 rewrite row; G-C1 verifier gates it).
  - `engine/gs/state/ir_apply.{h,cpp}` (class `DrawIrApplier`): ApplyHleDirectOps +
    ApplyHleDrawIR — the typed-payload consumers (kick-run native/bulk entry into
    vertex kick). TREE-GAP: the tree names gs/state/{registers,vertex_kick,flush};
    ir_apply is proposed as the typed-entry sibling.
  - GskSimdStageTriple SIMD kernel → gif_parser internal (NORM'd, scalar deleted).
  - All census/probe blocks → deleted or core/diag (see hacks).
- **Flags consumed here**:

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_GIF_FAST | ON unless "0" | bulk PACKED fast lane vs legacy per-qword parse | delete-losing-arm | NORM — fast lane only; legacy loop + GifDiagOn dual gating deleted |
  | PS2_GSK_SIMD | ON unless =0 | SIMD strip3 vertex staging vs scalar triple loop | delete-losing-arm | NORM — SIMD lane only |
  | PS2_SHADOW8_KTAP | OFF (num-if-set) | shadow-strip ingestion taps in ReceiveData (:391) / ProcessPacket (:4337) / PACKED (:2842) | move-to-diagnostics-build (mirrored, 16 sites/7 files) | DIAG — one probe in core/diag keyed off DrawIR provenance; the 3 in-file mirror latches die |
  | PS2_S26_DECAL | unarmed (minvf) | decal-stamp GIF-tag tap (:3686) for solved SHADOW2 loop-21 | delete-both | DELETE |
  | PS2_MIPCEN | OFF (num-if-set) | MIPTBP distinct-value census (:5043) | delete-losing-arm (A2 auto-cat; site is census-only) | DIAG — rides the gs_mipmap census family in core/diag |
  | (indirect) PS2_VERBOSE_HW + PS2_LOG via `R119ProbesOn()`/`hwlog::VerboseOn()` | log-open && verbose | arms every legacy probe block | move-to-diagnostics-build | DIAG — the whole R119/M0C latch idiom is replaced by compile-time log categories (ARCH §9) |

- **Hacks / workarounds** (blunt list):
  - :2904-3180 — **entire old ProcessBuffer commented out** (~280 dead lines). Delete.
  - :1737-2900 — **[CUTSCENE]/[COMPOSITE-DIAG] session narration region**: probes keyed
    on `frame[dc].fbp == 0x70000` (:1848,:1912), frame windows `ff >= 1845`,
    `ti.tbp == 0x150000 && psm==0` (:2432), TBP 0x70000 composite dump (:2655), plus an
    HTML/SVG batch dumper (:2683). These are concluded-investigation probes wearing
    VRAM constants. Correct implementation: DELETE; any surviving display/scene-RT
    address test becomes a `VramLayout` query in the title RendererProfile (A6 row C).
  - :382-540 — **SHADOW8_KTAP A+D-scan tap inside ReceiveData**, the hottest GIF entry:
    ~120 lines of packet scanning + stderr formatting behind a latched bool. Correct:
    core/diag probe registered at the gif/queue seam, compiled out of release.
  - :3601-3660 + :3894-4150 — **ParseSpan drift-check machinery** (s_drift_* origin
    ring comparing bulk lane vs legacy decode). It exists only because two parse paths
    exist; NORM'ing GIF_FAST deletes both the twin and the checker. The byte-level
    proof moves to tests (gif corpus replay).
  - :87-133 — **per-process env-latch test overrides** (GifFastOverrideForTest,
    GskSimdOverrideForTest): workaround for un-reflippable static latches. Correct:
    Config injection; tests construct a `GifParser` with explicit options.
  - :134-183 + :4461-4540 — **r158/ObjStrip/KickSrcTally census counters + EmitForBench
    exit riders** (bench exits via `std::_Exit`, skipping atexit — the rider pattern is
    itself a workaround). Correct: core/diag census registry with explicit flush owned
    by the harness.
  - :1292 — PRE=1 PRIM handling routed through ProcessPacked(GS_PRIM,…): legitimate
    PS2 rule (GIFtag PRE field), lands in GifParser.
  - [W10-EEFEED] `tag_rowable`/w10eefeed::On() coupling (:3673) — expired hatch header;
    purge-dissolves (MIGRATION §1.4 w10_eefeed row).
  - TplTryServe "first sighting stays legacy" duality + FNV-collision demotion guard:
    legitimate mechanism; keep in PacketTemplateCache with its stats.
- **Risks**: ReceiveData is the arbiter's delivery target — moving it changes the
  MTVU/gifdrain seam; the FlushBatch merge into the ONE flush risks batch-boundary
  regressions (draw_buffer move + reserve(512) refill at :2890). Proving tests:
  tests/test_gif_fast_equivalence.cpp (fast/SIMD A-B), gs_replay corpus byte-identity,
  the G-C1 flush verifier, vif_replay (PATH2/PATH3 interleave), FMV scanout PPM
  tripwires (PATHSYNC history shows pixel-diff sensitivity).

---

### host_app/hle_heap.cpp (3,995 lines)

- **Purpose**: Despite the name, this is the CDVD/loader HLE: ISO9660-backed archive
  table for the game's DATA.BD/DATA.BH pack (InitArchiveTable), the deferred async
  ISO read engine (g_deferred_iso_reads + ProcessDeferredIsoReads) with a DVD
  seek/transfer latency model, the [CHUNK-APPLY-SCOPED] completion-visibility hold
  model (chunk_gate:: ARM-A/E/C/V release arms), stream-file registration forwarding
  into recompiled STREAM.IRX batch functions, the game allocator malloc/free HLE
  (HLE_001815c0/001815f0 over a host dlmalloc-style heap), and a set of fixed-address
  sce HLE bodies (MC init/paths, cdvd file ops HLE_002c****). Also serializes CDVD
  file-handle state for savestates.
- **Public surface**: `HLE_FUN_002ad8f0/002adb18/002adb40` (stream file open/size/
  unregister), `HLE_FUN_002aac80` (async read issue), `HLE_FUN_002ad1a0`,
  `HLE_001815c0/001815f0` (malloc/free), `HLE_002c5080/5820/5940` (MC),
  `HLE_002c6ce0/6e88/7070/72d4/75e8/6d8c/71bc/7418/6f40/ccba0/cf930` (cdvd/fio),
  `ProcessDeferredIsoReads`, `HasDeferredIsoReads`, `chunk_gate::{ScopedOn,DrainSeq,
  OnTeardownDrain,NoteDrainObject,HoldCompletionForOrder,PeekHoldCompletionForOrder,
  GetReleaseTallies,...}`, `SndRegEnabled`, `GetHleCdvdFileByHandle`, `HostIso_FindFile`,
  `HleHeap_{SerializeCdvdFiles,RestoreCdvdFiles,HasPendingDeferredReads,
  AbandonDeferredReads}`. Installed by main.cpp hooks; called from sif_rpc_handlers,
  mpeg/cdvd_stream, savestate.
- **Classification**: MIXED (A6 §7 row C/G: mechanism generic, game allocator re-entry
  at :719-768 `0x1e2db0/0x1e2958/0x1e74a8`, DATA.BD file-table + type-hash knowledge,
  MC DAT_ addresses). Subsystem: cdvd (+ mc, ee-heap workaround family).
- **Refactor destination** (MIGRATION §1.4 + §1.6 row): split —
  - `engine/cdvd/iso_walker.{h,cpp}` (class `IsoWalker`): ISO9660 + PositionedIsoRead
    (the pread atomicity fix stays, documented).
  - `engine/cdvd/read_engine.{h,cpp}` (class `CdvdReadEngine`): deferred read queue,
    pump, delivery; single owner of the queue mutex.
  - `engine/cdvd/latency_model.{h,cpp}` (class `DvdLatencyModel`): seek/transfer
    model (42/112 ms seek classes, 1.08 sectors/ms DVD-4x, per pcsx2 CDVD.cpp),
    consuming ee/timing clocks; divides by turbo K via TimeDomain, not turbo.h.
  - `engine/cdvd/hle/loader.{h,cpp}` (class `LoaderCompletionModel`): chunk_gate hold/
    decision core (HARDWARE/TIMING HLE per HOOK §3.6); the FUN-shaped model params
    (drain-object list-head at obj+0x24, spin sites) move to
    `games/<title>/hle/loader_sync.cpp` (TitleHleModule).
  - `games/…/data/archive_table.*`: DATA.BD/BH layout, prefix cursors, type-hash
    ordering knowledge (0x3079DE93 Decal skip narrative at :456-542) — title data.
  - `games/…/iop/stream_irx/feed_glue.cpp`: ForwardStreamRegisterFile/Ship/Unregister
    (re-entry into recompiled 0x1e2db0/0x1e2958/0x1e74a8) + StreamRegScratch.
  - `engine/mc/hle/`: HLE_002c5080/5820/5940 as SdkBinding rows; the planted DAT_
    writes (0x3c85a4, 0x2e9ccc/d0, client struct 0x3c8580) become part of the libmc
    HLE state model with `// diverges:` comments.
  - `engine/cdvd/hle/`: the HLE_002c6***/002cc*** fio/cdvd bodies as SdkBinding rows.
  - HLE_001815c0/001815f0 malloc/free: **quarantined with the 0x181560 workaround
    family** (HOOK §3.1, beside ee/mem) — retired at the root fix, Phase 7.
  - Savestate serializers → `games/…/savestate/` section providers (cdvd handles).
  - `oldInitArchiveTable` (:790) — dead, delete.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_SNDREG | ON (=0 legacy hatch) | S42/S43 stream registration forwarding + CD job engine bring-up vs pre-S42 planted globals | keep-as-setting (CANON-membership auto-cat; caveat class per A2 headline) | NORM — engine live unconditionally; the pre-S42 arm deletes (proven-fix, default ON) |
  | PS2_CHUNK_APPLY_SCOPED | ON | scoped EE-side ordering gate vs S57 per-read pump hold | delete-losing-arm | NORM — scoped gate only |
  | PS2_CHUNKQ | OFF | stderr teardown-drain trace | delete-losing-arm | DELETE — trace dies; triage capture replaces the evidence channel |
  | PS2_CHUNK_APPLY_DORMANT | ON (nonzero=2) | ARM-E ring-empty release arm on/off | delete-losing-arm | NORM — ARM-E stays; knob dies |
  | PS2_CHUNK_APPLY_VALVE_TICKS | 90 (clamp 8..1200; 0=off) | ARM-V bounded-release valve threshold | delete-losing-arm | NORM — constant 90 in LoaderCompletionModel (title-overridable via TitleHleModule params) |
  | PS2_CHUNK_APPLY_ORDER | 1 (0=fix off, ≤16) | frame boundaries required before completion visible | delete-losing-arm | NORM — constant 1; the 8-tick escape valve stays as model behavior |
  | PS2_DVD_LATENCY_SCALE | 1.0 paced / 0.0 under FREERUN (explicit wins) | DVD latency model scale | keep-as-setting | SETTING — Config `cdvd.latency_scale` default 1.0; the freerun inversion becomes explicit in the ONE `time.mode` enum validation (A2 finding 3), not env inheritance |

- **Hacks / workarounds**:
  - **File name lies** ("heap"): 90% of content is cdvd/loader. Rename per concept.
  - :719-768 — **recompiled-function map re-entry at hardcoded game addresses**
    (0x1e2db0/0x1e2958/0x1e74a8) with narrative about batch lock no-ops. PS2 rule:
    these are the game's own STREAM registration calls; correct home is title glue in
    games/…/iop/stream_irx/ driven by the SIF RPC dispatch, not address literals in
    engine code.
  - :1143-1200 — **malloc zeroes memory + [HOSTWR-NAME] probe** reading game address
    0x9819a0 inside the allocator hot path, plus `memory::key_overlap_check`
    (KEY-REDIRECT family). The zeroing masks the game's uninitialized-field reliance;
    per HOOK §3.1 the whole allocator HLE is an emulation-bug workaround retired at
    its root fix. The probe deletes.
  - :456-542 — **archive insert-order model** reproducing FUN_001bb710/FUN_001ccf80
    stream-position semantics — game data-format knowledge narrated in comments;
    becomes title archive-table data with a README pointer, not inline prose.
  - :633-679 — HLE_FUN_002ad8f0 documented as a **verified split of retail behavior**
    (EnqueueCmd31 + ship): keep, but as title HLE with the disassembly citation moved
    to the title README.
  - MC HLE **plants magic values** ("mimic real PCSX2 value" 0x00081928 at :3838,
    fake sema ids 1/2): becomes explicit libmc HLE state with `// diverges:` lines.
  - :2765-2830 — **DVD model freerun default inversion** (scale 0 under
    VBlankFreerunOn) + turbo::WallDivisor coupling: becomes `time.mode`-scoped
    behavior of DvdLatencyModel; no cross-header env inheritance.
  - :1284 — `TryDecodePsmContainer` marked `__attribute__((unused))` — dead, delete.
- **Risks**: The completion-visibility model is load-bearing for the S57/BUG-001
  chunk-load freeze class; moving hold arms or valve constants can re-introduce
  livelocks (BUG-005 save-select park). Proving: loader harness legs
  (verify/run_bounded.sh + recomp_gates/run_leg_gate.sh), tests/loader_oracle_check.py,
  savestate chain tests (CDVD section round-trip), FMV start (PumpSectors interaction),
  and the Phase 7 per-address adjudication ledger for the allocator family.

---

### host_app/dmac.cpp (3,034 lines)

- **Purpose**: The EE DMA controller: 10 channels, register file (Read/Write +
  GetDmaChannelID), StartChannel dispatch, normal/chain walkers for GIF and VIF
  (ProcessGifDmaChain/Normal, ProcessVifDmaChain/Normal, TransferToGIF), SIF DMA
  (ProcessSifDma), deferred-VIF1 kick machinery (FlushDeferredVif1/ChaseVif1Tail),
  channel completion + interrupt raise (CompleteChannel/CheckInterrupt/
  DispatchInterrupt), and handler registration (g_dmac_queues, RemoveDmacHandler).
- **Public surface**: `DMAC g_dmac` (global, via dmac.h), all DMAC:: methods above,
  `GetDmaChannelID`, `RemoveDmacHandler`, `g_dmac_queues`/`g_next_dmac_handler_id`.
  Called by memory.cpp (MMIO), syscalls (handler registration), gif/vif, sif.
- **Classification**: GENERIC-PS2 (A6 does not flag dmac; probes carry game RAM
  windows but are diagnostics). Subsystem: dmac.
- **Refactor destination** (MIGRATION §1.4): `engine/dmac/` — verbatim + gate purge;
  MMIO via ee/mem router. One-class-per-file shape:
  - `engine/dmac/dmac.{h,cpp}` (class `Dmac`): register file + StartChannel dispatch.
  - `engine/dmac/chain_walker.{h,cpp}` (class `ChainWalker`): the GIF/VIF chain walk
    bodies (tag decode, ASR stack, TTE, MFIFO hooks).
  - `engine/dmac/deferred_vif1.{h,cpp}` (class `DeferredVif1Kick`): the FlushDeferred/
    ChaseTail scheduling seam (see hacks — kept as an explicit, documented mechanism).
  - `engine/dmac/handlers.{h,cpp}` (class `DmacHandlerRegistry`): g_dmac_queues →
    flat table (std::map dispatch dies per §1.5).
  - hle/: sce libdma surface rows arrive via sce_hle split (not this file).
- **Flags consumed here**:

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_BAKE_DIAG | OFF (num-if-set) | [S98-BAKEDIAG] VIF1-reverse (DIR=0) kick flag lines (:1201) | move-to-diagnostics-build (8 sites, mirrored) | DIAG — one latch in core/diag; note: the underlying GS→EE download gap is real missing hardware (BUSDIR readback), tracked as an engine gap, not a flag |
  | PS2_SHADOW8_KTAP | OFF | [S8-ARENA] ch1 tags touching RAM 0x400000-0x418000 (:1926) | move-to-diagnostics-build | DIAG — same single probe as gif.cpp's; mirror latch dies |
  | PS2_S8_ARENA_MINVF | 0 | re-window for the S8 arena tap (:1935) | delete-both | DELETE |
  | PS2_S26_DECAL | unarmed | [S26-DMA] decal claim-pool window tap 0x323d20+32×0x6f0 (:1969) | delete-both | DELETE |

- **Hacks / workarounds**:
  - :252-263 — **commented-out old ProcessGifDmaChain** stub. Delete.
  - :2708-2850 — **deferred-VIF1 kick + tail-chase** ([TAIL-CHASE]): VIF1 kicks are
    parked and flushed at CHCR-read observation points because our DMA is synchronous
    while the game appends to a live chain. PS2 rule: DMAC arbitration means a started
    channel drains asynchronously while the EE keeps writing the tail. Owning class:
    `DeferredVif1Kick` (explicit, unit-tested), consumed by the vif/ MTVU submit seam.
    This is a legitimate scheduling model, but today it hides inside dmac.cpp with
    log-latch narration — extract and document the invariant.
  - :2735-2760 — **[R101-SLOTW] flush-time content check** hardcoding guest slots
    0x584240/0x58f9c0 and expected quads (cutscene window session probe). DELETE.
  - :42-90 — DmacPerfScope outermost-only booking + R120-WALK per-walk log latch:
    replaced by core/diag perf scopes (compile-time).
  - midc:: census hooks throughout the walkers (mid_census.h): DIAG family.
- **Risks**: Chain-walk ordering vs GIF arbiter delivery is the shape of several past
  texture-vs-draw races; the deferred-VIF1 extraction must preserve flush points
  (CHCR read sites in memory.cpp). Proving: vif_replay + gs_replay corpora, the DMA
  chain unit tests (tests/), MTVU submit-equivalence gate (§1.4 vu1_async row),
  boot-to-gameplay leg (coldboot_gate.py).

---

### host_app/anim_pose_hle_bodies.inc (2,027 lines)

- **Purpose**: Generated native mirrors of 9 hot skeletal-animation pose functions
  (native_FUN_00293ec0, 00294300, 00294b18, 00297ba8, 00297b40, 00297d88, 00297e68,
  00297cc0, 00294480) — statement-for-statement copies of recompiled bodies with the
  memory layer (fm_*) and intra-set call dispatch swapped to direct native calls.
  Emitted once by tests/anim_pose_diff/gen_pose_natives.py; textually included by
  anim_pose_hle.cpp, which installs them over the recompiled originals for speed.
- **Public surface**: none (file-static functions consumed only by the including TU;
  forward decls provided by anim_pose_hle.cpp).
- **Classification**: GAME (A6 §7: "9 anim-pose mirrors of game functions"). Subsystem:
  ee/recomp performance workaround.
- **Refactor destination**: **retired in Phase 2** (MIGRATION §1.1 last row + §1.4
  "anim_pose" row; HOOK §3.2): this is an emission-perf workaround root-fixed by the
  v2 flat-dispatch + fastmem emission (ARCH §5.2). Not moved; deleted with the
  per-address harness adjudication (v2 body vs manual body, ledger to zero). It is
  also a textual `.inc` of hand-written-adjacent code — banned outright by ARCH §13.
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**: the file IS the hack — a hand-maintained shadow copy of 9
  recompiled game functions kept fast by bypassing the emission's perf defects
  (addr-switch prologue at :26-45 mirrors the label dispatch). Correct implementation:
  the v2 recompiler emits fast bodies for all functions; no mirrors exist.
- **Risks**: pose-blend regressions during Phase 2 retirement (visible as animation
  state-machine skips). Proving: tests/anim_pose_diff byte-identity suite per address,
  then the ledger row deletes both the mirror and its installer.

---

### host_app/vu1_async.cpp (1,908 lines)

- **Purpose**: The MTVU worker — an asynchronous VIF1→VU1 pipeline. The EE submits
  VIF1 DMA payloads (SubmitVif1/Staged/Flush) into segments; a dedicated worker thread
  parses them (WorkerLoop → ProcessSegment), with backpressure caps in bytes and
  display frames, adaptive spin-before-block, eager arbiter drains during EE waits,
  deferred frame-boundary delivery (DeferOn/TryParkDrain), vblank-drain policy, and an
  optional (failed, default-off) second "exec" thread for VU1 program execution
  (ExecThreadLoop/KickAsync/WaitExecIdle). Sync(site) is the enumerated barrier used
  by sceGsSyncPath/SyncV/privileged pushes.
- **Public surface**: namespace `vu1_async`: MtvuEnabled, Active, Start, Stop,
  IsRunning, OnWorkerThread, OnExecThread, ExecBusy, ExecAsyncOn, KickAsync,
  WaitExecIdle, SubmitVif1, SubmitVif1Staged, FlushVif1Staged, SubmitResetLatch,
  Sync(site), BatchSegmentsProcessed, EagerDrainOn/SetEagerDrainHook/TryEagerDrain,
  DeferOn, TryParkDrain, VbsyncGateOn, PathSyncRelaxOn, BackpressureIfNeeded.
  Consumers: vif.cpp (submit seam), gs_hle/gs.cpp (sync sites), gif_arbiter, memory.
- **Classification**: GENERIC-PS2 (host parallelization of a generic pipeline).
  Subsystem: vu (tiers).
- **Refactor destination** (MIGRATION §1.4): `engine/vu/tiers/mtvu_worker.{h,cpp}`
  (class `MtvuWorker`) — split:
  - `mtvu_worker.*`: segment queue, WorkerLoop, ProcessSegment, submit API.
  - `engine/vu/tiers/mtvu_backpressure.{h,cpp}` (class `MtvuBackpressure`): byte cap
    (256 MB) + frame-depth cap (pairs with runahead_guard.h's engine.mtvu_depth).
  - `engine/vu/tiers/mtvu_sync.{h,cpp}` (class `MtvuSync`): Sync sites, eager drain,
    defer/park delivery, vblank drain — the frame-boundary half wires into
    ee/timing BoundaryProtocol (ARCH §4.7).
  - ExecThreadLoop + KickAsync/WaitExecIdle + both spin knobs: **deleted** (failed
    experiment, see flags).
  - VIF-submit equivalence gate stays a test (§1.4 note).
- **Flags consumed here**:

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_MTVU | OFF-unless-1 (peak: 1) | worker on/off | keep-as-setting | NORM — peak config; worker is the only path, inline fallback deleted (harness starts the worker directly) |
  | PS2_MTVU_MAX_MB | 256 | pipeline byte cap | delete-losing-arm | NORM — constant 256 MB in MtvuBackpressure |
  | PS2_MTVU_SPIN | ON | worker adaptive spin-before-block | delete-losing-arm | NORM |
  | PS2_MTVU_SPIN_NS | 40000 (clamp 1µs..1ms) | spin budget | delete-losing-arm | NORM — constant 40µs |
  | PS2_VU1_EXEC_ASYNC | OFF (failed FIX-GATES 7/8: >40% fps regression) | second exec thread | delete-losing-arm (winner=OFF) | DELETE — exec thread + gates + KickAsync/WaitExecIdle die |
  | PS2_VU1_EXEC_IDLE_SPIN_NS | 100000 | exec-thread idle spin | delete-losing-arm | DELETE (dies with exec thread) |
  | PS2_VU1_EXEC_WAIT_SPIN_NS | 1000000 | exec-thread wait spin | delete-losing-arm | DELETE (dies with exec thread) |
  | PS2_MTVU_EAGER_DRAIN | ON | sync-wait eager arbiter drain | delete-losing-arm | NORM |
  | PS2_MTVU_VBSYNC | ON | unconditional vblank drain | delete-losing-arm (proven-fix ON) | NORM |
  | PS2_MTVU_PATHSYNC_RELAX | OFF (failed: 43-46% FMV pixel diff; sceGsSyncPath drain load-bearing) | relax path barrier | delete-losing-arm (winner=OFF) | DELETE — relax arm dies; the verdict comment becomes a one-line invariant on MtvuSync |
  | PS2_MTVU_DEFER | ON (requires MTVU) | deferred frame-boundary delivery | delete-losing-arm | NORM — prerequisite conjunction disappears with MTVU NORM'd |
  | PS2_PERF | num-if-set | gate self-report announces (×5 latches: :612,:1013,:1052,:1539,:1659) | keep-as-setting | SETTING — one startup config dump replaces every per-site announce (A2 finding 2) |

- **Hacks / workarounds**:
  - :764-1006 + gates — **the failed exec-thread experiment** compiled into the hot
    module (~250 lines): delete whole arm.
  - :1010-1035 — `s_gate_latched_at_startup` static-init side effect to force the
    announce line ordering: dies with the announce idiom.
  - Eager-drain **function-pointer hook** (SetEagerDrainHook) — link-seam dependency
    inversion so gif_arbiter needn't link: becomes an owned interface (MtvuSync holds
    a `GifArbiter&`), no global atomic slots.
  - W16FeedScope thread-local depth latch for perf booking: core/diag scope idiom.
  - PATHSYNC_RELAX post-mortem narrative (:1627-1641) — history moves to the bug KB
    (docs/bugs, BUG-002-adjacent); code keeps one invariant line.
- **Risks**: this file is the concurrency heart — boundary delivery order vs GS
  privileged pushes (gs.cpp fence) and FMV scanout are proven-sensitive. Proving:
  vif_replay MTVU submit-equivalence gate, FMV PPM tripwires, tsan leg,
  determinism/savestate chain tests (worker drain at quiescence), fps-class gates.

---

### host_app/pad_input.cpp (1,007 lines)

- **Purpose**: L3 of the input pipeline — stamps the mapped `input::PS2PadState` into
  the game's registered PADMAN DMA buffers in PS2 wire format: DISCONN→CONFIG→READY
  staging by vsync age, per-slot phase bytes, active-low inversion, analog bytes,
  press latching so sub-frame taps survive slow game sampling ([R183-INLAT]), and the
  pad-area mutex against half-written-slot races. Also hosts a scripted-input overlay
  (PS2_PAD_SCRIPT) and four riding memory probes (POSWATCH/HEADWATCH/U32WATCH/U32POKE)
  pumped once per frame.
- **Public surface**: PadInput_RegisterBuffer, PadInput_Update, PadInput_GetButtons,
  PadInput_NoteRead, PadInput_SerializeState/RestoreState, `g_pad_area_mutex`,
  `pad_input_test::g_write_recorder` (T4 test seam). Called by libpad HLE
  (sce_hle/libpad.cpp), main thread input pump, savestate.
- **Classification**: GENERIC-PS2 (scePad protocol model; probes are session tools).
  Subsystem: pad.
- **Refactor destination** (MIGRATION §1.4): split —
  - `engine/pad/hle/pad_protocol.{h,cpp}` (class `PadProtocol`): staging SM, slot
    stamping, press latch, area mutex, wire format. Keeps the T4 write-recorder seam.
  - `platform/sdl/input/`: device polling stays with the input/ cluster (mapper is
    its own row); pad_input consumes `PS2PadState` only — already SDL-free, keep it so.
  - PS2_PAD_SCRIPT overlay → `core/diag/input_script.*` (DIAG; also the natural
    replay half of core/triage input_log — note cross-link, TRIAGE_CAPTURE §5).
  - POSWATCH/HEADWATCH/U32WATCH probes → `core/diag/stick_trace.*` family (§1.4
    names stick_trace as the HEADWATCH host). U32POKE dies.
  - Frame pump coupling: the per-frame pump keys off `g_sceGsSyncV_count` — replaced
    by the frame counter published by ee/timing (gs_hle row, MIGRATION §1.4).
- **Flags consumed here**:

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_PAD_SCRIPT | unset (no-op) | scripted press/axis windows by vsync frame | move-to-diagnostics-build | DIAG — input_script in diag builds; triage replay supersedes for repro bundles |
  | PS2_POSWATCH | unset | onset-only float watches (≤4 addrs) | move-to-diagnostics-build | DIAG |
  | PS2_HEADWATCH | unset | per-frame float tracer (≤8 addrs) | keep-as-setting (A2 CSV) — hand-review caveat says diagnostics | DIAG — per A2 headline caveat + §1.4 stick_trace row |
  | PS2_U32WATCH | unset | per-frame u32 watches | move-to-diagnostics-build | DIAG |
  | PS2_U32POKE | unset | frame-windowed guest memory pokes | delete-losing-arm (winner=unset) | DELETE — memory pokes belong to the §15 mod surface (config/detour), never engine |

- **Hacks / workarounds**:
  - :152-206 — **staging timing constants** kStageDisconnUntilVSync=8,
    kStageConfigUntilVSync=214, kReadyDelayAfterCase2=2: model of PADMAN connection
    negotiation timing the game's pad SM expects. PS2 rule: scePad state transitions
    (libpad 3.0.1); owning class PadProtocol with the constants named and documented
    as protocol data (candidate TitleProfile quirk only if a second title disagrees).
  - :239-333 — **press latch** (kLatchMinReads=2): impedance fix between ~880 Hz host
    stamping and ≤5 reads/s game sampling. Legitimate HLE semantics — keep, with the
    R183 measurement reduced to one invariant comment.
  - `g_pad_area_mutex` — host equivalent of DIntr/EIntr bracketing; keep with a
    `// diverges:` note (real PADMAN DMA lands once per vsync).
  - ~450 of 1,007 lines are the script/watch/poke probes riding a pad file — move
    out per destinations above.
- **Risks**: input latency/drop regressions (BUG-027 input-lag class) and phantom
  Start (half-written slot race — the mutex exists for it). Proving:
  input/test_input_mapper.cpp + input/test_pad_buffer.cpp (live tests, §1.4), the T4
  write-recorder contract tests, a scripted-input repro leg under triage replay.

---

### host_app/test_prim_change_preservation.cpp (845 lines)

- **Purpose**: Standalone preservation-property tests for the GS state machine's
  primitive-change behavior (SetPrim same-type no-op, vertex assembly, Flush state
  propagation, empty-buffer type-change skip, PRMODECONT=0 semantics). Self-contained
  main() linking gs_state/gif/texture_decoder directly; written to pin pre-bugfix
  baseline behavior.
- **Public surface**: its own main(); nothing exports.
- **Classification**: GENERIC-PS2 (test scaffold). Subsystem: gs (test).
- **Refactor destination**: **delete** (MIGRATION §1.1 first row: one of the 23
  host_app-root test_*.cpp with zero CMake refs — verified: no reference in any
  CMakeLists). The properties it encodes are already covered by the tests/ suites and
  the G-C1 flush verifier; any gap gets a new tests/ case, not a resurrection.
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**: none beyond being dead: not compiled by any target.
- **Risks**: none (dead). Evidence: `grep test_prim_change_preservation */CMakeLists.txt`
  → no hits; A1 §5.1 census.

---

### host_app/test_rendering_preservation.cpp (572 lines)

- **Purpose**: Standalone preservation-property tests for rendering accuracy
  invariants: HLE hook registration non-null, PSMCT32 swizzled decode correctness,
  ABE=1 alpha-blend uniform computation, VSync scanout path order, interlace field
  toggle per sceGsSyncV.
- **Public surface**: its own main(); nothing exports.
- **Classification**: GENERIC-PS2 (test scaffold). Subsystem: gs (test).
- **Refactor destination**: **delete** (same §1.1 dead-root-test row; zero CMake
  refs, verified). Coverage owners after the move: swizzle/block-decode equivalence
  tests (gs/format), scanout/present tests (gs/hle/present), harness PPM anchors.
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**: none beyond being dead.
- **Risks**: none (dead).

---

### host_app/gif_arbiter.h (684 lines)

- **Purpose**: Declares (and partially defines, header-inline) the GIF PATH priority
  arbiter: collects packets from PATH1/2/3 during a DMA batch and drains them in
  priority order so PATH2 texture uploads land before PATH3 draws. Carries typed
  payload slots (gsdirect::Op streams, gsdrawir::Stream DrawIR) so VU1-HLE DIRECT
  packets ride the same FIFO positions as raw bytes, frame-boundary command slots
  ([R168-DEFER] boundary_seq), the gifdrain thread ([HUB1-B]: kick/fence cvs,
  DrainThreadLoop defined in-header), an XGKICK staging-buffer pool, and two
  function-pointer link seams (r192gif::MmioFenceHook, s8tail::DropHook).
- **Public surface**: `GifArbiterPacket`, `GifArbPayloadBytes`, class `GifArbiter`
  (submit/drain/DrainFence/hasPending/hasSealedReady/openBoundary/
  dispatchBoundaryInline, pool, census members), `g_gifArbiter` extern,
  `r192gif::{DrainThreadOn,DrainThreadOverrideForTest,MmioFence*}`, `s8tail::*`.
  Included by memory.cpp, vif.cpp, vu.cpp, vu1_async, gif.cpp, gs.cpp,
  hle/vu1_hle_runtime.
- **Classification**: GENERIC-PS2 (PATH arbitration is PS2 hardware behavior; the 2
  VRAM literals A6 row C names are in gif_arbiter.cpp — another shard). Subsystem: gif.
- **Refactor destination** (MIGRATION §1.2): `engine/gif/arbiter.{h,cpp}` (class
  `GifArbiter`) — verbatim move, then reshaped for §13:
  - DrainThreadLoop and every non-trivial member body move OUT of the header into
    arbiter.cpp (headers are declarations only).
  - `engine/gif/drain_thread.{h,cpp}` (class `GifDrainThread`) if the thread state
    warrants its own class; NORM'd permanent (see flag).
  - The MmioFenceHook/DropHook function-pointer slots become constructor-injected
    interfaces (the arbiter takes its fence consumer; test stubs inject fakes).
  - s8tail shadow-tail hold hook: quarantined with the SHADOW8 workaround family;
    retired when the shadow pipeline root fix lands (classrdr stencil/shadow classes).
- **Flags consumed here**:

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_GIF_DRAIN_THREAD | ON | gifdrain thread vs inline drains | delete-losing-arm (proven-fix ON) | NORM — thread permanent; inline-drain arm survives only as the drain body the thread runs |
  | PS2_PERF | num-if-set | [HUB1-B] 1 Hz drain census (:635) | keep-as-setting | SETTING — census line moves behind core/diag perf; announce latch dies |

- **Hacks / workarounds**:
  - **Function definitions in a header** (DrainThreadLoop + gates + pool): violates
    ARCH §13; split into .cpp on move.
  - `r192gif::MmioFenceHook` — atomic function-pointer so memory.cpp links without
    gif_arbiter.cpp: link-topology workaround → proper module boundary (ee/mem router
    calls a registered GifArbiter fence; registration at composition root).
  - `s8tail::DropHook` — shadow frame-tail hold list drop for test binaries: same
    pattern, plus it belongs to the SHADOW8 investigation family; correct home is the
    stencil/shadow class renderer's state, not a global hook.
  - `DrainThreadOverrideForTest` per-process latch override: Config injection.
  - Session namespaces `r192gif`/`s8tail` — banned names (§11); rename on move
    (`gifarb::` or members).
- **Risks**: drain ordering is the texture-before-draw guarantee; thread lifecycle
  interacts with savestate quiescence and teardown. Proving: vif_replay/gs_replay
  corpus order checks, savestate chain tests (drain-at-boundary), the arbiter unit
  tests that use the test-stub drain, HUB1 fps gates.

---

### host_app/gs.cpp (627 lines)

- **Purpose**: GS privileged register file (0x12000000 block): WritePrivilegedRegister
  / ReadPrivilegedRegister big switches over PMODE/SMODE1/2/SRFSH/SYNCH/DISPFB/
  DISPLAY/BGCOLOR/CSR/IMR/BUSDIR/SIGLBLID, GS_Reset, a DISPFB flip census
  ([R150-REGIME] NoteDispfbFlip + ring-log twin), a dedicated scanout side-log, and
  the [R168-DEFER] R168PrivPushFence that syncs+drains the MTVU/arbiter pipeline
  before DISPFB/DISPLAY/BGCOLOR writes push RenderJobs directly (so flips land after
  deferred frame content).
- **Public surface**: GS_Reset, WritePrivilegedRegister, WritePrivilegedLower/Upper,
  ReadPrivilegedRegister; `g_gs_regs` (from gs.h). Called by memory.cpp MMIO and
  gs_hle (SetGsCrt/PutIMR syscalls).
- **Classification**: GENERIC-PS2. Subsystem: gs (privileged) + ee/timing boundary.
- **Refactor destination** (MIGRATION §1.4 GS row): split —
  - `engine/gs/state/privileged.{h,cpp}` (class `GsPrivilegedRegs`): the register
    file + read/write switches; MMIO registered via ee/mem router.
  - CSR/vsync handshake → `engine/ee/timing/boundary_protocol.*` (the ONE
    frame-boundary sequence owns CSR field toggles; ARCH §4.7).
  - DISPFB/DISPLAY scanout registers + flip tracking → `engine/gs/hle/present/`
    (scanout composition + present policy; flip census becomes a present-pace input,
    not a log line).
  - R168PrivPushFence → BoundaryProtocol/MtvuSync responsibility (the fence is a
    boundary-ordering rule, stated once, not per-register-write boilerplate).
  - Scanout side-log (s_scanoutLog file) → deleted; core/log category `gs.scanout`.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_BAKE_DIAG | OFF | [S98-BAKEDIAG] BUSDIR arm log (:510) | move-to-diagnostics-build | DIAG — one core/diag latch shared with dmac's; the BUSDIR/GS→EE readback gap itself is an engine to-do (gs/xfer local→host), not a flag |

- **Hacks / workarounds**:
  - `s_scanoutLog` **fourth log layer** (private ofstream, own open/limits): dies into
    core/log categories (A2 finding 5).
  - Per-site idempotence guards + fence calls duplicated across the five
    DISPFB1/DISPLAY1/BGCOLOR/DISPFB2/DISPLAY2 cases: one guarded-write helper in
    GsPrivilegedRegs; fence hoisted to the boundary protocol.
  - Direct RenderJob pushes from register writes (bypassing the arbiter) — the
    documented reason the fence exists; in the target design scanout register changes
    flow through gs/hle/present via the queue, restoring one delivery order.
- **Risks**: FMV double-flip pacing (~2 flips/frame) and boundary ordering (BUG-026
  snapshot-epoch class). Proving: FMV scanout PPM anchors, present-pace gates,
  ring-log DISPFB twin comparison during migration, gs_replay privileged-write cases.

---

### host_app/turbo.h (502 lines)

- **Purpose**: [P2-TURBO] load-screen turbo — one scalar K on the one tick generator
  (roadmap-conformant: counts-per-tick never change, only tick period). Manual pin
  (PS2_TURBO=K / =0 freerun), auto-engage governor keyed on deferred-ISO queue
  activity with streak/engage/release hysteresis and input inhibit (PS2_LOAD_TURBO,
  default OFF since s71), audio duck gain, wall-latency divisor for the DVD/IOP
  latency models (WallDivisor), and pure period math (PeriodUsForK). Header-only:
  latches, atomics, and the governor all live inline.
- **Public surface**: namespace `turbo`: kBasePeriodUs, PeriodUsForK, ManualFreerun,
  ManualK, DuckGain, AutoOn, AutoK, K(), Active(), AudioGain(), WallDivisor(),
  NoteLoadQueue(), TickPeriodUs(), Governor/Cfg. Included by hle_heap.cpp,
  ps2_vblank.cpp, iop_stubs.cpp, loadspan.h, budget_lift.h, main.cpp.
- **Classification**: GENERIC-PS2 (host pacing mechanism; constants tuned on this
  game but mechanism generic). Subsystem: ee/timing.
- **Refactor destination** (MIGRATION §1.3 timing cluster row): `engine/ee/timing/` —
  - `engine/ee/timing/turbo_governor.{h,cpp}` (class `TurboGovernor`): tri-state K,
    auto-engage hysteresis, NoteLoadQueue liveness guard; declarations-only header.
  - Period math folds into `time_domain.*` (TickSource owns the one period; turbo is
    a TickSource modifier — ARCH §4.7).
  - Audio duck → the platform audio sink applies `AudioGain()` published by the
    TimeDomain (device stays master).
  - WallDivisor consumers (DVD latency model, IOP FIX-C floors) read DerivedClocks,
    not turbo:: directly.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_TURBO | unset (-1) | manual K pin (0=freerun, 2..64) for testing | delete-losing-arm | DELETE — testing lever; harness passes a Config/CLI option instead |
  | PS2_TURBO_DUCK | 0.0 | duck gain while turbo active | delete-losing-arm | NORM — constant 0.0 (roadmap: "duck to 0 for K>2") |
  | PS2_LOAD_TURBO | OFF (=1 opts in) | auto-engage load turbo | keep-as-setting | SETTING — Config `cdvd.load_turbo` (bool, default false per s71 user directive) |
  | PS2_LOAD_TURBO_K | 0 (freerun form) | engaged K (0=freerun, 2..64) | not in A2 CSV (non-literal EnvU32 site) | NORM — constant 0 (freerun engaged form proven effective for this game) |
  | PS2_LOAD_TURBO_ENGAGE_MS | 1500 | streak length before engage | not in A2 CSV (EnvU32) | NORM — Governor constant |
  | PS2_LOAD_TURBO_GAP_MS | 150 | max gap inside a streak | not in A2 CSV (EnvU32) | NORM — Governor constant |
  | PS2_LOAD_TURBO_RELEASE_MS | 250 | empty-queue release delay | not in A2 CSV (EnvU32) | NORM — Governor constant |
  | PS2_LOAD_TURBO_INPUT_MS | 2000 (0 disables) | input-inhibit window | not in A2 CSV (EnvU32) | NORM — Governor constant |
  | (indirect) PS2_PERF via presentpace::PerfEnvOn | num-if-set | gate announce line | keep-as-setting | SETTING — startup config dump |

- **Hacks / workarounds**:
  - **Mutable statics in a header** (CurK, LastNoteNs, governor state): violates the
    §11 size budget (header mutable globals = 0). Move state into the .cpp.
  - **Tri-state K encoding** (1=off, 0=freerun-engaged, ≥2=finite) — a footgun;
    correct: an explicit enum {Off, Freerun, Finite{k}} in TurboGovernor.
  - PS2_TURBO=0 meaning "unbounded freerun" (R156 warp semantics) — surprising env
    semantics; dies with the env.
  - 100+ lines of header narration (roadmap quotes, measurement history): one
    invariant paragraph stays ("counts-per-tick never change"); history → loop_state.
  - Stale-note fail-safe (>1 s pump silence ⇒ 1x): keep — it is a real liveness rule;
    document as TurboGovernor invariant.
- **Risks**: ordering contracts must never become wall-denominated (S57 freeze
  class); the fail-safe and frame-denominated valve interplay with
  LoaderCompletionModel. Proving: tests/test_present_pace.cpp §23 period math (unit
  pins survive), load-screen leg with turbo on/off (LOADTIME gates), audio duck
  behavior in sound tests.

---

### host_app/gs_state.h (473 lines)

- **Purpose**: Declares the GS drawing-state machine: `GSVertex`, `GSPrimType`, and
  `struct GSState` — dual-context register mirrors (frame/zbuf/tex0/tex1/miptbp/test/
  alpha/clamp/scissor/xyoffset), vertex attribute latches (RGBAQ/ST/UV/FOG), dirty-reg
  deferred-flush tracking (PCSX2 model), the vertex queue + draw_buffer, kick entries
  (KickVertex/KickVertexImpl/AssembleVertex/TryKickRunBulk/TryKickRunNative),
  transfer engine regs (BITBLTBUF/TRXPOS/TRXREG/TRXDIR + WriteHWReg +
  FinishHostToLocalTransfer + StampTransferDestEpoch), HLE provenance stamps
  (src_hle_image/kick_pc/ord), Flush, and diagnostic dumpers. Globals: `g_gs_state`,
  `gs_vram` (4 MB), transfer counters.
- **Public surface**: everything above; included by gif.cpp, gs_state.cpp, vif.cpp,
  render/graphics files, vu1_hle_runtime, tests.
- **Classification**: GENERIC-PS2 (A6 row C flags gs_state.cpp's VRAM constants; the
  header itself carries none — one 0x280000 mention in a comment). Subsystem: gs.
- **Refactor destination** (rides MIGRATION §1.3 gs_state.cpp row): split —
  - `engine/gs/state/gs_registers.{h,cpp}` (class `GsRegisters`): dual-context
    register mirrors + setters.
  - `engine/gs/state/vertex_kick.{h,cpp}` (class `VertexKick`): queue, assembly,
    kick-run entries; `GSVertex` POD declared beside it (≤30-line POD allowance).
  - `engine/gs/state/flush.{h,cpp}`: the ONE flush (with gif.cpp's FlushBatch merged).
  - `engine/gs/xfer/transfer_engine.{h,cpp}` (class `TransferEngine`): BITBLT/TRX +
    WriteHWReg swizzle via gs/format; epoch stamping folds into the VramImage commit
    protocol (ARCH §4.3 — PageRecord owns freshness, not ad-hoc epoch bumps).
  - DumpState/DiagnoseBatch/DumpVRAMRegion → core/diag (diagnostics builds).
  - `src_hle_*` provenance fields → DrawIR record provenance (gs/hle/ir), not live
    state.
- **Flags consumed here**: none directly (getenv-free header). Gate references live
  in the .cpp (another shard) — e.g. PS2_VRAM_EPOCH_REACT is documented at
  StampTransferDestEpoch's decl (default ON → NORM per that file's row).

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | (decl-only) PS2_VRAM_EPOCH_REACT | ON (impl in gs_state.cpp) | transfer-dest epoch stamp on IMAGE re-activation / SendImage | delete-losing-arm | NORM — the stamp becomes the VramImage single-writer commit rule (ARCH §4.3); flag dies in the owning shard |

- **Hacks / workarounds**:
  - `m_batch_texture_enable` [BATCH-STATE-SNAPSHOT] — TME captured at first kick so a
    later register write can't repaint a batch (grey-ocean bug). PS2 rule: draw state
    is per-primitive at kick time; correct implementation: DrawIR carries full
    per-draw state so flush never reads live registers. The field dies with the
    DrawIR path (already NORM: PS2_VU1_HLE_DIRECT/GS_DRAWIR).
  - StampTransferDestEpoch's shoreline-foam narrative — the ad-hoc epoch system it
    patches (WindowEpoch/EPOCH_KEY identity caches) is one of the 5 VRAM page
    trackers; §1.5 rewrites all five into ONE PageRecord. This header's hook is
    absorbed there.
  - `TryKickRunNative` / `TryKickRunBulk` / legacy per-vertex — a three-tier decline
    ladder in the kick path; NORM to the native+bulk pair with one documented decline
    reason (probes gone).
  - Extern session counters (g_bitbltbuf_count etc.) in a state header → core/diag.
- **Risks**: the flush merge + DrawIR-carried state changes batch boundaries — the
  exact class of the grey-ocean/texture-enable bug the snapshot field fixed. Proving:
  G-C1 verifier, gs_replay corpus byte-identity, test_gif_fast_equivalence kick-run
  cases, shoreline-foam scene anchor (staleness class).

---

### host_app/mid_census.h (425 lines)

- **Purpose**: [MID-CENSUS] middle-layer census probe: counts everything flowing
  through DMAC→VIF1→GIF→GSState (per-channel DMA tag/QWC/walk histograms, full VIF
  opcode/UNPACK histograms, GIF path calls/bytes/tag shapes, packet template
  shape/content hash recurrence) and prints one exit report. Ground-truth instrument
  for the MIDDLE de-emulation program's kill-target selection.
- **Public surface**: namespace `midc`: On(), FromFrame(), S() state singleton,
  Frame(), tally hooks (Dma*, Vif*, Path*, Tpl*), Report();
  `MidCensusEmitForBench` rider lives in gif.cpp. Included by vif.cpp, dmac.cpp,
  gif.cpp.
- **Classification**: GENERIC-PS2 (diagnostic). Subsystem: core/diag.
- **Refactor destination** (MIGRATION §1.4 durable-diagnostics keep-list):
  `engine/core/diag/mid_census.{h,cpp}` (class `MidCensus`) — move-to-diagnostics-
  build; hooks compile to no-ops outside PS2R_DIAGNOSTICS. Header-inline State with
  atomics moves to the .cpp; hook macros stay one-line.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_MID_CENSUS | OFF | census master | move-to-diagnostics-build | DIAG — compile-time category + runtime toggle in the diag registry |
  | PS2_MID_CENSUS_FROM | 0 | tally window start frame | move-to-diagnostics-build | DIAG — parameter of the same probe |

- **Hacks / workarounds**:
  - `std::atexit` registered inside the On() latch (report-at-exit side effect in a
    getter): diag registry owns lifecycle/flush explicitly.
  - `extern std::atomic<uint32_t> g_vsync_frame;` mid-header re-declaration —
    replaced by the ee/timing frame counter API.
  - Whole State struct + inline functions in a header: .cpp on move (§13).
- **Risks**: minimal (default-off probe). Proving: diag-build smoke leg exercising
  the census on a corpus replay; assert report format stability if any tool parses it.

---

### host_app/ps2_vblank.h (380 lines)

- **Purpose**: Declares the vblank ticker for the std::thread model: g_vblank_count +
  cv/mutex, VBlankWorkerFunc (60 Hz tick: sema 1/3 signals, INTC VBON/TIM2/VBOF,
  cv notify), the bench-mode gates VBlankFreerunOn/CdvdInstantOn, delivery-fired tick
  notification (NotifyCdvdCompletionDelivered), the [R140-CDVDBUDGET] instant-CDVD
  delivery-bandwidth meter (CdvdBudgetMeter pure accounting + epoch classification
  that excludes delivery-fired ticks), and the [S40-SUBTICK] tick wall-clock stamp
  (g_vblank_tick_ns) consumed by ee_timers' sub-tick T0_COUNT interpolation.
- **Public surface**: g_vblank_count/mutex/cv, VBlankWorkerFunc, VBlankFreerunOn,
  CdvdInstantOn, NotifyCdvdCompletionDelivered, g_vblank_cdvd_fired,
  g_vblank_tick_ns, CdvdBudgetEpoch, CdvdBudgetParseKb, CdvdBudgetBytes,
  `struct CdvdBudgetMeter`. Included by hle_heap.cpp, gs.cpp, ee_timers, iop_stubs,
  mpeg_hle, main.cpp.
- **Classification**: GENERIC-PS2. Subsystem: ee/timing (+ cdvd pacing).
- **Refactor destination** (MIGRATION §1.3 timing cluster): split —
  - `engine/ee/timing/tick_source.{h,cpp}` (class `TickSource`): the ticker thread,
    count/cv, sub-tick stamp (stamp-before-count ordering contract preserved
    verbatim — it is a real invariant).
  - `engine/ee/timing/time_domain.*`: paced/freerun becomes the ONE `time.mode`
    Config enum (ARCH §4.7); the NOVSYNC→FREERUN→CDVD_INSTANT env inheritance chain
    (A2 finding 3) is replaced by explicit dependent-field validation.
  - `engine/cdvd/delivery_meter.{h,cpp}` (class `CdvdDeliveryMeter`): the
    CdvdBudgetMeter + epoch classification, owned by the cdvd read engine, active
    only in bench mode. (TREE-GAP: tree names cdvd/ "async read engine, latency
    model" — delivery_meter proposed as a named sibling file.)
  - INTC/sema signaling per tick stays with ee/intc integration via
    boundary_protocol.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_CDVD_BUDGET_KB | 256 (0=unmetered) | per-game-frame delivery byte budget under instant CDVD | delete-losing-arm | NORM — constant 256 KB in CdvdDeliveryMeter (bench-mode-only mechanism under time.mode) |
  | PS2_PERF | num-if-set | budget gate announce (:164) | keep-as-setting | SETTING — startup config dump |
  | (decl-only) PS2_VBLANK_FREERUN / PS2_NOVSYNC / PS2_CDVD_INSTANT | impl in ps2_vblank.cpp (another shard) | bench pacing chain | per A2: bench-env chain | absorbed by the ONE `time.mode` enum (paced|freerun-bench); documented here because this header defines their accessors |

- **Hacks / workarounds**:
  - **Env inheritance chain** (FREERUN defaults from NOVSYNC; CDVD_INSTANT from
    FREERUN; documented in this header's comments): the A2 finding-3 cascade —
    correct implementation is one explicit pacing enum with validation, owned by
    core/config; consumers read DerivedClocks.
  - **Delivery-fired tick epoch exclusion** (CdvdBudgetEpoch subtracting
    g_vblank_cdvd_fired, with a documented transient-dip tolerance): subtle
    self-referential pacing carve-out; keep, but as a named rule inside
    CdvdDeliveryMeter with its unit test (the header already points at one).
  - 190 of 380 lines are narration of R139/R140 history: compress to the two
    invariants (refill epoch source; >0-admit liveness rule); history → loop_state.
  - Inline mutable globals in header (g_vblank_cdvd_fired, g_vblank_tick_ns):
    §11 violation; move into TickSource.
- **Risks**: sub-tick stamp ordering is load-bearing for the s40 slide-jump fix
  (BUG-029 class); budget liveness prevents load stalls (BUG-019 black-boot-budget
  is adjacent). Proving: header-only CdvdBudgetMeter unit test (tests/CMakeLists
  :1973 — retarget), ee_timers sub-tick tests, load-screen legs paced + bench modes.

---

### host_app/ring_log_decode.h (333 lines)

- **Purpose**: The single implementation of the ring-log text form: decodes binary
  ring-log events (channels PERF/EE/GFX/INPUT/MEM, dump reasons crash/hotkey/exit/
  fatal_nojump/test) into the documented RLHDR/RLRING/RL line format with per-tag
  pretty decoding. Included by both host_app/ring_log.cpp (hotkey/bundle .txt twins)
  and tools/ring_decode.cpp (offline tool) so text output has exactly one source.
  Not signal-safe by design (crash paths use the binary writer instead).
- **Public surface**: namespace `ringlog`: ChannelName, ReasonName, DecodePretty,
  the merge/format entry (DecodeToText-style walkers over ring_log_format events).
- **Classification**: GENERIC-PS2 (diagnostic infrastructure). Subsystem: core/log.
- **Refactor destination** (MIGRATION §1.4 diagnostics keep-list row: "ring_log.{h,cpp},
  ring_log_format.h, ring_log_decode.h, ring_tags.h → core/log/ring.* sink + decode
  tool — verbatim"): `engine/core/log/ring_decode.{h,cpp}` shared by the sink and
  `engine/tools/diagnostics/ring_decode.cpp` CLI. Function bodies move to a .cpp
  (§13); the header keeps declarations.
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**: header-defined functions (shared-TU convenience) — the
  one-implementation goal is right, the mechanism (header inline) becomes a normal
  library. Per-tag pretty switch will grow — keep tags and decoders adjacent
  (ring_tags.h stays the registry).
- **Risks**: tool/runtime format skew if the split diverges — prevented by keeping
  one TU. Proving: ring-log round-trip test (encode → decode → golden text),
  RING_LOG_SCHEMA.md examples as fixtures.

---

### host_app/mpeg_hle.h (318 lines)

- **Purpose**: Public surface of the MPEG/FMV HLE that replaces libmpeg + the IPU
  with FFmpeg + a PSS demuxer: per-context lifecycle (Init/Destroy/Reset, stream
  rewind/EOF notification), and two pure delivery pacers — `PaceDeadline`
  (wall-clock authored-rate deadlines, drift-free, ~2-frame debt clamp) and
  `TickPace` ([R156-WARP] the same arithmetic re-keyed to vblank ticks for warp
  mode). Both are headless-unit-testable by design.
- **Public surface**: namespace `mpeg_hle`: PaceDeadline, TickPace, Init, Destroy,
  Reset, stream-rewind/EOF hooks, picture delivery API (impl in mpeg_hle.cpp).
  Consumers: sce_hle libmpeg bindings, cdvd_stream (rewind), graphics/FMV path.
- **Classification**: GENERIC-PS2 (FMV pipeline mechanism; stream contents are title
  corpus). Subsystem: ipu.
- **Refactor destination** (MIGRATION §1.4): `engine/ipu/hle/mpeg_ipu.*` — split:
  - `engine/ipu/hle/mpeg_context.{h,cpp}` (class `MpegContext`): lifecycle + decoder
    state.
  - `engine/ipu/hle/fmv_pace.{h,cpp}` (`PaceDeadline` + `TickPace` PODs beside their
    single owner `FmvPacer`): PACE_AUTHORED folds into the ONE `time.mode`
    (§1.4 note) — wall pacer in paced mode, tick pacer in freerun/turbo, selected by
    TimeDomain, not env.
- **Flags consumed here**: none directly (PS2_MPEG_PACE_AUTHORED and decode caps are
  latched in mpeg_hle.cpp — another shard; A2: PACE_AUTHORED = delete-both,
  inverse-of-FREERUN default dies into time.mode).

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | (decl-only) PS2_MPEG_PACE_AUTHORED | inverse of FREERUN (impl in .cpp) | wall-pacer arm | delete-both | absorbed by time.mode selection of FmvPacer |

- **Hacks / workarounds**:
  - The wall-vs-tick pacer duality exists because the time domain is implicit; with
    TimeDomain explicit, FmvPacer picks the domain once. Keep both arithmetic cores —
    they are correct and unit-tested.
  - Debt clamp (~2 frames) and "first delivery arms the clock" are real pacing rules;
    document as invariants on FmvPacer.
- **Risks**: FMV audio/video sync (BUG-002 sound-desync class) and warp-mode picture
  trajectory. Proving: tests/test_fmv_pace.cpp (both pacers, headless), FMV scanout
  PPM anchors, w4_0 reference trajectory (pictures/tick = 0.5 ground truth).

---

### host_app/ps2_syscall_wrappers.cpp (265 lines)

- **Purpose**: Thin register-marshalling wrappers for the std::thread threading
  model: each Syscall_* extracts a0/a1 from CpuContext, calls the matching
  ps2_threading::* function (create/delete/start/exit/terminate thread, dispatch
  enable, priority, sleep/wake, suspend/resume, InitMainThread/InitHeap, semaphores),
  and writes v0. Called by runtime_syscall_dispatcher (syscalls.cpp).
- **Public surface**: the ~40 `Syscall_*` free functions declared in
  ps2_syscall_wrappers.h.
- **Classification**: GENERIC-PS2 (SDK 3.0.1 kernel surface). Subsystem: ee/hle.
- **Refactor destination** (MIGRATION §1.4): **merged into `engine/ee/hle/syscalls.*`**
  — split verb: the wrappers dissolve into the syscall table rows (one dispatch
  table entry = number → marshaller → threads/semas implementation; ARCH §1.3 frozen
  seams unchanged). No separate wrapper TU survives; ps2_threading::* itself lands in
  ee/hle/threads.* (its own row).
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**: none of substance — purely mechanical marshalling.
  `extern std::ofstream g_logFile` is vestigial (unused in most wrappers): dies with
  the global log.
- **Risks**: syscall ABI regressions (v0 sign-extension, i-variant vs non-i
  semantics). Proving: syscall unit tests + the determinism/boot legs
  (coldboot_gate.py); savestate chain tests exercise thread/sema state heavily.

---

### host_app/ps2_syscall_wrappers.h (62 lines)

- **Purpose**: Declarations for the ~40 Syscall_* wrappers grouped by kernel service
  family (thread management 32-38, dispatch 39-40, priority 41-44, wait-release
  45-46, info 47-49, sleep/wake 50-54, suspend/resume 55-58, init 60-62, semaphores
  64-70) — the syscall-number map is encoded in comments.
- **Public surface**: the declarations; included by ps2_syscall_wrappers.cpp and
  syscalls.cpp.
- **Classification**: GENERIC-PS2. Subsystem: ee/hle.
- **Refactor destination**: dissolves with its .cpp into `engine/ee/hle/syscalls.*`
  (the number→handler table makes the comment map real data).
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**: none.
- **Risks**: none beyond the .cpp's.

---

### host_app/texcache_key.h (235 lines)

- **Purpose**: The decoded-texture CONTENT key ([R118]/[R124-TEXL2]) extracted to one
  header so the GL backend's L1 path, the content-hash L2 cache, and the offline
  equivalence test share one keying contract: key = register tuple (tbw, psm, tw, th,
  cpsm, csm, csa + TEXA) ⊕ the exact source-VRAM decode footprint bytes ⊕ the whole
  CLUT snapshot. [R125-L2FIX] removed raw addresses (tbp/cbp) from the tuple with a
  written cancellation proof — identical re-streamed content at rotated addresses now
  keys identically (the double-buffered-upload rescue). FNV-1a or XXH64 backend.
- **Public surface**: namespace `texkey`: Fnv1aStep + the key-build entry(ies);
  "R125CK" salt. Included by opengl_backend.{h,cpp}, render.h,
  tests/test_block_decode_equivalence.cpp.
- **Classification**: GENERIC-PS2. Subsystem: gs texture sourcing.
- **Refactor destination** (MIGRATION §1.2): `engine/gs/hle/texsrc/content_key.{h,cpp}`
  (free-function concept pair per §13) — verbatim move; the hash backend selection
  (PS2_GL_FASTHASH, latched elsewhere) NORMs to XXH64 via core/hash.h (vendored
  xxhash moves there per the render.h row).
- **Flags consumed here**: none in-file (PS2_GL_FASTHASH is referenced in comments;
  its latch lives in render.h/backend — that shard's row; outcome there: NORM XXH64).

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | (referenced) PS2_GL_FASTHASH | latch elsewhere | FNV-1a vs XXH64 | delete-losing-arm | NORM — XXH64 only; FNV path deleted with the losing arm (owner shard) |

- **Hacks / workarounds**:
  - The 64-bit collision-risk acceptance (birthday bound ~2.8e-14 at 1024 entries,
    dims re-checked on hit) — a documented engineering tradeoff, keep the one-line
    invariant + the dims paranoia check in ResolveRead's cache tier.
  - `csa` included "defensively" though log-only in decoders: keep (duplicate-entry
    safe), note stays.
  - 70 lines of R124/R125 defect history: compress to the cancellation-proof
    invariant; history → loop_state.
- **Risks**: any keying change silently serves stale textures. Proving:
  tests/test_block_decode_equivalence.cpp (exact production keying contract,
  explicitly designed to run without the GL TU) + texsrc per-route case tests
  (§1.5 ResolveRead oracle).

---

### host_app/vu_function_registry.cpp (214 lines)

- **Purpose**: Descriptor table naming and classifying known VU microprogram entry
  points (mostly VU0 macro-mode kicks): Sony libsin/cos polynomial at VU0:0xF0
  (Kind::HLE with 16-byte fingerprint), plus game-observed entries (0x0 bootstrap,
  0x500 matrix×vector, 0x398/0x790 matrix multiplies/pose concat with a PCSX2-dump
  fingerprint, 0x548/0x668 quaternion pose blends, …) labeled from EE-side Ghidra RE
  with Kind::Interpreted/Unimplemented — the Unimplemented kind drives a one-shot
  first-call warning so unvalidated microprograms get flagged.
- **Public surface**: `vu_registry::` lookup API (impl of vu_function_registry.h —
  header is in another shard); `kVuFunctions` table.
- **Classification**: MIXED — mechanism (Descriptor/lookup/warn-once) generic; the
  table content is game-observed program knowledge keyed to this title's microcode
  (FUN_ call sites in comments, entry PCs, fingerprints). Subsystem: vu.
- **Refactor destination** (MIGRATION §1.4: "merged into vu/hle/registry.*" — split):
  - Mechanism → `engine/vu/hle/registry.*` (the §1.3 vu1_hle_programs registry row):
    Descriptor kinds, fingerprint check, first-call warning (the warning feeds
    core/triage's fallback detector — one format, two producers per §1.4 vu_trace
    note).
  - Table content → `games/<title>/` as data. TREE-GAP: the tree
    defines `games/…/vu1_library/` for VU1; VU0 macro-program descriptors have no
    named home — propose `games/…/vu0_programs.cpp` (descriptor table registered via
    the same Vu1ProgramLibrary-style span, or a small Vu0ProgramTable field on
    TitleProfile). The Sony libsin/cos entry is SDK content and may stay engine-side
    as a libvu0 HLE row (sce-signature-keyed, not game-keyed).
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**:
  - "Purpose labels, not authoritative" naming from caller-side RE — fine as data,
    but FUN_ addresses in engine-side comments violate LintEnginePurity: the table
    must physically move to games/ for the lint to pass.
  - Fingerprint 0 = "don't check" wildcards on Unimplemented rows: keep; the
    variant-hash tiers of §14.2 are the grown-up version.
- **Risks**: mis-keyed HLE (libsin/cos) corrupts camera/animation everywhere.
  Proving: vu0 micro corpus cases (vutrace) for 0xF0; first-call warnings routed to
  triage bundles for the rest.

---

### host_app/dmac.h (178 lines)

- **Purpose**: DMAC public model: DmacHandler + g_dmac_queues registration map, DMA
  channel ID enum (VIF0/VIF1/GIF/IPU×2/SIF×3/SPR×2), CHCR/D_STAT bit macros,
  DMAChannelRegs POD, class DMAC (global regs ctrl/stat/pcr/sqwc/rbsr/rbor/stadr/
  enabler/enablew + channels + deferred_vif1 state + the method surface), and
  GetDmaChannelID/RemoveDmacHandler declarations.
- **Public surface**: everything above; `g_dmac` extern. Included by memory.cpp,
  gif.cpp, syscalls, sif, vif.
- **Classification**: GENERIC-PS2. Subsystem: dmac.
- **Refactor destination** (MIGRATION §1.4): `engine/dmac/` verbatim + reshaped:
  `dmac.h` (class Dmac decl), `dmac_channel.h` (DMAChannelRegs POD + CHCR bits as
  typed constants, not #defines), `handlers.h` (DmacHandlerRegistry — the std::map
  becomes a flat table per §1.5). deferred_vif1 member state moves with the
  DeferredVif1Kick class (see dmac.cpp entry).
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**: preprocessor bit macros (CHCR_*/DSTAT_*) → constexpr;
  global handler map in a header → registry class. MFIFO/interleave/stall regs exist
  as fields but are unimplemented in the .cpp — keep fields, document the gap (they
  are the architecture's dmac to-do list, not dead).
- **Risks**: none beyond dmac.cpp's. Proving: same suites.

---

### host_app/loop_hist.h (149 lines)

- **Purpose**: [P1-LOOPHIST] EE frame-loop period histogram probe: measures intervals
  between successive hle_sceGsSyncV entries per thread (thread_local 1 ms bins),
  emits a 5-second-window stderr line with band shares (16.7/28.6/33.3/50 ms) and
  top bins. The instrument for the TIMING_NATIVE_ROADMAP P1 gate ("28.6 ms mode
  gone; bimodal 16.7/33.3").
- **Public surface**: `loophist::On()`, `loophist::OnBoundary()` (called at
  hle_sceGsSyncV entry, gs_hle.cpp).
- **Classification**: GENERIC-PS2 (diagnostic, session-scoped). Subsystem: diag.
- **Refactor destination**: **delete** — MIGRATION §1.1 names loop_hist in the
  "One-off diagnostics … concluded sessions (A2 §4)" deletion row. Note the
  conflict: A2's CSV says move-to-diagnostics-build for PS2_LOOPHIST; the migration
  row is the binding disposition (the P1 gate it served is concluded). If the timing
  rework wants a loop-period instrument later, core/diag perf histograms cover it.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_LOOPHIST | OFF | probe on/off | move-to-diagnostics-build (CSV) / §1.1 delete (binding) | DELETE — file dies with its gate |

- **Hacks / workarounds**: thread_local re-latch design (v2) exists because savestate
  loads rebuild guest threads — a real lesson that belongs to core/diag probe
  guidelines, not this file. The 120 ms clamp noise note ditto.
- **Risks**: none (default-off probe). Evidence for deletion: only includer is
  gs_hle.cpp (one call site); gate default off.

---

### host_app/intc.cpp (136 lines)

- **Purpose**: EE interrupt controller: INTC_STAT/INTC_MASK MMIO (write-1-to-clear
  stat, write-1-to-toggle mask), Enable/DisableIntc, RaiseInterrupt, CheckInterrupt,
  and handler dispatch — DispatchCause saves GPR/pc, sets a0/gp/ra=0x80001000, calls
  the recompiled handler via find_containing_function, restores context, and acks
  exactly the dispatched cause bit ([R195-TIM0] fixed the ack that used to wipe other
  pending bits through the MMIO write path).
- **Public surface**: `INTC g_intc`, `g_intc_queues` (map cause → handlers),
  `g_nextIntcId`, INTC::{Reset,Read,Write,EnableIntc,DisableIntc,RaiseInterrupt,
  CheckInterrupt,DispatchInterrupt,DispatchCause}. Called by memory.cpp (MMIO),
  syscalls (AddIntcHandler), graphics thread (VBLANK raise), ee_timers, scheduler.
- **Classification**: GENERIC-PS2. Subsystem: ee/intc.
- **Refactor destination** (MIGRATION §1.4): `engine/ee/intc/intc.{h,cpp}` (class
  `Intc`) — verbatim + MMIO handler registered via ee/mem router; `g_intc_queues`
  std::map → flat per-cause table (§1.5 flat-dispatch row);
  `engine/ee/intc/dispatch.*` if the context-save/dispatch grows (it also belongs
  beside the ee/cpu seams since it re-enters recompiled code).
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**:
  - `ra = 0x80001000` magic handler return address — the kernel return-stub
    convention. PS2 rule: INTC handlers return through a BIOS stub; owning class:
    Intc dispatch + the ee/cpu dispatch seam must trap that address explicitly
    (today it relies on the recompiled body's `jr ra` landing in a host-checked
    range). Name it kIntcReturnStub with a one-line invariant.
  - Full-GPR copy save/restore per dispatch (`auto original_gpr = ctx.cpuRegs.GPR`)
    — heavyweight but correct; revisit under the R5 rule (host stack carries no PS2
    state) when moving.
  - Capped dispatch logging (8 + every 256th) — core/log category.
- **Risks**: handler dispatch re-enters recompiled code — interacts with the shadow
  call stack and savestate quiescence. Proving: INTC unit tests (mask/ack semantics,
  the R195 regression case), vblank/timer legs (TIM0 cause-9 fires ~8.8/s),
  determinism gates.

---

### host_app/uv_bbox.h (125 lines)

- **Purpose**: [R172-RESMEMO-K6H] shared UV-bbox scan: one out-of-line Scan() (in
  uv_bbox.cpp) computing a texel-space bbox over a vertex run — hoisted to the
  producer side (RenderQueue::PushNoFlush stamps RenderBatch::uv_bb) so the
  gfx-thread memo probe can elide its O(vCount) re-derive. Handles the FE_TOWARDZERO
  persistent rounding mode of EE/PS2 worker threads by normalizing to FE_TONEAREST
  internally for fst=0 (STQ divide), skipping normalization for fst=1 (exact ops).
  Carries result-state contract (kAbsent/kOk/kBail), gates, and stamp census.
- **Public surface**: `uvbbox::{Result,Scan,ScanForTest,HoistOn,StampF1Only,
  StampF1OverrideForTest,StampCount0/1,CountStamp}`. Included by render.h
  (PushNoFlush stamp site) and opengl_backend (probe consumer); uv_bbox.cpp compiled
  into host_app and tests (tests/CMakeLists:185).
- **Classification**: GENERIC-PS2. Subsystem: gs texture sourcing.
- **Refactor destination** (MIGRATION §1.4): `engine/gs/hle/texsrc/uv_bbox.{h,cpp}`
  — verbatim ("read-footprint math [S109]/[CX8]"); Scan() becomes the ONE
  read-footprint primitive ResolveRead uses. The RESMEMO gates/census die with the
  memo experiment (see flags); the fenv normalization contract moves with Scan and
  keeps its unit test.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_GL_RESMEMO | OFF | producer-side bbox stamping for the last-resolution memo | delete-losing-arm (mirrored — deliberate duplicate of r172::MemoOn in gl_state_cache.h, A2 finding 4) | DELETE — memo experiment concluded (default OFF, not in peak env); Scan() survives as engine math, stamping decision becomes an owner-table concern in ResolveRead |
  | PS2_GL_RESMEMO_STAMPF1 | OFF | fst=1-only stamp fallback config | delete-losing-arm | DELETE |

- **Hacks / workarounds**:
  - **Documented intentional latch mirror** ("duplicated here because render.h cannot
    include gl_state_cache.h", parity asserted by a test): the include-cycle-driven
    copy is exactly the drift hazard A2 finding 4 names; dies with the gates.
  - The FE_TOWARDZERO note is a real cross-thread FP contract ([R24 EE-CHOP]) —
    correct home: `engine/vu/vu_float.*` / ee/cpu PS2-float docs own the rounding-mode
    rule; Scan keeps its internal normalization with the T13f test.
  - kBail floats "unspecified, MUST NOT be compared" — keep as typed Result contract.
- **Risks**: fenv-sensitive bit-identity (the file exists because cross-TU inline
  broke it). Proving: the T13/T13f unit tests (gate parity, fst legs, anti-vacuity),
  block-decode/texsrc route tests once absorbed into ResolveRead.

---

### host_app/texture_cache.cpp (116 lines)

- **Purpose**: A small LRU texture cache keyed by TextureCacheKey (tbp/tbw/psm/tw/
  th/cbp/cpsm) mapping to backend texture ids with insert/lookup/invalidate-region/
  evict — an early texture-cache design predating the R118 content-key caches in the
  GL backend.
- **Public surface**: TextureCacheKey::operator==, TextureCacheKeyHash,
  TextureCache::{SetBackend,Lookup,Insert,InvalidateAll,InvalidateRegion,Clear,
  EvictLRU}.
- **Classification**: GENERIC-PS2. Subsystem: gs texture sourcing (superseded).
- **Refactor destination**: **delete — dead code**. Evidence: compiled into host_app
  (CMakeLists:271-272) but `texture_cache.h` is included ONLY by texture_cache.cpp
  itself (grep across host_app/tests, 2026-09-23); no instance, no caller. Its
  address-tuple keying is precisely what [R125-L2FIX] proved wrong (texcache_key.h
  removed addresses); the live design is the R118/L2 content-key + the §1.5
  ResolveRead owner-table rewrite. MIGRATION §1.2's "texture_cache.{h,cpp} key logic
  (texcache_key.h) → gs/hle/texsrc/content_key.*" is satisfied by texcache_key.h
  alone; this TU contributes nothing.
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**: O(n) EvictLRU scan; address-keyed staleness model — both
  moot on deletion.
- **Risks**: none (no callers). Removal is build-file-only; prove with a clean build
  + the texsrc suites.

---

### host_app/savestate_writer.h (106 lines)

- **Purpose**: [W16R1][FIX-S1] async capture writer: moves slowspot auto-savestate
  file I/O (42 MB WriteStateFile + PPM copy) off the EE frame boundary onto one
  writer thread with one-slot state-write and PPM-copy jobs (busy ⇒ drop, never
  queue two), owned-copy job payloads (no pointers into live state), tmp+rename
  writes verbatim, and flush/join-for-exit hooks. User-invoked slot saves stay
  synchronous by design.
- **Public surface**: namespace `savestate`: SlowspotAsyncOn (+ test override),
  CaptureWriterTestHoldMs, CaptureWriteJob, CaptureWriterTryEnqueueState,
  CaptureWriterEnqueuePpmCopy, CaptureWriterFlush, CaptureWriterJoinForExit,
  CaptureWriterBusy. Consumers: savestate.cpp DoSave, slowspot.cpp, main.cpp
  teardown.
- **Classification**: GENERIC-PS2. Subsystem: core/savestate (+ triage).
- **Refactor destination** (MIGRATION §1.4): `engine/core/savestate/writer.{h,cpp}`
  (class `CaptureWriter`) — verbatim; per the §1.4 slowspot/bug_report rows its
  callers move into core/triage (TriageBundler enqueues through this writer), so the
  writer becomes the shared async I/O slot for both savestate captures and triage
  bundles.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | PS2_SLOWSPOT_ASYNC | ON unless "0" | async writer vs synchronous DoSave tail | delete-losing-arm | NORM — async path only; the sync arm survives solely as the user-slot save path (already a separate code path, not a flag arm) |

- **Hacks / workarounds**: inline env gate + test-hold knob in a header (move to
  .cpp/Config); the A1-A5 armor list is a good contract — becomes the class doc.
  Drop-when-busy semantics must stay (RSS bound A3).
- **Risks**: byte-identity of async vs sync files (armor A5) and teardown ordering
  (A4 — must run before bench `std::_Exit`). Proving:
  tests/test_savestate_writer.cpp (retargets verbatim, §1.4), savestate chain tests,
  triage bundle write leg.

---

### host_app/pad_input.h (81 lines)

- **Purpose**: Public contract of the pad wire-format layer: PS2PadButton bit enum
  (active-low wire), PadInput_RegisterBuffer/Update/GetButtons/NoteRead, savestate
  hooks, `g_pad_area_mutex` (documented half-written-slot race + phantom-Start
  post-mortem), and the `pad_input_test::g_write_recorder` T4 seam that mirrors
  every guest write for unit tests.
- **Public surface**: as above; included by pad_input.cpp, sce_hle/libpad.cpp,
  main.cpp, input tests.
- **Classification**: GENERIC-PS2. Subsystem: pad.
- **Refactor destination**: rides the pad_input.cpp split →
  `engine/pad/hle/pad_protocol.h` (class `PadProtocol` decl; enum + test seam move
  with it). The mid-header `#include <mutex>` and raw extern mutex become class
  members.
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**: header carries measurement narrative (r183 logs, vf
  numbers) — compress to the two invariants (latch-until-two-reads; slots exchange
  under mutex like DIntr/EIntr). Global mutex → member.
- **Risks**: as pad_input.cpp. Proving: test_pad_buffer.cpp (slot byte contract +
  write order via the T4 recorder).

---

### host_app/vu_aot_registry.h (70 lines)

- **Purpose**: Shared interface of the VU1 AOT tier: VuAotProgramDesc {image hash,
  entry pc, native fn}, VuAotEnabled/VuAotLookupByHash, the [W11-VUSKEL] skel-full
  and [W14-W1] span-fold generated-body A/B gates, and the optional
  PS2_VU_AOT_PROFILE counter plumbing — one definition shared by dispatch (vu.cpp)
  and the generated-program registry (vu_aot_programs.cpp).
- **Public surface**: VuAotProgramDesc, VuAotEnabled, VuAotLookupByHash,
  VuAotSkelFullOn, VuAotSpanFoldOn, VuAotProfCounters/VuAotProfDump(IfEnabled).
- **Classification**: GENERIC-PS2 (tier mechanism; payloads generated from this
  game). Subsystem: vu tiers.
- **Refactor destination**: **delete with the AOT tier** (MIGRATION §1.1 NOAOT row +
  §1.4 explicit "vu_aot_config.h, vu_aot_status.h, vu_aot_registry.h | delete";
  ARCH §3 tree note "AOT deleted (NOAOT)"). The surviving VU1 tiers are
  instrumented-interp + cached (engine/vu/tiers) + the VU1-HLE library (games/…).
- **Flags consumed here** (all die with the tier):

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | (decl) PS2_VU_AOT | ON in header comment; tier default OFF per NOAOT decision record | AOT dispatch enable | delete-losing-arm (tier-level: NOAOT) | DELETE — tier removed |
  | (decl) PS2_VU_AOT_SKELFULL | OFF | restore per-bundle VF0/VI0 re-hardwire stores | delete-both | DELETE |
  | (decl) PS2_VU_AOT_SPANFOLD | ON | folded vs legacy generated bodies | delete-losing-arm | DELETE (moot — generator retired) |
  | PS2_VU_AOT_PROFILE (CMake var) | OFF | STAGE-0 profile counters | test-hook | DELETE |

- **Hacks / workarounds**: the whole tier is a concluded perf experiment (233 MB
  .inc + 351 MB lib, 6 ctest gates — §1.1 row). Nothing to preserve; the span
  classifier header (vu_span_classify.h) survives independently per §1.4.
- **Risks**: none — NOAOT decision record already ran the tier default-OFF.
  Proving: vu1_replay corpus green with tier absent; CMake graph has no vu_aot_lib.

---

### host_app/cdvd_stream.h (48 lines)

- **Purpose**: CDVD streaming HLE surface: hle_sceCdSt{Init,Start,Seek,Stop,Read,
  Stat,Pause,Resume} + hle_sceCdDiskReady/2 (replacing recompiled sceCdSt* whose SIF
  RPCs would hang against the HLE'd IOP), and `cdvd_stream::PumpSectors` — the FMV
  block-pump read-ahead that mirrors retail's blocking sceMpegGetPicture refill by
  reading sectors from the SAME shared stream cursor the game's sceCdStRead uses
  (no double-feed, no desync).
- **Public surface**: the 10 hle_ functions (comment-annotated with their EE
  addresses 0x002c4140-0x002c4938) + PumpSectors. Consumers: sce_hle/libcdvd.cpp,
  mpeg_hle.cpp, cdvd_stream.cpp.
- **Classification**: GENERIC-PS2 (libcdvd 3.0.0 streaming API semantics; addresses
  are SDK-band bindings). Subsystem: cdvd.
- **Refactor destination** (MIGRATION §1.4): `engine/cdvd/stream_reader.{h,cpp}`
  (class `CdvdStreamReader`) — verbatim ("streaming reader over ISO"); the
  hle_sceCdSt* rows become SdkBinding manifest rows in `engine/cdvd/hle/` (ARCH
  §4.9 — signature-keyed; the hardcoded addresses in comments regenerate per title
  into games/<title>/gen/). PumpSectors stays the ipu/hle bridge with its
  shared-cursor contract.
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**: PumpSectors is a deliberate divergence from non-blocking
  host decode to emulate blocking retail decode — keep with a `// diverges:` line
  (cdvd/hle divergence-comment rule, ARCH §11). Address comments become manifest
  rows, not prose.
- **Risks**: FMV underrun/dead-air if the shared-cursor contract breaks. Proving:
  FMV legs (pss_census offline demux mirror), cdvd stream unit tests, BUG-004
  load-time pump-hold regression check.

---

### host_app/artdet_flicker.h (47 lines)

- **Purpose**: In-app left-zone period-2 flicker detector ([FL3-ARTDET]): per-present
  sampling of the presented backbuffer (band |Δluma| lag1/lag2 + autocorrelation ABAB
  signature), firing a savestate+backbuffer+counters bundle into bug_reports/ when a
  user-visible flicker that offline tools never reproduced happens live. Graphics
  thread, post-DrawScanout, rate-limited (900-frame cooldown, ≤8 bundles/session).
- **Public surface**: `artdet_flicker::{Enabled,OnPresent}`. Called from
  graphics_thread.cpp; impl in artdet_flicker.cpp (own shard row shares the split).
- **Classification**: GENERIC-PS2 (diagnostic/triage). Subsystem: core/diag +
  core/triage.
- **Refactor destination** (MIGRATION §1.4 [REV3 D9] row): split — detector/metrics →
  `engine/core/diag/artdet.{h,cpp}` (class `ArtifactDetector`; flicker ctest suite
  unchanged); the FIRE/WriteBundle flat-file path DELETES — fires report through
  `core/triage/TriageHost` as a spike-class bundle (TRIAGE_CAPTURE §12), landing in
  games/<title>/triage/.
- **Flags consumed here** (latches live in the .cpp; declared semantics here):

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | (decl) PS2_ARTDET | OFF | master detector switch | move-to-diagnostics-build | DIAG — diag-build category; triage owns firing |
  | (decl) PS2_ARTDET_FLICKER | ON under ARTDET | this sampler | delete-losing-arm | NORM within DIAG — sub-gate dies, sampler rides ARTDET |
  | (decl) PS2_ARTDET_TEST | OFF | synthetic ABAB toggle to verify trigger path | move-to-test-only | TEST-ONLY — harness arms it in the flicker suite |

- **Hacks / workarounds**: the synthetic-toggle writes to the live backbuffer
  (draws a gray strip on odd presents) — acceptable as a TEST-ONLY path but must
  never compile into release; becomes a harness-injected fake frame source. The
  "user sees it, agent can't catch it" coordination story is exactly what
  core/triage §18 generalizes.
- **Risks**: sampling cost on the saturated gfx thread (the header budgets and
  reports overhead — keep the cost census). Proving: flicker ctest suite (bbdiff/
  tdiff calibration fixtures), triage bundle end-to-end leg with ARTDET_TEST.

---

### host_app/iop_context.h (19 lines)

- **Purpose**: The IOP (R3000A) CPU context: 32× 32-bit GPRs + hi/lo/pc, a single
  inline global `iop_ctx`, and `iop_fix_zero` enforcing the hardwired-zero $0
  invariant. The execution context for recompiled STREAM.IRX code and IOP stubs.
- **Public surface**: `struct IopContext`, `inline IopContext iop_ctx`,
  `iop_fix_zero`. Included by iop_stubs, iop_loader, iop recompiled output, sif
  handlers (and referenced by the root CMake's generated-code glue at
  CMakeLists.txt:558).
- **Classification**: GENERIC-PS2. Subsystem: iop.
- **Refactor destination** (MIGRATION §1.4): `engine/iop/iop_context.h` — verbatim
  (exec env for recompiled IRX). The `inline IopContext iop_ctx` header global moves
  to an owning TU (`iop/iop_context.cpp`) — header mutable globals are budgeted 0
  (ARCH §11); ownership: the IOP exec env module, single writer on the EE thread
  (IOP is HLE'd/cooperative here).
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**: `iop_fix_zero` call-after-write discipline (manual $0
  enforcement) — correct implementation: the iop_recompiler emits writes that skip
  $0 (generator-level rule), keeping the helper only for stubs.
- **Risks**: minimal; savestate IOPM section captures this context (title
  savestate provider row). Proving: STREAM.IRX boot leg + IOPM savestate round-trip.

---

### host_app/vu_aot_programs_4.cpp · _10 · _16 · _21 · _27 (17 lines each)

- **Purpose**: Five of the K=24 fixed checked-in VU1-AOT chunk aggregator TUs
  ([R158-TUSPLIT]/[W12R2]): each includes vu.h + vu_aot_registry.h +
  vu_decode_slot.h + vu_fast_exec.h and then the generated `vu_aot_generated_N.inc`
  (emitted by vu_aot_gen even when empty, so CMake's source list stays static — the
  R126-SHARD principle). Each exposes a `g_vu_aot_chunk_descs_N` array aggregated by
  the master registry at static init.
- **Public surface**: `g_vu_aot_chunk_descs_{4,10,16,21,27}` (external linkage,
  consumed by vu_aot_programs.cpp).
- **Classification**: GENERIC-PS2 shells around GAME generated payloads (A6:
  vu_aot_programs*.inc payloads are GAME-generated). Subsystem: vu tiers (AOT).
- **Refactor destination**: **delete with the AOT tier** (MIGRATION §1.1 NOAOT row:
  "33 chunk TUs" — these are five of them). No replacement: the cached-interpreter
  tier + the games/ VU1 library cover the need.
- **Flags consumed here**: none directly (the .inc payload bodies reference the
  registry gates — all die together).

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**: the "include dance" (constexpr DecodeVuSlot + always_inline
  member defs required in-TU) is the textual-include pattern §13 bans; moot on
  deletion — and the generated-file exemption would not save it, since the tier
  itself retires.
- **Risks**: none (default-OFF tier per NOAOT). Proving: vu1_replay corpus green
  without the tier; build graph clean.

---

### host_app/recomp_test.h (9 lines)

- **Purpose**: Phase-1 relic: declaration of `GetPlayerMaxHealth_recomp`, the
  hand-recompiled proof-of-concept function from the project's original "hello
  world" milestone (see README Phase 1).
- **Public surface**: one function declaration; included only by recomp_test.cpp.
- **Classification**: GENERIC-PS2 (dead scaffold). Subsystem: none.
- **Refactor destination**: **delete** (MIGRATION §1.1: "main_old.cpp,
  recomp_test.cpp, memory_test.cpp — dead scaffolds"; the header rides
  recomp_test.cpp out). Historical value is already captured in README/loop_state.
- **Flags consumed here**: none.

  | var | default today | what it selects | A2 disposition | refactor outcome |
  |---|---|---|---|---|
  | — | — | — | — | — |

- **Hacks / workarounds**: none.
- **Risks**: none (dead; only includer is itself a §1.1 deletion).

---

## SHARD SUMMARY

Flags column = direct getenv/latch reads in the file, as N→{NORM/SETTING/DIAG/TEST/DELETE};
"+Nd" = decl-only gates whose latch lives in another file (counted in the owner's shard).

| file | lines | class | destination | shape | flags (N/S/D/T/X) | hacks |
|---|---|---|---|---|---|---|
| gif.cpp | 5,532 | MIXED | engine/gif/{gif_parser,gif_regs,packet_template}.* + engine/gs/state/{flush,ir_apply}.* | split | 5 (2/0/2/0/1) | 8 |
| hle_heap.cpp | 3,995 | MIXED | engine/cdvd/{iso_walker,read_engine,latency_model}.* + cdvd/hle/loader.* + mc/hle + games/…/{hle/loader_sync,iop/stream_irx,data/archive_table,savestate}; allocator HLE quarantined→retired | split | 7 (5/1/0/0/1) | 7 |
| dmac.cpp | 3,034 | GENERIC-PS2 | engine/dmac/{dmac,chain_walker,deferred_vif1,handlers}.* | verbatim+gate purge | 4 (0/0/2/0/2) | 5 |
| anim_pose_hle_bodies.inc | 2,027 | GAME | — retired Phase 2 (HOOK §3.2) | delete | 0 | 1 |
| vu1_async.cpp | 1,908 | GENERIC-PS2 | engine/vu/tiers/{mtvu_worker,mtvu_backpressure,mtvu_sync}.* | split | 12 (7/1/0/0/4) | 5 |
| pad_input.cpp | 1,007 | GENERIC-PS2 | engine/pad/hle/pad_protocol.* + core/diag/{input_script,stick_trace}.* | split | 5 (0/0/4/0/1) | 4 |
| test_prim_change_preservation.cpp | 845 | GENERIC-PS2 | — dead root test (§1.1) | delete | 0 | 0 |
| gif_arbiter.h | 684 | GENERIC-PS2 | engine/gif/arbiter.* (+drain_thread.*) | verbatim, reshaped per §13 | 2 (1/1/0/0/0) | 5 |
| gs.cpp | 627 | GENERIC-PS2 | engine/gs/state/privileged.* + ee/timing/boundary_protocol.* + gs/hle/present/ | split | 1 (0/0/1/0/0) | 3 |
| test_rendering_preservation.cpp | 572 | GENERIC-PS2 | — dead root test (§1.1) | delete | 0 | 0 |
| turbo.h | 502 | GENERIC-PS2 | engine/ee/timing/turbo_governor.* (+time_domain fold) | split | 8 (6/1/0/0/1) | 5 |
| gs_state.h | 473 | GENERIC-PS2 | engine/gs/state/{gs_registers,vertex_kick,flush}.* + gs/xfer/transfer_engine.* | split | 0 (+1d) | 4 |
| mid_census.h | 425 | GENERIC-PS2 | engine/core/diag/mid_census.* | move-to-diag | 2 (0/0/2/0/0) | 3 |
| ps2_vblank.h | 380 | GENERIC-PS2 | engine/ee/timing/tick_source.* + time_domain + engine/cdvd/delivery_meter.* | split | 2 (1/1/0/0/0) (+3d) | 4 |
| ring_log_decode.h | 333 | GENERIC-PS2 | engine/core/log/ring_decode.* + tools/diagnostics | verbatim | 0 | 1 |
| mpeg_hle.h | 318 | GENERIC-PS2 | engine/ipu/hle/{mpeg_context,fmv_pace}.* | split | 0 (+1d) | 2 |
| ps2_syscall_wrappers.cpp | 265 | GENERIC-PS2 | merged into engine/ee/hle/syscalls.* | split (dissolve) | 0 | 1 |
| texcache_key.h | 235 | GENERIC-PS2 | engine/gs/hle/texsrc/content_key.* | verbatim | 0 (+1d) | 3 |
| vu_function_registry.cpp | 214 | MIXED | engine/vu/hle/registry.* (mechanism) + games/…/vu0_programs.cpp (table) | split | 0 | 2 |
| dmac.h | 178 | GENERIC-PS2 | engine/dmac/{dmac,dmac_channel,handlers}.h | verbatim, reshaped | 0 | 2 |
| loop_hist.h | 149 | GENERIC-PS2 | — concluded one-off (§1.1) | delete | 1 (0/0/0/0/1) | 1 |
| intc.cpp | 136 | GENERIC-PS2 | engine/ee/intc/intc.* | verbatim (+flat table) | 0 | 3 |
| uv_bbox.h | 125 | GENERIC-PS2 | engine/gs/hle/texsrc/uv_bbox.* | verbatim (gates die) | 2 (0/0/0/0/2) | 3 |
| texture_cache.cpp | 116 | GENERIC-PS2 | — dead (zero external includers) | delete | 0 | 1 |
| savestate_writer.h | 106 | GENERIC-PS2 | engine/core/savestate/writer.* | verbatim | 1 (1/0/0/0/0) | 1 |
| pad_input.h | 81 | GENERIC-PS2 | engine/pad/hle/pad_protocol.h | rides pad split | 0 | 2 |
| vu_aot_registry.h | 70 | GENERIC-PS2 | — deleted with AOT tier (NOAOT) | delete | 0 (+4d→DELETE) | 1 |
| ps2_syscall_wrappers.h | 62 | GENERIC-PS2 | dissolves into ee/hle/syscalls.* | split (dissolve) | 0 | 0 |
| cdvd_stream.h | 48 | GENERIC-PS2 | engine/cdvd/stream_reader.* + cdvd/hle SdkBinding rows | verbatim | 0 | 2 |
| artdet_flicker.h | 47 | GENERIC-PS2 | core/diag/artdet.* + core/triage (fire path) | split | 0 (+3d) | 2 |
| iop_context.h | 19 | GENERIC-PS2 | engine/iop/iop_context.h (+.cpp for the global) | verbatim | 0 | 1 |
| vu_aot_programs_4.cpp | 17 | GENERIC shell/GAME payload | — deleted with AOT tier | delete | 0 | 1 |
| vu_aot_programs_10.cpp | 17 | 〃 | 〃 | delete | 0 | (shared) |
| vu_aot_programs_16.cpp | 17 | 〃 | 〃 | delete | 0 | (shared) |
| vu_aot_programs_21.cpp | 17 | 〃 | 〃 | delete | 0 | (shared) |
| vu_aot_programs_27.cpp | 17 | 〃 | 〃 | delete | 0 | (shared) |
| recomp_test.h | 9 | GENERIC-PS2 | — dead scaffold (§1.1) | delete | 0 | 0 |

Totals: 37 files / 24,607 lines. Direct flag reads: 57 sites over 40 unique vars →
24 NORM, 6 SETTING, 11 DIAG, 0 TEST-ONLY (in-file; +1 decl TEST-ONLY via ARTDET_TEST),
13 DELETE (per-file counting; mirrored vars counted once per file). Deletions: 12 of
37 files die outright (2 dead tests, recomp_test.h, texture_cache.cpp, loop_hist.h,
anim_pose .inc, vu_aot_registry.h, 5 AOT chunk TUs).

### TREE-GAPs (destinations the ARCH §3 tree does not name)

1. `engine/gs/state/ir_apply.{h,cpp}` — typed DrawIR/DirectOps application entry
   (gif.cpp ApplyHleDirectOps/ApplyHleDrawIR). Tree names gs/state/{registers,
   vertex_kick, flush} only; the typed-payload consumer needs a named home.
2. `engine/cdvd/delivery_meter.{h,cpp}` — the CdvdBudgetMeter (bench-mode delivery
   pacing). Tree names the async read engine + latency model; the meter is a third
   concept.
3. `games/<title>/vu0_programs.cpp` — VU0 macro-program
   descriptor table (vu_function_registry.cpp data). Tree defines vu1_library/ only;
   VU0 program knowledge has no data home (TitleProfile field proposed).
4. `games/<title>/data/archive_table.*` — DATA.BD/BH archive
   layout + type-hash ordering knowledge (hle_heap.cpp InitArchiveTable). §1.6 says
   "cdvd file-table → title data" without naming a path.

### Dead code found in this shard (evidence)

- `test_prim_change_preservation.cpp`, `test_rendering_preservation.cpp` — zero
  references in any CMakeLists (grep 2026-09-23); members of the §1.1 23-file set.
- `recomp_test.h` — sole includer is recomp_test.cpp, itself a §1.1 dead scaffold.
- `texture_cache.cpp` (+ its header) — compiled into host_app (CMakeLists:271-272)
  but texture_cache.h is included by no other file; no instantiation anywhere.
- `hle_heap.cpp:790 oldInitArchiveTable` and `:1284 TryDecodePsmContainer`
  (`__attribute__((unused))`) — uncalled.
- `gif.cpp:2904-3180` (old ProcessBuffer) and `dmac.cpp:252-263` (old
  ProcessGifDmaChain) — commented-out blocks.
- The five `vu_aot_programs_*.cpp` TUs are live in the vu_aot_lib build today but
  the tier is default-OFF (NOAOT decision record) — delete per §1.1, not dead-code
  cleanup.

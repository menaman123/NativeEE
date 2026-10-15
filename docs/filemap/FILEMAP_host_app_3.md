# FILEMAP — host_app shard 3 (37 files, 24,610 lines)

Scope: 37 host_app top-level files assigned in `/tmp/s116/s116_shard3.txt`. Sources read
2026-09-23 from `$PS2R_OLD_ROOT` (read-only). Destinations
follow ARCHITECTURE.md §3 tree; dispositions follow A2 registry + MIGRATION_PLAN §1;
classifications follow A6. Refactor-outcome vocabulary: NORM / SETTING / DIAG /
TEST-ONLY / DELETE per the shard instructions. Peak config (`run_play.sh`):
`PS2_GS_SCALE=2 PS2_GS_INTERP=1 PS2_FPS_CAP=60 PS2_MTVU=1 PS2_GL_FAST=1 PS2_GL_FAST2=1
PS2_VU1_HLE=1 PS2_VU1_HLE_DIRECT=1` — everything it turns on becomes NORM;
SCALE/INTERP/FPS_CAP become SETTINGs with those defaults.

“⚠A2” marks rows where the A2 CSV disposition disagrees with the code default
(gate-purge step 1 requires adjudication before deletion).

---

### host_app/gs_state.cpp (5,623 lines)

- **Purpose**: The GS drawing-state engine on the EE thread: all internal GS register
  setters (PRIM/RGBAQ/ST/XYOFFSET/SCISSOR/FRAME/ZBUF/TEX0-2/TEST/ALPHA/CLAMP/TEXA/
  FBA/PABE/SCANMSK/FOGCOL…), vertex kick + primitive assembly (KickVertexImpl/
  AssembleVertex/FlushPrimitive), the PCSX2-style deferred flush (TestDrawChanged/
  SnapshotState/Flush), the transfer engine (BITBLTBUF/TRXPOS/TRXREG/TRXDIR, WriteHWReg,
  host→local and local→local transfers), and VRAM write-epoch stamping for snapshot/
  texture-cache freshness.
- **Public surface**: `GSState` methods (declared in gs_state.h), global `g_gs_state`,
  `g_s8ktap_rxseq`, `g_fmv_order_seq`, `g_spiketri_count`, `g_gif_active_path`, transfer
  counters, `GsEmitR160CensusForBench()`. Included/called by gif.cpp, gif_arbiter.cpp,
  graphics_thread.cpp, opengl_backend.cpp, vu.cpp (XGKICK path), tests
  (test_gif_fast_equivalence et al.).
- **Classification**: MIXED (A6 §3.3 row C — 47 code-level VRAM-constant hits on a
  generic GS state machine). Subsystem: GS.
- **Refactor destination** (MIGRATION §1.3 + §1.6 row C): split into
  `engine/gs/state/gs_registers.{h,cpp}` (class `GsRegisters` — register setters, dual
  ctx), `engine/gs/state/vertex_kick.{h,cpp}` (class `VertexKick` — kick + 3-slot window
  assembly; TryKickRunNative/Bulk become the ONE implementation), `engine/gs/state/
  flush.{h,cpp}` (class `DrawFlush` — the ONE flush → DrawIR), `engine/gs/xfer/
  transfer_engine.{h,cpp}` (class `TransferEngine` — TRX regs + WriteHWReg, swizzle via
  `gs/format/PsmLayout`), epoch stamping folded into `engine/gs/vram/vram_image.*` /
  `page_record.*` (§4.3 single-writer commit). Game parts → `games/<title>_crash_
  the title/renderer_profile.cpp` VramLayout (kTbps {0x370000,0x3c0000,0x3f0000},
  tile-atlas 0x380000+tile·0x10000/+0x8000 CLUT, ktap {0x70000,0x230000}) and the :2909
  cutscene latch → TitleProfile scene-hook.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_SHADOW8_KTAP | num-if-set (OFF) | S8 kick/FRAME-write ordering census (2 sites here; 16 sites/7 files total, mirror) | move-to-diagnostics-build | DIAG (core/diag, one latch) |
| PS2_R199_XFERLATCH | ON-unless-0 | TRXDIR-latch xfer staging vs direct register→transfer mutation | delete-both ⚠A2 (code default ON) | NORM — latch (xfer_stage) becomes the only path in TransferEngine |
| PS2_SPIKE_GUARD | OFF-unless-1 (2 sites) | culls off-screen ADC=1 spike-seed tris; confirmed ineffective for the visible needle | delete-losing-arm-keep-winner | NORM (winner = OFF; guard block deleted — real fix is XGKICK drain-time read, see vu.h) |
| PS2_GS_TERM_NATIVE | ON-unless-0 | [M5A] native DrawRun→triangle assembly vs legacy per-vertex AssembleVertex | delete-losing-arm-keep-winner | NORM — native assembly becomes the one path in VertexKick |
| PS2_GS_TYPED_BULK | ON-unless-0 | [M4A] bulk run kick vs per-vertex path | delete-losing-arm-keep-winner | NORM (merged with TERM_NATIVE into the one assembly) |
| PS2_BAKE_DIAG | num-if-set (5 sites here, 8 total, mirror) | composite-atlas FRAME/upload census | move-to-diagnostics-build | DIAG |
| PS2_BAKE_DUMP | path-if-set | raw atlas-window byte dumps (48 files max) | move-to-diagnostics-build | DIAG |
| PS2_BAKE_SRCSCAN | presence | scan main RAM for atlas staging buffer | move-to-diagnostics-build | DIAG |
| PS2_MIPCEN | num-if-set (site :3067) | TEX1 census (this site log-only; behavior sites live in gs_mipmap.h) | delete-losing-arm-keep-winner | DIAG for this census site; NORM for the var’s knob sites (owned elsewhere) |
| PS2_R198_QORDER | num-if-set | queue-order census window (vf) | delete-both | DELETE |
| PS2_R198_QORDER_BASE | hex, default 0x2f0000 | census window base | delete-both | DELETE |
| PS2_VRAM_EPOCH_REACT | ON-unless-0 | commit-path epoch stamp (StampTransferDestEpoch) | delete-losing-arm-keep-winner | NORM — becomes PageRecord commit semantics (§4.3) |
| PS2_R199_UPLOADEAGER | ON-unless-0 | complete host→local transfer at exact pixel count (fixes one-draw-late invalidation) | delete-both ⚠A2 (code default ON, proven fix) | NORM — “transfer completes when rrw·rrh pixels land” is the PS2 rule in TransferEngine |

- **Hacks / workarounds** (file:line):
  - :2909 `frame[ctx].fbp==0x230000 ⇒ g_cutscene_active` — scene latch keyed on a game
    VRAM base. Correct: TitleProfile scene-hook fed by RendererProfile.VramLayout
    (`layout.is(scene_rt, fbp)`); engine keeps no scene notion.
  - :170-224 [ATLAS-WINDOW] probe hardcodes frames 1677–1685 and kTbps
    {0x370000,0x3c0000,0x3f0000} + 0x8000/0x2000 page split — session probe; DELETE
    (row K); the page math itself is PsmLayout’s job.
  - :726 tile-atlas index `((cbp&0xFFFF0000)-0x380000)>>16` — game atlas formula in
    engine code → VramLayout query.
  - :85-105 R119ProbesOn/S8KtapKickOn hot-path probe latches + dozens of scene-keyed
    splash/ocean/FMV probes (:346 ffo≥1845, :550 TBP=0x150000, :1498-1785 font-atlas
    0x370000/0x3F0000 dumps, :2025-2204 skybox/ocean census) — parallel-VCS probes;
    DELETE; durable ones become core/diag categories.
  - :1900-1990 [SPIKE-GUARD] culls geometry the game legitimately kicks — a workaround
    for snapshot-at-kick XGKICK (root cause owned by vu.h drain-time model, already
    landed); guard deletes.
  - m4a::Elide* latches (:21-26) — the M4A dead-format-elision logging layer; replaced
    by core/log compile-time categories.
  - TryKickRunNative + TryKickRunBulk + legacy AssembleVertex = THREE assembly paths of
    one semantic (dual fast/legacy) — collapse to one in VertexKick (equivalence
    argument already written at :2327-2363).
- **Risks**: vertex-assembly equivalence (strip parity, ADC consume rules, sprite
  corner-normalize) is subtle — prove with gs_replay corpus byte-identity + the
  G-M5a/b equivalence tests; epoch semantics move must keep SNAPVERIFY mismatches=0;
  cutscene-latch consumers (interp/present) must be re-pointed before the latch moves.
  Proving tests: tests/test_gif_fast_equivalence, gs_replay corpus sweep, SNAPVERIFY
  leg, coldboot_gate.

---

### host_app/render.h (3,734 lines)

- **Purpose**: The EE→graphics-thread boundary grab-bag: DrawIR-ish record types
  (`RenderBatch`, `BatchTextureInfo`, `MipLevel`, `RenderJob`, `RenderCommandType`),
  the bounded `RenderQueue` (VSync high-water producer throttle), the r160 draw-merge/
  RUNDRAW machinery (merge keys, census, RundrawClass), a vendored XXH64, and ~21
  session-named gate namespaces (r119/r119dep/r120/r121/r123/r124/r125/r147/r148/
  r154/r159/r160/r162/r164/r167/r184/r197/r198/r201/sh5/aquad/a1a) latching ~44 env
  vars directly plus 13 more through `r120::GateOn(name)`.
- **Public surface**: all of the above; included by gs_state.cpp, gif.cpp,
  graphics_thread.cpp, opengl_backend.cpp, texture_decoder.h, gs_interp.h, tests.
- **Classification**: MIXED (generic queue/IR mechanism + game-tuned gate constants,
  e.g. r197 D3’s 0x2f0000 staging heuristic). Subsystem: GS-HLE queue/IR.
- **Refactor destination** (MIGRATION §1.3 row): split into `engine/gs/hle/ir/
  draw_ir.h` (+ `render_batch.{h,cpp}` = class `RenderBatch`, vertex/texture records —
  merged with gs_draw_ir.h per §1.2), `engine/gs/hle/queue/render_queue.{h,cpp}`
  (class `RenderQueue`, high-water = Config `engine.rq_highwater` const 3),
  `engine/gs/hle/queue/draw_run_merge.{h,cpp}` (class `DrawRunMerge` — the RUNDRAW
  winner; merge-census machinery → core/diag), vendored xxhash → `engine/core/hash.h`.
  All gate namespaces dissolve in the Phase 4 purge (note: the §1.3 row says “5 gate
  namespaces” — actual count is ~21; see TREE-GAP/errata note at end). The
  `*OverrideForTest()` int-hooks survive as ctor-injected test seams, not globals.
- **Flags consumed here** (44 direct getenv sites; A2 rows verified):

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GL_FAST | ON (peak sets 1; explicit 0 = family restore) | r118 family master re-check (2 restore-check sites here) | keep-as-setting | NORM (peak; family code becomes unconditional) |
| PS2_GL_FAST2 | ON via peak (presence-gated master) | r120/r121+ GL fast tier master (13 sub-gates via GateOn) | keep-as-setting | NORM (peak; sub-gates EPOCH_KEY/UNIF_CACHE/BIND_CACHE/BLOCK_DECODE/TEXSUB/LAZY_RTSYNC/FASTHASH/PAR_DECODE/HASH_L2/ALT16_GPU/SYNC_RECT/LAZY_ZSYNC/IDX_PBO all NORM with it) |
| PS2_GS_SNAPSHOT_FAST | ON-unless-0 ([M1A]) | shared snapshot capture vs per-flush private memcpy | delete-both ⚠A2 (default ON, proven by SNAPVERIFY=0) | NORM |
| PS2_GS_SNAP_FOOTPRINT | follows SNAPSHOT_FAST (ON) | decode-footprint-narrowed snapshot windows | move-to-diagnostics-build ⚠A2 (behavioral, not diag) | NORM |
| PS2_XFER_FAST | ON-unless-0 ([M2A]) | WriteHWReg word-unroll IMAGE upload fast path | delete-both ⚠A2 (default ON) | NORM |
| PS2_SH1_DEPEPOCH | numeric (OFF) | dep-epoch experiment | delete-both | DELETE |
| PS2_SH1_SNAPPROBE | numeric (OFF) | one-off snapshot probe | delete-both | DELETE |
| PS2_SNAPVERIFY | num-if-set | snapshot-vs-live oracle (mismatch counter) | move-to-diagnostics-build | DIAG (also stays a harness gate) |
| PS2_NOVSYNC | numeric | vsync off (bench cascade root: FREERUN→CDVD_INSTANT ride it) | keep-as-setting | SETTING → Config `video.vsync` (cascade broken into explicit pacing enum per A2 finding 3) |
| PS2_GL_ZTEX | presence (OFF) | Z-texture experiment | delete-both | DELETE |
| PS2_GL_IDXTEX | presence (OFF) | indexed-texture serve experiment | delete-both | DELETE |
| PS2_GL_IDXTEX_16 | presence (OFF) | IDX-B CT16-pair render experiment | delete-both | DELETE |
| PS2_GL_IDXTEX_32 | numeric | IDX-A CT32 class knob | delete-losing-arm-keep-winner | NORM |
| PS2_GL_IDXTEX_FRESH | numeric | idx-tex freshness knob | delete-losing-arm-keep-winner | NORM |
| PS2_GL_IDXTEX_CLUTSNAP | numeric | idx-tex CLUT snapshot knob | delete-losing-arm-keep-winner | NORM |
| PS2_GL_VCONV_FAST | numeric | vertex-convert fast path | delete-losing-arm-keep-winner | NORM |
| PS2_GL_BSTATE_FAST | numeric | blend-state fast path | delete-losing-arm-keep-winner | NORM |
| PS2_GL_ALIAS_ALPHAQUAD | numeric | alias alpha-quad refresh knob | delete-losing-arm-keep-winner | NORM |
| PS2_GL_FRAMEFLUSH | numeric | per-frame flush knob | delete-losing-arm-keep-winner | NORM |
| PS2_GL_L2_PROB | numeric | L2 cache probation knob | delete-losing-arm-keep-winner | NORM |
| PS2_GL_L2_SU | numeric | L2 single-use policy | delete-losing-arm-keep-winner | NORM |
| PS2_GL_L2_SU_MB | numeric | L2 single-use budget MB | delete-losing-arm-keep-winner | NORM (constant) |
| PS2_GL_TEXDEMAND | numeric | demand-decode policy | delete-losing-arm-keep-winner | NORM |
| PS2_GL_UVREGION | numeric | UV-region decode v1 | delete-losing-arm-keep-winner | NORM (v2 winner) |
| PS2_GL_UVREGION_V2 | numeric | UV-region decode v2 | delete-losing-arm-keep-winner | NORM |
| PS2_GL_SCANOUT_RT | numeric | scanout-RT diagnostics | move-to-diagnostics-build | DIAG |
| PS2_R197_D2FIX | OFF ([R197-REGR]) | blob-shadow snapshot-refresh fix (superseded by R201+fmt6 stack) | delete-both | DELETE |
| PS2_R197_D1FIX | OFF | FMV CLAMP_TO_EDGE broad fix (superseded by narrow R198_D1FMV) | delete-both | DELETE |
| PS2_R197_D3FIX | OFF | refuse RUNDRAW merge on RT-read (superseded) | delete-both | DELETE (rule itself re-lands as TextureSource freshness, §4.5) |
| PS2_GL_UPLOAD_DEPOSIT | OFF ([R202-REGR2]) | stream-ordered CPU-upload deposit onto live FBOs | delete-losing-arm-keep-winner | NORM (winner=OFF today; superseded by the ONE PageRecord deposit rewrite §1.5) |
| PS2_R198_IDX8CT16 | follows UPLOAD_DEPOSIT (OFF) | GPU-side PSMT8-over-CT16 serve | delete-both | DELETE (re-lands as TextureSource route if profile needs it) |
| PS2_R198_D1FMV | ON-unless-0 | narrow FMV-signature CLAMP_TO_EDGE fix | delete-both ⚠A2 (code default ON) | NORM |
| PS2_SH5_IDXDIRTY | OFF | idx dirty-tracking experiment | delete-both | DELETE |
| PS2_SH5_ALIASEPOCH | ON-unless-0 (2nd site opengl_backend) | alias-epoch guard | delete-both ⚠A2 (code default ON) | NORM |
| PS2_GS_DRAWMERGE | OFF ([R160-BODY]) | key-adjacent draw merge (loses to RUNDRAW) | delete-losing-arm-keep-winner | NORM (winner = RUNDRAW; merge arm deleted) |
| PS2_GS_RUNDRAW | ON-unless-0 | run-draw batching (the winner) | delete-losing-arm-keep-winner | NORM |
| PS2_GS_RUNDRAW_CHECK | numeric (OFF) | rundraw self-check | delete-losing-arm-keep-winner | TEST-ONLY (harness assertion, not runtime) |
| PS2_GL_RESID_DIAG | numeric (mirror w/ opengl_backend) | residency diagnostics | move-to-diagnostics-build | DIAG |
| PS2_GL_BBOX_COPY | presence (OFF; RundrawBboxCopyLatch also re-reads PS2_GL_FAST) | bbox-scoped rt_copy experiment | delete-both | DELETE |
| PS2_R197_D3LOG | numeric | one-off D3 log | delete-both | DELETE |
| PS2_R198_QORDER(+_BASE) | num-if-set (mirror of gs_state sites) | queue-order census | delete-both | DELETE |
| PS2_RQ_HIGHWATER | num=0→3 | RenderQueue un-presented-frame cap override (bench-only; MUST be unset in scored runs) | delete-losing-arm-keep-winner | NORM (constant 3; keep a Config debug field only if harness needs it → TEST-ONLY) |
| PS2_R197_MERGELOG | num-if-set | one-off merge log | delete-both | DELETE |
| PS2_R197_MERGEMAX | num=-1 | merge-cap experiment | delete-both | DELETE |

- **Hacks / workarounds**:
  - ~21 session-named gate namespaces (r119…sh5) with inline mutable state in a header —
    the “5 gate namespaces” §1.3 row undercounts; all dissolve at purge; survivors are
    renamed per §11 (no session tags).
  - :767 `GateOn(const char*)` generic getenv helper — a config system smuggled into a
    header; dies with the Config schema (getenv only in core/config).
  - :871 NoVsync latch inside r123 — pacing policy read in an IR header; moves to
    ee/timing/present pacing via Config.
  - :878-1006 vendored XXH64 in a grab-bag header → core/hash.h (verbatim, tests keep
    reference vectors).
  - r197::D3RtReadOn 0x2f0000-family “RENDERED footprint” heuristic encodes the game’s
    staging area — the durable rule is TextureSource.ResolveRead freshness (§4.5) with
    VramLayout data; the env-gated refusal deletes.
  - RundrawBboxCopyLatch (:2485-2492) re-reads PS2_GL_FAST — mirror latch of the r118
    family (drift hazard class, A2 §2.3).
  - RenderQueue::Push interleave census + MergeCensus formatting in-header — census
    mechanics → core/diag.
- **Risks**: RenderQueue is the EE↔GFX contract — moving it breaks everything at once;
  land as Phase-6 cluster with the byte-identity harness + test_integration_render_queue.
  RUNDRAW-as-norm must be proven against the merge-off baseline (r160 census equivalence
  + corpus). High-water constant change would alter pacing — keep 3 and gate with the
  pace ledger tests.

---

### host_app/texture_decoder.cpp (3,049 lines)

- **Purpose**: The CPU texture decode layer: per-PSM swizzled address math
  (GetAddressPSMCT32/CT16/T8/T4/Z32/Z16/Z16S), per-format decoders to RGBA8888
  (CT32/CT24/CT16/T8/T4/T8H/Z*), CSM1/CSM2 CLUT reads (ReadCLUT32/16), 16→32 pixel
  expansion, scanout-buffer decode, DISPFB/DISPLAY field parsing, decode-footprint and
  row-band window math, plus the r121blk block-LUT fast decoders and the r123par row
  pool.
- **Public surface**: `TextureDecoder::*` (see texture_decoder.h), `r121blk::Decode*`,
  `r123par::RowPoolRun/Lanes`. Called by opengl_backend.cpp, graphics_thread.cpp,
  gs_interp, gs_hle, tests (gs_replay, block-decode equivalence).
- **Classification**: MIXED (A6 row C: generic format layer + 7 game-VRAM literals, all
  inside log-only probes). Subsystem: GS format.
- **Refactor destination** (§1.3/§1.6): split into `engine/gs/format/psm_layout.{h,cpp}`
  (class `PsmLayout` — address math + footprint/row-band tables; one of the 4 swizzle
  families collapsing, §1.5), `engine/gs/format/clut.{h,cpp}` (ONE Csm1Swizzle +
  ReadClut32/16), `engine/gs/format/expand.{h,cpp}` + `texa.{h,cpp}` (Expand16to32,
  CT24 TEXA handling, ScaleAlphaPS2ToGPU → alpha.{h,cpp}), `engine/gs/format/
  block_decode.{h,cpp}` (r121blk LUT decoders, renamed), `engine/gs/hle/texsrc/`
  (decode caches + row pool `DecodePool`), scanout decode → `engine/gs/hle/present/`.
  Game literals → deleted with their probes (below).
- **Flags consumed here**: none (0 getenv). Behavior toggles arrive via r118/r120/r121
  latches read in texture_decoder.h/render.h (BlockDecodeOn, ParDecodeOn — NORM with
  the GL_FAST/FAST2 family).
- **Hacks / workarounds**:
  - :1194, :1299, :2016 tile-atlas index `((cbp&0xFFFF0000)-0x380000)>>16` and
    :1419-1447 [CLUT-RED-SCAN] / :1662-1700 T8 tile-dump probes keyed on the game’s
    bullseye/font atlas (0x380000–0x3F0000, CLUT at tile+0x8000, 128×256) — scene
    diagnostics for solved bugs; DELETE (row K). Any live need becomes a VramLayout
    query in the renderer profile.
  - :1185-1203 [READ-CLUT32-DIAG] alpha-range census on font-atlas CLUTs — DELETE.
  - Per-format decoders duplicate the “17 texture-serve paths” problem A3 names; the
    §1.5 rewrite folds them under ONE ResolveRead with an owner table — this file’s
    decoders become the format-layer leaves of that ladder.
- **Risks**: address math must stay byte-identical (Z-format block tables differ from
  CT!); proven by tests/test_block_decode_equivalence.cpp extended to write/encode
  sides (§1.5 oracle) + gs_replay corpus. The row pool move must keep FE_TOWARDZERO
  discipline of callers (see uv_bbox).

---

### host_app/manual_switch_dispatchers.cpp (2,303 lines)

- **Purpose**: Runtime manual-body layer: installs 235 hand-written/machine-restitched
  EE function bodies over the recompiled dispatch (3 fully hand-written state-machine
  dispatchers — FUN_00203358 particle, FUN_002a13e0 bullseye, FUN_00255f30 text/decal
  with VU0 sin/cos folded to std::sin/cos — plus jr-table batches and 121 Phase-B
  auto-merged bodies pulled in via 222 textual `fun_*_body.inc` includes). Wraps every
  install with ring-log entry records and shadow-call-stack frames; keeps the
  manual-address registry savestate classification queries (`IsManualDispatcherFunction`).
- **Public surface**: `install_manual_switch_dispatchers()` (called unconditionally from
  main.cpp:5177), `IsManualDispatcherFunction(addr)` (used by savestate.h). Compiled
  into host_app and tests/test_shadow_offset.
- **Classification**: GAME-SPECIFIC (A6 row D — the largest GAME mass; every body is a
  SLUS-12345 address). Subsystem: EE recomp glue.
- **Refactor destination** (§1.1 last row, §1.6 row D, HOOK §3.2): **retired in Phase 2**
  per-address harness adjudication (v2 emitted body vs hand body across 243 distinct
  addresses; expected end state 0 manual bodies). The install/registry MECHANISM
  (update_function_impl seam, shadow stack) is already owned by `engine/ee/cpu/`
  frozen seams; `IsManualDispatcherFunction` consumers in savestate lose the concept
  with the layer. Nothing from this file lands in games/ as code.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| LOGO_MEMSNAP | OFF-unless-nonzero (OR sentinel file `logs/MEMSNAP_ON`) | full 32 MB EE RAM dump per loading-screen frame | move-to-diagnostics-build | DELETE with the file (concluded logo investigation; the mechanism, if ever needed, is a core/diag RAM-dump tool) |
| LOGO_MEMSNAP_FRAMES | num=60 | dump frame budget | move-to-diagnostics-build | DELETE with above |

- **Hacks / workarounds**:
  - 222 textual `.inc` includes of hand-written code (:1300-1982) — the direct
    counterexample of ARCH §13; retired with the layer, and the rule bans recreating it.
  - :778-790 sentinel-file gate `logs/MEMSNAP_ON` — config via filesystem probe; dies.
  - :770-812 [LOGO-MEMSNAP] 32 MB/frame RAM snapshots — session archaeology; dies.
  - manual_FUN_00255f30 folds VU0 macro sin/cos into std::sin/std::cos — a silent
    precision divergence from PS2 float; the v2 body + vu_float helpers are the correct
    implementation (adjudicated on the harness).
  - Disabled install for FUN_0018e620 (:2060-2065) documents a recompiler COP2 mis-emit
    already root-fixed — comment narrating history; the ledger row is
    HOOK_RECLASSIFICATION’s, not a code comment.
- **Risks**: retirement must be per-address (a divergence = v2 emitter bug or a smuggled
  behavioral fix — never a reason to keep the body). Savestate chain classification and
  the 0x294480 anim-pose overlap ordering must be re-verified as bodies retire. Proving:
  Phase-2 per-address harness ledger to zero + savestate chain tests + corpus audit
  GAPS=0.

---

### host_app/test_preservation.cpp (1,246 lines)
### host_app/test_black_screen_scanout.cpp (918 lines)
### host_app/test_alpha_blend_preservation.cpp (745 lines)
### host_app/test_prim_change_bug_condition.cpp (498 lines)
### host_app/test_boot_bug_condition.cpp (453 lines)

- **Purpose** (shared entry — one bug-campaign family): observation-first property tests
  from early GS bug campaigns. test_preservation + test_alpha_blend_preservation encode
  then-current behavior that a fix must not change (flush state propagation, SetAlpha
  parsing, opaque-batch fields); test_black_screen_scanout, test_prim_change_bug_condition
  and test_boot_bug_condition encode EXPECTED-fixed behavior for since-fixed bugs
  (FBO-bind/VSync sequence, SetPrim queue clear, sprite winding/scanout flip) and were
  designed to FAIL on unfixed code.
- **Public surface**: none (each has its own main/gtest runner intent); **zero CMake
  references** (verified 2026-09-23: no CMakeLists/*.cmake outside build dirs names
  them).
- **Classification**: GENERIC-PS2 test content, but dead.
- **Refactor destination**: **delete** (MIGRATION §1.1 row “23 host_app-root test_*.cpp,
  zero CMake refs”). The behaviors they encoded are covered by live tests/ suites
  (gs_replay, gif_fast_equivalence, boot coldboot_gate); nothing to salvage.
- **Flags consumed here**: none.
- **Hacks**: they duplicate GS state-machine expectations in prose-heavy standalone
  files — the “tests as session records” anti-pattern; the durable acceptance lives in
  tests/ + verify/.
- **Risks**: none (dead). Proving: CMake grep evidence above; Phase-1 deletion gate.

---

### host_app/vu.h (1,171 lines)

- **Purpose**: The VU1 device header: three-tier execution mode enum (interp/cached/
  native stub), predecoded slot + cached-program types (`VuDecodedSlot`,
  `VuCachedProgram` with R146 fast-op classification), flag/dest/opcode constant
  namespaces, and the `VU1` class — registers, 16 KB code/data memories, flag pipeline
  hooks, and the drain-time XGKICK pace model (PCSX2 XGKickHack parity: kick ARMS a
  transfer; data is read from VU memory as the drain pointer arrives, 1 QW / 2 cycles;
  inline `XgkickPace()` early-out with constructor-latched gates).
- **Public surface**: `VU1` (g_vu1 instance in vu.cpp), `VuExecMode`,
  `VuModeResolvedName()`, decode-slot types consumed by vu_fast_exec.h, vu_aot chunk
  TUs, vu1_async (MTVU), tests. Included by half the runtime.
- **Classification**: GENERIC-PS2 (A6: vu.cpp zero code-level game tokens; XGKick
  deferral is a title-tuned switch of a generic mechanism). Subsystem: VU.
- **Refactor destination** (§1.3 vu.cpp row): split into `engine/vu/vu1_state.{h,cpp}`
  (class `Vu1State` — registers/memories), `engine/vu/xgkick.{h,cpp}` (class
  `XgkickDrain` — the pace model; `xgkick_defer` + quantum become TitleProfile.quirks/
  Config data), `engine/vu/flags.h` (flag namespaces), `engine/vu/tiers/interp.*` and
  `engine/vu/tiers/cached.*` (VuDecodedSlot/VuCachedProgram + mode resolve; canonical
  decode already vu/decode.h per §1.2), `VuAotProgramDesc` fwd-decl and every [R152-
  VUAOT] surface **deleted** with the AOT tier (§1.1 NOAOT).
- **Flags consumed here**: no direct getenv (both grep hits are comments). Semantically
  owned member latches (env read in vu.cpp at construction): PS2_VU_MODE (SETTING →
  Config `engine.vu1_tier`; interp stays the instrumented reference), PS2_VU_PACE_NEXTDUE
  (NORM — default ON), PS2_VU_RUNPACE (NORM — default ON), PS2_VU_XGKPACE_QUANTUM
  (NORM — constant 32), PS2_VU_FASTPACK (NORM), PS2_PERF census member (SETTING, see
  ee_timers row). Registry rows live with vu.cpp’s shard; listed here because the
  latched members and their defaults are declared in this header.
- **Hacks / workarounds**:
  - `static constexpr bool kEnableXgkickPace = true` with a “=false restores snapshot
    semantics” escape — compile-time dual path; the snapshot arm deletes (it is the
    root cause of the spike-triangle family that SPIKE_GUARD tried to patch).
  - Header carries per-instance census counters and PERF-gated bookkeeping in the hot
    inline (`pace_census_on`) — census moves to core/diag; the inline keeps only the
    early-out.
  - [R31/W11/W13/R146/R152/R155/R161/R166] session-tag comment strata narrating the
    optimization history — trimmed to hardware-invariant one-liners per §11.
- **Risks**: XGKICK drain timing is THE title-critical quirk (GameDB XGKickHack —
  wrong timing = stale TEX0/FRAME/XYZ in GIF packets); prove with vu1_replay over the
  3,419-case vutrace corpus + gs corpus byte-identity. Cached-tier bit-exactness
  contract must be re-proven after the split (structural tests flip runpace/nextdue per
  instance).

---

### host_app/gs_interp.h (666 lines)

- **Purpose**: Frame interpolation (“Phase E”): identity-stamp-keyed record/match/lerp
  between consecutive sim ticks on the graphics thread — decomposed similarity
  (Procrustes) + residual lerp, HUD/raw/S9 exempt, snap threshold, phase-fix rotation
  order, FL5 nearest-centroid pairing + phase-coherence drop gate, counters.
- **Public surface**: `gsinterp::Intermediates/On/SnapPx/PhaseFixOn/CoherentOn/Cnt` +
  record/replay entry points consumed by graphics_thread.cpp and opengl_backend.cpp;
  `IntermediatesOverrideForTest()`.
- **Classification**: GENERIC-PS2 (mechanism; stamp vocabulary comes from the title’s
  VU1 program names via classrdr). Subsystem: GS-HLE interp.
- **Refactor destination** (§1.4): `engine/gs/hle/interp/frame_interp.{h,cpp}` (class
  `FrameInterp`) — verbatim move, then gate purge; counters → core/diag category.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GS_INTERP | num=0 (peak sets 1) | K intermediate frames per sim tick (0–3) | keep-as-setting | SETTING → Config `video.interp_frames`, default 1 (peak) |
| PS2_GS_INTERP_SNAP | float=64 px | cut/teleport snap threshold | delete-losing-arm-keep-winner | NORM (constant 64; revisit only via Config if a title needs it) |
| PS2_INTERP_PHASEFIX | ON-unless-0 | tick-ring rotation BEFORE pending fire (kills 60 Hz zigzag judder) | delete-losing-arm-keep-winner | NORM |
| PS2_INTERP_COHERENT | ON-unless-0 | nearest-centroid pairing + drop-incoherent-intermediate gate | delete-losing-arm-keep-winner | NORM |
| PS2_GS_SCALE | presence (peak sets 2; 2 sites :423/:577) | internal-resolution scale consumed for interp coordinate math | keep-as-setting | SETTING → Config `video.scale`, default 2 (single read; today 4 latch sites across files — mirror hazard) |

- **Hacks / workarounds**:
  - 0x230000 named in the PhaseFix comment (cavern glow buffer) — comment-only; the
    behavioral rule (“rotate before fire”) is generic and stays.
  - PS2_GS_SCALE latched independently here AND in gs_scale.h/artdet (4 sites, A2
    mirror list) — one Config read replaces all.
  - “Retail never synthesized frames” drop-gate is the right shape; keep as the
    documented invariant of FrameInterp (one-line why-comment).
- **Risks**: interp reads the consumer side of the queue and private shadow targets —
  must not touch EE-visible bytes (savestate/page tracker); prove with the interp
  identity ctest family + BUG-009 (interp-identity-flicker) regression + pace-ledger
  tests (cap semantics).

---

### host_app/ee_timers.cpp (605 lines)

- **Purpose**: EE timers T0–T3 in the tick domain: COUNT/MODE/COMP/HOLD semantics,
  clock-source counts-per-tick, compare/overflow events, real TIM0 ISR dispatch through
  INTC, the emulated-time domain hookup (timedom::DomainTick), and sub-tick T0 COUNT
  interpolation (deterministic read-step mode 2 / wall-anchored mode 1).
- **Public surface**: `eetimers::g_ee_timers` (EETimers), MMIO read/write entries used
  by memory.cpp router; tick feed from ps2_vblank. Tests: tests/test_ee_timers_timr.
- **Classification**: MIXED — generic timer model + a hardcoded game accumulator address
  (0x3097D0) in two paths. Subsystem: EE timing/INTC.
- **Refactor destination** (§1.3 timing row): `engine/ee/intc/ee_timers.{h,cpp}` (class
  `EeTimers` — registers/events/ISR via INTC) + sub-tick presentation into
  `engine/ee/timing/time_domain.*` (DerivedClocks); MMIO registration via ee/mem router.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_T0TRACE | OFF | wrap-protocol tracer (reads game acc 0x3097D0) | move-to-diagnostics-build | DELETE (solved slide-jump probe; game address must not survive in engine diag) |
| PS2_T0_SUBTICK | ON (presence parse) | sub-tick COUNT interpolation vs whole-tick quanta | delete-losing-arm-keep-winner | NORM |
| PS2_PERF | num-if-set (announce re-read inside SUBTICK latch) | gate self-report line | keep-as-setting | SETTING → Config `diag.perf`; the 21-site announce idiom becomes one startup config dump |
| PS2_TIME_EMU_INTRA | num=2 | intra-frame mode: deterministic read-step vs wall interpolation | delete-losing-arm-keep-winner | NORM (mode 2 — the deterministic arm — is the only path; determinism profile depends on it) |
| PS2_T0_NOISR | OFF | suppress guest TIM0 ISR, maintain game acc host-side | delete-losing-arm-keep-winner | DELETE (winner = OFF; the hatch writes guest RAM at a game address — both arms of the diag die) |
| PS2_TIMEDOM_DIAG | numeric | time-domain diagnostics | move-to-diagnostics-build | DIAG |

- **Hacks / workarounds**:
  - :482-495 + :497-520 game wrap-accumulator 0x3097D0 read/written host-side (T0_NOISR
    arm and T0TRACE) — a game address inside the engine timer; correct owner is the
    title budget/loader HLE family (games/…/hle) and both probe arms delete.
  - :154-171 env-latched intra-mode inside the hot COUNT read — becomes a TimeDomain
    constant.
  - [R195-TIM0] stderr dispatch-cost lines every 512 dispatches (:552-566 region) —
    core/diag.
- **Risks**: T0 cadence drives the game’s frame-budget predicate; any drift shows as
  slide-jump/budget regressions. Prove with tests/test_ee_timers_timr + the S40/S94
  anchor legs + determinism gates (same-input identical-tick sequences), BUG-029.

---

### host_app/bug_report.cpp (564 lines)

- **Purpose**: Runtime glue of the U5 bundled bug reporting: startup crash scan (crash
  sentinel → next-launch crash bundle), the slowspot pending-bundle state machine
  (probe every 30 presents, expire at 600), detached-worker bundle assembly (never on
  the graphics thread — [FIX-SLOWSTICK]), manifest generation, retention pruning
  (keep 20), GUI result surface. Pure decision/formatting logic lives in bug_report.h.
- **Public surface**: `bugreport::` SetExeDir/CrashScanAtStartup/SlowspotOnTrigger/
  SlowspotPoll/manual-bundle entry + result polling (GUI). Consumers: main.cpp,
  slowspot.cpp, gui/bug_report_section.cpp; tests/test_bug_report.cpp.
- **Classification**: GENERIC-PS2 (host tooling). Subsystem: capture/triage.
- **Refactor destination** ([REV3 D9], §1.4 keep-list row): generalizes into
  `engine/core/triage/triage_bundler.{h,cpp}` (class `TriageBundler` — crash+manual
  become trigger kinds) + `engine/core/triage/bundle_manifest.{h,cpp}` (class
  `BundleManifest` — MANIFEST.md AI contract replaces manifest.txt inventory); bundle
  root moves `bug_reports/` → `games/<title>/triage/`; retention/dedup →
  `triage_dedup`. test_bug_report retargets the pure helpers verbatim.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_BUGREPORT | ON-unless-“0” | master bundling enable | move-to-diagnostics-build ⚠A2 (superseded by D9: triage compiles into ALL builds) | SETTING → Config `triage.enabled` (default on) |
| PS2_BUGREPORT_SLOWSPOT_MAX | num=1 | slowspot bundles per run cap | move-to-diagnostics-build ⚠A2 | SETTING → Config `triage.perf_max_per_run` (TRIAGE_CAPTURE §3) |
| PS2_BUGREPORT_SLOWSPOT | ON-unless-“0” | GUI slowspot-bundling initial toggle | move-to-diagnostics-build ⚠A2 | SETTING → Config `triage.perf_capture` (GUI-writable) |

- **Hacks / workarounds**:
  - `kRoot="bug_reports"` + flat `logs/bundle_*` + `save_states/slowspot_*` scatter
    (M9 gap) — unified under games/<title>/triage/ layout (TRIAGE_CAPTURE §7).
  - Exact-string `v[0]=='0' && v[1]=='\0'` parses (three variants of bool parse in one
    file) — Config loader owns parsing.
  - Crash-safety rule (nothing in the signal path) is a durable invariant — keep as a
    one-line contract comment on TriageBundler.
- **Risks**: worker-thread lifetime at shutdown; crash-scan runs pre-log — ordering with
  Config load must be preserved. Prove: tests/test_bug_report retargeted + a triage
  end-to-end smoke (trigger → bundle → MANIFEST.md fields) + on-disk example diff.

---

### host_app/s8_ctxlatch.h (404 lines)

- **Purpose**: [S8-CTXLATCH] character-shadow frame-tail context bracket: accumulates a
  PATH1-order shadow of ctx0 state registers (PRMODECONT/PRMODE/TEX0_1/ALPHA_1/FRAME_1…)
  from A+D header kicks, and when a bare wrapped-z strip is about to parse under a
  spliced-in PATH2 context, re-applies the accumulated PATH1 state for that one packet.
  Pure logic (no GS/arbiter deps); integrated at the gif_arbiter drain; unit suite
  tests/test_s8_ctxlatch.cpp. Default OFF — the loop-7 verdict is SPLIT (light-shaft
  strips retail-route to 0x70000, tone strips retail-route to 0x230000, so a blanket
  bracket mis-routes one class), and the residual shadow bug is CLUT alpha content,
  not ordering.
- **Public surface**: `s8ctx::On()`, state-shadow accumulate/predicate/apply API used by
  gif_arbiter.cpp and the unit test.
- **Classification**: MIXED — generic “strips parse under stream-order state” logic with
  game trigger constants (0x230000/0x70000/0x2F0000 families) and a the title-specific
  packet taxonomy. Subsystem: GIF/GS.
- **Refactor destination**: §1.4 lists it under “purge-dissolve into ee/timing + ee/hle”
  — that row’s target is WRONG for this file’s subject (it is GIF drain ordering, not EE
  timing); noted in the errata. Actual outcome: **workaround retired** (default OFF,
  acceptance failed) — the file dies at the purge; the PS2 rule it approximates
  (“PATH-arbitrated GS stream order is the state order strips parse under”) is owned by
  `engine/gif/arbiter.*` (drain must preserve PATH1-relative order instead of
  re-bracketing after the fact); the strip-class trigger data, if a class-gated bracket
  is ever re-landed, is RendererProfile data.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_SHADOW_ORDERFIX | presence (default OFF via `return false`) | enable the ordering bracket | delete-both | DELETE (root fix = arbiter order; residual = CLUT content bug, tracked separately) |
| PS2_SHADOW_CTXLATCH | presence (legacy alias, ORDERFIX wins) | same, legacy opt-in name | delete-both | DELETE |

- **Hacks / workarounds**:
  - The entire file is a post-hoc state bracket compensating for the host’s deferred
    VIF1 walk regrouping the frame tail — correct implementation: order-preserving GIF
    arbiter (engine/gif/), plus the VU0-visibility/CLUT-content fixes the verdict names.
  - `return false` single-site default lever + two-env alias resolution — config
    archaeology; dies.
  - 90 lines of session narration (qi numbers, /tmp paths) — history to docs/loop_state.
- **Risks**: deleting the bracket is behavior-neutral (default OFF); the risk is
  LOSING the documented split verdict — carry it into the bug KB (shadow family) before
  deletion. Proving: tests/test_s8_ctxlatch retires with the file; kicksum census legs
  + retail swdump comparisons remain in the corpus for the future arbiter-order work.

---

### host_app/hle_meta_arena.cpp (350 lines)

- **Purpose**: Host-side quarantining allocator for the game’s list-node metadata blocks
  ([R144-LISTLEAK]): fixed-capacity block states (never-used/live/quarantined), FIFO
  quarantine, double/invalid-free accounting, and tombstoning of freed descriptor fields
  in guest memory (+0x10/+0x14/+0x18 cleared, +0x1c flag mask) so stale readers see
  benign bytes. Backing store `meta_arena_memory` lives in memory.cpp; tests rebind via
  ResetForTest.
- **Public surface**: `hle_meta_arena::` alloc/free/stats API (header in another shard);
  consumed by the 0x181560 allocator-workaround family installed from main.cpp; test
  test_r144_list_leak.
- **Classification**: GAME-SPECIFIC (the descriptor layout and recycled-flags contract
  at 0x20268c are this game’s allocator). Subsystem: EE mem (workaround family).
- **Refactor destination** (§1.4 row): **quarantined beside `engine/ee/mem/`** as a
  named workaround module (`ee/mem/workaround_meta_arena.{h,cpp}`, HOOK §3.1 family
  0x181560) and **retired at the root fix**; its META savestate section follows it out
  under the accepted-difference protocol. Never games/ code — a workaround row, not a
  title feature.
- **Flags consumed here**: none (0 getenv).
- **Hacks / workarounds**:
  - The arena itself is the hack: a host invention isolating the game allocator’s
    metadata to mask a translation-era heap corruption. Correct implementation: the v2
    recompiled allocator running against plain guest RAM (Phase 7 adjudication).
  - Tombstone bit-surgery encodes the game’s descriptor layout (+0x1c state nibble,
    10-bit lock counter) in engine code — dies with the workaround.
  - Rate-limited [META-ARENA] g_logFile lines — core/log category while quarantined.
- **Risks**: removal changes savestate section set (META) — needs the accepted-difference
  savestate protocol + chain tests; prove with test_r144_list_leak (must convert to a
  regression proving the ROOT fix keeps lists leak-free) + load-window harness legs.

---

### host_app/ps2_threading.h (326 lines)

- **Purpose**: The PS2 kernel threading model over std::thread: THS_*/TSW_* status and
  KE_* error constants, `ThreadInfo`/`SemaInfo`/`EventFlagInfo`, guest-visible refer
  structs (ee_thread_status_t, ee_sema_t), `ThreadExitException`, and — in the header —
  inline mutable registries (g_threads/g_semas/g_eventFlags maps + mutexes + id
  counters + thread_local current-tid) plus the create/start/exit/sema/eventflag API
  implemented in ps2_threading.cpp.
- **Public surface**: everything above; consumed by syscalls.cpp, runtime.cpp,
  savestate (park harvest), sif_rpc_handlers, timer_hle, main.cpp.
- **Classification**: GENERIC-PS2 (SDK 3.0.1 thbase semantics). Subsystem: EE kernel HLE.
- **Refactor destination** (§1.3 row): `engine/ee/hle/threads.{h,cpp}` — class
  `ThreadManager` owns the maps/mutexes/counters in the .cpp (the “map leaves the
  header” A1 §2.3 item; header keeps declarations + POD constants only, satisfying the
  header-mutable-globals=0 budget). KE_*/THS_* constants stay (SDK-documented values).
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  - :86-112 inline mutable globals in a header (7 registries/mutexes/counters +
    thread_local) — ODR-fragile and the direct violation of the §11 budget; ThreadManager
    members fix it.
  - `SaveResumeFrame`/`ResumeSpec` savestate coupling in the kernel header — moves next
    to the savestate SectionProvider for threads.
- **Risks**: every syscall touches this; move verbatim-then-encapsulate in one commit
  with the scheduler tests + savestate chain tests (park/harvest ordering) green.
  Proving: thread/sema ctest family, savestate resume legs (menu/island/hub1 anchors).

---

### host_app/texture_decoder.h (267 lines)

- **Purpose**: Public API of the decode layer (see texture_decoder.cpp) plus the r118
  GL-fast gate family (MasterOn + GateOn(name) + 4 sub-gate latches), the r122dec
  decode census atomics, and r123par row-pool exports.
- **Public surface**: `TextureDecoder::*`, `r118::MasterOn/GateOn/TexCacheOn/BboxCopyOn/
  NoFlushOn/VboRingOn`, `r122dec::` counters, `r123par::` pool API. Included by the four
  gated TUs (opengl_backend, graphics_thread, gif, gs_state) and tests.
- **Classification**: GENERIC-PS2. Subsystem: GS format.
- **Refactor destination** (§1.3/§1.6): API header splits with the .cpp —
  `engine/gs/format/` (addressing/decode decls) + `engine/gs/hle/texsrc/` (cache/pool);
  the r118 namespace dissolves at purge (family NORM); census atomics → core/diag.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_GL_FAST | ON-unless-explicit-0 ([M2A]; peak sets 1) | r118 family master (texcache, bbox-copy routing, no-flush, VBO ring, snapshot narrowing) | keep-as-setting | NORM — family code becomes unconditional; the compositional restore recipe retires |
| PS2_GL_TEXCACHE / PS2_GL_BBOX_COPY / PS2_GL_NO_FLUSH / PS2_GL_VBO_RING (via GateOn) | follow master (ON) | per-fix bisection sub-gates | keep-as-setting (family) | NORM (bisection value replaced by per-route tests) |

- **Hacks / workarounds**:
  - “Shared home because all four gated TUs include it” — a config family living in a
    decode header; dies with the Config schema.
  - GateOn(const char*) second copy of the generic env helper (twin of render.h:767) —
    duplicated resolution logic, the documented gl_state_cache mirror drifts from it
    (A2 finding 4).
- **Risks**: same as texture_decoder.cpp; family-NORM proven by [M2A] acceptance battery
  re-run (corpus 8/8 byte-identical, census equivalence) after purge.

---

### host_app/savestate_format.cpp (242 lines)

- **Purpose**: The .ps2st container codec: zlib-compatible CRC32, PS2SAVE1 envelope
  read/write (magic at :170), section put/get primitives (PutPod/PutBytes), and the
  GSST v2 vertex-latch array codec. Pure stdio; no globals beyond the CRC table.
- **Public surface**: `savestate::Crc32/Put*/Get*` + envelope writer/reader; consumed by
  savestate.cpp, savestate_writer.cpp, tests/test_savestate_format.cpp, tests replay
  tools.
- **Classification**: GENERIC-PS2. Subsystem: core savestate.
- **Refactor destination** (§1.2 verbatim row): `engine/core/savestate/format.{h,cpp}`
  (class `SavestateFormat`; format FROZEN — PS2SAVE1 magic and section framing must not
  change so existing anchor states keep loading).
- **Flags consumed here**: none.
- **Hacks / workarounds**: none of note (this is what most files should look like).
  GsVertexRec codec knowledge could arguably live beside gs/state — keep it here; the
  SectionSpec registry (§4.11) owns per-module layout.
- **Risks**: byte-format freeze; prove with tests/test_savestate_format + loading the
  ~40 anchor savestates unchanged (corpus).

---

### host_app/cuelag_probe.h (200 lines)

- **Purpose**: [P0-CUELAG] frame-domain vs wall-domain drift probe: per-window stderr
  line comparing EE frame boundaries (sceGsSyncV), vblank ticks, audio device frames and
  KON census against authored 59.94 Hz; slope math documenting the cue-lag class.
- **Public surface**: `cuelag::On/WindowTicks/OnTick`, `g_device_frames_mixed` inline
  atomic (bumped by sound MixInto). Called by the vblank worker.
- **Classification**: GENERIC-PS2 diagnostic. Subsystem: EE timing diagnostics.
- **Refactor destination** (§1.1 one-off diagnostics row): **delete** — concluded
  session probe (the P0 classification it existed to prove is done; the time-domain
  work landed). The audio-device frame counter, if still consumed elsewhere, moves to
  sound/ as a plain counter.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_CUELAG | OFF-unless-nonzero | probe enable | delete-both | DELETE |
| PS2_CUELAG_WIN_TICKS | num=300 clamp[30,36000] | window length | delete-losing-arm-keep-winner ⚠A2 (modifier of a delete-both probe — dies with it) | DELETE |

- **Hacks / workarounds**: inline atomic device-clock counter in a probe header consumed
  cross-TU by sound_core — untangle before deletion (counter → sound/, probe → gone).
- **Risks**: none behavioral; verify nothing but the probe reads g_device_frames_mixed
  (grep at deletion; if the pace work reads it, it moves rather than dies).

---

### host_app/display_settings.cpp (191 lines)

- **Purpose**: Display/screen-fit settings: atomics for mode/aspect/custom scale+offsets
  with clamped setters, live drawable/VCK geometry publication, applied-rect readback,
  display.ini round-trip (LoadConfig/SaveConfig/Init), and the U3 viewport probe latch.
- **Public surface**: `display::Get*/Set*/LoadConfig/SaveConfig/Init/ProbeOn` +
  geometry publication; consumers: gui, graphics_thread/present path, gs_replay test
  builds; tests/test_display_settings.
- **Classification**: GENERIC-PS2. Subsystem: config + present.
- **Refactor destination** (§1.4 row): split — persisted keys (mode/aspect/scale/offsets)
  → `engine/core/config/` [video] fields (ini round-trip test retargets the Config
  loader); live geometry publication + applied-rect → `engine/gs/hle/present/
  view_state.{h,cpp}` (class `ViewState`).
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_U3_VIEWPORT | OFF | viewport fit probe logging | delete-both | DELETE |

- **Hacks / workarounds**: settings-as-atomics with a private ini dialect — replaced by
  the ONE Config schema (§4.8); mode/aspect string tables stay with the Config enum.
- **Risks**: GUI writes these live from the graphics thread — ViewState keeps the
  atomic publication contract. Prove: test_display_settings retargeted + a present
  fit/aspect visual anchor.

---

### host_app/snd_bank_ledger.cpp (148 lines) and host_app/snd_bank_ledger.h (49 lines)

- **Purpose**: [S46 FIX-2] persistent SFX bank source ledger: records STREAM.IRX wire
  pairings {cmd28 id → cmd41 file,offset,size} to a sidecar TSV merged across runs;
  after savestate load, walks the RESTORED module bank/file tables in IOP RAM, resolves
  ids through the ledger, re-reads the exact bytes from the ISO and rewrites SPU RAM —
  making pre-V4 states resume with working SFX.
- **Public surface**: `snd_bank_ledger::OnCmd28/OnCmd41/LoadSidecar/Persist/
  RestoreAfterLoad`; called from the SIF RPC dispatch (wire capture) and the savestate
  load path; compiled into host_app.
- **Classification**: MIXED — mechanism is generic “re-derive SPU uploads from source”,
  but every constant is the recompiled STREAM.IRX’s (IOP table ptrs 0x133e0/0x13388/
  0x12e60, cmd numbering, 0x28-byte records). Subsystem: sound / STREAM.IRX.
- **Refactor destination**: §1.4 says `engine/sound/bank_ledger.*` verbatim — but per
  the §1.6 sound row the STREAM.IRX contract points are title data; the honest split is
  class `BankLedger` (generic ledger+restore engine) in `engine/sound/bank_ledger.{h,cpp}`
  with the wire-command decoding + IOP table offsets supplied by
  `games/<title>/iop/stream_irx/` feed contract (TitleProfile).
  Sidecar path moves under the title’s save dir.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  - IOP absolute addresses 0x133e0/0x13388/0x12e60 in engine code (:102-107) — STREAM.IRX
    module layout; becomes title feed-contract data.
  - `logs/snd_bank_ledger.tsv` sidecar — a save-adjacent artifact living in logs/;
    moves beside the title save data with a Config path.
  - Positioned ISO reads sharing g_isoFile FILE* — replaced by the cdvd async read
    engine’s handle discipline.
- **Risks**: savestate-load audio recovery is user-visible; prove with the SOUND_P5/S46
  anchor states (pre-V4 state load → SFX present) + a ledger round-trip unit test.

---

### host_app/savestate.h (140 lines)

- **Purpose**: The savestate request/orchestration surface: quiescent-boundary contract
  (save only inside the frame-boundary hook with MTVU/queue/DMA/FMV drained), park-site
  shadow-chain harvest API (NotePark/NoteUnpark), live thread-ctx registry, save/load
  request plumbing (slot, named-auto with silent expiry, named-user synchronous with
  loud give-up), GUI LastResult/refusal surfaces, CLI ParseArgs, LoadCriticalActive
  guard, and the gfx frame-counter reseed handshake.
- **Public surface**: `savestate::` API above + `IsManualDispatcherFunction` extern;
  consumed by gs_hle (boundary hook), ps2_threading (park), slowspot/bug_report
  (captures), gui, main.
- **Classification**: GENERIC-PS2. Subsystem: core savestate.
- **Refactor destination** (§1.4 savestate row): split — envelope/orchestration → 
  `engine/core/savestate/savestate_host.{h,cpp}` (class `SavestateHost`: requests,
  quiescence, results); park-harvest + ctx registry stay coupled to ee/hle/threads’
  SectionProvider; `IsManualDispatcherFunction` coupling **dies in Phase 2** with the
  manual layer. [REV3 D9]: RequestSaveNamed also serves core/triage SavestateRing.
- **Flags consumed here**: none directly (PS2_SAVE_PROBE documented here, read in
  savestate.cpp — that shard owns its row; outcome DIAG).
- **Hacks / workarounds**:
  - Extern hook into the manual-dispatcher registry from the savestate header — cross-
    layer coupling that vanishes with the manual layer.
  - Two request classes (auto-silent vs user-loud) encoded as parallel functions —
    keep, but as one enum’d request type on SavestateHost.
- **Risks**: boundary semantics (a load consumes the boundary and early-returns) are
  load-bearing for every capture path; prove with savestate chain tests + slowspot/
  triage capture smoke + the R191 pending-save warning test.

---

### host_app/ring_tags.h (125 lines)

- **Purpose**: The ring-log tag registry: single X-macro table (`RING_TAG(sym,id,
  channel,arg0_doc,arg1_doc)`) naming every ring event (EE/GFX/INPUT/PERF/MEM channels,
  0xNNxx id spaces) so in-process emitters and the offline decoder share one source of
  truth; arg-doc strings are parsed by agents per docs/RING_LOG_SCHEMA.md.
- **Public surface**: `RING_TAG_TABLE(X)` macro + ringlog tag enums; consumed by
  ring_log.{h,cpp}, ring_log_decode.h, tools/ring_decode.cpp, and every emitter.
- **Classification**: GENERIC-PS2. Subsystem: core log.
- **Refactor destination** (§1.4 diagnostics keep-list): `engine/core/log/ring_tags.h`
  — verbatim (rides the ring.* sink + decode tool row). Tag DOCS stay in sync with the
  schema doc (keep the rule).
- **Flags consumed here**: none.
- **Hacks / workarounds**: some tags are session-born (FMV_PEND_EDGE, BOOT_SM_EDGE
  document game state words) — tag PRUNE at purge time for tags whose emitters die;
  the X-macro registry pattern itself is the correct shape and stays.
- **Risks**: decoder compatibility — bump the ring format version if ids are renumbered
  (prefer: never renumber). Prove: ring decode round-trip test.

---

### host_app/gif_template.h (116 lines)

- **Purpose**: [M1T] decode-once GIF packet template cache: structurally hashes complete
  PACKED/REGLIST packets at the zero-copy seam, first sighting builds a flat item list
  (tag/prim/packed/state/endxfer/flush), later sightings pour live payload bytes through
  the stored items — bypassing the GIFtag state machine. Fidelity-proven, perf-neutral
  (default OFF), retained as the seam for M5 terminator replacement.
- **Public surface**: `giftpl::On/ForceMissEvery/Item/Stats/S()` + template cache API;
  consumed by gif.cpp ReceiveData seam; [MID-TPL] bench rider.
- **Classification**: GENERIC-PS2. Subsystem: GIF.
- **Refactor destination**: §1.4 row says `gif/` packet-template helpers — verbatim.
  BUT: default OFF + “moves NO perf bucket” + its stated successor (M5 DrawRec serve)
  already shipped as the M5A terminator-native path (gs_state) — the honest Phase-4
  outcome is **delete both arms** unless the M5-refactor keeps the seam; carry as a
  purge-wave decision with the MIGRATION row as fallback (verbatim move to
  `engine/gif/packet_template.{h,cpp}`, class `PacketTemplateCache`). Flagged in errata.
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_MID_TEMPLATE | OFF (explicit wins) | template-serve enable | delete-both | DELETE (winner=OFF; supersession by M5A documented in the header itself) |
| PS2_MID_TEMPLATE_FORCEMISS | num=0 | every-Nth forced decline (equivalence exerciser) | delete-losing-arm-keep-winner ⚠A2 (it is a test hook) | TEST-ONLY → harness knob if the cache survives; else DELETE |

- **Hacks / workarounds**: immortal-entry no-eviction contract (serve dereferences
  outside the lock) — fine as documented; the hack is keeping a proven-neutral cache
  alive behind a default-OFF gate for two sessions. Decide, don’t carry.
- **Risks**: none while OFF. If kept: FNV-collision verify path needs its G-M1c test
  moved to tests/.

---

### host_app/uv_bbox.cpp (101 lines)

- **Purpose**: The ONE out-of-line UV bounding-box scan shared by producer (RenderQueue
  push stamp on EE/PS2 worker threads) and consumer (gfx-thread memo probe/legacy
  derive), with explicit rounding-mode normalization (FE_TOWARDZERO producers) so both
  sides compute bit-identical results; plus stamp census counters.
- **Public surface**: `uvbbox::Scan/CountStamp/StampCount0/1`; header uv_bbox.h (other
  shard); consumers: render queue push, opengl_backend memo, test_gs_classrdr.
- **Classification**: GENERIC-PS2. Subsystem: GS texsrc.
- **Refactor destination** (§1.4 row): `engine/gs/hle/texsrc/uv_bbox.{h,cpp}` (class/
  free-fn pair `UvBbox`) — verbatim ([S109]/[CX8] read-footprint math).
- **Flags consumed here**: none.
- **Hacks / workarounds**: fenv normalization inside the scan is load-bearing and
  correct (single-definition rule) — keep with its one-paragraph why; census atomics →
  core/diag.
- **Risks**: L1 stamp-validity depends on “same machine code” — keep it one TU after the
  move (no inlining into two homes). Prove: test_gs_classrdr + memo-stamp equivalence
  leg.

---

### host_app/bullseye_trace.h (86 lines)

- **Purpose**: 5-stage layered trace (EE chain bytes → DMAC tags → GIF tags → GS writes
  → GL draw) for the solved “bullseye text doesn’t render” bug; gated on PS2_BTRACE=1
  AND game scene word DAT_00309718==3, per-(stage,key) caps, hex-dump helper.
- **Public surface**: `bullseye_trace::active/log/hex_dump/reset`; emitters were
  sprinkled across dmac/gif/gs/backend TUs.
- **Classification**: GAME-SPECIFIC diagnostic (scene word address, bullseye scene).
  Subsystem: diagnostics.
- **Refactor destination** (§1.1 one-off diagnostics row): **delete** (concluded
  session; the layered-tap IDEA survives as core/diag categories + the .gsdl recorder).
- **Flags consumed here**: none in this header (PS2_BTRACE read in bullseye_trace.cpp —
  registry row delete-both; outcome DELETE).
- **Hacks / workarounds**: gating diagnostics on a guest RAM scene word (DAT_00309718)
  read from engine code — the anti-pattern the D5 split exists to kill.
- **Risks**: none; verify all emitter call sites go with it (grep bullseye_trace::).

---

### host_app/timer_hle.cpp (67 lines)

- **Purpose**: HLE replacement for the game’s TimerWaitMicroseconds busy-wait
  (func 0x191BD0): instead of spinning on T0_COUNT (which deadlocked the old fiber
  scheduler), sleeps 1 ms under a pacebrk EE_SLP bracket and returns 1 (“wait
  completed”), skipping the guest’s tick math entirely.
- **Public surface**: `hle_TimerWaitMicroseconds(CpuContext&)` (timer_hle.h); referenced
  by main.cpp hook wiring (comment at main.cpp:3238 notes the always-return-1 behavior);
  compiled into host_app.
- **Classification**: GAME-SPECIFIC (binds a game function’s semantics; 0x191bd0 is the
  same address as the frame-budget predicate family, §1.6 row E). Subsystem: EE timing
  HLE.
- **Refactor destination**: §1.4 row says `ee/hle/` (sce timer/alarm HLE) verbatim —
  imprecise: this is NOT an sce library surface, it is a title wait-model. Route per
  §1.6 row E instead: the mechanism (budget_anchor emu-domain wait) is
  `engine/ee/timing/`; the FUN_00191bd0 model parameters live in
  `games/<title>/hle/budget.cpp` (TitleHleModule). If the
  main.cpp:3226-3608 budget hook already supersedes this body, it retires with that
  adjudication. Flagged in errata.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  - “We don’t need to replicate the exact math” + unconditional `return 1` — a timing
    model that lies to the caller; correct implementation computes elapsed emu-domain
    ticks against the object’s tick_rate/divisor (DerivedClocks) and returns the real
    predicate.
  - Fixed 1 ms host sleep as guest time — wall/emu domain mixing (BUG-014 family).
- **Risks**: load-time and budget behavior; prove on the budget/boot-phase anchor legs
  ([S96]/[S112]) + BUG-019/BUG-013 regressions when re-modeled.

---

### host_app/slowspot.h (61 lines)

- **Purpose**: Contract header of the frame-cadence slow-spot watchdog: graphics-thread
  OnPresent (window p50 over threshold for a full 60-present window, cooldown-gated) →
  ring event + stderr line always; savestate/snap/bundle capture only when armed;
  OnSnapWritten copy hook; exit summary.
- **Public surface**: `slowspot::OnPresent/OnSnapWritten/PrintExitSummary`; called from
  the present site and bug_report/gui.
- **Classification**: GENERIC-PS2. Subsystem: capture/triage.
- **Refactor destination** ([REV3 D9] §1.4 row): detector core →
  `engine/core/triage/perf_monitor.{h,cpp}` (class `PerfMonitor`, O(1) p50 window moves
  verbatim from slowspot_detector.h); the capture trio (state/snap/_logs naming,
  slowspot_paths) retires into `TriageBundler` layout; env gates absorbed by Config
  `triage.*` (thresholds per TRIAGE_CAPTURE §3).
- **Flags consumed here**: none directly (documents PS2_SLOWSPOT/_MS/_COOLDOWN_S, read
  in slowspot.cpp — that shard’s rows; outcomes: SETTING → triage.perf_threshold_ms=40,
  triage.perf_cooldown_s=30, triage.perf_capture).
- **Hacks / workarounds**: default detector-on/capture-off split born of
  [FIX-SLOWSTICK] — the D9 TriageWriter async budget makes capture safe-by-construction;
  keep the lesson as PerfMonitor’s cost-governance contract (§9).
- **Risks**: hot-path cost on every present; prove with the pace tests + a triage
  perf-capture smoke; BUG-025 (log format tax) guards the emit path.

---

### host_app/memory_test.cpp (45 lines)

- **Purpose**: Phase-1-era gtest for WriteMemory32/ReadMemory32 + out-of-bounds death
  tests, against an API (`WriteMemory32`) that no longer exists (memory:: templated API
  replaced it).
- **Public surface**: none; **zero CMake references** (verified); named a dead scaffold
  in §1.1.
- **Classification**: dead test. **Destination: delete** (§1.1). Live memory coverage
  belongs to tests/ against ee/mem router.
- **Flags**: none. **Hacks**: n/a. **Risks**: none.

---

### host_app/thread_qos.h (19 lines)

- **Purpose**: macOS QoS pin: `PinThreadInteractive()` sets USER_INTERACTIVE on calling
  thread so emulation threads land on performance cores (bimodal VU1 timing fix);
  no-op elsewhere; PS2_NO_QOS=1 disables for A/B.
- **Public surface**: `PinThreadInteractive(const char*)`; called at thread starts
  (cpu/gfx/vblank/MTVU workers).
- **Classification**: GENERIC-PS2 (host platform). Subsystem: platform threads.
- **Refactor destination** (§1.4 row): `engine/platform/threads.*` — verbatim, behind
  the ONE Platform interface (macos/ implementation; other targets no-op; no #ifdef
  outside platform/ per D7).
- **Flags consumed here**:

| var | default today | what it selects | disposition (A2) | refactor outcome |
|---|---|---|---|---|
| PS2_NO_QOS | OFF (QoS pin ON) | disable QoS pinning (A/B) | delete-losing-arm-keep-winner | NORM (pin unconditional on macOS; losing arm deleted) |

- **Hacks / workarounds**: `#ifdef __APPLE__` in a shared header — exactly what
  platform/ folders exist for.
- **Risks**: none; perf sanity via bench legs on macOS.

---

### host_app/vu_aot_programs_6.cpp · _12.cpp · _18.cpp · _23.cpp · _29.cpp (17 lines each)

- **Purpose**: Fixed checked-in TU aggregators for the VU1 AOT tier (chunks 6/12/18/23/29
  of K=24-era split, [R158-TUSPLIT]): each includes vu.h + vu_aot_registry.h +
  vu_decode_slot.h + vu_fast_exec.h then the generated `vu_aot_generated_N.inc`,
  exporting `g_vu_aot_chunk_descs_N` for static-init aggregation.
- **Public surface**: chunk desc arrays consumed by vu_aot_programs.cpp master registry;
  compiled into `vu_aot_lib`.
- **Classification**: GENERIC-PS2 build scaffolding for a retired tier. Subsystem: VU.
- **Refactor destination**: **delete** with the whole AOT tier (§1.1 NOAOT row:
  vu_aot_gen, 33 chunk TUs, registry consumers, 6 ctest gates, 233 MB inc + 351 MB lib).
- **Flags**: none. **Hacks**: the generated-.inc-into-fixed-TU dance exists to keep
  CMake source lists static — dies whole. **Risks**: none (default-OFF tier); ensure
  the vu_aot_lib target and its ctest gates go in the same commit.

---

### host_app/vu_aot_status.h (13 lines)

- **Purpose**: Tiny accessor header (`VuAotRegistryCount()`) deliberately separate from
  vu_aot_registry.h so GUI consumers don’t trigger the 2.5 h AOT lib recompile.
- **Public surface**: one function; consumers: gui/vu_microcode_section, vu_aot_programs.
- **Classification**: GENERIC-PS2 (retired tier). **Destination: delete** with the AOT
  tier (§1.4 explicitly lists vu_aot_status.h in the delete row).
- **Flags**: none. **Hacks**: the header exists to dodge a build-cost problem the
  deletion removes. **Risks**: GUI section loses the count readout — the Triage/VU panel
  replaces it (TRIAGE_CAPTURE §12).

---

## SHARD SUMMARY (shard 3 — 37 files, 24,610 lines)

Flags column = unique env vars read in the file → NORM/SETTING/DIAG/TEST/DELETE outcome
counts. “+N fam” = additional sub-gate names read through a generic `GateOn(name)`
helper in this file.

| file | lines | class | destination | shape | flags (N→N/S/Dg/T/Del) | hacks |
|---|---:|---|---|---|---|---:|
| gs_state.cpp | 5,623 | MIXED | gs/state/{gs_registers,vertex_kick,flush} + gs/xfer/transfer_engine + gs/vram; game consts → renderer_profile VramLayout + TitleProfile scene-hook | split | 13 → 6/0/5/0/2 | 7 |
| render.h | 3,734 | MIXED | gs/hle/ir/{draw_ir,render_batch} + gs/hle/queue/{render_queue,draw_run_merge} + core/hash.h; ~21 gate ns dissolve | split | 45 (+13 fam) → 24/1/3/1/16 | 7 |
| texture_decoder.cpp | 3,049 | MIXED | gs/format/{psm_layout,clut,expand,texa,alpha,block_decode} + gs/hle/texsrc + gs/hle/present (scanout) | split | 0 | 3 |
| manual_switch_dispatchers.cpp | 2,303 | GAME | retired Phase 2 (per-address adjudication; mechanism already in ee/cpu seams) | deleted | 2 → 0/0/0/0/2 | 5 |
| test_preservation.cpp | 1,246 | dead test | — | deleted (§1.1, 0 CMake refs) | 0 | 1 |
| vu.h | 1,171 | GENERIC | vu/{vu1_state,xgkick,flags} + vu/tiers/{interp,cached}; AOT surfaces deleted | split | 0 direct (latched members; rows in vu.cpp shard) | 3 |
| test_black_screen_scanout.cpp | 918 | dead test | — | deleted | 0 | — |
| test_alpha_blend_preservation.cpp | 745 | dead test | — | deleted | 0 | — |
| gs_interp.h | 666 | GENERIC | gs/hle/interp/frame_interp | verbatim + purge | 5 → 3/2/0/0/0 | 3 |
| ee_timers.cpp | 605 | MIXED | ee/intc/ee_timers + ee/timing/time_domain | split | 6 → 2/1/1/0/2 | 3 |
| bug_report.cpp | 564 | GENERIC | core/triage/{triage_bundler,bundle_manifest} (D9) | split/rewrite | 3 → 0/3/0/0/0 | 3 |
| test_prim_change_bug_condition.cpp | 498 | dead test | — | deleted | 0 | — |
| test_boot_bug_condition.cpp | 453 | dead test | — | deleted | 0 | — |
| s8_ctxlatch.h | 404 | MIXED | workaround retired (default OFF); rule → gif/arbiter order; trigger data → renderer_profile if relanded | deleted (purge) | 2 → 0/0/0/0/2 | 3 |
| hle_meta_arena.cpp | 350 | GAME | quarantined ee/mem/workaround_meta_arena → retired at 0x181560 root fix (HOOK §3.1) | quarantine→delete | 0 | 3 |
| ps2_threading.h | 326 | GENERIC | ee/hle/threads (class ThreadManager; maps leave header) | split/rewrite | 0 | 2 |
| texture_decoder.h | 267 | GENERIC | gs/format + gs/hle/texsrc decls; r118 ns dissolves | split | 1 (+4 fam) → 5/0/0/0/0 | 2 |
| savestate_format.cpp | 242 | GENERIC | core/savestate/format (frozen) | verbatim | 0 | 0 |
| cuelag_probe.h | 200 | GENERIC | — (concluded probe) | deleted | 2 → 0/0/0/0/2 | 1 |
| display_settings.cpp | 191 | GENERIC | core/config [video] + gs/hle/present/view_state | split | 1 → 0/0/0/0/1 | 1 |
| snd_bank_ledger.cpp | 148 | MIXED | sound/bank_ledger (mechanism) + games/…/iop/stream_irx feed contract (IOP table data) | split | 0 | 3 |
| savestate.h | 140 | GENERIC | core/savestate/savestate_host (+ thread SectionProvider coupling) | split | 0 | 2 |
| ring_tags.h | 125 | GENERIC | core/log/ring_tags.h | verbatim | 0 | 1 |
| gif_template.h | 116 | GENERIC | gif/packet_template (MIGRATION row) — or delete at purge (default-OFF, superseded by M5A; errata) | verbatim-or-delete | 2 → 0/0/0/1/1 | 1 |
| uv_bbox.cpp | 101 | GENERIC | gs/hle/texsrc/uv_bbox | verbatim | 0 | 1 |
| bullseye_trace.h | 86 | GAME diag | — (concluded session) | deleted | 0 | 1 |
| timer_hle.cpp | 67 | GAME | per §1.6 row E: mechanism ee/timing, params games/…/hle/budget.cpp (MIGRATION §1.4 row imprecise; errata) | rewritten | 0 | 2 |
| slowspot.h | 61 | GENERIC | core/triage/perf_monitor (D9) | split | 0 | 1 |
| snd_bank_ledger.h | 49 | MIXED | with snd_bank_ledger.cpp | split | 0 | (with .cpp) |
| memory_test.cpp | 45 | dead test | — | deleted | 0 | 0 |
| thread_qos.h | 19 | GENERIC | platform/threads (macos impl) | verbatim | 1 → 1/0/0/0/0 | 1 |
| vu_aot_programs_6.cpp | 17 | GENERIC (retired tier) | — | deleted (NOAOT) | 0 | 1 |
| vu_aot_programs_12.cpp | 17 | 〃 | — | deleted | 0 | 〃 |
| vu_aot_programs_18.cpp | 17 | 〃 | — | deleted | 0 | 〃 |
| vu_aot_programs_23.cpp | 17 | 〃 | — | deleted | 0 | 〃 |
| vu_aot_programs_29.cpp | 17 | 〃 | — | deleted | 0 | 〃 |
| vu_aot_status.h | 13 | GENERIC (retired tier) | — | deleted | 0 | 1 |

Shard flag totals (unique vars read in these files): 83 direct (+17 family sub-gates) →
NORM 41, SETTING 7, DIAG 9, TEST-ONLY 2, DELETE 28 (+ family sub-gates NORM).

## TREE-GAPs and plan errata found by this shard

1. **No TREE-GAP proper** — every file has a tree home or a deletion row. Four plan
   ERRATA to reconcile:
2. MIGRATION §1.3 render.h row says “5 gate namespaces dissolved by purge”; actual count
   in render.h is ~21 session namespaces (r119, r119dep, r120, r121, r123, r124, r125,
   r147, r148, r154, r159, r160, r162, r164, r167, r184, r197, r198, r201, sh5, aquad)
   plus xxh (vendored) and a1a.
3. MIGRATION §1.4 assigns s8_ctxlatch.h to “purge-dissolve into ee/timing + ee/hle” —
   subject matter is GIF drain ordering; the dissolve target should read engine/gif/
   (arbiter stream-order rule). Outcome (file dies at purge) unchanged.
4. MIGRATION §1.4 timer_hle row (“ee/hle sce timer/alarm HLE — verbatim”) mislabels a
   game-function wait model (0x191bd0 = the §1.6 row E budget family). Route per row E:
   mechanism → ee/timing, params → games/…/hle/budget.cpp; adjudicate against the
   main.cpp budget hook that supersedes it.
5. gif_template.h: MIGRATION §1.4 moves it verbatim to gif/, but it is a default-OFF
   proven-neutral cache whose declared successor (M5 terminator-native) already shipped
   in gs_state.cpp — recommend the Phase-4 wave decides delete-both vs keep; both
   documented in the entry.
6. ⚠A2 discrepancies needing gate-purge step-1 adjudication (code default ON but CSV
   says delete-both): PS2_R199_XFERLATCH, PS2_R199_UPLOADEAGER, PS2_GS_SNAPSHOT_FAST,
   PS2_XFER_FAST, PS2_R198_D1FMV, PS2_SH5_ALIASEPOCH; plus PS2_GS_SNAP_FOOTPRINT
   (behavioral but categorized diagnostic) and PS2_BUGREPORT* (A2 pre-dates D9 triage
   decision) and PS2_MID_TEMPLATE_FORCEMISS / PS2_GS_RUNDRAW_CHECK (test hooks
   categorized as knobs).

## Dead code found (with evidence)

- host_app/test_preservation.cpp, test_black_screen_scanout.cpp,
  test_alpha_blend_preservation.cpp, test_prim_change_bug_condition.cpp,
  test_boot_bug_condition.cpp, memory_test.cpp — zero references in any
  CMakeLists.txt/*.cmake outside build output dirs (grep 2026-09-23); memory_test.cpp
  additionally targets a long-removed WriteMemory32/ReadMemory32 API. All named by
  MIGRATION §1.1.
- vu_aot_programs_{6,12,18,23,29}.cpp + vu_aot_status.h — live in the build
  (vu_aot_lib) but the tier is default-OFF per the NOAOT decision record and §1.1
  deletes it wholesale; not dead today, dead by decision.
- bullseye_trace.h / cuelag_probe.h — compiled but gated permanently OFF by default and
  tied to concluded sessions (§1.1 one-off diagnostics row).

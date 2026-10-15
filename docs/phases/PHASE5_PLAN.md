# PHASE5_PLAN — engine/{gif,vu,vif,dmac} + the VU1 library as title data: CONSTRUCTION_PLAN §4.5 made executable (MODULE GATE scope)

Status: planning complete (2026-09-27 — part 1: header + §1–§2; part 2: §3–§5; part 3: §6–§8 + HANDOFF). Stage 5A
starts from the HANDOFF at the end of this file.

Sources for §1–§2, and nothing else was read: `/tmp/p5/CONTRACT.md`, `/tmp/p5/5P/FACTS.md` (stage 5P fact sheet,
2026-09-27; every OLD `file:line` below is quoted from it, re-verified there with `grep -n` against the frozen tree),
`docs/design/CONSTRUCTION_PLAN.md` §4.5 (`:536-596`), `docs/design/ARCHITECTURE.md` §4.10 / §14.2,
`docs/phases/PHASE4_PLAN.md` (format only). `STAGE_RULES.md` and the PARALLEL-WORK CONTRACT (§1.5) bind every stage
below (bounded runs, OLD read-only and never built or `git`-touched, no commits, tree green at every stop, edits only
under OWNED paths). OLD = `$PS2R_OLD_ROOT` (frozen; unqualified OLD paths are under
`OLD/host_app/`). NEW = `<repo>-p5`, branch `phase5`.

## 1. Goal and scope

### 1.1 Start state — tree HEAD `78b5655 Phase 4 WIP (4A-4B'')`

- What exists (FACTS §2.10): `ps2r_core`, `ps2r_diag` (`PS2R_DIAGNOSTICS` only), `ps2r_ee` (cpu state incl. the
  608-byte `VUregs` COP2 block, memory, timing, hle), `ps2r_gs_format` + `ps2r_gs_sync`, `ps2r_iop`, `ps2r_platform`,
  `ps2r_title_hle`, `ps2r_title_recomp`; executable `nativeee`. Root `CMakeLists.txt:37-53` adds
  `engine/{core,platform,gs,ee,iop}`, `games/${PS2R_TITLE}/{recomp,hle,iop/stream_irx}`, `app/host`, `tests`;
  `tests/CMakeLists.txt` ends with `add_subdirectory(core|gs|oracle|ee|iop)`.
- Public surface Phase 5 consumes (FACTS §2): `engine/core/types.h`, `engine/core/hash/hash.h`
  (`ps2r::hash::Fnv1a64` — the harness hash, NOT the VU image hash), `engine/core/log/log.h` (categories
  VIF/GIF/VU/DMAC exist), `engine/core/diag/diag_registry.h`, `engine/ee/mem/{main_memory,scratchpad,quad_word,
  fast_mem,address_map,mmio_handler,mmio_router}.h`, `engine/ee/cpu/cpu_state.h` (`VUregs`, sizeof 608),
  `engine/gs/format/psm.h` (the only `engine/gs` dependency allowed), `games/…/hle/title_profile.h`
  (`quirks.xgkick_defer = true`, passed into the engine as a bool — the engine never includes a `games/` header).
- `engine/{vu,vif,gif,dmac}` are README-only: `engine/{dmac,gif,vif,vu}/README.md` + `hle/README.md` each,
  `engine/vu/{tiers,vu0}/README.md`, `games/<title>/vu1_library/README.md`. No `.h/.cpp`, no
  CMake, no tests under `tests/{gif,vu,vif,dmac}`, no `tools/{vu1_replay,vif_replay}`.
- Not in NEW and needed here (FACTS §2.6/§2.3): no VU1 register block / VU memories / flag-pipeline state; no VU
  float helpers (`ps2_float_clamp` does NOT flush denormals — never reused for VU); no `Vu1Case` reader; no
  `MmioHandler` for GIF/VIF/DMAC (ranges idled by `MmioSink`, `engine/ee/mem/mmio_sink.cpp:18-37`).

### 1.2 End state — the MODULE GATE (and only that)

The phase lands when every item below is quoted in `PHASE5_IMPL.md` from a bounded run in this tree:

1. `tools/vu1_replay --compare <case>` over all 126 `OLD/vutrace_cases2/*.bin` (126 × 33,456-byte `Vu1Case`,
   FACTS §7.1) → 126 × `COMPARE PASS: … regs_identical aot_dispatches=0 (interp vs cached)`, script total
   `cases=126 pass=126 fail=0` — matching OLD `build/tests/vu1_replay` on the SAME cases, whose recorded result is
   `TOTAL cases=126 pass=126 fail=0` (FACTS §1.1 `old_compare.log`). Per case the NEW post-run 16 KB data memory +
   `VUregs` are also diffed against the OLD tool's `[postdata_out]` dump (bit-exact, not just PASS-vs-PASS).
2. `tools/vu1_replay --hle <case>` over the same 126 → 126/126 with the SAME verdict per case as OLD: `HLE PASS`
   for the 124 cases on the 61 covered images, exit 77 `HLE SKIP` for the cases on the two uncovered images
   `0d20914e` / `50b7302d` (OLD `--catalog`: `CATALOG cases=126 images=63 hle_covered=61/63
   bundle_coverage=100.00%`, FACTS §7.2). Header line (`image=… variant=… prefix=… extent=… entry=… top=…
   dispatch=…`) and verdict line compared token-for-token, `[VU-MODE]` announce lines skipped, the `direct=` token
   per §2.9 (tool-side `PacketToOps`, FACTS R7). `--catalog OLD/vutrace_cases2` → 63 rows + footer = `old_catalog.log`.
3. The 3,293-case `OLD/vutrace_collect` sweep (`vu1_case_%04d_%03x_%08x.bin`, all 33,456 bytes) with
   `--compare` → `FAIL=0`, each case compared to the OLD tool run bounded on the same case.
4. `tools/vif_replay`: the 6 surviving OLD ctest arms (FACTS §7.3 — the 8 OLD arms minus the two
   `PS2_VU1_EXEC_ASYNC=1` arms, whose exec-thread code is DELETE) print the same invariant tokens as OLD's
   `old_vif_arms.log`: `MTVU EQUIVALENCE PASS: 20 iterations, 120 packets total, byte-identical VU1 data+code mem,
   identical packet census+order, identical VF/VI/TPC`, `converged every iteration, monotone idle`, `EAGER-DRAIN
   pass: order+mem identical`, `BOUNDARIES … 60 enqueued+delivered position-exact`, 30 × `[UNPKB][<name>] PASS
   (bulk_cmds=N skip_cmds=M)` + `UNPKB EQUIVALENCE PASS: 30 cases, …`, `runahead selftest: PASS` ×2. The arms are
   SELF-CONTAINED SYNTHETIC chains (no `mem.bin` capture exists in OLD); the timing-dependent counts (`busy
   views`, `pkts eager-drained`, `(k/20 iters)`, `parked N ms`) are excluded from the assertion.
5. The VU1 HLE library as title data under `games/<title>/vu1_library/`, one file per
   family (exact split §2.5). Reconciliation of "15 families" with FACTS §3.5: OLD `hle/vu1_hle_programs.cpp`
   holds 8 named family namespaces (`ttx`, `pcs`, `lit`, `crys`, `skn`, `idx`, `zk`, `spr`) + the top-level
   `fam` group = 9 code groups, and 7 loop-driven registration tables (`s_reg_zk_all`, `s_reg_zk_cont`,
   `s_reg_spr_all`, `s_reg_crys_all`, `s_reg_fog_all`, `s_reg_f20442_all`, `s_reg_cs9a_disp`) — 8 + 7 = 15 is the
   brief's count; the plan splits by CODE GROUP (9 program files), each registration table living in the file of
   its family. Pinned counts come from the transplanted table, not the docs: 140 `Register(` sites, 176 distinct
   variant hashes (120 inside literal `Register(...)` args), 12 program names; the "137 entries / 120 hashes"
   figure in CONSTRUCTION §4.5 / ARCH §14.2 is STALE (FACTS R6). The NEW registry self-reports
   `entries=N distinct_variant_hashes=M` and `PHASE5_IMPL.md` records N/M.
6. Tree green: `cmake --build --preset macos-dev` 0 warnings (`-Wall -Wextra -Werror`), `ctest --preset macos-dev`
   all pass (incl. the new `tests/{gif,vu,vif,dmac}` rows and `CoreSingleGetenvSite`), both lints PASS; the five
   family constants `{0x1720, 0x090E0003, {0x70,0x90}, 0xA0}` and every image/variant hash appear ONLY under
   `games/…/vu1_library/` (review-enforced: FACTS §2.12 notes the 4-digit literals escape the purity regex).

### 1.3 Lanes — NONE claimed in this phase

No harness lane (`frames`, `audio`, `mem`) is claimed. CONSTRUCTION §4.5's "`--lanes audio,mem` IDENTICAL on
ALL 12 legs", the `gsops.ohash` artifact, `MmioRouter::UnhandledRanges()` shrinking, the P4FIX-F1 legs and the
`--vu1-library none` [GD1] boot are INTEGRATION gates: they need `MmioRouter` registration, `BoundarySequencer`
steps, `app/host` wiring and savestate `SectionSpec` registration, all FORBIDDEN here (§1.5); they are carried as the
HANDOFF integration list (§8, seeded from FACTS §10). Two harness facts shape the module work anyway (FACTS §3.7/§3.1):
the frozen baselines were recorded with `PS2_MTVU=0` and the GIF drain thread OFF — the synchronous VIF1→VU1→GIF
inline path is what the harness will certify; MTVU and the drain thread are proven only by `vif_replay` + `tests/gif`.

### 1.4 In scope (per directory; all OWNED)

- `engine/gif/`: GIFtag parse + eligibility classification, GIF registers as an `MmioHandler` (not registered),
  the canonical `ParseSpan` parser writing to an abstract `GsSink`, the recording `RegisterOpStream` sink + FNV
  hasher, `GifArbiter` (submission-order drain, PATH1/2/3, markers, boundary slots), `GifDrainThread`.
- `engine/vu/`: `Vu1State` (registers + 16 KB micro + 16 KB data), ONE decode (`vu_decode_slot.h` canon),
  flag pipelines, ONE stall model, `XgKick` (paced drain, 32-cycle quantum), VU float helpers incl. the 4-lane
  FNV image hash, the FROZEN `Vu1Case` POD + reader, span classification; `tiers/`: interpreter (reference),
  cached tier (`vu_fast_exec.h` dissolves), tier dispatch (HLE → cached → interp), MTVU worker / backpressure /
  sync / runahead guard; `hle/`: contract, registry (exact → variant → wildcard lookup in registration
  order), image analyzer parameterized by `Vu1FamilySignature`, runtime (memo + `TryExecute`), raw-only
  `KickLowering` with the Phase-6 seam, `GifPacketBuilder`; `vu0/`: libvu0 sin/cos fingerprint HLE +
  VU0 descriptor mechanism.
- `engine/vif/`: register POD + bit constants, both units' state machine + `ExecuteCommand` + latch path +
  `MmioHandler`, UNPACK (all formats/masks/CL-WL, scalar oracle), fast span parser, bulk NEON/scalar lanes.
- `engine/dmac/`: register file + `MmioHandler` (not registered), channel POD/ids/constexpr bits, chain walker
  (VIF/GIF tags, TTE, normal), deferred VIF1 kick + tail chase + suspended-chain retry, handler registry.
  SPR channels replicate OLD's stub (no copy — FACTS R2); SIF/IPU bodies are injected callbacks.
- `games/<title>/vu1_library/`: `Vu1Library` (signature literal + registration table in
  REGISTRATION ORDER), one file per family, shared helpers, `vu0_programs` data.
- `tools/vu1_replay/`, `tools/vif_replay/`: the two oracle tools built against the subsystem libraries only
  (the layering proof); `tests/{gif,vu,vif,dmac}/`: ported unit tests + the corpus ctest rows (SKIP when
  `PS2R_OG_ROOT` is absent, the `tests/oracle` precedent).
- Shared append-only edits (exact form §1.5): four `add_subdirectory(engine/<dir>)   # Phase 5` lines, one
  `add_subdirectory(games/${PS2R_TITLE}/vu1_library)   # Phase 5` line, four `add_subdirectory(<dir>)` lines.

### 1.5 Out of scope + the PARALLEL-WORK CONTRACT (restated from CONTRACT.md §2; narrows STAGE_RULES)

Out of scope (with destination): `MmioRouter` registration of GIF/VIF/DMAC ranges and the VU memory hardware
windows → integration (HANDOFF); `BoundarySequencer` steps (`ChaseVif1Tail`/`FlushDeferredVif1` at SyncV, MTVU
`openBoundary`+`drain`) → integration; savestate `SectionSpec` registration for `TAG_VU0C/VU0D/VU1C/VU1D/DMAC/
VIFS/GIFS/VU1R` → integration (modules expose factories only); DrawIR / DirectOps kick tiers and
`gs_draw_ir.h`/`gs_direct_ops.h` → Phase 6 (`engine/gs/hle/ir`); the `RegisterOpStream` → `engine/gs/hle/ir` and
`Vu1Case` → `engine/core/diag` moves → when those dirs reopen; `Vu1Case` WRITER / `vutrace_collector` (DIAG) → Phase 9;
`dmac/hle/libdma_bindings.cpp` → not installed (rows resolve via `SdkBindingManifest` in `games/…/hle`);
`packet_template` → dropped (`PS2_MID_TEMPLATE` default OFF, zero `TplTryServe` hits in any baseline); VU-AOT
surfaces → never (NOAOT); the exec-thread arm (`PS2_VU1_EXEC_ASYNC` family, `W13E3_WaitPoint`, `unpack_shadow`,
`CommitStagedUnpacks`) → never (DELETE).

PARALLEL-WORK CONTRACT:

- OWNED (create/edit freely): `engine/dmac/**`, `engine/gif/**`, `engine/vif/**`, `engine/vu/**`,
  `games/<title>/vu1_library/**`, `tests/dmac/**`, `tests/gif/**`, `tests/vif/**`,
  `tests/vu/**`, `tools/vu1_replay/**`, `tools/vif_replay/**`, `docs/phases/PHASE5_PLAN.md`,
  `docs/phases/PHASE5_IMPL.md`.
- READ-ONLY (use public headers; never edit): `engine/core/**`, `engine/platform/**`, `engine/ee/**`,
  `engine/iop/**`, `engine/gs/**` (only `engine/gs/format` may be depended on), `app/**`,
  `games/**/{recomp,hle,iop,title_profile*}`, `docs/**` except the two PHASE5 files, `verify/**`.
- SHARED — append-only, exact form: root `CMakeLists.txt` gains `add_subdirectory(engine/<dir>)   # Phase 5`
  after the existing `add_subdirectory(engine/iop)` line (one each for gif, vu, vif, dmac) and
  `add_subdirectory(games/${PS2R_TITLE}/vu1_library)   # Phase 5` after the existing `games/` lines;
  `tests/CMakeLists.txt` gains `add_subdirectory(<dir>)` lines at the end (gif, vu, vif, dmac). NOTHING else
  in those two files.
- FORBIDDEN this phase: registering with `MmioRouter`; adding `BoundarySequencer` steps; adding `Config` keys or
  touching `docs/filemap/settings_table.tsv`, `FLAG_LEDGER.md`, `FLAG_PHASE_COUNTS.txt`, any `README.md` outside
  OWNED paths; wiring into `app/host`; binding DMAC SIF channels to `engine/sif`; claiming any harness lane;
  `git commit` (the user commits); Phase-4 symbols beyond the tree at `78b5655`; building/editing/`git`-touching
  OLD; launching `OLD/build*/host_app/host_app` (only `OLD/build/tests/{vu1_replay,vif_replay}` run, bounded).
- Settings rule: where a NORM/SETTING ledger row wants a `Config` field, define a module-local `<Module>Settings`
  struct with the PEAK default instead of a Config key and list the intended key in the HANDOFF integration list.
  This phase: `VuSettings::vu1_mode = Cached` (← `engine.vu1_mode`, exists), `MtvuSettings::enabled = true`
  (← `engine.mtvu`, exists; deterministic profile = false), `MtvuSettings::depth_frames = 2` (← `engine.mtvu_depth`,
  exists), `VifUnpackSettings::bulk_lanes = true` (tool/test knob, no key intended). Exactly ONE `getenv` stays in
  the tree (ConfigLoader); Phase 5 adds none — TEST-ONLY env pins become tool CLI options.

## 2. Module table

Sources: FACTS §3 (OLD anatomy), §4 (GsSink), §5 (flag outcomes), §8 (tests); CONSTRUCTION §4.5 rows (`:538-551`).
Conventions: one primary type per `.h/.cpp` pair, file = snake_case(type); types-only headers (PODs, enums, free
functions) have zero primary types and are lint-clean (`lint_one_class.py:17`). "Flags applied" = NAMES only at the
row of the OLD `getenv` site; "(omit)" = DIAG not written this phase, "(not written)" = DELETE arm. OLD anchors are
under `OLD/host_app/` unless prefixed `tests/`. `gs_sink.h` + `register_op_stream` are placed under OWNED
`engine/gif/` (CONSTRUCTION names `gs/hle/ir/`; `engine/gs` is read-only — FACTS §4.3/R1); the moves are HANDOFF rows.

### 2.1 `engine/gif` — 7 rows (+2 dropped/folded)

| New file | Class | OLD source file:lines | Flags applied |
|---|---|---|---|
| `gif_tag.h/.cpp` | `GifTag` — `Parse(lo,hi)` (nloop/eop/pre/prim/flg/nregs 0→16/regs nibbles) + eligibility bits `has_ad / all_known / strip3 / rowable` | `gif.h:89-112`; `gif.cpp:3643-3675` | — |
| `gif_regs.h/.cpp` | `GifRegs : MmioHandler` — CTRL/MODE/STAT (composed bits M3R/M3P/IMT/PSE/OPH/APATH/FQC)/TAG0-3/CNT/P3CNT/P3TAG, `path3_masked` atomic; NOT registered. Collapses §4.5 `gif_unit` + `gif_regs` (the unit has no state beyond regs + FIFO count) | `gif.h:120-202`; `gif.cpp:185-213, :215-300, :302-380` | — |
| `gif_parser.h/.cpp` | `GifParser` — `ParseSpan` canonical walk (Q=1.0 per tag, PRE→PRIM, NLOOP=0 legal, PACKED strip3/rowable/per-QW lanes, REGLIST two per QW + odd pad, IMAGE→HWREG halves, A+D via the one dispatcher, TEXCLUT dropped, REGLIST XYZ2 ignores ADC — FACTS R10), `ReceiveData/ProcessBuffer/FinishDMA`, PACKED decoders, generic `ProcessPacked/ProcessReglist` per-QW bodies; writes to `GsSink&` | `gif.cpp:552, :647-658, :716-735, :3183-3200, :3211-3264, :3266-3350, :3585-4324, :4374-4400, :4541-4699, :4700-5156, :5157-5224` (probes `:382-715` not written except the size-0 / PATH3 stale-reset rules `:552, :647-658`; dead twin `:2904-3182` not written) | `PS2_GIF_FAST`, `PS2_GSK_SIMD`, `PS2_EEFEED` (B2 row loop), `PS2_SHADOW8_KTAP` (omit), `PS2_MIPCEN` (omit), `PS2_S26_DECAL` (not written) |
| `gs_sink.h` | `GsSink` — abstract, 10 pure virtuals `BeginTag / WriteRegister / PackedRgbaq / PackedSt / PackedUv / PackedFog / KickVertex / ImageData / EndOfPacket / FinishPath3`, one per OLD `g_gs_state` mutation (FACTS §4.2); header-only interface | `gif.cpp` `g_gs_state` surface (FACTS §4.1: `:3216-3237, :3641, :4710-4746, :5002-5008, :5083, :5160-5172`) | — |
| `register_op_stream.h/.cpp` | `RegisterOpStream : GsSink` — 24-byte `RegisterOp` POD records, `Hash()` = `ps2r::hash::Fnv1a64` chained from `kFnv1a64Offset`, floats hashed as raw bits; `Count/Clear/Ops` (+ test-only `CountingGsSink` in tests/gif) | new (FACTS §4.3); §4.5 row `gs/hle/ir/register_op_stream` | — |
| `gif_arbiter.h/.cpp` | `GifArbiter` — `GifArbiterPacket {path, is_directhl, is_path3_image, data, boundary_seq, bnd_enq_ns}` (hle_ops/hle_ir fields arrive Phase 6), `GifMtvuMarker`, `submit/submitOwned`, `acquireBuffer/recycleBuffer` (`kPoolMax=4`), `drain()` in SUBMISSION ORDER, `openBoundary/dispatchBoundaryInline` (hook ctor-injected), `openMarker/sealMarker/collectReady`, `PendingBytes/PendingSlots` as members; `boundary_slot` (TREE-GAP #9) is the `boundary_seq` field, no separate class | `gif_arbiter.h:85-112, :184-195, :229-247, :352-372, :562-590, :611`; `gif_arbiter.cpp:20-30, :579-783, :933-1808` (`:52-578, :590-650, :700-840` experiments not written) | `PS2_R198_PATH3DEFER`, `PS2_R198_PATH3DEFER_BASE`, `PS2_SHADOW_TAILFIX`, `PS2_S8_PKTDUMP`, `PS2_S8_PKTDUMP_MINVF` (not written), `PS2_SHADOW8_KTAP` (omit) |
| `gif_drain_thread.h/.cpp` | `GifDrainThread` — Start/Stop/Notify/DrainFence/DrainIdleGeneration/OnDrainThread; started ONLY when `MtvuSettings::enabled` (OLD `main.cpp:14228-14239` rule) | `gif_arbiter.h:470-557, :629-679` (loop body → `.cpp`) | `PS2_GIF_DRAIN_THREAD` |
| — | `packet_template.h/.cpp` DROPPED: `PS2_MID_TEMPLATE` default OFF → zero `TplTryServe` hits in every baseline | `gif_template.h` (116 ln) | `PS2_MID_TEMPLATE`, `PS2_MID_TEMPLATE_FORCEMISS` (not written) |
| — | `w10_eefeed.h` arms become unconditional code (B2 here, B1 in §2.7); `s8_ctxlatch.h` not written (its order rule IS the submission-order contract) | `w10_eefeed.h:50`; `s8_ctxlatch.h:112-114` | `PS2_SHADOW_CTXLATCH`, `PS2_SHADOW_ORDERFIX` (not written) |

### 2.2 `engine/vu` core — 11 rows

| New file | Class | OLD source file:lines | Flags applied |
|---|---|---|---|
| `vu1_state.h` | `Vu1State` — `VUregs regs` (NEW `cpu_state.h:74-146`, sizeof 608) + `alignas(16) micro[16384]` + `data[16384]` + per-run scratch (`suspended`, `xtop_*`, `xgkick_count`, `cur_bundle_pc`); `Vu0State` = 4 KB/4 KB (`mem_mask` 0xFFF vs 0x3FFF). POD, zero primary types | `vu.h:464-1167` (members), `memory.h:17-20`, `vu.cpp:718, :761-775` | — |
| `vu_unit.h/.cpp` | `VuUnit` — owns state + `FlagPipeline` + `StallModel` + `XgKick`; `WriteDataMem/WriteMicroMem` (bumps `micro_generation`)/`ReadDataMem` masked byte loops; `Reset`; `Execute` → `VuTierDispatch` | `vu.cpp:1598-1602, :1792-1798, :1804-1810, :12416` (`:1603-1791` probes not written) | `PS2_VU0_MODE`, `PS2_VU1_MODE`, `PS2_S13_*`/`PS2_S8_*` probes (not written) |
| `decode.h/.cpp` (+`.inl`) | `VuDecoder` — `VuDecodedSlot` (sizeof 60), `VUK_U_*/VUK_L_*`, `DecodeVuSlot`, `DecodeStallStatics`, `ClassifyFastKinds`, `*CanTouchHardwired`, `IsBranchKind`; the ONE decode (HACK 268) consumed by both tiers | `vu.h:61-200`; `vu_decode_slot.h:21, :219, :380, :537, :560, :630, :643` | — |
| `span_classify.h/.cpp` | `SpanClassifier` — `ComputeRunLenTouchHw / ComputeStallClasses / ComputeFoldClasses / ComputeSpanRem`, `kFmacWbLatencyShared=kIaluLwLatencyShared=4` | `vu_span_classify.h:34-195` | `PS2_VU_SUPERBLOCK`, `PS2_VU_STALLFOLD`, `PS2_VU_SPANFOLD`; `PS2_VU_SPANFOLD2`, `PS2_VU_RUNFLAT`, `PS2_VU_RUNFLAT_CENSUS` (not written) |
| `flag_pipeline.h/.cpp` | `FlagPipeline` — CLIP ring 8 (`kClipDelay=4`), MAC/STATUS ring 8 + lazy accumulators (`kMacDelay=4`), Q latch (`kDivDelay=kSqrtDelay=7, kRsqrtDelay=13`), P latch (13 EFU latencies); `Push*/Visible*/Flush*`, `ComputeMACFlags/UpdateMACFlags/UpdateStatusFlags`; lazy ring on NEON (`PS2R_SIMD`), eager pack on scalar, property-tested identical | `vu.h:625-630, :661-687, :731-737, :873-888`; `vu.cpp:797-829, :872-935, :950-1107, :1109-1161, :1228-1258, :1861-1934` | `PS2_VU_LAZYFLAGS`, `PS2_VU_RUNRETIRE` |
| `stall_model.h/.cpp` | `StallModel` — `FmacWb[8]` + `IaluWb[4]` rings, decode-time `ApplyFmacReadStalls` (Spec/Fast/Classed, `VUSM_*` classes), `FlushFmacWb/FlushIaluWb`; ONE implementation via the decoded slot (runtime twin `vu.cpp:1261-1584` and copy `:616-663` NOT written — HACK 269) | `vu.h:771-775, :814-816`; `vu.cpp:1198-1214`; `vu_fast_exec.h:337-352, :387-388, :393-639` | `PS2_VU_STALLSPEC` |
| `xgkick.h/.cpp` | `XgKick(GifArbiter&, bool paced_drain)` — Arm/Pace/Transfer/Flush/`PacketSize`; quantum 32, 1 QW per 2 cycles, second kick stalls `remaining_qw*2`, `0x4000` size cancel, `0x40000` runaway, `& 0x3FFF` wrap; `paced_drain=false` = full drain at the instruction (FACTS R5; harness certifies `true` from `TitleProfile.quirks.xgkick_defer`) | `vu.h:930-1000`; `vu.cpp:11359-11384, :11566-11732, :12233-12251` (snapshot arm `:12262-12389` not written) | `PS2_VU_XGKPACE_QUANTUM`, `PS2_VU_PACE_NEXTDUE`, `PS2_VU_RUNPACE`; `PS2_R198_XGKPROBE`, `PS2_R198_XGK_WINDOW` (not written) |
| `vu_float.h/.cpp` | `VuFloat` — `ClampBits1/4` (denormal flush — never `Ps2Float`), `MacFlags1/4`, `WriteMaskClamp`, `ClampStoreACC`, `CommitQStatus`, `FpMax/FpMin`, `ApplyMinMaxRaw`, `kLaneMask[16][4]`, `VuDiv`, `Ftoi0`, `Rsq`, `bits2f/f2bits` | `vu_fast_exec.h:38, :121-145, :158-330, :357-360, :1807-1821`; `vu.cpp:1814-1829`; `vu1_hle_programs.cpp:72-101, :1380-1393` | `PS2_VU_NOCLAMP`, `PS2_VU_CLAMPELIDE` (not written) |
| `chop_round_guard.h/.cpp` | `VuChopRoundGuard` — RAII `FE_TOWARDZERO`; the ONE copy (HACK 270; 6 library copies collapse) | `vu.cpp:54-69`; `vu1_hle_programs.cpp:552-3931` (6 pairs) | — |
| `vu_image_hash.h/.cpp` | `VuImageHash::Fast` — the 4-lane FNV verbatim (cache key AND every registry/catalog hash — FACTS §3.4 erratum) | `vu_hash_fnv.h:22-50` | `PS2_VU_FASTPACK` |
| `vu1_case.h/.cpp` | `Vu1Case` FROZEN POD (`VUC1`, v1, 33,456 bytes) + `ReadCase/WriteCase` free functions; §4.5 row `core/diag/vutrace_case` placed here (engine/core read-only; no diag dependency); move = HANDOFF row | `vu_trace_case.h:24-79` (`VuTraceForce :85-89` not written) | — (capture family → §2.8) |

### 2.3 `engine/vu/tiers` — 7 rows

| New file | Class | OLD source file:lines | Flags applied |
|---|---|---|---|
| `interp_tier.h/.cpp` | `VuInterpreterTier` — the reference: bundle loop (`cycle_limit` 1,000,000), I/E bits, upper/lower ordering (`discard_lower`, `hide_upper_from_lower`, `write_i_after`), VF0/VI0 re-pin, delay slot + `TPC=(pc+8)&0x3FFF`, E-bit drain of every pipe + `XgkickFlush`, XTOP yield (>1024 reads — KEEP with rule), VU0 self-spin suspend/resume, `RunUpper/RunSpecial2Upper/RunLower` | `vu.cpp:5203-5210, :5300-5301, :5990-7910, :8095, :8104-11314` (probe forest `:4334-4999, :5027-5195` not written) | `PS2_VU1_CYCLE_CAP` (→ `--cycle-cap`), `PS2_VUTRACE` (omit), `PS2_S15_*`/`PS2_S26_*`/`PS2_W15_KICKCENSUS` (not written) |
| `cached_tier.h/.cpp` | `VuCachedTier` — `VuCachedProgram` table (cap 64, evict min `exec_count`), hash-epoch skip, `DecodeProgram/AcquireCachedProgram/ExecuteCached/ExecuteCachedLoop/Resume`, `ExecUpperFastEl/ExecUpperFast/ExecLowerFast` bodies, `VuLinkMask()=0x7FF`; AOT fields not written | `vu.h:218-252`; `vu.cpp:2228-2695, :2696-3928`; `vu_fast_exec.h:111-117, :663-1806` | `PS2_VU_HASHEPOCH`, `PS2_VU1_LINK11`, `PS2_VU_O3` (CMake); VU-AOT family (not written) |
| `tier_dispatch.h/.cpp` | `VuTierDispatch` — the Execute ladder: HLE hook → MTVU EE-kick redirect (`Sync(VU1_EE_KICK)`, worker context) → `VuSettings::vu1_mode` → cached / interp; `VuSettings` struct (`vu1_mode=Cached`, semantics fixed in §3 per FACTS Q1) | `vu.cpp:387-407, :4318-4332, :4808-4816, :5009-5198` | `PS2_VU_MODE` (SETTING), `PS2_VU1_HLE` |
| `mtvu_worker.h/.cpp` | `MtvuWorker` — `Segment {Data, ResetLatch, Batch}` deque, `kBatchFlushBytes=32 KiB / kBatchFlushRecords=32`, batch record `[u32 len][u8 tte][pad3][bytes]`, 32 MB-stack pthread, `SubmitVif1/SubmitVif1Staged/FlushVif1Staged/SubmitResetLatch`, marker open/seal, owns the worker `Vu1State`; `MtvuSettings {enabled=true, depth_frames=2}` | `vu1_async.h:39-358`; `vu1_async.cpp:47-90, :464, :521-536, :1093-1100, :1250` (exec arm `:366-374, :764-853, :1037-1069, :1174-1234` and W16 hooks not written) | `PS2_MTVU` (Settings field), `PS2_EEFEED` (B1); `PS2_VU1_EXEC_ASYNC`, `PS2_VU1_EXEC_IDLE_SPIN_NS`, `PS2_VU1_EXEC_WAIT_SPIN_NS` (not written) |
| `mtvu_backpressure.h/.cpp` | `MtvuBackpressure` — `kPipelineHighBytes=256 MiB`, low-water `high-high/8`, frame depth via `RunaheadGuard`, 100 ms valve, spin `kSpinBudgetNs=40000` clamp [1000,1000000], `kRecentNs=200000`, `CpuRelax` | `vu1_async.cpp:186-194, :247-258, :608-641, :861-989` | `PS2_MTVU_MAX_MB`, `PS2_MTVU_SPIN`, `PS2_MTVU_SPIN_NS` |
| `mtvu_sync.h/.cpp` | `MtvuSync` — `SyncSite` enum (12 ids), `Sync(site)`, `NoteSiteSkipped`, eager drain (ctor-injected `GifArbiter&`), `TryParkDrain`, `TryVif1StatBusyView` (`0x03 | 1<<2 | 16<<24`), `DeferOn` = MTVU-on only path; callers (gs_hle sites) are integration | `vu1_async.h:176-195, :210-345`; `vu1_async.cpp:1366-1530, :1531-1549, :1586-1599, :1615-1687` | `PS2_MTVU_EAGER_DRAIN`, `PS2_MTVU_VBSYNC`, `PS2_MTVU_DEFER`; `PS2_MTVU_PATHSYNC_RELAX` (not written) |
| `runahead_guard.h/.cpp` | `RunaheadGuard` — `EffMaxFrames` (depth 2, dyn lift to 3 from an injected `low_band` predicate — `g_pace_low_band` header global becomes an input), `DepthFrames`, `Tripwire {kPersistPresents=30, kMaxReports=5}` | `runahead_guard.h:60-75, :104-131, :134-177` | `PS2_MTVU_MAX_FRAMES` (SETTING), `PS2_RUNAHEAD_ALARM`, `PS2_MTVU_DYNDEPTH` |

### 2.4 `engine/vu/hle` mechanism + `engine/vu/vu0` — 9 rows (+ placement of `gs_packet` / `gs_direct_ops`)

| New file | Class | OLD source file:lines | Flags applied |
|---|---|---|---|
| `hle/vu1_hle_contract.h` | types-only: `Input {micro,data,start_pc,top,tops,itop,itops,regs,image_hash}`, `Output {kicks, data_writes, has_regs, regs}`, `Fn`, `Entry {name,image_hash,variant_hash,start_pc,fn}`, `ImageInfo`; NO `kVariantBase` (games/) — §4.5 `contract.h` | `hle/vu1_hle.h:8-107` | — |
| `hle/vu1_family_signature.h` | `Vu1FamilySignature` POD `{variant_base, dispatcher_op0, exit_stubs[2], family_exit_tpc}` — definition here, the VALUES only in games/ | `hle/vu1_hle.h` (`kVariantBase`), `hle/vu1_hle.cpp:46-66`, `hle/vu1_hle_runtime.cpp:407-410` | — |
| `hle/vu1_registry.h/.cpp` | `Vu1Registry` — `Register/Entries/Lookup` in REGISTRATION ORDER: exact image → first variant match → wildcard, `start_pc` filter (`UINT32_MAX` = any); reports `entries=N distinct_variant_hashes=M` | `hle/vu1_hle.cpp:19-32` | — |
| `hle/vu1_image_analyzer.h/.cpp` | `Vu1ImageAnalyzer(const Vu1FamilySignature&)::Analyze` — image hash (`VuImageHash::Fast`), extent scan, prefix/variant hashes, dispatcher detect (`dispatcher_op0`), family prefix learned lazily per instance (OLD observable `prefix=family|other`), dispatch chain ≤64 steps to an exit stub | `hle/vu1_hle.cpp:46-90` | — |
| `hle/vu1_hle_runtime.h/.cpp` | `Vu1HleRuntime::TryExecute(Vu1State&, start, micro_gen, ext_gen)` — memo keyed `(micro_gen, extgen)`, `s_memo > 512 → clear`, `Lookup`, `Input` build from state + VIF1 regs, `fn`, DECLINE → false, apply `data_writes` (`& 0x3FFF`, clipped), kicks → `KickLowering`, exit `regs` or `TPC = family_exit_tpc`; explicit construction (no static installer) | `hle/vu1_hle_runtime.cpp:160-423` (`:430-437` not written) | `PS2_VU1_HLE`, `PS2_VU1_HLE_HASHEPOCH`, `PS2_VU1_HLE_DIRECT` + `PS2_GS_DRAWIR` (tiers deferred to Phase 6 — raw only), `PS2_VU1_HLE_DEBUG` (omit) |
| `hle/kick_lowering.h/.cpp` | `KickLowering` — raw PATH1 `submitOwned` per kick (OLD tier 3) + `virtual bool TryLower(const std::vector<uint8_t>&)` seam Phase 6 fills with DrawIR/DirectOps | `hle/vu1_hle_runtime.cpp` kick loop (tier 3 of `:160-423`) | `PS2_GS_DRAWIR_VERIFY` (Phase 6 ctest) |
| `hle/gif_packet_builder.h/.cpp` | `GifPacketBuilder` — `BeginTag/EndTag/QW/PRIM/RGBAQ/ST/UV/XYZ2/XYZF2/AD/Take` (generic GIF semantics; the library's emit path) | `hle/gs_packet.h:350-398` | — |
| `vu0/libvu0_sincos.h/.cpp` | `LibVu0SinCos` — VU0 pc 0xF0 fingerprint (3 instruction words) → `VF31 = {sin,cos,sin,cos}` clamped, `TPC=0x1C0`; SDK content keyed by code words, engine-side (FACTS Q4) | `vu.cpp:2570-2600, :5550-5594` | — |
| `vu0/vu0_registry.h/.cpp` | `Vu0Registry` — `Descriptor {vu_id, entry_pc, name, Kind, fingerprint}` mechanism; consumer = `PS2R_LOG(VU,…)` census only (zero behaviour) | `vu_function_registry.{h,cpp}` (214 ln) | — |

Placement this phase (engine/gs read-only, FACTS §3.4): `hle/gs_packet.h` splits — `Builder` → `gif_packet_builder`
above; `Decode/Normalize/Diff/Tolerance/DiffResult` → `tools/vu1_replay/gs_packet_diff` (§2.9). `hle/gs_direct_ops.h`
`PacketToOps` → `tools/vu1_replay/gs_direct_ops_check` (tool-side, for the `direct=` token — FACTS R7).
`hle/gs_draw_ir.h` → Phase 6, not written. FILEMAP's `engine/gs/hle/sce/gs_packet` home is the recorded deviation.

### 2.5 `games/<title>/vu1_library` — 13 rows (one file per code group; the 7 loop tables live in their family's file)

| New file | Class | OLD source file:lines (`hle/vu1_hle_programs.cpp`) | Flags applied |
|---|---|---|---|
| `vu1_library.h/.cpp` | `Vu1Library` — the ONE home of `Vu1FamilySignature{0x1720, 0x090E0003, {0x70,0x90}, 0xA0}`; `Register(Vu1Registry&)` replays the 140 registration sites in REGISTRATION ORDER (99 `s_reg_*` statics + 3 loops + 7 lambda tables); pins `entries=N distinct_variant_hashes=M` (expected 176 distinct) | `:610` (`s_reg_bbe`), tables `:4208, :4995, :5322, :6083, :6131, :6174, :6198` | `PS2_VU1_HLE_WHY` (not written) |
| `family_tristrip.cpp` | `FamilyPrograms` — `vs_family_tristrip` (the `fam` top-level group) | `:21-296` | — |
| `textured_tristrip.cpp` | `TtxPrograms` — `vs_textured_tristrip` | `:297-334, :679-2060` | — |
| `poscolor_strip.cpp` | `PcsPrograms` — `vs_poscolor_strip` | `:2146-2381` | — |
| `lit_tristrip.cpp` | `LitPrograms` — `vs_lit_tristrip` | `:2478-2908` | — |
| `skinned_lit_strip.cpp` | `SknPrograms` — `vs_skinned_lit_strip` | `:3016-3650` | — |
| `indexed_displace.cpp` | `IdxPrograms` — `vs_indexed_displace` (bw/cw) | `:3788-3989` | — |
| `zero_kick.cpp` | `ZkPrograms` — `vs_zero_kick(_a/_b/_c/_a_cont/_cont)`; the three OLD blocks merged; `s_reg_zk_all` (64 rows) + `s_reg_zk_cont` | `:4055-4203, :4310-4657, :4703-4988, :4208, :4995` | — |
| `sprite_text.cpp` | `SprPrograms` — `vs_sprite_text` (entries `start_pc` 0x1748 / 0x1980); `s_reg_spr_all` | `:5110-5315, :5322` | — |
| `crys_programs.cpp` | `CrysPrograms` — `vs_crys_dual`, `vs_crys_displace` (multi-entry), ring walk `kMeshRingBound=4096` data-driven, gate LIFTED; `s_reg_crys_all`, `s_reg_fog_all`, `s_reg_f20442_all`, `s_reg_cs9a_disp` | `:251-278, :3011-3014, :5386-6066, :6083, :6131, :6174, :6198` | `PS2_VU1_CRYS_RING`, `PS2_VU1_CRYS_GATE` |
| `library_helpers.h/.cpp` | `LibraryHelpers` — `QwPtr, V4, XformEmit, XformClip, GuardScale, CrossZSign, CrossXYZ, ClipNf, LatestOfClass` (~40 game-convention helpers; generic float helpers went to `vu_float`) | `:102-244, :739` (`WhyNote/WhyNotef :212-244` not written) | — |
| `clip_gate.h/.cpp` | `ClipGate` — the clip pre-scan shared by fam/lit/crys | (FACTS §3.5 recommendation; extracted from the three families' shared bodies) | — |
| `vu0_programs.cpp` | `Vu0Programs` data — `kVuFunctions[]` (15 rows incl. `{0, 0x00f0, "Sony libsin/cos polynomial", …}`), `kVuSubroutines[]`; §4.5 row `games/<t>/vu0_programs.cpp` placed under `vu1_library/` (the only OWNED `games/` dir) | `vu_function_registry.cpp` table | — |

Each program file is a free-function TU in a namespace (zero primary types) or a `class <X>Programs` — both lint-clean.
`README.md` (owned) is corrected from "137 entries" to the pinned N/M. CMake: `ps2r_title_vu1_library` links `ps2r_vu`
only; `-ffp-contract` pinned per FACTS R8 (OLD sets no explicit flag; keep the setting that passes the 126-case gate).

### 2.6 `engine/vif` — 5 rows (+1 not written)

| New file | Class | OLD source file:lines | Flags applied |
|---|---|---|---|
| `vif_regs.h` | types-only: `VifRegs` POD (`stat…col[4]`, `GetCL/GetWL`), `VifCommand` enum (NOP…DIRECTHL, UNPACK 0x60-0x7F), `VifStat/VifErr/VifFbrst` constexpr bits, `MaskAction`, `VifState` | `vif.h:106-218` | — |
| `vif.h/.cpp` | `Vif : MmioHandler` — both units; `Reset` (`cycle=0x0101`), `ProcessData` (fast span → latch decline), `WriteToLatch/WriteToLatch2/ProcessFifo` word path, `ExecuteCommand` (STCYCL…MSCAL/MSCALF/MSCNT with TOPS/DBF flip → `VuUnit::Execute`, MPG → `WriteMicroMem`, DIRECT/DIRECTHL → PATH2 `submit`, IRQ→INT unless MII), `Read/Write` (`offset & 0xFF`; ROW/COL read 0 — FACTS R11), `UpdateStat`; ctor-injected `VuUnit& vu0, vu1`, `GifArbiter&`, `GifRegs&` (path3 mask); the MTVU seam = register-file selection + `tte_feed[]` + `ResetLatch` (so `vif_mtvu_seam` is NOT a file) | `vif.h:223-347`; `vif.cpp:39-42, :703-830, :831-966, :1229-1905, :1906-2549, :3504-3609` | `PS2_VIF_FAST`; `PS2_SHADOW8_KTAP`, `PS2_VIF_ALIGN_LOG` (omit); `PS2_S8_VIFTAP`, `PS2_VIF_WTRACE` (not written) |
| `vif_unpack.h/.cpp` | `VifUnpack` — `UNPACK_ELEMS/BITS/DEST_ELEMS`, `GetUnpackSize` (num 0→256, CL<WL rule, round to 4), scalar loop (CL/WL skip, V4-5, S replicate, V2 `z=x,w=y`, V3 `w` peek + phase rule, mode row/col, mask WRITE/NOWRITE/ROW/COL, `dest=(addr_qw*16)&0x3FFF`); `VifUnpackSettings {bulk_lanes=true}` (test knob replacing `OverrideForTest`) | `vif.cpp:168-231, :769-775, :2680-3503` (dead `vp_row :2942-2990`, `mipsrc/midc/KICKCNT` not written) | `PS2_UNPKT_CENSUS` (omit) |
| `vif_fast_span.h/.cpp` | `VifFastSpan` — `ProcessDataFast`: decline set (`tte_feed`, `size&15`, fifo/latch non-empty, `word_index<4`, STALL), DIRECT continuation/partial→stream, `bytes_needed` per command, zero-payload immediate, partial → WAIT_DATA, full → payload + one look-ahead word | `vif.cpp:967-1228` | `PS2_VIF_FAST` |
| `vif_unpack_bulk.h/.cpp` | `VifUnpackBulk` — `UnpackBulk` (V3-32/V4-32/V4-16/V4-8, `cl==wl`, no mask, mode 0) + `UnpackBulkSkip` (`wl==1 && cl>1`, V3-32/V4-32/V4-8); NEON under `PS2R_SIMD`, scalar twin always compiled; the `!g_logFile.is_open()` term dropped | `vif.cpp:309-311, :420-575, :576-702, :2825-2828, :2856-2858` | `PS2_VIF_UNPACK_SIMD`, `PS2_VIF_UNPKBX` |
| — | `vif_mtvu_seam.h/.cpp` NOT written: `W13E3_WaitPoint` / `unpack_shadow` / `CommitStagedUnpacks` exist only under the exec-thread arm (DELETE); HACK 309 moot | `vif.cpp:58-62, :71-90, :2783-2785` | `PS2_VU1_EXEC_ASYNC` (not written) |

### 2.7 `engine/dmac` — 5 rows (+ hle NOT installed)

| New file | Class | OLD source file:lines | Flags applied |
|---|---|---|---|
| `dmac_channel.h` | types-only: `DmacChannelRegs` POD (28 B, savestate layout), `DmacChannelId` (VIF0…SPR1, COUNT=10), `kChcr*`/`DstatCis/Cim` constexpr | `dmac.h:16-56` | — |
| `dmac.h/.cpp` | `Dmac : MmioHandler` — register file (`channels[10]`, ctrl/stat/pcr/sqwc/rbsr/rbor/stadr/enabler/enablew), `Read` page map (`0x8000…0xD400`, globals `0xE000-0xE060`, `0xF520`), VIF1 CHCR-read flush + suspended retry, `Write` (PCR CPC flush; non-channel globals DROPPED as OLD; CHCR STR → `StartChannel`), `StartChannel` per channel (VIF1 chain deferred; GIF flushes VIF1 first; SPR = stub, CompleteChannel only — FACTS R2; SIF/IPU = injected callbacks), `CompleteChannel/CheckInterrupt/DispatchInterrupt` (mask never set — FACTS R3); `SectionSpec` factory for `TAG_DMAC` (not registered) | `dmac.h:50-170`; `dmac.cpp:1045-1253, :1254-1578, :2851-2858, :2927-2999` | `PS2_BAKE_DIAG`, `PS2_SHADOW8_KTAP` (omit); `PS2_S8_ARENA_MINVF`, `PS2_S26_DECAL` (not written) |
| `chain_walker.h/.cpp` | `ChainWalker` — tag decode (qwc/pce/id/irq/addr/spr), all-ones terminator, `kMaxTags=10000`, `fromSpr` tag/data source `0x70000000|(x&0x3FFF)`, CHCR.TAG mirror, ids REFE/CNT/NEXT/REF/REFS/CALL(ASR push)/RET/END, TTE tag QW feed, VIF normal/chain (→ `Vif::ProcessData` or `MtvuWorker::SubmitVif1Staged`), GIF chain accumulated into ONE `submitOwned(PATH3)`, GIF normal `TransferToGIF`, VIF1 END → `Vif1TailRecord` | `dmac.cpp:495-1044, :1592-1650, :1664-2707` (`:2735-2760` not written) | `PS2_EEFEED` (B1 staged submit) |
| `deferred_vif1.h/.cpp` | `DeferredVif1Kick` — kick-time regs park, `FlushDeferredVif1(reason)` (+ ≤5 resume retries, arbiter drain), `ChaseVif1Tail` (re-read END slots, resume sane tags), `SuspendedVifChain {kMaxRetries=120}` + `ResumeSuspendedDma/HasSuspendedDma`; flush points inside `Dmac`; SyncV/WaitForVblank callers = integration | `dmac.h` (`SuspendedVifChain/Vif1TailRecord/DeferredVif1Kick`); `dmac.cpp:1383-1398, :2708-2781, :2782-2850, :2870-2926` | — |
| `dmac_handlers.h/.cpp` | `DmacHandlerRegistry` — `DmacHandler {id, handler_pc, gp, arg, flag, active}` flat table replacing `g_dmac_queues`; dispatcher callback injected (`SyscallTable` hand-off = integration) | `dmac.h:7-14, :172-178`; `dmac.cpp:2934-2999` | — |
| — | `dmac/hle/libdma_bindings.cpp` NOT installed: libdma rows already resolve via `SdkBindingManifest` / `sce_dma_glue.cpp` under read-only `games/…/hle` | `sce_hle/libdma.cpp` | — |

### 2.8 `core/diag` rows — DEFERRED (engine/core read-only; DIAG outcome)

| §4.5 row | Disposition | OLD source | Flags |
|---|---|---|---|
| `core/diag/vutrace_case.h/.cpp` | POD + READER written as `engine/vu/vu1_case` (§2.2; tools need it). WRITER (`MaybeCapture`, config struct, LOCKED trace-line format) deferred: DIAG + non-owned dir | `vu.cpp:93-98, :116-348` (`:118-130, :252-346`) | `PS2_VUTRACE`, `_CAPTURE`, `_DIR`, `_FILE`, `_IMG_GAP`, `_MAX`, `_MINFRAME`, `_NEWIMG`, `_PC`, `_PER_IMAGE` (deferred) |
| `core/diag/vutrace_collector.h/.cpp` | deferred to Phase 9 (triage producer) | `vu.cpp` vutrace half | — |
| other DIAG (omit, no mechanism) | `PS2_SHADOW8_KTAP` (16 sites), `PS2_UNPKT_CENSUS`, `PS2_VIF_ALIGN_LOG`, `PS2_VU1_HLE_DEBUG`, `PS2_BAKE_DIAG`, `PS2_MIPCEN` — `[KICKCNT]`/`[VU1-HLE-RT]` lines become `PS2R_LOG` or vanish | FACTS §5.3 | 6 names |

### 2.9 `tools/{vu1_replay,vif_replay}` + `tests/{gif,vu,vif,dmac}` — 8 rows

| New file | Class / content | OLD source file:lines | Flags applied |
|---|---|---|---|
| `tools/vu1_replay/vu1_replay.cpp` | `main` — `<case>`, `--selftest`, `--mode interp\|cached`, `--compare [--compare-cycle]`, `--bench N`, `--dump-kicks`, `--hle [--hle-ref] [--skip-unregistered] [--exact] [--tol-*] [--strict-packets] [--max-lines]`, `--catalog`, `--cycle-cap N`; exit 0/1/2/77; the `COMPARE`/`HLE`/`CATALOG` grammars verbatim (FACTS §7.2); prints NO `[VU-MODE]` line; links `ps2r_vu` + `ps2r_gif` + `ps2r_title_vu1_library` only | `tests/vu1_replay.cpp:130-182, :400-473, :554, :575-586, :638-730, :806-814, :935, :1094-1099, :1166` | `PS2_VU1_CYCLE_CAP` → `--cycle-cap` |
| `tools/vu1_replay/xgk_capture_sink.h/.cpp` | `XgkCaptureSink` — captures PATH1 kicks from the arbiter (replaces the OLD global shims) | `tests/vu1_test_shims.cpp:33-40`, `tests/vu1_xgk_capture.h` | — |
| `tools/vu1_replay/gs_packet_diff.h/.cpp` | `GsPacketDiff` — `Write/TagInfo/Stream/DecodePacket/Decode` (`q_latch=0x3F800000` per packet), `Tolerance {xy=z=rgba=fog=uv=1, st_rel=1e-5f, …}`, `DiffResult`, `Normalize`, `Diff` | `hle/gs_packet.h:28-130, :174-245, :406-507, :509` | — |
| `tools/vu1_replay/gs_direct_ops_check.h/.cpp` | `GsDirectOpsCheck` — `PacketToOps` port for the `direct=ok\|fallback\|MISMATCH\|n/a` token | `hle/gs_direct_ops.h` (151 ln) | `PS2_VU1_HLE_DIRECT` (tool-side only) |
| `tools/vif_replay/vif_replay.cpp` | `main` — `--mtvu N [--vif-fast]` (`BuildChains`, `RunStructuralE3`, STAT poll, eager drain, boundaries), `--unpack-bulk [--vif-fast]` (30 `b_*`/`bx_*` cases, lane off vs on via `VifUnpackSettings`), `--runahead --depth 2`, `--runahead-off --depth 0`; grammars per FACTS §7.3 | `tests/vif_replay.cpp:543-620, :1322-1470` (+ `ring_log_stub.cpp`) | `PS2_MTVU_MAX_FRAMES` → `--depth`; `VIF_REPLAY_DUMPW/LOG/MTVU_DEBUG/RGBDUMP/TTE2` → options or dropped; `PS2_VU1_EXEC_ASYNC` arms dropped |
| `tests/gif/` | `test_gif_parser_equivalence` (generic per-QW vs bulk lanes → identical `RegisterOpStream`), `test_gif_drain_thread` (+ TSan twin), `test_defer_boundary_order`, `test_gif_regs` (STAT composition, new), `counting_gs_sink.h/.cpp` (`CountingGsSink`) | `tests/test_gif_fast_equivalence.cpp` (662), `test_gif_drain_thread.cpp` (285), `test_defer_boundary_order.cpp` (403) | — |
| `tests/vu/` | `test_cached_superblock` (winner arm only), `test_lazyflags_algebra`, `test_vu0_shadow_replay`, `test_vu0_vis_micro`, `test_vu0_decal_eval` (fixtures `.inc` → `tests/vu/fixtures/*.bin`), `test_vu1_transform_gt`, `test_runahead_guard`, `test_gs_packet`; corpus rows `Vu1ReplaySelfTest`, `Vu1CachedCompare_<case>` ×126, `Vu1Hle_<case>` ×126 (SKIP 77), `Vu1HleRef_<first 8>`, `Vu1HleCatalog`, `Vu1CollectSweep` (3,293, long) — SKIP when `PS2R_OG_ROOT` absent | `tests/test_vu_superblock.cpp` (1,579), `test_lazyflags_algebra.cpp` (490), `test_vu0_shadow_replay.cpp` (347) + `vu0_shadow_image.inc`, `test_vu0_vis_micro.cpp` (259), `test_vu0_decal_eval.cpp` (124), `test_vu1_transform_gt.cpp` (175), `test_runahead_guard.cpp` (152), `test_gs_packet.cpp` (163); `test_vu_spanfold2.cpp` NOT ported | env twins (not written) |
| `tests/vif/`, `tests/dmac/` | vif: the 6 arms as ctest rows + `test_vif_unpack_scalar` (new: `GetUnpackSize`, V3 phase, mask table); dmac (new — OLD has none): `test_dmac_regs` (page map, drop rule, CHCR-read flush), `test_chain_walker` (8 ids, TTE, all-ones, `kMaxTags`), `test_deferred_vif1` (flush points, tail chase, SPR stub) | `tests/CMakeLists.txt:1408-1444` (arm table) | — |

### 2.10 Row count + CONSTRUCTION §4.5 reconciliation

| Group | Rows | | §4.5 row (`:538-551`) | Disposition |
|---|---:|---|---|---|
| 2.1 engine/gif | 7 (+2 dropped/folded) | | 1 dmac (+ `hle/libdma_bindings`) | §2.7; bindings NOT installed |
| 2.2 engine/vu core | 11 | | 2 gif (`gif_unit`, `packet_template`, `boundary_slot`) | §2.1; unit→`GifRegs`, template DROPPED, slot = field |
| 2.3 engine/vu/tiers | 7 | | 3 vif (`vif_mtvu_seam`) | §2.6; seam NOT written (exec-async only) |
| 2.4 engine/vu/hle + vu0 | 9 | | 4 vu core | §2.2 (+ `chop_round_guard`, `vu_image_hash`, `vu1_case`) |
| 2.5 games vu1_library | 13 | | 5 vu/tiers | §2.3 (7/7) |
| 2.6 engine/vif | 5 (+1 not written) | | 6 vu/hle (`contract`, `runtime` tiers) | §2.4; DIRECT/DrawIR tiers → Phase 6 seam |
| 2.7 engine/dmac | 5 (+1 not installed) | | 7 vu1_library (`ttx.cpp`… + "137 entries") | §2.5 (renamed per program; counts pinned 140/176) |
| 2.8 core/diag | 0 (deferred) | | 8 `games/<t>/vu0_programs.cpp` | §2.5 under `vu1_library/` |
| 2.9 tools + tests | 8 | | 9 `core/diag/vutrace_case` + `collector` | §2.8 DEFERRED (reader → `engine/vu/vu1_case`) |
| **Total** | **65 (+4 explicit not-written)** | | 10 `gs/hle/ir/register_op_stream` | §2.1 under `engine/gif/` (move = HANDOFF) |
| | | | 11 tests + tools | §2.9 (AOT tests and the 2 exec-on arms not ported) |

## 3. Interfaces (the seams every stage codes against)

Sources: FACTS §4, §3.7, §3.2.2, §5.2; ARCHITECTURE §14.2 (`:1252-1283`). Declarations only; bodies land in the owning stage (§2).

### 3.1 `engine/gif/gs_sink.h` — `GsSink`, what the GIF hands to the GS in stream order

One pure virtual per OLD `g_gs_state` mutation site (FACTS §4.1: `gif.cpp:3216-3237, :3641, :4710-4746, :5002-5008, :5083,
:5160-5172` + `SetPrim`/`KickVertex`/the 30 `Set*`+`WriteHWReg` setters/`FlushBatch`). Verbatim from FACTS §4.2:

```cpp
// engine/gif/gs_sink.h -- what the GIF hands to the GS, in stream order. One call per OLD g_gs_state
// mutation so a recording sink reproduces the OLD write sequence byte-for-byte.
class GsSink {
public:
    virtual ~GsSink() = default;
    virtual void BeginTag(const GifTag& tag, GifPath path) = 0;          // OLD: q = 1.0f; (+ PRE → WriteRegister(PRIM))
    virtual void WriteRegister(uint8_t reg, uint64_t value) = 0;         // OLD: ProcessReglist(reg, &value) for every non-vertex reg
                                                                          //      (PRIM, RGBAQ(64), ST(64), UV(64), FOG(64), TEX0.., SIGNAL, FINISH, LABEL, TEXCLUT-dropped ...)
    virtual void PackedRgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;   // OLD GifPackedRGBAQ (PACKED form does NOT touch q)
    virtual void PackedSt(float s, float t, float q) = 0;                // OLD GifPackedST (q applied only when != 0.0f; uv_written_since_st=false)
    virtual void PackedUv(uint16_t u, uint16_t v) = 0;                   // OLD GifPackedUV (uv_written_since_st=true)
    virtual void PackedFog(uint8_t f) = 0;                               // OLD GifPackedFOG
    virtual void KickVertex(float x, float y, float z, uint8_t fog, bool draw) = 0;   // OLD KickVertex/KickVertexImpl
    virtual void ImageData(uint64_t lo, uint64_t hi) = 0;                // OLD ProcessImage per QW: (activate transfer) + WriteHWReg(lo), WriteHWReg(hi)
    virtual void EndOfPacket() = 0;                                      // OLD FlushBatch() at EOP (nloop==0&&eop, or last loop && eop)
    virtual void FinishPath3() = 0;                                      // OLD FinishDMA() after a drain pass that delivered PATH3
};
```

`GifTag` and `enum class GifPath : uint8_t { Path1 = 1, Path2 = 2, Path3 = 3 }` come from `engine/gif/gif_tag.h` (§2.1). The
four `Packed*` methods stay separate: PACKED RGBAQ never writes Q and PACKED ST writes Q only when non-zero — no lossless
`(reg,u64)` re-encoding exists (FACTS §4.2). SIGNAL/FINISH/LABEL arrive as `WriteRegister(0x60|0x61|0x62, v)`; the CSR/INTC
side effect is Phase 6's. `GifParser` holds a `GsSink&` and nothing else from the GS side.

### 3.2 `engine/gif/register_op_stream.h` — the recording sink (`gsops.ohash`)

Verbatim from FACTS §4.3 (placed under OWNED `engine/gif/`; CONSTRUCTION names `gs/hle/ir/register_op_stream` — HANDOFF move):

```cpp
struct RegisterOp { uint8_t kind; uint8_t reg; uint8_t path; uint8_t pad; uint32_t aux; uint64_t lo; uint64_t hi; };   // 24 bytes, POD
//   kind ∈ { Tag, Reg, Rgbaq, St, Uv, Fog, Kick, Image, Eop, Finish }; Kick packs x,y as raw float bits in lo, z bits | fog<<32 | draw<<40 in hi
class RegisterOpStream : public GsSink {  // records every call in order, keeps a running ps2r::hash::Fnv1a64 over the POD records
public:
    uint64_t Hash() const;            // Fnv1a64 chained across records (seed = previous hash) -- the per-frame gsops.ohash value
    size_t Count() const; void Clear(); const std::vector<RegisterOp>& Ops() const;
};
```

- Hash contract: `ps2r::hash::Fnv1a64(&op, sizeof(RegisterOp), seed)` (`engine/core/hash/hash.h`) per record, seed chained
  from `kFnv1a64Offset`; floats hashed as raw IEEE bits, never re-formatted; `pad` always 0.
- Callers (all through `GifArbiter::drain()` → `GifParser::ReceiveData(path, data, size)` in SUBMISSION ORDER — §2.1):
  PATH1 = `XgKick` → `submitOwned(Path1, …)` (under `--hle` the same bytes come from `KickLowering`'s raw tier); PATH2 =
  `Vif` DIRECT/DIRECTHL → `submit(Path2, …, directhl)`; PATH3 = `ChainWalker` (one `submitOwned(Path3, full_payload)` per
  chain, `TransferToGIF` for normal mode); after a pass that delivered PATH3 the parser calls `FinishPath3()`.
- Phase 6 implements `class GsState : public GsSink`; its G-C1 proof ("DrawIR-apply ≡ register-op replay") replays
  `RegisterOpStream::Ops()` into a fresh `GsState`. `CountingGsSink` (`tests/gif/`) is test-only. `vu1_replay --hle` does
  NOT use this sink: it diffs kick BYTES through `GsPacketDiff` (§4.1), as OLD does.
- Layering (review-enforced): `engine/gif/**` and `engine/vu/**` include NO `engine/gs/` header other than `engine/gs/format/*.h`;
  `ps2r_gif`/`ps2r_vu` link `ps2r_core` (+`ps2r_ee` for `MmioHandler`/`VUregs`), never `ps2r_gs_*` beyond `ps2r_gs_format`.

### 3.3 Tier ladder — `engine/vu/tiers/tier_dispatch.h` (`VuTierDispatch`)

OLD `VU1::Execute` entry order (FACTS §3.2.2 `vu.cpp:4329-4332, :4808-4816, :5009-5198`) is the contract, written once:

1. `hle_enabled && vu_id == 1 && library present` → `Vu1HleRuntime::TryExecute(state, start_pc, micro_gen, ext_gen)`; `true` =
   ran (kicks submitted, `data_writes` applied, regs or `TPC = family_exit_tpc`) → return; a MISS or DECLINE returns `false`
   and falls through with the state untouched (OLD `:4329-4332`).
2. MTVU EE-kick redirect: worker running, not on the worker → `MtvuSync::Sync(SyncSite::Vu1EeKick)`, then execute on the
   worker's `Vu1State` (ONE VU1 register file; OLD `:4808-4816`). Absent under the deterministic profile (MTVU off).
3. `VuSettings::vu1_mode == Cached` → `VuCachedTier::Execute` (cache keyed by `VuImageHash::Fast`, generation skip); `Interp` →
   `VuInterpreterTier::Execute` (the instrumented reference: every pipe cycle-exact, `cycle_limit` 1,000,000 / `--cycle-cap`, the
   vutrace hook point, the `bundles` counter `--catalog` prints). Both tiers consume the ONE `VuDecoder`/`StallModel`/
   `FlagPipeline`/`XgKick` (§2.2); identity is proven by `--compare` (§4.1/§4.5), never assumed.

`VuSettings` (FACTS Q1 fixed here): `enum class VuTierMode : uint8_t { Cached, Interp }` is the FALLBACK tier; `vu1_mode = Cached`
mirrors `engine.vu1_mode` (`config.h:56-59`); Config's `Hle` maps to `Cached` at integration (OLD has no HLE-only tier — a DECLINE
always needs a fallback). `hle_enabled = true` is the `PS2_VU1_HLE` winner; the tool sets it `false` for `--mode`/`--compare`, `true`
with the library for `--hle`/`--catalog`; `--vu1-library none` ([GD1]) is app/host's. VU0 never enters steps 1/2 (`LibVu0SinCos` at `pc == 0xF0`).

### 3.4 `Vu1FamilySignature` / `Vu1LibraryEntry` / `Vu1ProgramLibrary` — ARCHITECTURE §14.2 verbatim

```cpp
struct Vu1FamilySignature {          // the five constants A6 §2.2 extracts from vu1_hle.{h,cpp}
  uint32_t variant_base;             // 0x1720 — first byte of the per-material variable region
  uint32_t dispatcher_op0;           // 0x090E0003 — ILW.x vi14,3(vi00), the family dispatcher's first op
  uint16_t exit_stubs[2];            // 0x70, 0x90 — the dispatch chain's E-bit terminators
  uint32_t family_exit_tpc;          // 0xA0 — the round-1 TPC exit contract
};
struct Vu1LibraryEntry {             // per-game entry the engine dispatcher looks up by hash
  uint64_t     image_hash;           // exact-image tier (0 = variant-keyed, as all 137 current entries are)
  uint64_t     variant_hash;         // MPG-patched per-material family tier — THE key for this title
  const char*  name;                 // "vs_family_tristrip" … (also the RendererProfile stamp vocabulary)
  uint16_t     start_pc;             // 0 dispatcher, 0x1748 sprite direct, …
  Vu1Outcome (*fn)(const VuRunInput&, VuRunOutput&);   // native program; may DECLINE → fallback tier
  std::span<const uint32_t> oracle_case_ids;   // vutrace corpus cases proving THIS entry (FAIL=0 gate)
};
struct Vu1ProgramLibrary {
  Vu1FamilySignature family;                   // drives Analyze()'s chain walk + prefix check
  std::span<const Vu1LibraryEntry> entries;    // 137 entries / 120 hashes for SLUS-12345
};
```

Binding to §2: `Vu1FamilySignature` is DEFINED in `engine/vu/hle/vu1_family_signature.h`, INSTANTIATED once in
`games/…/vu1_library/vu1_library.cpp` (`{0x1720, 0x090E0003, {0x70, 0x90}, 0xA0}`). `Vu1LibraryEntry`/`Vu1ProgramLibrary` live
in `vu1_library.h` (games/): `VuRunInput`/`VuRunOutput` = the contract's `Input`/`Output` (OLD `vu1_hle.h:8-107`), `Vu1Outcome` =
the OLD `bool` (`false` = DECLINE), `start_pc` widens to `uint32_t` in the registry `Entry` (`UINT32_MAX` = any), `oracle_case_ids`
empty this phase (HANDOFF). `Register(Vu1Registry&)` walks `entries` in REGISTRATION ORDER. "137 / 120" is STALE; pinned = 140 / 176 (§1.2).

### 3.5 MTVU seam + the `<Module>Settings` structs

The seam is what the worker needs from `Vif` (FACTS §3.6: `vif_mtvu_seam` is NOT a file — HACK 309 moot): `Vu1State& CtxFor(int
vif)` (OLD `Vif_VuCtx`, `vif.cpp:39-42`), `void SetTteFeed(int vif, bool on)` (`tte_feed[]`), `void ResetLatch(int vif)` (OLD
`vu1_async.cpp:517-519`), `void ProcessData(int vif, const uint8_t*, size_t)`. Worker side (OLD `vu1_async.h:39-358`; exec/W16 slots not written):

```cpp
// engine/vu/tiers/mtvu_worker.h -- ONE producer (EE thread), ONE consumer (the worker); segments replay in submission order.
class MtvuWorker {
public:
    MtvuWorker(Vif& vif, GifArbiter& arbiter, Vu1State& worker_state, const MtvuSettings& settings);
    void Start(); void Stop(); bool IsRunning() const; bool OnWorkerThread() const;             // OLD :44-79
    void SubmitVif1(const uint8_t* data, size_t size, bool tte = false);                          // OLD :108  (Segment::Op::Data)
    void SubmitVif1Staged(const uint8_t* data, size_t size, bool tte = false);                    // OLD :122  (Batch record [u32 len][u8 tte][pad3][bytes])
    void FlushVif1Staged();                                                                       // OLD :123  (kBatchFlushBytes=32 KiB / kBatchFlushRecords=32)
    void SubmitResetLatch();                                                                      // OLD :132  (Segment::Op::ResetLatch)
    uint64_t BatchSegmentsProcessed() const;  bool IsBusy() const;                                // OLD :127, :340
    std::shared_ptr<GifMtvuMarker> ActiveMarker() const;  Vu1State& Vu1Context();                 // OLD :345, :356
};
```

`MtvuSync` (`mtvu_sync.h`): `enum class SyncSite : uint8_t { Vif1RegRead = 0, Vif1RegWrite, Vu1Mem, SceGsSyncV, SceGsSyncPath,
SceGsResetPath, SceDmaSync, SceGifSyncDma, WaitVblank, Vu1EeKick, Other, PrivPush, Count }` (OLD `:176-195`) + `Sync(site = Other)`,
`NoteSiteSkipped`, `TryVif1StatBusyView(uint32_t*)`, `TryParkDrain`, `DeferOn` (OLD `:210-318`); callers are integration.

Settings (module-local, PEAK defaults, mirroring EXISTING Config keys — none added, FACTS §5.2; integration copies `Config` → these):

| Struct (header) | Field | Peak default | Mirrors | OLD site |
|---|---|---|---|---|
| `MtvuSettings` (`mtvu_worker.h`) | `bool enabled` | `true` (deterministic profile: `false`) | `engine.mtvu` | `vu1_async.cpp:1011` (`PS2_MTVU`) |
| `MtvuSettings` | `int depth_frames` | `2` (0 = no park; dyn lift to 3 from `low_band`) | `engine.mtvu_depth` | `runahead_guard.h:66` (`PS2_MTVU_MAX_FRAMES`) |
| `VuSettings` (`tier_dispatch.h`) | `VuTierMode vu1_mode` | `Cached` | `engine.vu1_mode` (`Hle` → `Cached`) | `vu.cpp:373` (`PS2_VU_MODE`) |
| `VuSettings` | `bool hle_enabled` | `true` | — (NORM `PS2_VU1_HLE`; tool/boot knob only) | `vu1_hle_runtime.cpp:61` |
| `VifUnpackSettings` (`vif_unpack.h`) | `bool bulk_lanes` | `true` | — (test knob replacing `OverrideForTest`) | `vif.h:40/:91` |

`GifDrainThread` has no struct: it starts iff `MtvuSettings::enabled` (OLD `main.cpp:14228-14239`). `XgKick`'s `paced_drain` is a ctor bool from `TitleProfile.quirks.xgkick_defer` (§2.2).

## 4. Oracle and test strategy

Principle (STAGE_RULES): the proof is the OLD test binaries (`OLD/build/tests/{vu1_replay,vif_replay}` only, cwd under
`/tmp/p5/`, via `verify/run_bounded.sh`) run bounded on the SAME inputs + the ported unit tests, never resemblance.

### 4.1 `tools/vu1_replay` — the corpus oracle (OLD `tests/vu1_replay.cpp`, 1,201 ln)

OLD usage, verbatim (`/tmp/p5/5P/vu1_replay_noargs.log`; NEW prints the same text plus one `--cycle-cap N` line):

```
usage: vu1_replay <case.bin> [trace_out] [postdata_out]
       vu1_replay --selftest
       vu1_replay --mode interp|cached <case.bin> [postdata_out]
       vu1_replay --compare [--expect-aot|--expect-noaot] <case.bin | --fabricate>
       vu1_replay --bench N <case.bin>
       vu1_replay --dump-kicks <case.bin> [out.txt]
       vu1_replay --hle [--hle-ref] [--skip-unregistered] [--exact]
                  [--tol-xy N] [--tol-z N] [--tol-rgba N] [--tol-st F]
                  [--strict-packets] [--max-lines N] <case.bin>
       vu1_replay --catalog <dir | case.bin ...>
```

Also parsed by OLD (`:1094-1099, :1166`): `--compare-cycle` (adds `vu_cycle` + `xgk_staging_inserts`), `--bench-cached`. Exit
codes: 0 pass, 1 fail, 2 usage/load error (`vu1_replay: cannot load case %s (bad path, magic, or version)`), 77 = ctest SKIP
(`kSkipExit`, `:554`). NEW never prints `[VU-MODE] active mode: …` (an OLD announce, `vumode::AnnounceOnce`); the driver skips it.

- Input: `Vu1Case` (`engine/vu/vu1_case.h`, FROZEN: `kMagic=0x31435556 "VUC1"`, `kVersion=1`; fields in order `magic, version,
  start_pc, reserved0, VUregs regs (608), micro_mem[16384], data_mem[16384], vif1_top, vif1_tops, vif1_itop, vif1_itops, u8
  q_pending_valid, u8 p_pending_valid, reserved1[6], u64 q_pending_retire, u32 q_pending_value_bits, u32 q_pending_status,
  u64 p_pending_retire, u32 p_pending_value_bits, u32 reserved2, u64 vu_cycle`; `sizeof == 33456`, FACTS §3.2.9). `InstallCase`
  (OLD `:130-154`) loads all of it into a fresh `VuUnit`; PATH1 kicks are captured by `XgkCaptureSink` (§2.9).
- `--compare` = `Execute(start_pc)` twice from the same case, `Interp` then `Cached` (HLE off); full 16 KB post-run data memory
  + whole `VUregs` compared. Grammar (`:464-473`): `"COMPARE %s: pc=0x%04x data_diff_bytes=%zu/%u regs_%s aot_dispatches=%llu%s
  (interp vs cached)\n"` — `regs_identical|regs_DIFFER`, `aot_dispatches=0` literal in NEW, suffix ` EXPECTED-AOT-BUT-DID-NOT-
  DISPATCH` / ` EXPECTED-FALLBACK-BUT-DISPATCHED`; on DIFFER first the per-register lines `  VF%02d: interp=%08x,… cached=…`,
  `  VI%02d: …`, `  ACC differs`, `  Q:`, `  P:`, `  I:`, `  R:`, `  status_flag:`, `  mac_flag:`, `  clip_flag:`, `  TPC:`
  (`:400-440`). OLD over 126 cases: `TOTAL cases=126 pass=126 fail=0`. `--mode interp|cached <case> [postdata_out]` writes the
  post-run image OLD writes (layout read from `tests/vu1_replay.cpp` at 5C, reproduced byte-for-byte) — the driver `cmp`s OLD
  vs NEW dumps per tier, so identity is bit-exact per tier, not PASS-vs-PASS.
- `--hle` = library entry (`Lookup` by the analyzer's hashes + `start_pc`) vs the interpreter on the same case; kicks
  decoded by `GsPacketDiff` (OLD `gs_packet.h`): `DecodePacket` (`q_latch=0x3F800000` per PACKET — a harness convention,
  not GS; PRE → PRIM write; PACKED one QW per reg; REGLIST two per QW; IMAGE skipped), `Normalize` (drops consecutive
  identical STATE rewrites, never vertex/PRIM/RGBAQ/ST/UV/FOG), `Tolerance {xy=1, z=1, rgba=1, fog=1, uv=1, st_rel=1e-5f,
  st_abs=1e-6f, q_rel=1e-5f, strict_packets=false, ignore_nop_state=true}` (`--exact` zeroes them), `Diff(a,b,tol,max_lines)`
  → `DiffResult {pass, compared, mismatches, vertices_a/b, kicks_a/b, lines, summary}`. Header (`:575-586`):
  `"image=%016llx (%s) variant=%016llx prefix=%s extent=0x%x entry=0x%04x top=%u tops=%u itop=%u itops=%u dispatch="` +
  `(n/a)` or `0x%x->0x%x…` (+ ` (unterminated)`); verdict (`:806-814`): `"HLE %s: %s vs interp -- %s data=%s regs=%s
  direct=%s (tol xy=%u z=%u rgba=%u st=%g)\n"`, `data=ok|MISMATCH`, `regs=ok|MISMATCH|n/a`, `direct=ok|fallback|MISMATCH|
  n/a` (tool-side `GsDirectOpsCheck`, §2.9); also `HLE SKIP|FAIL: no HLE registered for image %s (entry 0x%04x)`, `HLE
  FALLBACK: %s declined image %s (entry 0x%04x)`. OLD sample: `HLE PASS: vs_sprite_text vs interp -- writes A=36 B=36
  vertices A=14 B=14 kicks A=14 B=14 packets A=1 B=1 mismatches=0 data=ok regs=ok direct=ok (tol xy=1 z=1 rgba=1 st=1e-05)`.
  `--hle-ref` registers `{"ref_interpreter", 0, 0, UINT32_MAX, ReferenceInterpreter}` (wildcard).
- `--catalog`: header `image    image_hash       variant_hash     entry  cases kicks  verts   bundles  bundle%   extent
  dispatch               prefix   hle / prims`, one row per image sorted by `bundles` desc (dispatch `>`-joined hex, `-` when
  none), footer `CATALOG cases=126 images=63 hle_covered=61/63 bundle_coverage=100.00% (corpus-weighted; live weights are in
  docs/loop_state/W14_PROFILE.md)` (`:935`; the footer's doc path is quoted OLD text, kept verbatim).
- Driver `/tmp/p5/<stage>/compare_vs_old.py` (helper FILE, sequential): `--old-bin --new-bin --cases <dir|list> --mode
  compare|hle|catalog --out <dir>`; per case runs OLD via `verify/run_bounded.sh 60 <out>/old/<case>.log -- <old-bin> …` with
  cwd `/tmp/p5/<stage>/oldcwd`, then NEW the same way; strips `[VU-MODE]` lines; compares exit codes and every remaining
  stdout line textually (`compare` also `cmp`s the `--mode` postdata dumps per tier); prints `CMP <case> same|DIFF old_exit=N
  new_exit=N` per case and `CMP cases=N same=N diff=N old_fail=N new_fail=N`; gate = `diff=0`.

### 4.2 `tools/vif_replay` — the 6 surviving arms (OLD `tests/vif_replay.cpp`, 1,950 ln; `tests/CMakeLists.txt:1408-1444`)

OLD usage: `usage: vif_replay [--vif-fast] <mem.bin> <tadr-hex> <dump-lo> <dump-hi> [out.bin] [vu1c] [vu1d]`; the arms are
SELF-CONTAINED synthetic chains (no `mem.bin` exists in OLD). Env pins become CLI flags (`--depth` fills `MtvuSettings::depth_frames`;
`--vif-fast` selects `VifFastSpan` vs the word path); the five `VIF_REPLAY_*` debug vars (§5.4) become options only if a ported arm needs them.

| OLD ctest | OLD env | OLD args → NEW args | Invariant tokens asserted (from `old_vif_arms.log`) |
|---|---|---|---|
| `VifReplayMtvuEquivalence` | — | `--mtvu 20` → same | `MTVU EQUIVALENCE PASS: 20 iterations, 120 packets total, byte-identical VU1 data+code mem, identical packet census+order, identical VF/VI/TPC`; `converged every iteration, monotone idle`; `EAGER-DRAIN pass: order+mem identical`; `BOUNDARIES [R168-DEFER]: 60 enqueued+delivered position-exact (one per chain, drain-thread only, hook fired 60)`. NOT asserted: `N busy views`, `N pkts eager-drained`, `(k/20 iters)` |
| `VifReplayMtvuEquivalenceFast` | — | `--mtvu 20 --vif-fast` → same | identical token set through the fast span parser |
| `VifReplayMtvuEquivalenceExecOn` / `…ExecOnFast` | `PS2_VU1_EXEC_ASYNC=1` | — | DROPPED (exec-thread arm is DELETE; no NEW counterpart) |
| `VifUnpackBulkEquivalence` | — | `--unpack-bulk` → same | 30 × `[UNPKB][<name>] PASS (bulk_cmds=N skip_cmds=M)` (names/counts as OLD, e.g. `[UNPKB][v432_unsigned_n7] PASS (bulk_cmds=1 skip_cmds=0)`) + `UNPKB EQUIVALENCE PASS: 30 cases, byte-identical VU1 data memory, must-execute/must-decline census clean`; lane OFF vs ON = two `VifUnpack` instances with `VifUnpackSettings{bulk_lanes=false|true}` (replaces `OverrideForTest`) |
| `VifUnpackBulkEquivalenceFast` | — | `--unpack-bulk --vif-fast` → same | identical 30 lines + summary |
| `VifRunaheadBound` | `PS2_MTVU_MAX_FRAMES=2` | `--runahead` → `--runahead --depth 2` | `runahead frame-cap selftest (gate on)`, `A ok: parked`, `released by frame progress`, `B ok: frozen depth released by valve at 100ms`, `runahead selftest: PASS` (the `N ms` values not asserted) |
| `VifRunaheadBoundOff` | `PS2_MTVU_MAX_FRAMES=0` | `--runahead-off` → `--runahead-off --depth 0` | `C ok: gate off, no park`, `runahead selftest: PASS` |

### 4.3 Unit tests ported (FACTS §8 → NEW ctest names; `-Wall -Wextra -Werror`, warning fixes expected; AOT twins not ported)

- `test_vu_superblock.cpp` → `VuCachedSuperblock` (`tests/vu/test_cached_superblock.cpp`, winner arm only; the 5 env twins die with their gates).
- `test_lazyflags_algebra.cpp` → `VuLazyFlagsAlgebra` (lazy NEON ring ≡ eager scalar pack over the flag algebra, property test).
- `test_vu0_shadow_replay.cpp`, `test_vu0_vis_micro.cpp`, `test_vu0_decal_eval.cpp` (+ their `.inc` images) → `Vu0ShadowReplay`, `Vu0VisMicro`, `Vu0DecalEval` (fixtures lifted to `tests/vu/fixtures/*.bin`; no `.inc`).
- `test_vu1_transform_gt.cpp` → `Vu1TransformGroundTruth`; `test_runahead_guard.cpp` → `VuRunaheadGuard`; `test_gs_packet.cpp` → `VuGsPacketCodec`.
- `test_gif_fast_equivalence.cpp` → `GifParserEquivalence` (generic per-QW bodies vs bulk lanes → identical `RegisterOpStream::Hash()`);
  `test_gif_drain_thread.cpp` (+ TSan twin) → `GifDrainThread`, `GifDrainThreadTsan`; `test_defer_boundary_order.cpp` → `GifDeferBoundaryOrder`.
- NEW (no OLD test): `GifRegsStat`, `VifUnpackScalar`, `DmacRegs`, `DmacChainWalker`, `DmacDeferredVif1` (§2.9). Corpus rows (SKIP when
  `PS2R_OG_ROOT` absent): `Vu1ReplaySelfTest`, `Vu1CachedCompare_<case>` ×126, `Vu1Hle_<case>` ×126 (77 = SKIP), `Vu1HleRef_<first 8>`,
  `Vu1HleCatalog`, `Vu1CollectSweep` (LABEL `long`, §4.5). Not ported: `test_vu_spanfold2.cpp` (gate DELETE), `test_gs_draw_ir.cpp` (Phase 6).

### 4.4 `RegisterOpStream::Hash()` as the Phase-6 oracle seed

`GifParserEquivalence` also feeds every PATH1 kick of the 126 `vutrace_cases2` cases (interp output) through `GifParser` →
`RegisterOpStream` and writes `<case> <count> <hash>` lines to `tests/gif/fixtures/gsops_seed.txt` (NEW-only artifact, generated
once at 5C — the first stage with the parser AND the interp tier running under `vu1_replay` — asserted afterwards). Phase 6's `GsState` proves G-C1 against the same lines; the per-frame `gsops.ohash` emitter is Phase 6/8 (§1.3).

### 4.5 The 3,293-case `vutrace_collect` sweep (gate item 3)

`OLD/vutrace_collect/*.bin` (3,293 × 33,456 B, 116 MB, read-only). `/tmp/p5/5H/sweep.sh` lists the files sorted, splits them into
chunks of 200 (17 chunks, the last 93), and per chunk runs ONE bounded call `verify/run_bounded.sh 900 /tmp/p5/5H/chunk_NN.log --
python3 /tmp/p5/5H/compare_vs_old.py --mode compare --cases /tmp/p5/5H/chunk_NN.lst …` (OLD and NEW both bounded inside; status via
`${PIPESTATUS[0]}`); `aggregate.py` sums the totals into `SWEEP cases=3293 same=N diff=N old_fail=N new_fail=N` and lists every DIFF
case (copied to `/tmp/p5/5H/diffs/`, each a §7.1 finding). Criterion: `diff=0` (FAIL=0). ctest `Vu1CollectSweep` runs the same script
(LABEL `long`, TIMEOUT 18000), excluded from the default preset by label.

## 5. Flags consumed (114 rows: 32 NORM / 2 SETTING / 15 DIAG / 8 TEST / 57 DELETE — the `FLAG_PHASE_COUNTS.txt:49-53` P5 block, FACTS §5.0)

"OLD site" = FACTS §5 / `getenv_census.txt` (under `OLD/host_app/` unless prefixed). Applied form (STAGE_RULES): NORM → only the
winning arm; SETTING → `<Module>Settings` field (§3.5); DIAG → omitted unless mechanism-shaped (§2.8); TEST → tool CLI option or
nothing; DELETE → not transplanted. Zero new `getenv` sites.

### 5.1 NORM — 32 (write the winning arm only)

| Var | OLD site | Winning arm → applied form | §2 row |
|---|---|---|---|
| PS2_GIF_DRAIN_THREAD | `gif_arbiter.h:48` | `GifDrainThread` class exists; STARTED only when `MtvuSettings::enabled` (`main.cpp:14228-14239` rule) | 2.1 `gif_drain_thread` |
| PS2_GIF_FAST | `gif.cpp:92` (+`graphics_thread.cpp:2035`) | `ParseSpan` zero-copy entry + bulk PACKED lanes (all_known/rowable/strip3); `ProcessPacked/ProcessReglist` only as generic per-QW bodies | 2.1 `gif_parser` |
| PS2_GSK_SIMD | `gif.cpp:122` | strip3 triple staging SIMD under `PS2R_SIMD`, scalar triple otherwise; both compiled, property-tested identical | 2.1 `gif_parser` |
| PS2_GS_DRAWIR | `hle/vu1_hle_runtime.cpp:122` | DrawIR tier first — Phase 6; here `KickLowering` raw-only + the `TryLower` seam | 2.4 `kick_lowering` |
| PS2_MTVU_DYNDEPTH | `runahead_guard.h:108` | `EffMaxFrames`: base==2 && low_band → 3, unconditional | 2.3 `runahead_guard` |
| PS2_MTVU_EAGER_DRAIN | `vu1_async.cpp:1537` | eager drain during `Sync` waits always on | 2.3 `mtvu_sync` |
| PS2_MTVU_MAX_MB | `vu1_async.cpp:187` | `constexpr uint64_t kPipelineHighBytes = 256ull << 20` | 2.3 `mtvu_backpressure` |
| PS2_MTVU_SPIN | `vu1_async.cpp:610` | adaptive spin-before-block always on | 2.3 `mtvu_backpressure` |
| PS2_MTVU_SPIN_NS | `vu1_async.cpp:628` | `constexpr kSpinBudgetNs = 40000` (no clamp arm) | 2.3 `mtvu_backpressure` |
| PS2_MTVU_VBSYNC | `vu1_async.cpp:1620` | vblank drain unconditional (caller = Phase 6/7 gs_hle site) | 2.3 `mtvu_sync` |
| PS2_RUNAHEAD_ALARM | `runahead_guard.h:77` | `Tripwire` always fed | 2.3 `runahead_guard` |
| PS2_VIF_FAST | `vif.cpp:801` (+`graphics_thread.cpp:2021`) | `VifFastSpan::ProcessDataFast` is THE parser; the latch/word path = decline target only | 2.6 `vif`, `vif_fast_span` |
| PS2_VIF_UNPACK_SIMD | `vif.h:40` | `UnpackBulk` lane always used when eligible (the `!g_logFile.is_open()` term dropped) | 2.6 `vif_unpack_bulk` |
| PS2_VIF_UNPKBX | `vif.h:91` | `UnpackBulkSkip` lane likewise | 2.6 `vif_unpack_bulk` |
| PS2_VU1_CRYS_GATE | `hle/vu1_hle_programs.cpp:267` | pre-scan decline LIFTED (games/) | 2.5 `crys_programs` |
| PS2_VU1_CRYS_RING | `hle/vu1_hle_programs.cpp:253` | data-driven ring walk, `kMeshRingBound = 4096` (games/) | 2.5 `crys_programs` |
| PS2_VU1_HLE | `hle/vu1_hle_runtime.cpp:61` | HLE tier always first in the ladder (`VuSettings::hle_enabled = true`); miss/decline → fallback tier | 2.3 `tier_dispatch`, 2.4 `vu1_hle_runtime` |
| PS2_VU1_HLE_DIRECT | `hle/vu1_hle_runtime.cpp:90` | typed-op tier — Phase 6 (seam as `PS2_GS_DRAWIR`); tool-side `GsDirectOpsCheck` for `direct=` | 2.4 `kick_lowering`, 2.9 |
| PS2_VU1_HLE_HASHEPOCH | `hle/vu1_hle_runtime.cpp:182` | generation-keyed image-hash memo only | 2.4 `vu1_hle_runtime` |
| PS2_VU1_LINK11 | `vu_fast_exec.h:113` | link mask `0x7FF` in all four BAL/JALR writers (both tiers) | 2.3 `cached_tier`, `interp_tier` |
| PS2_VU_FASTPACK | `vu.cpp:443` | `VuImageHash::Fast` (4-lane FNV) is the only image hash; `VuHashImage` not written | 2.2 `vu_image_hash` |
| PS2_VU_HASHEPOCH | `vu.cpp:2474` | `cache_current->generation == micro_generation` skips hashing | 2.3 `cached_tier` |
| PS2_VU_LAZYFLAGS | `vu.cpp:873` | lazy MAC ring on NEON (`PS2R_SIMD`) builds, eager pack on scalar builds — compile-time, never env | 2.2 `flag_pipeline` |
| PS2_VU_O3 | root `CMakeLists.txt` | build option: `ps2r_vu` compiled `-O3` (or `-O2` + OLD `-fno-strict-aliasing -fwrapv`; `ffp-contract` pinned per FACTS R8) | 2.3 `cached_tier` (CMake) |
| PS2_VU_PACE_NEXTDUE | `vu.cpp:747` | `XgKick::Pace` next-due early-out arm (`pace_nextdue_on` winner) | 2.2 `xgkick` |
| PS2_VU_RUNPACE | `vu.cpp:755` | slim pace form in the cached run body | 2.2 `xgkick`, 2.3 `cached_tier` |
| PS2_VU_RUNRETIRE | `vu.cpp:903` | O(1) evict-on-full retire in runs | 2.2 `flag_pipeline` |
| PS2_VU_SPANFOLD | `vu.cpp:526` | span stall-dispatch elision on | 2.2 `span_classify` |
| PS2_VU_STALLFOLD | `vu.cpp:510` | fold class 15/16 on | 2.2 `span_classify` |
| PS2_VU_STALLSPEC | `vu.cpp:497` | decode-time stall-class specialization on (`ApplyFmacReadStallsSpec/Fast/Classed`) | 2.2 `stall_model` |
| PS2_VU_SUPERBLOCK | `vu.cpp:478` | `run_len` backward pass + run fast path on | 2.2 `span_classify`, 2.3 `cached_tier` |
| PS2_VU_XGKPACE_QUANTUM | `vu.cpp:738` | `constexpr int64_t kXgkickPaceQuantum = 32` | 2.2 `xgkick` |

### 5.2 SETTING — 2 (module-local fields; the Config keys ALREADY EXIST, `config.h:56-59` — none added)

| Var | OLD site | Field (peak default) → mirrors | §2 row |
|---|---|---|---|
| PS2_MTVU_MAX_FRAMES | `runahead_guard.h:66` (default 2, <0→0) | `MtvuSettings::depth_frames = 2` ← `engine.mtvu_depth` (ledger `L119`) | 2.3 `runahead_guard`, `mtvu_worker` |
| PS2_VU_MODE | `vu.cpp:373` (default cached; interp when vutrace armed) | `VuSettings::vu1_mode = Cached` ← `engine.vu1_mode` (ledger `L117`; `Hle` → `Cached`, §3.3) | 2.3 `tier_dispatch` |

### 5.3 DIAG — 15 (all OMITTED this phase: none is trivially mechanism-shaped inside OWNED dirs; `Vu1Case` writer = Phase 9, §2.8)

| Var | OLD site | Applied form | §2 row |
|---|---|---|---|
| PS2_BAKE_DIAG | `dmac.cpp:1201` | omit (bake census rider) | 2.7 `dmac` |
| PS2_SHADOW8_KTAP | `vu.cpp:1635/:1677/:11318`, `vif.cpp:2172/:2250/:2468`, `gif.cpp:391/:2842/:4337`, `gif_arbiter.cpp:299/:520`, `dmac.cpp:1926` | omit (kick tap, 12 sites) | 2.1, 2.2, 2.6, 2.7 |
| PS2_UNPKT_CENSUS | `vif.cpp:344` | omit (also hard-codes `frame>=1990`) | 2.6 `vif_unpack` |
| PS2_VIF_ALIGN_LOG | `vif.cpp:1515/:1730/:1789` | omit (3 mirror latches) | 2.6 `vif` |
| PS2_VU1_HLE_DEBUG | `hle/vu1_hle_runtime.cpp:222` | omit (`[VU1-HLE-RT]` lines → `PS2R_LOG(HLE,…)` or nothing) | 2.4 `vu1_hle_runtime` |
| PS2_VUTRACE, PS2_VUTRACE_FILE, PS2_VUTRACE_MAX, PS2_VUTRACE_CAPTURE, PS2_VUTRACE_PC, PS2_VUTRACE_DIR, PS2_VUTRACE_MINFRAME, PS2_VUTRACE_PER_IMAGE, PS2_VUTRACE_IMG_GAP, PS2_VUTRACE_NEWIMG | `vu.cpp:136/:875`, `:138`, `:140`, `:142`, `:144`, `:148`, `:150`, `:152`, `:154`, `:246` | deferred with the `Vu1Case` WRITER (`MaybeCapture`, capture config `vu.cpp:118-130`, LOCKED trace-line format `vu.cpp:93-98`) | 2.8 |

### 5.4 TEST — 8 (tool CLI options or nothing; no product `getenv`)

| Var | OLD site | Applied form | §2 row |
|---|---|---|---|
| PS2_GS_DRAWIR_VERIFY | `hle/vu1_hle_runtime.cpp:149` | Phase 6 ctest (DrawIR-apply ≡ op replay); nothing here | 2.4 `kick_lowering` |
| PS2_MID_TEMPLATE_FORCEMISS | `gif_template.h:63` | dies with the template cache | 2.1 (dropped) |
| PS2_VU1_CYCLE_CAP | `vu.cpp:458` | `vu1_replay --cycle-cap N`; live constant 1,000,000 | 2.3 `interp_tier`, 2.9 |
| VIF_REPLAY_DUMPW | `tests/vif_replay.cpp:266` | `vif_replay --dump-w` if a ported arm needs it, else dropped | 2.9 `vif_replay` |
| VIF_REPLAY_LOG | `tests/vif_replay.cpp:1816` | `--log` likewise | 2.9 |
| VIF_REPLAY_MTVU_DEBUG | `tests/vif_replay.cpp:1072/:1083` | `--mtvu-debug` likewise | 2.9 |
| VIF_REPLAY_RGBDUMP | `tests/vif_replay.cpp:183` | `--rgb-dump` likewise | 2.9 |
| VIF_REPLAY_TTE2 | `tests/vif_replay.cpp:1880` | `--tte2` likewise | 2.9 |

### 5.5 DELETE — 57 (never written: var, gate and subject arm; grouped by the structure their deletion removes)

| Vars | OLD sites | What goes | §2 row |
|---|---|---|---|
| PS2_VU1_EXEC_ASYNC, PS2_VU1_EXEC_IDLE_SPIN_NS, PS2_VU1_EXEC_WAIT_SPIN_NS, PS2_MTVU_PATHSYNC_RELAX | `vu1_async.cpp:1040`, `:390`, `:400`, `:1642` | the exec thread, `W13E3_WaitPoint`, `unpack_shadow`/`CommitStagedUnpacks`, `KickAsync/WaitExecIdle` (2 vif arms dropped); the path-sync relax arm (sync sites keep the DeferOn-only skip) | 2.3 `mtvu_worker`, `mtvu_sync`, 2.6 |
| PS2_MID_TEMPLATE, PS2_SHADOW_CTXLATCH, PS2_SHADOW_ORDERFIX | `gif_template.h:51`, `s8_ctxlatch.h:114`, `:112` | `gif_template.h` (116 ln, `packet_template` dropped) and `s8_ctxlatch.h` (its order rule = the submission-order contract) | 2.1 |
| PS2_R198_PATH3DEFER, PS2_R198_PATH3DEFER_BASE, PS2_SHADOW_TAILFIX, PS2_S8_PKTDUMP, PS2_S8_PKTDUMP_MINVF | `gif_arbiter.cpp:55`, `:62`, `:160`, `:1496`, `:1504` | arbiter experiments `:52-578`, `[ARB-SUBMIT]` dump `:590-650` | 2.1 `gif_arbiter` |
| PS2_R198_XGKPROBE, PS2_R198_XGK_WINDOW, PS2_W15_KICKCENSUS | `vu.cpp:11408`, `:11445/:11449`, `:3998` | XGKICK probe/window, kick census | 2.2 `xgkick` |
| PS2_S13_PCRING, PS2_S13_TBLCAP, PS2_S13_TBLCAP_ENTRY, PS2_S13_TBLCAP_N, PS2_S13_TBLWATCH, PS2_S15_HDRCAP, PS2_S15_HDRCAP_ENTRY, PS2_S15_HDRCAP_MINVF, PS2_S15_HDRCAP_N, PS2_S15_VUENT | `vu.cpp:109`, `:4612`, `:4616`, `:4621`, `:1609/:4712`, `:4936`, `:4940`, `:4944`, `:4948`, `:4913` | table/header-capture probe forest (`:4334-4999`) | 2.2 `vu_unit`, 2.3 `interp_tier` |
| PS2_S26_FORCEPASS, PS2_S26_UTRACE, PS2_S26_VUDUMP, PS2_S8_FORCEVIS, PS2_S8_MTXCAP, PS2_S8_MTXCAP_EERAM, PS2_S8_MTXCAP_MINVF, PS2_S8_MTXCAP_N | `vu.cpp:4381`, `:10082`, `:4406`, `:10993`, `:4832`, `:4860`, `:4836`, `:4840` | behaviour-mutating diags (HACK 184), micro trace/dump, shadow force-visible + matrix capture | 2.3 `interp_tier` |
| PS2_VU0_MODE, PS2_VU1_MODE, PS2_VU0_SHADOWGATE, PS2_VU0_VISCAP, PS2_VU0_VISCAP_MINVF, PS2_VU0_VISCAP_N, PS2_VU0_XCHECK | `vu.cpp:726` (one `getenv(id==0 ? … : …)`), `:4460`, `:4548`, `:4551`, `:4555`, `:5041` | per-unit mode override, VU0 shadow gate / visibility capture / cross-check | 2.2 `vu_unit` |
| PS2_VU_RUNFLAT, PS2_VU_RUNFLAT_CENSUS, PS2_VU_SPANFOLD2, PS2_VU_NOCLAMP, PS2_VU_CLAMPELIDE | `vu.cpp:575`, `:585`, `:547`, `vu_fast_exec.h:61`, `:83` | second span bodies (default OFF; `test_vu_spanfold2` not ported); clamp bypass (`kVuNoClamp`, `kVuClampElide`) | 2.2 `span_classify`, `vu_float` |
| PS2_VU_AOT, PS2_VU_AOT_SKELFULL, PS2_VU_AOT_SPANFOLD, PS2_VUAOT_COLLECT_DIR, PS2_VUAOT_INI, PS2_VUAOT_TEST_REGEN, PS2_VUAOT_TEST_SETCOLLECT, PS2_VUAOT_TEST_SETMODE | `vu_aot_programs.cpp:37/:47/:70`, `main.cpp:5020/:5002`, `gui/vu_microcode_section.cpp:79/:163/:360/:348/:339` | the VU-AOT family (NOAOT; `aot_*` fields of `VuCachedProgram` not written) | 2.3 `cached_tier` |
| PS2_S8_VIFTAP, PS2_VIF_WTRACE, PS2_S8_ARENA_MINVF, PS2_VU1_HLE_WHY | `vif.cpp:2553`, `:3456`, `dmac.cpp:1935`, `hle/vu1_hle_programs.cpp:214/:225` | `s8viftap` namespace `:2550-2678`, VIF write trace, arena probe, `WhyNote/WhyNotef` (`:212-244`) | 2.6 `vif`, 2.7 `dmac`, 2.5 `library_helpers` |

### 5.6 Outside the count — ledger gaps (FACTS §5.6; none changes the 114)

- Read in Phase-5 files, ledgered in OTHER phases: `PS2_EEFEED` (`w10_eefeed.h:50`, NORM P3) → both arms are Phase-5 code, winners
  written here (B1 `ChainWalker` staged submit, B2 `GifParser` row loop); `PS2_MTVU` (`vu1_async.cpp:1011`, NORM P3) →
  `MtvuSettings::enabled`; `PS2_MTVU_DEFER` (`vu1_async.cpp:1656`, NORM P3) → deferred boundary delivery is the only MTVU-on path;
  `PS2_PERF` (10 sites, SETTING P3) → announce latches die; `PS2_MIPCEN` (`gif.cpp:5043`, DIAG P2) → omit; `PS2_S26_DECAL`
  (`vu.cpp:4346`, `gif.cpp:3686`, `dmac.cpp:1969`, DELETE P1) → not written.
- P5-block names with NO `getenv` site in the Phase-5 files: `PS2_VU0_MODE`/`PS2_VU1_MODE` (one ternary site, `vu.cpp:726`),
  `PS2_VU_O3` (CMake), the 8 VU-AOT vars — all dispositioned above. `PS2_VIF_FAST` has a test-side site `tests/vif_replay.cpp:947`
  → the `--vif-fast` flag (§4.2), not a `getenv`.

## 6. Proof gate and Definition of Done (CONSTRUCTION §4.5, restated as checkable items — MODULE GATE only)

Sources for this step (nothing else was read): §1–§5 above; FACTS §1.1 (OLD artifact numbers), §3.5 (library census), §7 (tool
grammars), §9–§10 (risks, integration seeds); `PHASE4_PLAN.md` §6–§8 + HANDOFF (format only); CONTRACT §2/§4/§6. Every item is
quoted in `PHASE5_IMPL.md` from a bounded run in THIS tree (`verify/run_bounded.sh`, cwd under `/tmp/p5/<stage>/`); OLD tool runs
are ORACLES (`OLD/build/tests/{vu1_replay,vif_replay}` only). "Identical" = textual line identity after the `[VU-MODE]` skip plus
`cmp`-identical postdata dumps — never PASS-vs-PASS alone. No harness leg is run and no lane is claimed (§1.3).

| # | Gate item | Check |
|---|---|---|
| G1 | Tree green | `cmake --build --preset macos-dev -j4` → 0 warnings (`-Wall -Wextra -Werror` on `ps2r_gif`, `ps2r_vu`, `ps2r_vif`, `ps2r_dmac`, `ps2r_title_vu1_library`, both tools, every test TU); `ctest --preset macos-dev` 100 % (the `long`-labelled `Vu1CollectSweep` excluded by label, run separately in G5); `LintOneClass` PASS, `LintEnginePurity` PASS; `CoreSingleGetenvSite` = 1 (Phase 5 adds no `getenv`; TEST-ONLY pins became `--cycle-cap`, `--depth`, `--vif-fast`); root `CMakeLists.txt` / `tests/CMakeLists.txt` diffs are EXACTLY the append-only lines of §1.5 (`git diff --stat` quoted) |
| G2 | `--compare` 126/126 vs OLD | `compare_vs_old.py --mode compare --cases OLD/vutrace_cases2` → `CMP cases=126 same=126 diff=0 old_fail=0 new_fail=0`; every NEW line `COMPARE PASS: pc=0x…​ data_diff_bytes=0/16384 regs_identical aot_dispatches=0 (interp vs cached)` equals OLD's (OLD recorded `TOTAL cases=126 pass=126 fail=0`, `old_compare.log`); per case the `--mode interp` and `--mode cached` postdata dumps `cmp` identical OLD vs NEW (bit-exact per tier, §4.1) |
| G3 | `--hle` 126/126 vs OLD | `--mode hle` driver → `same=126 diff=0`: 124 cases `HLE PASS: <name> vs interp -- … data=ok regs=ok|n/a direct=ok|fallback|n/a (tol xy=1 z=1 rgba=1 st=1e-05)` token-identical to OLD incl. the header line (`image= variant= prefix= extent= entry= top= tops= itop= itops= dispatch=`), 2 cases (images `0d20914e`, `50b7302d`) exit 77 `HLE SKIP: no HLE registered …` in both; `Vu1HleRef_<first 8>` (`--hle --hle-ref --exact`) green; ctest `Vu1Hle_<case>` ×126 pass/SKIP exactly as OLD |
| G4 | `--catalog` identity | `vu1_replay --catalog OLD/vutrace_cases2` output (66 lines after the `[VU-MODE]` skip: header + 63 image rows + blank line + footer; `old_catalog.log` is 67 with the announce line) `diff` empty against `/tmp/p5/5P/old_catalog.log` after the `[VU-MODE]` skip; footer literally `CATALOG cases=126 images=63 hle_covered=61/63 bundle_coverage=100.00% (corpus-weighted; live weights are in docs/loop_state/W14_PROFILE.md)`; the registry self-report `entries=223 distinct_variant_hashes=176` (corrected at 5H per P5-5D-ENTRY-COUNT: 223 table rows / 176 distinct variant hashes / 12 program names / 15 program functions; the planning text said `entries=140`, which was the OLD `Register(` SITE count) |
| G5 | 3,293-case sweep FAIL=0 | `/tmp/p5/5H/sweep.sh` (17 chunks × ≤200, §4.5) → `SWEEP cases=3293 same=3293 diff=0 old_fail=0 new_fail=0`; every chunk log present, `${PIPESTATUS[0]}`=0; any `old_fail>0` case is recorded (OLD's own failure, not a NEW divergence) as a §7.1 row before the gate is called; ctest `Vu1CollectSweep` (LABEL `long`; registered DISABLED unless `-DPS2R_VU1_SWEEP=ON` because no preset carries a label filter — P5-5H-SWEEP-CTEST) registered and run once, its line quoted |
| G6 | `vif_replay` arms identical | the 6 surviving OLD arms (of 8; the 2 `PS2_VU1_EXEC_ASYNC=1` arms are DELETE, §4.2) run bounded NEW vs OLD (`run_old_vif_arms.sh` twin) → every invariant token of §4.2 present in both: `MTVU EQUIVALENCE PASS: 20 iterations, 120 packets total, byte-identical VU1 data+code mem, identical packet census+order, identical VF/VI/TPC`, `converged every iteration, monotone idle`, `EAGER-DRAIN pass: order+mem identical`, `60 enqueued+delivered position-exact`, the 30 `[UNPKB][<name>] PASS (bulk_cmds=N skip_cmds=M)` lines textually identical + `UNPKB EQUIVALENCE PASS: 30 cases, …`, `runahead selftest: PASS` ×2 (`A ok`/`B ok`/`C ok`); ctests `VifMtvuSubmitEquivalence(Fast)` (registered under this name — the driver keeps OLD's `VifReplayMtvuEquivalence(Fast)` arm names; P5-5G-ARM-NAMES), `VifUnpackBulkEquivalence(Fast)`, `VifRunaheadBound(Off)`, `VifArmsVsOld` green |
| G7 | Unit-test ports green | §4.3 complete: `VuCachedSuperblock`, `VuLazyFlagsAlgebra`, `Vu0ShadowReplay`, `Vu0VisMicro`, `Vu0DecalEval` (fixtures as `.bin`, no `.inc`), `Vu1TransformGroundTruth`, `VuRunaheadGuard`, `VuGsPacketCodec`, `GifParserEquivalence` (+ `tests/gif/fixtures/gsops_seed.txt` 126 lines asserted), `GifDrainThread`, `GifDrainThreadTsan`, `GifDeferBoundaryOrder`, NEW `GifRegsStat`, `VifUnpackScalar`, `DmacRegs`, `DmacChainWalker`, `DmacDeferredVif1`, `Vu1ReplaySelfTest`, `Vu1CachedCompare_<case>` ×126, `Vu1HleCatalog`; every row SKIPs (not fails) when `PS2R_OG_ROOT` is absent. `macos-dev` runs these against the SCALAR lanes (`PS2R_SIMD` OFF); the three NEON-twin property tests (`VuLazyFlagsAlgebra`, `GifParserEquivalence`, `VifUnpackBulkEquivalence`) are additionally built and run once from a bounded `-DPS2R_SIMD=ON` configure (`/tmp/p5/<stage>/build-simd`), quoted at 5B/5E and here (§7.1 P5-NEON-DEFAULT) |
| G8 | Purity of title data | `grep -rn` for `0x1720`, `0x090E0003`, `0x70, 0x90`/`exit_stubs`, `0xA0` (as `family_exit_tpc`), every `vs_*` stamp and every 16-hex-digit image/variant hash over `engine/` + `tools/` → 0 hits (they live ONLY under `games/<title>/vu1_library/`); `LintEnginePurity` 0 (`0x090E0003`, `vs_*`, `kVariantBase` regexes) — the 4-digit literals are review-checked by this grep (FACTS §2.12/R14); `engine/gif/**` + `engine/vu/**` include no `engine/gs/` header other than `engine/gs/format/*.h` (§3.2 layering grep quoted) |
| G9 | `PHASE5_IMPL.md` complete | one §8.2-template section per stage (5A…5H incl. every split taken) with quoted outputs; the §7.1 ledger has a disposition per row; the final section carries the **INTEGRATION CHECKLIST** (seeded below and from FACTS §10: MMIO ranges, BoundarySequencer steps, Config keys, SIF/IPU bindings, savestate sections, threads, `--vu1-library none`, the exact lane-gate commands) and the two file MOVES (`register_op_stream` → `engine/gs/hle/ir`, `vu1_case` → `engine/core/diag`) |
| DoD | all of G1–G9 | the four subsystem libraries + the title library build warning-free and are proven bit-exact against the frozen OLD tools on 126 + 3,293 cases and 6 arms; every §5 flag has its outcome applied at transplant time (no `getenv`, no latch, no losing arm); DIAG/DELETE arms absent; the module classes expose (but do not register) their `MmioHandler`s / `SectionSpec` factories / boundary duties; **Phase 5 module gate: PASSED** is written only when G1–G9 are each quoted, else the exact failing command is the handoff |

INTEGRATION GATES handed off (NOT this phase's — they need `MmioRouter`/`BoundarySequencer`/`app/host`/savestate registration,
all FORBIDDEN by §1.5; each becomes a checklist row in `PHASE5_IMPL.md`'s final section and is executed by the integration owner):
- `audio` + `mem` lanes IDENTICAL on all 12 legs through the Phase-5 stopping rule (CONSTRUCTION §4.5 DoD: coldboot to its
  first FMV, each anchor ≥ 60 boundaries) — `run_leg.py --tree new … --frames <1500|600>` + `diff_legs.py --cross-engine
  --lanes audio,mem`; the deterministic profile runs MTVU OFF and the drain thread OFF (FACTS §3.7/§3.1), so the certified path
  is the synchronous VIF1→VU1→GIF inline drain.
- `MmioRouter::UnhandledRanges()` lists only GS-priv (`0x12000000`) + IPU (`0x10007000`, `0x10002000`) after GIF/VIF/VIF1-FIFO/
  DMAC registration and the four VU memory `RegisterHwWindow`s (FACTS §10 addresses; OLD's observable per range: VIF non-FBRST/ERR
  reads = 0, DMAC global writes dropped, `D_ENABLEW` write range).
- [GD1] empty-profile contract: `--vu1-library none` boots with the registry EMPTY (every image → fallback tier), `mem` lane
  identity unchanged on the anchors that never enter the HLE path; the CLI switch is `app/host`'s.
- P4FIX-F1 legs (f57292 / f59991): closure lives in Phase 4/5 per CONSTRUCTION §7; Phase 5 contributes only the DMAC/VIF/VU
  modules the probe runs through — the verdict is recorded on the integration leg, not here.
- Also: `gsops.ohash` per-frame emitter (`RegisterOpStream` per present, §4.4) — Phase 6/8; `SectionSpec` registration for
  `TAG_VU0C/VU0D/VU1C/VU1D/DMAC/VIFS/GIFS/VU1R` + `RestoreMhashAnchors` re-run; `ChaseVif1Tail`/`FlushDeferredVif1` at the
  SyncV/WaitForVblank boundary + the MTVU `openBoundary`+`drain` slot; `Dmac` SIF1 → `engine/sif` `ProcessSifDma` HLE and IPU0/IPU1
  → Phase 7; `SyscallTable` handler table → `DmacHandlerRegistry` (mask stays 0 — FACTS R3); `MtvuWorker`/`GifDrainThread` start
  iff `engine.mtvu`; `Config → {MtvuSettings, VuSettings}` copy at composition; the SPR-stub decision (FACTS R2) through the
  accepted-difference protocol if a real SPR copy is ever wanted.

Accepted-difference protocol here: NONE expected — the module gate compares tool output to tool output on identical inputs, so any
DIFF is a construction bug (fix, never tolerate). MAY become a §7.1 row (recorded, not tolerated): an `old_fail>0` sweep case (OLD's
own FAIL), a `-ffp-contract` setting pinned after a mismatch (FACTS R8), the `direct=` token where the tool-side `PacketToOps` port
proves incomplete (masked ONLY with the row filed first, FACTS R7). MAY NOT: masking any `COMPARE`/`HLE`/`UNPKB` token, shortening the
case list, running OLD unbounded or from OLD's own cwd.

## 7. Stages (≤ 45 min each; every stage ends with the tree building and `ctest --preset macos-dev` green)

Order = the harness path first (FACTS R4): GIF sink/parser + the arbiter's inline half → VU core + interp → cached + the corpus
oracle → HLE mechanism + library → VIF → DMAC → the cross-thread half (MTVU, drain thread) → sweep + record. Every stage is
pre-registered to split (5X′ — the 4A′/4B′/4D′ precedent): at ~35 min STOP, tree green, the named remainder moves to the primed
stage, recorded as a `P5-5X-SPLIT` row. A class that needs a collaborator from a LATER stage takes it by reference through an
interface that already exists (e.g. `XgKick` submits to `GifArbiter&` from 5A) — never a stub global. "Flags" = §5 rows applied.

| Stage | Scope | OLD sources read | Green criterion (own) |
|---|---|---|---|
| **5A** engine/gif core + arbiter inline half + tests/gif | `engine/gif/{gif_tag, gif_regs, gs_sink.h, register_op_stream, gif_parser}` (§2.1) + `gif_arbiter` SYNCHRONOUS half (`GifArbiterPacket`, `submit/submitOwned`, buffer pool `kPoolMax=4`, `drain()` in submission order over `m_queue`, `hasPending/clear`, `FinishPath3` after a PATH3 pass; markers/inbox/boundary/drain-thread deferred to 5G — §7.1 P5-ARBITER-SPLIT); `packet_template` decision RECORDED as dropped (README row); `engine/gif/CMakeLists.txt` (`ps2r_gif` → `ps2r_core`, `ps2r_ee`, `ps2r_gs_format`; `PS2R_SIMD` strip3 lane) + the two append-only CMake lines; `tests/gif/{test_gif_regs, test_gif_parser_equivalence (synthetic half), counting_gs_sink}`. Flags: `PS2_GIF_FAST`, `PS2_GSK_SIMD`, `PS2_EEFEED` (B2), `PS2_GIF_DRAIN_THREAD` (class deferred, rule noted), DIAG/DELETE rows of §2.1 (omit / not written) | `gif.h:15-118, :120-202`; `gif.cpp:185-380, :716-735, :3183-3258, :3585-4324, :4374-4400, :4541-5224`; `gif_arbiter.h:85-112, :229-247`; `gif_arbiter.cpp:20-30, :579-783, :933-1808` (skipping `:52-578, :590-650, :700-840`); `gif_template.h` (decision only); `tests/test_gif_fast_equivalence.cpp` | `GifRegsStat` (STAT bit composition, TAG0-3 nibbles, CTRL reset/PSE, MODE&7), `GifParserEquivalence` synthetic half (per-QW generic bodies vs strip3/rowable lanes → identical `RegisterOpStream::Hash()`; REGLIST odd pad; NLOOP=0 EOP; IMAGE halves; TEXCLUT dropped; REGLIST XYZ2 ADC ignored) green; `sizeof(RegisterOp)==24`, `sizeof(GifTag)` pinned; lints 0; `CoreSingleGetenvSite`=1. **5A′:** at ~35 min STOP with `gif_tag`, `gif_regs`, `gs_sink`, `register_op_stream`, `gif_parser` PACKED/REGLIST/IMAGE + `GifRegsStat` green; hand the arbiter inline half, the strip3/rowable lanes + `GifParserEquivalence`, `FinishPath3` and the README rows to **5A′** |
| **5B** engine/vu core + interp tier + tests/vu | `engine/vu/{vu1_state.h, vu_unit, decode(+.inl), span_classify, flag_pipeline, stall_model, xgkick, vu_float, chop_round_guard, vu_image_hash, vu1_case}` (§2.2) + `tiers/interp_tier` (§2.3) + `vu0/libvu0_sincos` (interp path `vu.cpp:5550-5594`); `engine/vu/CMakeLists.txt` (`ps2r_vu` → `ps2r_gif`, `ps2r_ee`, `ps2r_core`; `-O2 -fno-strict-aliasing -fwrapv`, `PS2R_SIMD` lazy-ring lane, `ffp-contract` left at clang default until a mismatch pins it — FACTS R8); `tests/vu/{test_lazyflags_algebra, test_vu1_transform_gt, test_vu0_shadow_replay, test_vu0_vis_micro, test_vu0_decal_eval}` + `fixtures/*.bin` lifted from the three `.inc`. Flags: `PS2_VU_LAZYFLAGS`, `PS2_VU_RUNRETIRE`, `PS2_VU_XGKPACE_QUANTUM`, `PS2_VU_PACE_NEXTDUE`, `PS2_VU_RUNPACE`, `PS2_VU_FASTPACK`, `PS2_VU1_LINK11` (interp writers), `PS2_VU1_CYCLE_CAP` (→ ctor arg), `PS2_VU_STALLSPEC`, `PS2_VU_STALLFOLD`, `PS2_VU_SPANFOLD`, `PS2_VU_SUPERBLOCK` (classifier side); DELETE probe families of §5.5 rows 4–8 (not written) | `vu.h:61-252, :464-1167`; `vu.cpp:54-69, :387-407, :456-466, :717, :761-829, :872-1258, :1598-1602, :1792-1829, :1861-1934, :5203-5210, :5300-5301, :5550-5594, :5990-7910, :8095-11314, :11359-11384, :11566-11732, :12233-12251, :12416`; `vu_decode_slot.h`, `vu_span_classify.h`, `vu_fast_exec.h:38-360, :387-639, :1807-1821`, `vu_hash_fnv.h:22-50`, `vu_trace_case.h:24-79`, `memory.h:17-20`; the 5 test TUs + 3 `.inc` | `VuLazyFlagsAlgebra` (lazy ≡ eager over the flag algebra), `Vu1TransformGroundTruth`, `Vu0ShadowReplay`, `Vu0VisMicro`, `Vu0DecalEval` green; `sizeof(VuDecodedSlot)==60`, `sizeof(Vu1Case)==33456` static_asserts; `VuImageHash::Fast` of `vutrace_cases2/vu1_case_0000_1748.bin` micro == the hash in its `--catalog` row (`old_catalog.log`); lints 0. **5B′:** at ~35 min STOP with `vu1_state`, `decode`, `vu_float`, `chop_round_guard`, `vu_image_hash`, `vu1_case`, `flag_pipeline`, `stall_model` + `VuLazyFlagsAlgebra` green; hand `span_classify`, `xgkick`, `vu_unit`, `interp_tier`, `libvu0_sincos` and the four remaining tests to **5B′** |
| **5C** cached tier + tier dispatch + `tools/vu1_replay` → 126/126 vs OLD | `engine/vu/tiers/{cached_tier, tier_dispatch}` (§2.3; `VuSettings`, HLE hook slot present but `hle_enabled=false` until 5D); `tools/vu1_replay/{vu1_replay.cpp, xgk_capture_sink}` with `<case>`, `--selftest`, `--mode`, `--compare [--compare-cycle]`, `--bench`, `--dump-kicks`, `--cycle-cap` (the `--hle`/`--catalog` grammars stubbed to exit 2 until 5D); `/tmp/p5/5C/compare_vs_old.py` (§4.1 driver); `tests/vu/test_cached_superblock.cpp` + ctest rows `Vu1ReplaySelfTest`, `Vu1CachedCompare_<case>` ×126 (SKIP without `PS2R_OG_ROOT`); `GifParserEquivalence` corpus half → `tests/gif/fixtures/gsops_seed.txt` (§4.4). Flags: `PS2_VU_HASHEPOCH`, `PS2_VU1_LINK11` (cached writers), `PS2_VU_O3` (CMake), `PS2_VU_SUPERBLOCK`/`STALLFOLD`/`SPANFOLD` (run bodies), `PS2_VU_MODE` (SETTING field); VU-AOT family + `RUNFLAT`/`SPANFOLD2` (not written) | `vu.cpp:443-524, :2096, :2228-2695, :2696-3928, :4318-4332, :5009-5198`; `vu_fast_exec.h:111-117, :663-1806`; `tests/vu1_replay.cpp:130-182, :400-473, :554, :1094-1099, :1166`, `tests/vu1_test_shims.cpp:33-40`, `tests/vu1_xgk_capture.h`, `tests/test_vu_superblock.cpp` (winner arm) | `compare_vs_old.py --mode compare` over `OLD/vutrace_cases2` → `CMP cases=126 same=126 diff=0 old_fail=0 new_fail=0` + per-tier postdata `cmp` identical (G2 evidence, quoted); `VuCachedSuperblock`, `Vu1ReplaySelfTest`, 126 `Vu1CachedCompare_*` green; `gsops_seed.txt` 126 lines; lints 0. **5C′:** at ~35 min STOP with `cached_tier` + `tier_dispatch` + `--mode`/`--selftest` + `Vu1ReplaySelfTest` + `VuCachedSuperblock` green and the first 8 cases `--compare` identical; hand `--compare` over 126, the driver, the 126 ctest rows and the seed file to **5C′** |
| **5D** vu/hle mechanism + library group A + `--hle` partial | `engine/vu/hle/{vu1_hle_contract.h, vu1_family_signature.h, vu1_registry, vu1_image_analyzer, vu1_hle_runtime, kick_lowering, gif_packet_builder}` + `vu0/vu0_registry` (§2.4); `tools/vu1_replay/{gs_packet_diff, gs_direct_ops_check}` + the `--hle [--hle-ref] [--skip-unregistered] [--exact] [--tol-*] [--strict-packets] [--max-lines]` and `--catalog` grammars; `tier_dispatch` step 1 wired (`hle_enabled=true` with a library); `games/…/vu1_library/{vu1_library, library_helpers, clip_gate, family_tristrip, textured_tristrip, poscolor_strip, lit_tristrip}` (§2.5 **group A = fam/ttx/pcs/lit** with `s_reg_*` statics of those namespaces), `vu1_library/CMakeLists.txt` (`ps2r_title_vu1_library` → `ps2r_vu` only) + the append-only root line; `tests/vu/test_gs_packet.cpp`. Flags: `PS2_VU1_HLE`, `PS2_VU1_HLE_HASHEPOCH`, `PS2_VU1_HLE_DIRECT`/`PS2_GS_DRAWIR` (raw tier + seam), `PS2_VU1_HLE_DEBUG` (omit), `PS2_VU1_HLE_WHY` (not written), `PS2_GS_DRAWIR_VERIFY` (nothing) | `hle/vu1_hle.h:8-107`, `hle/vu1_hle.cpp:19-90`, `hle/vu1_hle_runtime.cpp:160-423`, `hle/gs_packet.h:28-130, :174-245, :350-398, :406-509`, `hle/gs_direct_ops.h` (151), `vu_function_registry.{h,cpp}`; `hle/vu1_hle_programs.cpp:21-334, :679-2060, :2146-2381, :2478-2908` (+ helpers `:72-244, :739, :1380-1393`); `tests/vu1_replay.cpp:575-586, :638-730, :806-814, :935`, `tests/test_gs_packet.cpp` | `VuGsPacketCodec` green; registry self-report printed; `--hle` on every `vutrace_cases2` case whose catalog `hle` column names a group-A program → token-identical to OLD (driver `--mode hle --cases <group-A list>` → `diff=0`); `--hle --hle-ref --exact` on the first 8 cases green (`Vu1HleRef_*`); G8 grep 0 hits over `engine/`+`tools/`; lints 0. **5D′ (group B = skn/idx/zk/spr):** `skinned_lit_strip`, `indexed_displace`, `zero_kick` (3 blocks merged + `s_reg_zk_all` 64 rows + `s_reg_zk_cont`), `sprite_text` (+ `s_reg_spr_all`, entries 0x1748/0x1980) — `hle/vu1_hle_programs.cpp:3016-3650, :3788-3989, :4055-4203, :4208, :4310-4657, :4703-4988, :4995, :5110-5315, :5322`; green = group-B cases `diff=0`. **5D″ (group C = crys + tables + vu0 data):** `crys_programs` (`vs_crys_dual`, `vs_crys_displace`, ring walk `kMeshRingBound=4096`, gate LIFTED; `s_reg_crys_all`, `s_reg_fog_all`, `s_reg_f20442_all`, `s_reg_cs9a_disp` — `:251-278, :3011-3014, :5386-6066, :6083-6198`; flags `PS2_VU1_CRYS_RING`, `PS2_VU1_CRYS_GATE`), `vu0_programs.cpp`, README count fix; green = `--mode hle` over all 126 → `same=126 diff=0` (124 PASS + 2 × exit 77), `--catalog` `diff` empty vs `old_catalog.log`, `entries=N distinct_variant_hashes=M` pinned (expect 140/176), ctest `Vu1Hle_*` ×126 + `Vu1HleCatalog` green (G3/G4 evidence) |
| **5E** engine/vif + `tools/vif_replay` UNPACK arms | `engine/vif/{vif_regs.h, vif, vif_unpack, vif_fast_span, vif_unpack_bulk}` (§2.6; `Vif` ctor-injected `VuUnit& vu0, vu1`, `GifArbiter&`, `GifRegs&`; `VifUnpackSettings`; the MTVU seam = `CtxFor/SetTteFeed/ResetLatch` members); `engine/vif/CMakeLists.txt` (`ps2r_vif` → `ps2r_vu`, `ps2r_gif`, `ps2r_ee`; `PS2R_SIMD` NEON lanes) + append-only lines; `tools/vif_replay/vif_replay.cpp` with `--unpack-bulk [--vif-fast]` (30 `b_*`/`bx_*` cases) and the `--mtvu`/`--runahead*` arms parsed but exiting 2 until 5G; `tests/vif/test_vif_unpack_scalar.cpp` + ctest rows `VifUnpackBulkEquivalence`, `VifUnpackBulkEquivalenceFast`. Flags: `PS2_VIF_FAST`, `PS2_VIF_UNPACK_SIMD`, `PS2_VIF_UNPKBX`; `PS2_UNPKT_CENSUS`, `PS2_VIF_ALIGN_LOG`, `PS2_SHADOW8_KTAP` (omit); `PS2_S8_VIFTAP`, `PS2_VIF_WTRACE`, `PS2_VU1_EXEC_ASYNC` seam (not written); `VIF_REPLAY_*` (options only if needed) | `vif.h:106-347`; `vif.cpp:39-42, :168-231, :309-311, :420-702, :703-830, :831-966, :967-1228, :1229-1905, :1906-2549, :2680-3503, :3504-3609`; `tests/vif_replay.cpp:1322-1470` (+ `:183, :266, :947`), `tests/CMakeLists.txt:1408-1444` | `VifUnpackScalar` (`GetUnpackSize` incl. `num==0→256` and CL<WL, V3-32 phase rule, V4-5 expansion, mask table `bit=row*8+field*2`, `dest=(addr_qw*16)&0x3FFF`) green; NEW `vif_replay --unpack-bulk` and `--unpack-bulk --vif-fast` → the 30 `[UNPKB][…] PASS (bulk_cmds=N skip_cmds=M)` lines + summary textually identical to `old_vif_arms.log` (bounded OLD twin re-run quoted); ctest 2 arms green; lints 0. **5E′:** at ~35 min STOP with `vif_regs`, `vif_unpack`, `vif_unpack_bulk`, `vif` (`ExecuteCommand` + latch path + `Read/Write`) + `VifUnpackScalar` green; hand `vif_fast_span`, the tool, the 2 arms and the seam members to **5E′** |
| **5F** engine/dmac + tests/dmac | `engine/dmac/{dmac_channel.h, dmac, chain_walker, deferred_vif1, dmac_handlers}` (§2.7; `Dmac : MmioHandler` NOT registered; `DmacHandlerRegistry` table present, dispatcher callback injected, NOT handed the syscall table; SPR = stub; SIF/IPU = injected callbacks, unbound; `SectionSpec` factory for `TAG_DMAC` unregistered); `engine/dmac/CMakeLists.txt` (`ps2r_dmac` → `ps2r_vif`, `ps2r_gif`, `ps2r_ee`) + append-only lines; `dmac/hle/README.md` row "libdma bindings NOT installed"; `tests/dmac/{test_dmac_regs, test_chain_walker, test_deferred_vif1}` over a `MainMemory`+`Scratchpad` fixture with a `CountingGsSink`/recording `Vif`. Flags: `PS2_EEFEED` (B1 staged submit — `MtvuWorker*` optional, null until 5G); `PS2_BAKE_DIAG`, `PS2_SHADOW8_KTAP` (omit); `PS2_S8_ARENA_MINVF`, `PS2_S26_DECAL` (not written) | `dmac.h:7-178`; `dmac.cpp:137-253 (read for the callback shape), :495-1044, :1045-1253, :1254-1578, :1592-1650, :1664-2707, :2708-2850, :2851-2858, :2870-2999` (skipping `:2735-2760`); `w10_eefeed.h:50`; `savestate.cpp:1185-1213, :1811-1832` (layout only) | `DmacRegs` (page map `0x8000…0xD400`, `0xE000-0xE060`, `0xF520/0xF590`; non-channel global writes DROPPED; CHCR-read flush of a deferred VIF1 kick; `CheckInterrupt` false with mask 0), `DmacChainWalker` (8 tag ids, CALL/RET ASR stack, all-ones terminator, `kMaxTags=10000`, TTE tag QW with `tte_feed`, SPR-sourced tags `0x70000000|(x&0x3FFF)`, GIF chain = ONE PATH3 submit, VIF1 END → tail record), `DmacDeferredVif1` (deferred kick, flush points, tail chase resume on a sane terminator, `kMaxRetries=120`, SPR channel = `CompleteChannel` only) green; `sizeof(DmacChannelRegs)==28`; lints 0. **5F′:** STOP with `dmac_channel`, `dmac` (Read/Write/StartChannel/Complete) + `DmacRegs` green; hand `chain_walker`, `deferred_vif1`, `dmac_handlers` + 2 tests to **5F′** |
| **5G** cross-thread half: arbiter markers/inbox/boundary + drain thread + MTVU + remaining arms | `engine/gif/gif_arbiter` completion (`GifMtvuMarker`, `openMarker/sealMarker/collectReady`, `m_inbox`, `openBoundary/dispatchBoundaryInline` + ctor-injected hook, `PendingBytes/PendingSlots` members) + `gif_drain_thread` (§2.1); `engine/vu/tiers/{mtvu_worker, mtvu_backpressure, mtvu_sync, runahead_guard}` (§2.3; `MtvuSettings`; `tier_dispatch` step 2 wired; `Vif` seam consumed); `vif_replay --mtvu N [--vif-fast]`, `--runahead --depth 2`, `--runahead-off --depth 0`; `tests/gif/{test_gif_drain_thread (+tsan), test_defer_boundary_order}`, `tests/vu/test_runahead_guard.cpp`; ctest rows `VifReplayMtvuEquivalence(Fast)`, `VifRunaheadBound(Off)`. Flags: `PS2_GIF_DRAIN_THREAD`, `PS2_MTVU` (field), `PS2_MTVU_MAX_FRAMES` (field), `PS2_MTVU_DYNDEPTH`, `PS2_MTVU_EAGER_DRAIN`, `PS2_MTVU_MAX_MB`, `PS2_MTVU_SPIN`, `PS2_MTVU_SPIN_NS`, `PS2_MTVU_VBSYNC`, `PS2_MTVU_DEFER`, `PS2_RUNAHEAD_ALARM`; `PS2_MTVU_PATHSYNC_RELAX`, `PS2_VU1_EXEC_*`, `PS2_R198_*`, `PS2_SHADOW_TAILFIX`, `PS2_S8_PKTDUMP*` (not written) | `gif_arbiter.h:184-195, :352-372, :470-557, :562-590, :629-679`; `gif_arbiter.cpp:723-783` (routing), `:1138-1156`; `vu1_async.h:39-358`; `vu1_async.cpp:47-90, :186-258, :464, :521-536, :608-641, :861-989, :1009-1024, :1093-1100, :1250, :1366-1687`; `runahead_guard.h:60-177`; `main.cpp:14228-14239`; `tests/vif_replay.cpp:543-620, :1072-1083, :1880`, `tests/{test_gif_drain_thread, test_defer_boundary_order, test_runahead_guard}.cpp` | `GifDrainThread`, `GifDrainThreadTsan`, `GifDeferBoundaryOrder` (60 boundary slots position-exact), `VuRunaheadGuard` (Tripwire: `over = depth > cap+1`, 30 presents, ≤5 reports; `EffMaxFrames` 2→3 lift) green; NEW `vif_replay` 4 arms print every §4.2 invariant token, bounded OLD twins quoted alongside (G6 evidence); ctest 4 arms green; the deterministic-profile shape stated in `PHASE5_IMPL` (MTVU off ⇒ none of this code runs on a harness leg); lints 0. **5G′:** STOP with the arbiter completion + `gif_drain_thread` + `runahead_guard` + `mtvu_backpressure` + their 4 tests green; hand `mtvu_worker`, `mtvu_sync`, the dispatch step-2 wiring and the 4 arms to **5G′** |
| **5H** 3,293 sweep + `PHASE5_IMPL` finalization + INTEGRATION CHECKLIST | `/tmp/p5/5H/{sweep.sh, compare_vs_old.py, aggregate.py}` (§4.5), ctest `Vu1CollectSweep` (LABEL `long`, TIMEOUT 18000) registered in `tests/vu/CMakeLists.txt`; full `ctest --preset macos-dev` + both lints + `check_getenv_sites.py` re-run and quoted; G1–G9 table filled in `PHASE5_IMPL.md`; §7.1 dispositions closed; the INTEGRATION CHECKLIST written from §6's list + FACTS §10 (MMIO ranges with handler class and OLD observable per range; VU `RegisterHwWindow` ×4 + the micro-store epoch hook; `BoundarySequencer` slot for `ChaseVif1Tail`/`FlushDeferredVif1` and the MTVU `openBoundary`+`drain`; Config keys `engine.mtvu`/`engine.mtvu_depth`/`engine.vu1_mode` → Settings copy; SIF1 `ProcessSifDma` + IPU0/1 bindings; `SectionSpec` ×8 with restore order; threads start/teardown order; `--vu1-library none`; the exact lane-gate commands `run_leg.py … --tree new --profile deterministic --frames <1500|600>` + `diff_legs.py --cross-engine --lanes audio,mem` per leg); README rows in the 5 owned dirs re-read for history-free wording. No new module code | none beyond re-checks; `OLD/vutrace_collect/` listing (`ls | wc -l` = 3293; `stat` sizes) | `SWEEP cases=3293 same=3293 diff=0 old_fail=0 new_fail=0` (G5); all of G1–G9 quoted; **Phase 5 module gate: PASSED** or the failing command as the handoff. **5H′:** if the sweep exceeds the stage (17 chunks × ≤15 min OLD+NEW bounded), STOP after the chunks completed so far with their `chunk_NN.log` lines quoted and hand the remaining chunks + the aggregate + the final record to **5H′** |

Stage rule reminders (STAGE_RULES + CONTRACT §4): each stage appends its §8.2 section + HANDOFF to `docs/phases/PHASE5_IMPL.md`
(files with class per file and OLD `file:lines`, flags with outcome, ctest names with result lines, oracle commands with quoted
output, `P5-*` findings, next command); the tree is GREEN at every stop (build 0 warnings, ctest 100 %, both lints, getenv gate) or
the new CMake registration is removed and the revert is stated; every OLD binary run goes through `verify/run_bounded.sh` with cwd
under `/tmp/p5/<stage>/` (never OLD's cwd; OLD `logs/` never written); helpers are FILES under `/tmp/p5/<stage>/`, never `python3 -`,
never multiprocessing, killed before the stage ends; no `getenv`, no session tags in code, no game constants in `engine/`; only the
append-only lines in the two shared CMake files; no commits (the user commits).

### 7.1 Findings ledger (recorded, never silently tolerated; ids `P5-<NAME>`, seeded at planning, filled during the stages)

| Id | Stage | Finding | Disposition |
|---|---|---|---|
| P5-GS-READONLY | plan | CONSTRUCTION §4.5 row 10 places `register_op_stream` under `engine/gs/hle/ir/`; `engine/gs/**` is READ-ONLY this phase (only `format/` may be depended on). | Written under OWNED `engine/gif/` (`gs_sink.h`, `register_op_stream`); the MOVE is a checklist row (G9). `GsSink` stays in `engine/gif/` permanently (the GIF owns what it hands over); only the recording sink may move. **Disposition at 5H:** closed-by-stage 5A (`gs_sink.h` + `register_op_stream` under `engine/gif/`); the MOVE to `engine/gs/hle/ir/` = INTEGRATION CHECKLIST (h)1. |
| P5-CORE-READONLY | plan | §4.5 row 9 puts the FROZEN `Vu1Case` POD + reader in `engine/core/diag/vutrace_case`; `engine/core/**` is READ-ONLY and the POD has no diag dependency. | `engine/vu/vu1_case.{h,cpp}` (reader; `WriteCase` kept as the frozen codec's other half, no capture logic); WRITER/collector = Phase 9 (§2.8); move = checklist row. **Disposition at 5H:** closed-by-stage 5B (`engine/vu/vu1_case.{h,cpp}` reader + `WriteCase`); MOVE to `engine/core/diag/vutrace_case` when the writer lands (Phase 9) = checklist (h)2. |
| P5-FAMILY-COUNT | plan | CONSTRUCTION §4.5 / ARCH §14.2 / `vu1_library/README.md` say "137 entries / 120 hashes"; FACTS §3.5 counts 140 `Register(` sites and 176 distinct variant hashes (120 only inside literal args; the `s_reg_zk_all` lambda alone registers 64 rows). | Pinned from the transplanted table at 5D″ (`entries=N distinct_variant_hashes=M`, expected 140/176); README corrected; the doc delta for CONSTRUCTION/ARCH is a checklist row (docs outside OWNED). **Disposition at 5H:** closed-by-stage 5D″ — pinned 223 rows / 176 distinct variant hashes / 12 program names / 15 program functions (`vu1_replay --registry`, `static_assert`); §6 G4 text corrected at 5H; CONSTRUCTION/ARCH "137/120" doc delta = checklist (h)4. |
| P5-VIF-ARMS-SYNTHETIC | plan | OLD `vif_replay`'s file mode needs a `mem.bin` RAM capture that does not exist in OLD; the 8 ctest arms are self-contained synthetic chains; 2 arms pin `PS2_VU1_EXEC_ASYNC=1` (DELETE). | 6 arms ported with env pins → CLI flags (`--depth`), exec-on arms dropped, timing-dependent counts excluded from the assertion (§4.2); file mode not ported. **Disposition at 5H:** closed-by-stage 5E/5G — 6 arms `VIFARMS arms=6 same=6 diff=0`; exec-on arms dropped; file mode not ported. |
| P5-SETTINGS-MIRROR | plan | The SETTING rows' Config keys ALREADY EXIST (`engine.mtvu`, `engine.mtvu_depth`, `engine.vu1_mode`, `config.h:56-59`) but Phase 5 may neither add keys nor wire `app/host`. | Module-local `MtvuSettings`/`VuSettings`/`VifUnpackSettings` with peak defaults (§3.5); the `Config → Settings` copy is a checklist row; `Hle` → `Cached` mapping fixed in §3.3. **Disposition at 5H:** closed-by-stage 5B/5E/5G (`VuSettings`, `VifSettings`/`VifUnpackSettings`, `MtvuSettings`, `GifParserSettings`); the `Config → Settings` copy = checklist (c) — carried-to-integration. |
| P5-ARBITER-SPLIT | plan | The brief schedules `gif_arbiter` in 5G, but `XgKick` (5B), `Vif` DIRECT (5E) and `ChainWalker` PATH3 (5F) all submit through `GifArbiter&`, and the harness path is the inline drain (FACTS R4). | `gif_arbiter` lands in 5A as its synchronous half (submit/pool/drain/FinishPath3); markers, inbox, boundary slots, `PendingBytes/Slots` and the drain thread complete it in 5G. One class, two stages, recorded in both `PHASE5_IMPL` sections. **Disposition at 5H:** closed-by-stage 5A (synchronous half) + 5G (markers/inbox/boundary/drain thread); one class, recorded in both sections. |
| P5-EEFEED-P3 | plan (FACTS §5.6) | `PS2_EEFEED` (`w10_eefeed.h:50`) is ledgered NORM in Phase 3 but both arms are Phase-5 code. | B2 (rowable row loop) written unconditionally in 5A `gif_parser`; B1 (staged MTVU submit) in 5F `chain_walker`; no ledger edit (read-only), noted for the ledger owner. **Disposition at 5H:** closed-by-stage 5A (B2 in `gif_parser`) + 5F (B1 staged seam in `chain_walker`) + 5G (`DmacMtvuFeed`); FLAG_LEDGER note for its owner = checklist (h)4. |
| P5-MTVU-P3 | plan (FACTS §5.6) | `PS2_MTVU` (`vu1_async.cpp:1011`) and `PS2_MTVU_DEFER` (`:1656`) are Phase-3 NORM rows consumed here. | `MtvuSettings::enabled`; deferred boundary delivery is the only MTVU-on path (`DeferOn` ≡ enabled) — 5G. **Disposition at 5H:** closed-by-stage 5G (`MtvuSettings::enabled`; `DeferOn ≡ worker.IsRunning()`). |
| P5-PERF-P3 | plan (FACTS §5.6) | `PS2_PERF` (10 sites, SETTING P3) announce latches sit inside vu/gif/dmac code. | Not written (announce lines die; census → `DiagRegistry` only if trivially shaped, else omitted). **Disposition at 5H:** closed-by-stage 5B/5G — `PS2_PERF` announce/census not written. |
| P5-MIPCEN-S26 | plan (FACTS §5.6) | `PS2_MIPCEN` (`gif.cpp:5043`, DIAG P2) and `PS2_S26_DECAL` (`vu.cpp:4346`, `gif.cpp:3686`, `dmac.cpp:1969`, DELETE P1) appear in Phase-5 files. | Omit / not written; no count change. **Disposition at 5H:** closed-by-stage 5A/5F — `PS2_MIPCEN` omitted, `PS2_S26_DECAL` not written. |
| P5-NO-GETENV-SITE | plan (FACTS §5.6) | `PS2_VU0_MODE`/`PS2_VU1_MODE` share one ternary `getenv` (`vu.cpp:726`); `PS2_VU_O3` is CMake-only; the 8 VU-AOT vars live outside the Phase-5 files. | All DELETE/CMake — nothing to write; `PS2_VU_O3` becomes the `ps2r_vu` compile-option decision (5B/5C). `PS2_VIF_FAST`'s test-side site (`tests/vif_replay.cpp:947`) → `--vif-fast`. **Disposition at 5H:** closed-by-stage 5B/5C/5E — nothing written; `PS2_VU_O3` NOT applied (`ps2r_vu` pinned `-O2 -fno-strict-aliasing -fwrapv`, allowed by §5.1); `--vif-fast` replaces the test pin; `getenv sites: 1` at every stage. |
| P5-SPR-STUB | plan (FACTS R2) | OLD SPR0/SPR1 DMA is a STUB (`dmac.cpp:1562-1576`, `CompleteChannel` only); a real copy would change RAM/SPAD vs OLD (`mem` lane). CONSTRUCTION §4.5's "SPR … transfers now exist" is not what OLD does. | Replicate the stub (5F, `DmacRegs`/`DmacDeferredVif1` pin it); BUG-KB candidate + accepted-difference candidate recorded in the checklist; never "fixed" in Phase 5. **Disposition at 5H:** closed-by-stage 5F (`DmacSprStub` pins the stub); BUG-KB candidate + accepted-difference candidate = checklist (j). |
| P5-DMAC-MASK | plan (FACTS R3) | Nothing in OLD sets `D_STAT` mask bits (`_EnableDmac` computes, never writes) → `CheckInterrupt()` is always false on a cold boot; handlers dispatch only after a savestate restores a non-zero mask. | `Dmac` keeps the observable (mask written only by `Write`-dropped globals = never, or by `SectionSpec` restore); `DmacHandlerRegistry` is mechanism only; BUG-KB candidate; the `SyscallTable` hand-off is a checklist row. **Disposition at 5H:** closed-by-stage 5F (`CheckInterrupt` false with mask 0 pinned in `DmacRegs`; `InterruptRaise` slot); `SyscallTable → DmacHandlerRegistry` hand-off = checklist (d)3-5; BUG-KB candidate (j). |
| P5-SYNC-FIRST | plan (FACTS R4) | The frozen baselines were recorded with `PS2_MTVU=0` and the GIF drain thread off; MTVU/drain code never runs on a harness leg. | Stage order 5A→5F lands the inline path; 5G lands the cross-thread half proven only by `vif_replay` + `tests/gif`; stated in `PHASE5_IMPL` 5G. **Disposition at 5H:** closed-by-stage 5G (deterministic-profile statement in 5G p1/p2: MTVU off ⇒ none of the cross-thread code runs on a harness leg). |
| P5-XGKICK-DEFER | plan (FACTS R5) | Only the paced XGKICK arm is alive in OLD; `TitleProfile.quirks.xgkick_defer=true` has no OLD consumer. | `XgKick(GifArbiter&, bool paced_drain)`; `false` = immediate full drain (`XgkickTransfer(0,true,false)` after arm) sharing `PacketSize`/submit code; harness certifies `true`; a unit test covers both arms. **Disposition at 5H:** closed-by-stage 5B (`XgKick(GifArbiter&, bool paced_drain)`, both arms in `VuXgKick`); `paced_drain ← TitleProfile.quirks.xgkick_defer` = checklist (c)/(e). |
| P5-DIRECT-TOKEN | plan (FACTS R7) | The `--hle` verdict's `direct=` token needs `gsdirect::PacketToOps` (Phase-6 header). | Tool-side port `tools/vu1_replay/gs_direct_ops_check` (pure GIF-tag semantics, 151 ln) at 5D; masking only via a filed row (§6 protocol). **Disposition at 5H:** closed-by-stage 5D (`tools/vu1_replay/gs_direct_ops_check` port; `direct=` token identical on 126 — nothing masked). |
| P5-FP-CONTRACT | plan (FACTS R8) | `VuClampBits1`/`CF` flush denormals (NEW `ps2_float_clamp` does not); OLD sets no explicit `-ffp-contract` (clang default `on`), `[FP-CONTRACT]` comments avoid `std::fma`. | `vu_float` owns VU clamp; `ps2r_vu` + `ps2r_title_vu1_library` compile with clang's default contraction; if G2/G3 mismatch on a float lane, `-ffp-contract=off` vs `on` is tried and the passing setting pinned + recorded. **Disposition at 5H:** closed-by-stage 5C/5D — clang default contraction, explicit `std::fmaf` on fused lanes, G2/G3 identical; no `-ffp-contract` pin needed. |
| P5-NEON-DEFAULT | plan (FACTS R9) | OLD picks lazy flags / SIMD lanes by `__ARM_NEON && __OPTIMIZE__`; the baselines came from an -O2 arm64 NEON build. | NEW keys on `PS2R_SIMD`, which is `option(PS2R_SIMD … OFF)` (root `CMakeLists.txt:15`; Phase 5 may not edit it): the `macos-dev` preset leaves it OFF, only `macos-diag` sets it ON (`CMakePresets.json:27`). Consequence: every G2–G7 gate under `macos-dev` exercises the SCALAR lanes (eager MAC pack, scalar strip3, scalar UNPACK); the NEON twins are compiled and property-tested (`VuLazyFlagsAlgebra`, `GifParserEquivalence`, `VifUnpackBulkEquivalence`) under a bounded `-DPS2R_SIMD=ON` configure (`verify/run_bounded.sh 900 <log> -- cmake -S . -B /tmp/p5/<stage>/build-simd -G Ninja -DPS2R_SIMD=ON` + build + the three ctest rows), quoted once at 5B/5E and in 5H's G1/G7 evidence. Scalar twins always compiled. **Disposition at 5H:** closed-by-stage 5A/5B/5C/5E — bounded `-DPS2R_SIMD=ON` twins quoted (IMPL `:245-250`, `:380-382`, `:527-530`, `:659-660`, `:794-795`, `:1684`, `:1692-1694`; the 5E p1 twin `:1571-1573`); listed once in the 5H G7 row. |
| P5-REGLIST-ADC-TEXCLUT | plan (FACTS R10) | OLD REGLIST XYZ2 kicks ignore ADC (`draw = reg==XYZ2`) and TEXCLUT (0x1C) has no dispatcher case. | Replicated in `gif_parser` (5A, pinned by `GifParserEquivalence`); both BUG-KB candidates in the checklist. **Disposition at 5H:** closed-by-stage 5A (`texclut_dropped`, `reglist_xyz_adc_ignored` cases; P5-5A-DROP-TABLE); BUG-KB candidates = checklist (j). |
| P5-VIF-MMIO-OBSERVABLE | plan (FACTS R11) | OLD routes only VIF FBRST/ERR (+ VIF1_STAT busy view, + the `0x10005000` FIFO window); other VIF register reads return 0 and ROW/COL are unreachable (`offset & 0xFF`). | `Vif::Read/Write` implement the full register file; the ROUTER must preserve OLD's observable (which offsets are routed) — checklist row with the exact ranges. **Disposition at 5H:** closed-by-stage 5E (full register file in `Vif::Read/Write`); the router's routed-offset observable = checklist (a) — carried-to-integration. |
| P5-SAVESTATE-UNOWNED | plan (FACTS R12) | `TAG_VU0C/VU0D/VU1C/VU1D/DMAC/VIFS/GIFS/VU1R` have no owner in NEW; anchors cannot restore VU/VIF/GIF/DMAC state until registered. | Modules expose `SectionSpec` factories with the OLD layouts (FACTS §3.8); registration + restore order = checklist row; `RestoreMhashAnchors` re-run is the integration check. **Disposition at 5H:** PARTIAL — only `Dmac::SavestateSection()` (`TAG_DMAC`, `DmacSectionRoundTrip`) exists; NO `SectionSpec` factory for VIFS/GIFS/VU0C/VU0D/VU1C/VU1D/VU1R (P5-5H-SECTIONSPEC-GAP); the 7 raw layouts are OWNED-path work (follow-up owned stage 5H′, checklist (f)); only the registration + restore order is carried-to-integration; the MODULE GATE SUMMARY names this DoD clause PARTIAL (1 of 8). |
| P5-LINT-POD-HEADERS | plan (FACTS R13) | Types-only headers (`vu1_hle_contract.h`, `vif_regs.h`, `dmac_channel.h`, `vu1_state.h`, `vu1_family_signature.h`) must satisfy `lint_one_class`. | Resolved in §2: zero primary types = lint-clean (`lint_one_class.py:17`); verified by running the lint at 5A/5B/5E/5F. **Disposition at 5H:** closed-by-stage (every stage) — `LintOneClass … 0 findings -> PASS`. |
| P5-FAMILY-CONST-REVIEW | plan (FACTS R14) | `0x1720`, `0x70`, `0x90`, `0xA0` are 4/2-digit literals the purity regex does not catch. | G8's grep over `engine/`+`tools/` is the review gate, run at every stage from 5D on and quoted at 5H. **Disposition at 5H:** closed-by-stage 5D (G8 grep 0; ctest `Vu1LibraryPurity`), re-quoted at 5H. |
| P5-VU1MODE-HLE | plan (FACTS Q1) | `engine.vu1_mode` has an `Hle` enumerator; OLD has no HLE-only tier (a DECLINE always needs a fallback). | Fixed in §3.3: `VuTierMode {Cached, Interp}` is the fallback tier, `hle_enabled` separate; `Hle` → `Cached` at the Config copy (checklist row). **Disposition at 5H:** closed-by-stage 5C (`VuTierMode {Cached, Interp}` + `hle_enabled`); `Hle → Cached` copy = checklist (c); see P5-5H-HLE-DEFAULT-FALSE. |
| P5-SYNC-SITES | plan (FACTS Q2) | `MtvuSync` sites' callers are gs_hle/sif_hle code (Phase 6/7). | Class + `SyncSite` enum written in 5G (`vif_replay` needs `Sync(Other)`); callers = checklist rows per site with the OLD anchors (`gs_hle.cpp:446-494, :768-802, :910-978, :1023`; `sif_hle.cpp:225`). **Disposition at 5H:** closed-by-stage 5G (`SyncSite` enum, OLD ids 0..11); the 12 callers = checklist (g) — carried-to-integration. |
| P5-VU1CASE-WRITER | plan (FACTS Q3) | `Vu1Case` writer/collector (`MaybeCapture`, DIAG) — this phase or later? | Reader only (tools need it); writer + LOCKED trace-line format = Phase 9 (§2.8). **Disposition at 5H:** closed-by-stage 5B (reader only; P5-5B-NO-TRACE-SINK); writer = Phase 9. |
| P5-LIBVU0-ENGINE | plan (FACTS Q4) | VU0 pc 0xF0 libsin/cos fingerprint HLE (`vu.cpp:2570-2600, :5550-5594`) — engine or games? | Engine-side `engine/vu/vu0/libvu0_sincos` (keyed by 3 SDK instruction words, no game address); lint-clean; `Vu0ShadowReplay`/`Vu0VisMicro` exercise the VU0 path. **Disposition at 5H:** closed-by-stage 5B (`engine/vu/vu0/lib_vu0_sin_cos`, `LibVu0SinCosPin`). |
| P5-5D-ENTRY-COUNT | 5D | OLD `Entries().size()` is 223 (140 `Register(` sites, 176 distinct variant hashes); §1.2 / §6 G4's "entries=140" conflated sites with rows. | Pinned 223 / 176 / 140 (`vu1_replay --registry`, `static_assert`, `vu1_library/README.md`); G4 expects `entries=223 distinct_variant_hashes=176`. **Disposition at 5H:** closed-by-stage 5D (disposition written by 5D; unchanged at 5H). |
| P5-5D-TABLE-ORDER | 5D | Registration order = textual static-init order and is observable (variant `0x59b148e8392fd464`: skn `any` row precedes the crys `0x12a0` row). | ONE ordered table in `vu1_library.cpp` generated from `/tmp/p5/5D/registrations.tsv`; never sorted or split per family; later parts swap `DeclineStub` in place. **Disposition at 5H:** closed-by-stage 5D (disposition written by 5D; unchanged at 5H). |
| P5-5D-FULL-TABLE-AT-5D | 5D | Group A's gate cases include 3 crystal-table siblings; the catalog `hle` column names every row. | All 223 rows registered at 5D with `Vu1Library::DeclineStub` for groups B/C (`HLE FALLBACK` exit 1 until 5D′/5D″); `--catalog` diff-empty already. **Disposition at 5H:** closed-by-stage 5D (disposition written by 5D; unchanged at 5H). |
| P5-5D-BUILDER-TOOLSIDE | 5D | The library never uses `gsp::Builder` (kicks are lifted from the working copy by `KickAt`). | `GifPacketBuilder` kept under `engine/vu/hle` per §2.4; consumers = `VuGsPacketCodec` + tools. **Disposition at 5H:** closed-by-stage 5D (disposition written by 5D; unchanged at 5H). |
| P5-5D-TOPS-SEAM | 5D | `VuUnit` carries TOP/ITOP only; the runtime's `Vu1HleInput` needs TOPS/ITOPS and the EE-window store epoch. | `Vu1HleRuntime::SetVifTops` / `BumpExternalGeneration` seams (VIF 5E, EE window at integration); `ext_gen` starts 0. **Disposition at 5H:** closed-by-stage 5D/5E (`SetVifTops` wired by `Vif::SetTopsObserver`); `BumpExternalGeneration` from the EE VU1 micro window = checklist (a) — carried-to-integration. |
| P5-5D-HLE-DIRECT-FN | 5D | OLD's tool called the library `fn` directly (no runtime TU linked). | NEW tool identical; `Vu1HleRuntime` covered by ctest `Vu1HleMechanism` (hook path == direct call on a corpus case). **Disposition at 5H:** closed-by-stage 5D (disposition written by 5D; unchanged at 5H). |
| P5-5D-CATALOG-TIES | 5D | `--catalog` sorts with `std::sort` (unstable) over map order. | Reproduced OLD's tie order exactly (diff empty); `stable_sort` not needed. **Disposition at 5H:** closed-by-stage 5D (disposition written by 5D; unchanged at 5H). |
| P5-5D-CLASS-NAMES | 5D | §2.5's `*Programs` class names vs its file names under the lint's snake_case rule. | Classes `FamilyTristrip` / `TexturedTristrip` / `PoscolorStrip` / `LitTristrip` (+ `QwPtr` own pair). **Disposition at 5H:** closed-by-stage 5D (disposition written by 5D; unchanged at 5H). |
| P5-5D-CLIPNF-ENGINE | 5D | `ClipNf` was the one generic helper NEW lacked. | `VuFloat::ClipNf` (engine/vu, OWNED); every other library helper is game-convention → `games/`. **Disposition at 5H:** closed-by-stage 5D (disposition written by 5D; unchanged at 5H). |
| P5-5D-CRYS-COVERAGE | 5D″ | Group C's 21 captures prove `vs_crys_dual` on one image (`8980533e`, both subs + gated meshes) and `vs_crys_displace` only at 0x1400 (`2e48aa24`); the second dual row and the 0x12a0 / 0x15a0 displace entries are transcribed, corpus-unproven (as in OLD). | Recorded in the library README oracle column; a future capture of those entries goes through `hle_vs_old.py`. **Disposition at 5H:** closed-by-stage 5D (disposition written by 5D; unchanged at 5H). |
| P5-5D-G8-GENERIC-HEX | 5D″ | §6 G8's raw `0x[0-9a-f]{16}` grep hits 4 generic constants outside the library (FNV-1a offset basis in `engine/core/hash`, libc byte masks in `engine/ee/hle/libc.cpp`) — none is title data. | `tests/vu/check_vu1_library_purity.py` (ctest `Vu1LibraryPurity`) matches the library's OWN 207 hash literals + the family literals + the `vs_*` stamps over `engine/` + `tools/` → 0 hits. **Disposition at 5H:** closed-by-stage 5D (disposition written by 5D; unchanged at 5H). |
| P5-5D-REGISTRY-REPORT | 5D″ | The pinned counts are 223 rows / 176 distinct variant hashes / 12 program names / 15 program functions (the plan's 140 = OLD `Register(` sites). | `vu1_replay --registry` prints the pinned first line unchanged, then one row per program (`entries= distinct_hashes= entry_pcs=`) and `REGISTRY programs=12`; README counts generated from it. **Disposition at 5H:** closed-by-stage 5D (disposition written by 5D; unchanged at 5H). |
| P5-5D-VU0-CENSUS-NO-CALLER | 5D″ | `Vu0Registry` (mechanism) + `Vu0Programs` (15 entries + 1 subroutine) have no NEW consumer this phase. | Unit-tested (`Vu0RegistryCensus`); the interpreter tier's entry hook → `NoteEntry` → `PS2R_LOG(VU, …)` is a 5H integration row. **Disposition at 5H:** closed-by-stage 5D (mechanism + `Vu0RegistryCensus`); the `NoteEntry` caller = checklist (e) — carried-to-integration. |
| P5-5D-CTEST-ROWS | 5D″ | OLD's `Vu1Hle_<case>` ×126 rows are subsumed by ONE OLD-identity row. | ctest `Vu1ReplayHleCorpus` (`hle_vs_old.py`, 126/126 line-identical, SKIP 77 without OLD) + `Vu1HleGroupA/B/C` (PASS gates without OLD) + `Vu1HleRef_<first 8>` + `Vu1CatalogIdentity`. **Disposition at 5H:** closed-by-stage 5D (disposition written by 5D; unchanged at 5H). |
| (stage rows below) | 5A–5H | one row per finding, copied at 5H from each stage's `### Findings` table in `PHASE5_IMPL.md` (99 stage findings + the 5H rows); dispositions as written by the stage | closed-by-stage unless the row says carried-to-integration / BUG-KB candidate |
| P5-GIF-TAG1-NREG16 | 5A | `gif_regs.cpp` TAG1 — OLD composes TAG1 from the POST-Parse nregs; 16<<28 leaves the 32-bit word and reads 0 — accidentally the hardware encoding. After RST the tag reads nregs 0 (not 16). | Carried verbatim; pinned in `GifRegsStat`. |
| P5-GIF-APATH3 | 5A | `gif_regs.cpp` STAT — APATH reads 3 whenever a tag is in transfer regardless of the real path; P1Q/P2Q/P3Q never set; FQC always 0 (nothing fills the FIFO). | Carried verbatim; pinned. |
| P5-GIF-MASK-ALLPATHS | 5A | `GifRegs::Path3Masked()` — MODE.M3R blocks parsing of every path in OLD, not just PATH3. The register side is written; the parser (part 2) must consult `Path3Masked()` at the zero-copy / ProcessBuffer decision for all paths. | Register side done; parser rule → part 2. |
| P5-GIF-RST-TAIL | 5A | `GifRegs::Reset()` — OLD `Reset()` does not clear `dma_buffer`; NEW `GifRegs::Reset()` therefore does not reach the parser's buffered tail (the parser owns it). A CTRL.RST mid-tail leaves the tail for the next ReceiveData, as OLD. | Carried; part 2 must NOT clear the tail on RST. |
| P5-GIF-TRANSFER-OWNER | 5A | `gif_regs.h` — Design choice (FACTS §12.1 option A): `GifRegs` owns `GifTransferState`; the parser advances it through `Transfer()`. No back-pointer, no init-order hazard; eligibility bits stay parser-private so `sizeof(GifTag)==24` holds. | Recorded; part 2 codes against it. |
| P5-GIF-PACKED-FOG-FGE | 5A | `gs_sink.h` `PackedFog` comment — OLD PACKED FOG stores `f != 0` into the PRIM fog-enable bool (FACTS §6). Documented on the method; Phase 6 `GsState` must reproduce it for identity. | Accepted-difference candidate for Phase 6/8; recorded. |
| P5-GIF-P3-STALE-RESET | 5A | (parser, part 2) — `gif.cpp:647-658` is a behaviour rule inside the "probes not written" range: a PATH3 packet arriving with `in_transfer` set resets the transfer and clears the tail. | Must be written in part 2 (FACTS §5.1 R2). |
| P5-5A-NLOOP0-LANES | 5A | `gif_parser.cpp` bulk branch vs generic — Q1 (NLOOP=0, EOP=0) is lane-dependent in OLD too: an all-known descriptor set reaches the bulk branch with `remaining == 0` → run 0 → the tag completes without eating a quadword; a non-bulk set eats the next quadword as one register write. The lanes-off leg therefore cannot be A/B-compared on the all-known Q1 case (`ops on=10 off=8`). | Bulk arm is the NORM; pinned single-leg (`nloop0_noeop_allknown`), the generic form A/B'd (`nloop0_noeop_generic`). README row. |
| P5-5A-LANE-ENTRY-ALIGNMENT | 5A | `ConsumePackedBulk` — The lanes enter only when `reg == 0` at the START of the run; a mid-vertex / mid-row split rides the per-descriptor remainder for the entire run of that call even after it re-aligns (`2_split_non16` triples=0, `9b` rows=1). Sink sequence identical; only the census differs. Carried verbatim (a lane re-entry inside a run would be a perf-only change). | Recorded; pinned by the census assertions. |
| P5-5A-DROP-TABLE | 5A | `RegisterIsForwarded` — The "register has an OLD effect" set is parser knowledge (FACTS §7.1 recommended form): Phase 6 `GsState::WriteRegister` never receives 0x0B/0x0E/0x0F/0x1C/undefined numbers or the four vertex registers. If Phase 6 prefers a GS-side no-op default, the `texclut_dropped` pin must move to the GS test. | Decision recorded; README row. |
| P5-5A-ARBITER-CTOR-INJECT | 5A | `gif_arbiter.h` — `GifArbiter(GifParser&)` with a no-argument `drain()` (FACTS §9.7 / part-1 HANDOFF) rather than `drain(GifParser&)`: the 5F/5G/Phase-6 callers (`dmac`, `gs_hle`, `sif_hle`, `vu1_async` in OLD) call `drain()` without a parser argument. | Recorded; 5G extends the same class. |
| P5-5A-TEST-RIG-NAMES | 5A | `tests/gif/test_*.cpp` — `lint_one_class` applies the file-name rule to test rigs with member functions; the rigs are `TestGifParser` / `TestGifArbiter` (snake_case prefix of the file name; precedent `TestBoundary` in `tests/ee/test_boundary.cpp`). | Pattern for 5B–5H test files. |
| P5-5A-SIMD-PROOF-SHAPE | 5A | `GifParserEquivalence` — The NEON proof is "same binary: NEON lane vs scalar generic path → identical ops/hash" plus "same case lines across the scalar and SIMD builds" (diff of the two logs). `macos-dev` never compiles the NEON body (objdump: 0 vector instructions); the bounded `-DPS2R_SIMD=ON` configure is the only place it runs. | Quoted above; 5B/5E reuse the same run for their NEON twins (plan §7.1 P5-NEON-DEFAULT). |
| P5-5B-FILENAME-LINT | 5B | `engine/vu/` — `lint_one_class` names files after the primary type: `decode.h` → `vu_decoder.h`, `chop_round_guard` → `vu_chop_round_guard`, `span_classify` → `span_classifier` (5A precedent P5-5A-TEST-RIG-NAMES). Plan §2.2 rows keep their logical names. | Renamed; README maps them. |
| P5-5B-VU0STATE-ALIAS | 5B | `vu1_state.h` — `Vu0State` is `using Vu0State = Vu1State` with `mem_mask = kVu0MemMask` (FACTS §16.2): one record type, one file, `Vu1Case` copies 16 KB straight in; the VU0 instance never reaches the upper 12 KB. | Recorded; `VuUnit(id)` (part 2) sets the mask. |
| P5-5B-KIND-COUNT | 5B | `vu_decoder.h` — FACTS §1.1 captions the upper kinds "29 values, 0..28" but lists 28 (`VUK_U_ITOF0 == 27`); the list order is carried verbatim. | Pinned in `VuDecodeTable`. |
| P5-5B-RSQ-NOT-GENERIC | 5B | `vu_float.h` — `Rsq` (plan §2.2 row) is library-specific (ITOF0→MULAw→MADDw→FTOI0 over a constant row, FACTS §7.6) — not written in `VuFloat`; its primitives (`Itof0`, `ClampFloat`, `Ftoi0`, fmaf) are. | 5D writes it in `library_helpers`. |
| P5-5B-FTOI-TWO-FORMS | 5B | `vu_float.h` — `Ftoi0` clamps the source (library form, FACTS §7.6); the interpreter's FTOI0/4/12/15 use the RAW source (FACTS §11.4). Equal for scale 1; `Ftoi4/12/15` carry the raw form, `FtoiSat(float)` is the shared saturation. | Both pinned in `VuFloatRules`. |
| P5-5B-MICROGEN-PLAIN | 5B | `vu1_state.h` — `micro_generation` is a plain `uint64_t` in the POD (single writer under the deterministic profile); if 5G's MTVU worker needs atomicity, `VuUnit` wraps the bump. | Part 2 / 5G decide. |
| P5-5B-CYCLE-OWNER | 5B | `flag_pipeline.h` — `vu_cycle` lives in `FlagPipeline` (FACTS §16.3 option 1); `StallModel` takes `FlagPipeline&`, `XgKick` a `uint64_t&`, `Vu1Case` replay goes through `SetCycle`. | Recorded; the interp tier uses `flags().cycle()`. |
| P5-5B-ONE-STALL-BODY | 5B | `stall_model.inl` — Spec/Fast/Classed are ONE template body (`ApplyBody<M>`); `kDynamic` adds the run-time guards and the branch/EFU/FDIV waits. FACTS §5.3's unrolled push order (wr_reg[0] under WR1\|WR2, wr_reg[1] under WR2) is kept literally. | Pinned by `VuStallModel` (classed ≡ dynamic). |
| P5-5B-XGK-CANCEL-KEEPS-STAGED | 5B | `xg_kick.cpp` — A tag walk reaching 0x4000 AFTER earlier tags were staged still submits the staged bytes (OLD order: cancel breaks the loop, the submit block runs). | Kept; pinned in `VuXgKick`. |
| P5-5B-FLUSH-RESETS-BOUNDS | 5B | `stall_model.cpp` — `FlushFmacWb` zeroes `min_retire`/`regs` (OLD left them stale); unobservable (`n == 0` guards every use) — FACTS §5.5 sanctioned. | Recorded. |
| P5-5B-XGK-TOPS | 5B | `vu_unit.cpp` — `SaveCase` writes `vif1_tops = vif1_top`, `vif1_itops = vif1_itop` (the unit holds two words); XITOP reads ITOPS per FACTS §11.6 — the interp must read `vif_itop()` for both until the VIF hands distinct values (5E). | Part 3 / 5E decide. |
| P5-5B-SINCOS-PAIRING | 5B | `lib_vu0_sin_cos.cpp` — `std::sin(a); std::cos(a)` back to back is paired by clang into one `sincosf`; its huge-argument reduction differs in the last bit from a lone `cosf` (seen at MAX_FLOAT: `3f5a5f96` paired vs a different lone value). OLD has the same shape (`vu.cpp:5560-5563`), so the pairing IS the reference behaviour; a constant-folded `std::sin(0.5f)` (round-to-nearest) also differs from the runtime FE_TOWARDZERO call. | Kept as written; the pin test mirrors the call shape and uses volatile sources; recorded as an R-EFU sub-risk (host libm + compiler pairing). |
| P5-5B-XITOP-ITOPS | 5B | `vu_unit.cpp` — `LoadCase` fed `vif_itop_` from `vif1_itop`; XITOP reads ITOPS (FACTS §11.6 KEEP-DIVERGENCE) → replay would diverge on cases where the two differ. | Fixed: `LoadCase` reads `vif1_itops`; `SaveCase` still writes both words from the one value (the VIF hands distinct values in 5E). |
| P5-5B-DISCARD-LOWER-SIDE-EFFECTS | 5B | `vu_interpreter_tier.cpp` — `discard_lower` skips the WHOLE lower op (LQI's `VI++`, a load's address side effects) — hardware would still bump VI. | Replicated (pinned in `VuInterpSmoke`); BUG-KB candidate. |
| P5-5B-DELAYSLOT-CONTROL | 5B | `vu_interpreter_tier.cpp` — A branch or E bit inside a delay slot is ignored; an E bit on a taken-branch bundle wins over the branch; TPC is not set on a redirect. | Replicated (pinned); BUG-KB candidates (with R-VU0-SIZE, NOP sled, XTOP yield). |
| P5-5B-NO-TRACE-SINK | 5B | `vu_interpreter_tier.h` — The brief's "trace sink optional" `RunOptions` field is not written: `PS2_VUTRACE` is DIAG and no diag consumer exists this phase. | `RunOptions{cycle_limit}` only; a sink lands with `core/diag` wiring (Phase 9 writer). |
| P5-5B-STALE-TWIN-RACE | 5B | `/tmp/p5/5B/build-simd` — A SIMD twin build started while `VuUnit`'s ctor changed failed to link (stale `.a`); re-run on the stable tree: 15/15. | Process note: run the twin AFTER the last edit. |
| P5-5C-STALLMODEL-SURFACE | 5C | `StallModel::ApplyPrunePush` / `MaterializeFmac` added (FACTS §0.6 / §A.6.3 gap 1). | resolved |
| P5-5C-INTERP-FRIEND | 5C | the GENERIC fallbacks reached through `friend class VuCachedTier` (gap 2); no public wrapper. | resolved |
| P5-5C-MATERIALIZE-K3 | 5C | `MaterializeFmac` rebuilds from the last 3 slots (OLD `kmax = min(executed, 3)`); a writer 4 back (retire `cycle + 1`) is not re-created. It is dead for every consumer (pruned at the next bundle) EXCEPT a program-end drain immediately after a cap cut inside a span whose last 3 bundles write nothing — then the drained clock could be 1 lower than the interpreter's. Not observed (both `w14bkstp_*` cap cases and the 15 backstop legs have identical clocks); left as OLD so the cached tier stays OLD-identical; revisit only if `--compare-cycle` ever flags it. | recorded, OLD-faithful |
| P5-5C-TESTS-MERGED | 5C | the 5B HANDOFF's "cached legs of Vu0ShadowReplay / Vu0VisMicro / Vu0DecalEval + lazyflags PART 2 mode loop" are covered by ONE suite (`VuCachedVsInterpCorpus`: the three fixture images from their entries + 126 corpus cases, both MAC rings via the SIMD twin) instead of edits to four suites. | recorded |
| P5-5C-HLE-DEFAULT | 5C | `VuSettings::hle_enabled = false` this stage (FACTS §B.1 says `true` = the `PS2_VU1_HLE` winner); 5D flips it when it installs `Vu1HleRuntime::TryExecute` — until then a `true` default would be a no-op (null hook) but would misstate the shipped configuration. | recorded |
| P5-5C-EE-FLAKE | 5C | see Oracle / gate status. | recorded |
| P5-5C-SPLIT | 5C | part 1 = tiers + dispatch + the two suites; part 2 = `tools/vu1_replay` (+ `XgkCaptureSink`, `Vu1ReplaySelfTest`, `Vu1CachedCompare_*` rows, `compare_vs_old.py`, `gsops_seed.txt`). | recorded |
| P5-5C-FABRICATE-TPC | 5C | FACTS §C.4 says the fabricated case ends at `TPC 0x30` like `Vu1TransformGroundTruth` variant A; the fabricated program has SEVEN bundles (the `IADDIU vi1,vi0,5` slot precedes NOP[E]) so the E-bit delay bundle is at 0x30 and TPC = 0x38. Both OLD and NEW agree (`--compare --fabricate` PASS on both); the selftest pins 0x38. | FACTS correction |
| P5-5C-ARBITER-OBSERVER | 5C | `GifArbiter::SetSubmitObserver` (function pointer + user, null default) is the byte tap OLD had as a test shim on `submit`; chosen over a parser-side sink because 5D's `--hle` / `--dump-kicks` decode the raw PATH1 packet bytes (`GsPacketDiff`). Engine cost when unset: one null check per submit. | surface added, OWNED |
| P5-5C-COMPARE-CYCLE-XGK | 5C | OLD's `--compare-cycle` also compared `xgk_staging_inserts` (a PERF counter with no NEW twin); NEW compares `vu_cycle` and the kicked `RegisterOpStream` count + hash instead (a stronger GS-visible check). The driver never passes `--compare-cycle`, so no OLD line is affected. | substitution, recorded |
| P5-5C-SELFTEST-REDEFINED | 5C | OLD's selftest asserted a VUT trace file (DIAG, not transplanted); NEW asserts round-trip + replay determinism + tier identity + the transform ground truth. Same exit contract; the text is never diffed. | recorded |
| P5-5C-COMPARE-ROWS | 5C | PLAN row 5C names `Vu1CachedCompare_<case>` ×126 ctest rows; this tree registers ONE `Vu1ReplayCompareCorpus` row (the OLD-vs-NEW driver over all 126, which subsumes 126 NEW-only PASS checks) plus the part-1 `VuCachedVsInterpCorpus` (in-process 126/126). 126 separate rows would add ~250 process launches per `ctest` for no extra evidence; revisit if a per-case failure locator is wanted. | recorded |
| P5-5C-DUMPKICKS-PARTIAL | 5C | `--dump-kicks` prints the packet census only (no `image=` header, no GIF decode) until 5D lands `Vu1ImageAnalyzer` + `GsPacketDiff`; `--hle` / `--catalog` exit 2 with a stderr note. The driver never runs these arms. | recorded |
| P5-5C-BENCH-CENSUS | 5C | OLD's `--bench` printed per-image `CENSUS` PERF counters (DELETE); NEW prints `CENSUS entries=<table size>` only. Timing lines are never diffed. | recorded |
| P5-5C-LINK-DUPLICATE | 5C | linking the tool to `ps2r_vu ps2r_gif ps2r_core ps2r_ee ps2r_gs_format` (as the row said) produced `ld: warning: ignoring duplicate libraries`; the tool links `ps2r_vu` alone (its PUBLIC links carry the rest). | recorded |
| P5-5C-DRIVER-COST | 5C | each bounded run costs ≥ 1 s of `run_bounded.sh` polling (`RB_POLL_S` is an integer); 126 × 2 compare runs + 16 × 4 postdata runs ≈ 6 min wall. The 5H sweep (3,293 cases) inherits this — batching per process is the lever. | recorded |
| P5-5D-CRYS-FAN-EARLY | 5D | `NearClipper::EmitCrysClipFan` (fan_kind 3) is written at 5D (self-contained, same switch); reachable only from the 5D″ crystal programs. | recorded |
| P5-5D-QWPTR-OUTLINE | 5D | `QwPtr::W/SetW/F` are out-of-line (the lint's header-body rule); bit-exactness is unaffected, per-call cost is a note for the integration perf pass (LTO or `.inl`). | recorded |
| P5-5D-GROUPB-COVERAGE | 5D | the 54 group-B captures exercise skn ONLY through the `0xcb0 → 0x1720 → 0x90` chain (`RunScalePhase` with the stale-w rule, `RunSkinCb0`, `RunSkinnedLitCall`), zk ONLY at entry 0 (classes A/B/C), spr ONLY at `0x1748`, idx both parameterizations. Transcribed but corpus-UNPROVEN here (as in OLD, which proved them from interpreter traces): `RunSkin1040`, `RunClipMark0B30`, `RunSkinEnv1aa0`, the skn `0x12a0` / `0x90` entries, `ZeroKick::RunCCont` (`0x1820`/`0x1ac8`), `RunACont` (`0x1750`), the spr `0x1980` re-entry, `DisplacePrefix:: RunDisplaceFloat`. They are exercised live at 5H; any divergence surfaces as an `HLE FALLBACK`/mismatch there, never silently (the runtime's `has_regs` install is all-or-nothing). | recorded |
| P5-5D-SKN-NEEDS-DISPLACE | 5D | `DisplacePrefix` landed in part 2 with skn; 5D″'s `vs_crys_displace` calls `SkinnedLitStrip::RunScalePhase` (0x12a0) / `DisplacePrefix::RunDisplaceInt` (0x1400) / `RunDisplaceFloat` (0x15a0). | closed |
| P5-5D-MAC-HELPERS-GAMES | 5D | `ApplyMac/PushMac/DivQ` are generic VU rules but live in `vu1lib` (games) because the engine exposes them only as `FlagPipeline` instance methods over `VUregs`; an engine `VuFloat::ApplyMacToStatus(u16&, u16&, u16)` static would let the library drop its copy — deferred (bit-identical either way). | recorded |
| P5-5D-ZK-READONLY-COPY | 5D | the zero-kick stub reads data memory through the 16 KB `WorkingCopy` instead of a const `QwPtr` view (a second `struct` in `qw_ptr.h` would be a second primary type); one memcpy per stub run. | recorded |
| P5-5D-DECLINESTUB-REMOVED | 5D | with every row bound, `Vu1Library::DeclineStub` is deleted (no decline-by-construction rows remain; declines are the programs' own unproven-shape verdicts). | recorded |
| P5-5E-FAST-DEFAULT | 5E | Engine default `fast_span = true` (NORM); the tool's no-flag arm must run the word path (OLD pinned `PS2_VIF_FAST=0`). | `VifSettings.fast_span` set by the tool from `--vif-fast`; not env, not Config. |
| P5-5E-RESET-SCOPE | 5E | OLD `Reset()` left latch/cursor/direct buffer/tte/phase; the tool's `reset_case` cleared them. | NEW `Reset()` clears everything (superset); FBRST.RST keeps OLD's narrower scope. README notes it. |
| P5-5E-FQC-DIV16 | 5E | `UpdateStat` FQC = `fifo.size()/16` over a quadword deque (16× under-report). | Carried verbatim (`TAG_VIFS` identity). |
| P5-5E-VIF0-MSCALF | 5E | VIF0 MSCALF/MSCNT no-ops; only MSCAL kicks VU0; VIF0 UNPACK ignores TOPS. | Carried verbatim; tested. BUG-KB candidate. |
| P5-5E-NUM-STATIC | 5E | `VifRegs.num` never updated (hardware counts down). | Carried. |
| P5-5E-UNKNOWN-CMD | 5E | Unknown VIFcodes ignored (no ER1/stall). | Carried. |
| P5-5E-ROWCOL-UNREACHABLE | 5E | OLD `Read/Write` masked `addr & 0xFF` → ROW/COL dead. | NEW: full file addressable (`kRegsSize 0x180`); the router observable stays OLD's (checklist row 1). |
| P5-5E-CLWL-FILL | 5E | CL<WL writes every cycle from the exhausted source (lookahead leak then zeros), no hardware ROW fill. | Carried verbatim; tested (`{0x77,0,0,0}`). BUG-KB candidate (EE UM 6.3.5). |
| P5-5E-VU0-MASKED-READ | 5E | OLD read `vu0_data_memory` (4 KB) unmasked at `dest_addr` up to 0x3FF0 (UB). | NEW reads through `ReadDataMem` (masked); tested. |
| P5-5E-ITOP-WORD | 5E | `VuUnit` carries (top, itops); `Vu1HleInput.itop` will hold ITOPS. | Unobservable (no library program reads it); `TopsObserver` hands `(tops, itops)` to `SetVifTops`. |
| P5-5E-CTXFOR-TYPE | 5E | PLAN §3.5 wrote `Vu1State& CtxFor`; the worker needs the unit (Execute), so `VuUnit& CtxFor(int)` + `BindVu1(VuUnit*)`. | Recorded; 5G may wrap it. |
| P5-5E-VPS-TRANSIENT | 5E | Mid-DIRECT STAT.VPS differs between feeds (word path Transfer, span path Idle) exactly as in OLD; identical once the stream ends. | Carried; the feed-equivalence test compares end states only. |
| P5-5E-TEST-LINT | 5E | `tests/**` is linted by LintOneClass: one primary type per test TU (`TestVifCommands`), streams as POD + free functions. | Applied. |
| P5-5E-ARMS-FRESH-RIG | 5E | OLD ran every leg on one static `g_vif` + `Reset()` and the global atomics; NEW builds a fresh rig per leg (units, arbiter, `Vif`). The observables (deltas, memory, end state) are unaffected; the census counters start at 0 per leg. | Recorded; identical output proven. |
| P5-5E-ARMS-REFERENCE | 5E | `/tmp/p5/5P/old_vif_arms.log` is a session artifact; the ctest row therefore passes `--refresh-old` and re-makes the reference from the frozen OLD binary bounded (≈4 s), SKIP 77 when it is absent. Hand runs default to the 5P log. | Recorded. |
| P5-5E-USAGE-TEXT | 5E | The usage text is NEW (the OLD line promised the file mode); exit code 2 is the same. No arm compares it. | Accepted (P5-VIF-ARMS-SYNTHETIC). |
| P5-5E-VU-MODE-LINE | 5E | OLD prints `[VU-MODE] active mode: cached` from `vu.cpp` static init; NEW does not. | Dropped by the driver's `SKIP_PREFIXES` (PLAN §2.9 row 1 precedent). |
| P5-5E-TOOL-LINK | 5E | The tool links `ps2r_vif` only; the brief's six-library list is satisfied transitively (PUBLIC links). Naming them again would emit ld duplicate-library warnings. | Recorded. |
| P5-5F-WRITE-PAGE-DROP | 5F | OLD `GetDmaChannelID` switches on `offset & 0xFF00` with cases `0x8000..0xD000` only; the inner `offset & 0x400/0x800` tests are dead, so MMIO writes to the IPU TO (`0xB400`), SIF1 (`0xC400`), SIF2 (`0xC800`) and SPR TO (`0xD400`) pages are DROPPED — those four channels can never start through the register file in OLD (`StartChannel` has no other caller; `ProcessSifDma`, the IPU TO arm and the SPR TO stub are unreachable). Reads still map them by range. | Replicated exactly (`Dmac::ChannelForWrite`), pinned by `DmacRegs`; the arms are kept as the PS2-rule bodies reachable through `StartChannel` (kernel HLE / library bindings). Bug-KB + accepted-difference candidate for the checklist; the FACTS §3.5 page-rule note is corrected by this row. |
| P5-5F-SUSPEND-DEAD | 5F | `SuspendedVifChain` never arms in OLD (§2); retry loops, CHCR force-complete and `MAX_RETRIES` unreachable. | Not written; observable identical. |
| P5-5F-RESET-UNCALLED | 5F | `DMAC::Reset()` (`enabler = 0x1201`) has no caller; cold-boot D_ENABLER reads 0. | No `Reset()`; ctor leaves everything 0; pinned by `DmacRegs`. |
| P5-5F-GIF-NORMAL-NO-CIS | 5F | GIF normal mode clears STR (via `Write`) but never sets CIS(2). | Replicated; pinned by `DmacChainWalk`. |
| P5-5F-WALK-GRAMMARS | 5F | GIF and VIF walks differ (terminator, REFS/SPR, TADR masking, TTE, tag-read address). | Two bodies transcribed; both pinned. |
| P5-5F-ENABLEDMAC-RETURN | 5F | Phase-3 `SyscallTable::ToggleMask(dmac_mask_)` sets mask bits and returns 1-then-0 where OLD's `_EnableDmac` returned 1 every call and never changed `stat`. | Checklist row for the Phase-3/integration owner: never mirror `DmacMask()` into `Dmac::globals().stat`. |
| P5-5F-INT1-SLOT | 5F | OLD never asserted INT1 from the DMAC (`CheckInterrupt` polled only at `sceSifSetDma`). | `InterruptRaise` slot fires from `CompleteChannel` iff CIS & CIM ≠ 0 — never on a cold boot; null in every harness leg. Integration decides whether to bind it (binding changes nothing while the mask is 0). |
| P5-5F-REKICK-DEAD-ARM | 5F | `StartChannel`'s VIF1 "re-kick" flush (`dmac.cpp:1383-1386`) is unreachable through the register file: `Write` flushes any parked kick BEFORE storing CHCR, so `deferred_vif1.active` is always false when `StartChannel(VIF1)` runs. The arm is reachable only by a direct `StartChannel` call (none in OLD), where it would park the POST-walk registers (STR already clear). | Kept verbatim (defensive, behaviour-neutral); not pinned by a test — `DmacDeferredVif1` pins the reachable order (TADR write flushes, CHCR parks, read flushes) and the same-TADR re-kick (snapshot = where the previous walk ended). |
| P5-5F-TAG-MIRROR-ID | 5F | The CHCR.TAG mirror copies bits 16-31 of tag word 0, which includes the ID field: after an END the channel's CHCR reads `0x7000xxxx` and the tail record stores that value and resumes with it (`chcr — STR` — the mirrored bits are overwritten by the next tag). | Observable kept; pinned by `DmacDeferredVif1` (record CHCR) and `DmacChainWalk` (final CHCR). |
| P5-5F-SPAD-TADR0 | 5F | A VIF1 chain whose TADR is scratchpad offset 0 (`0x70000000`) reads as TADR 0 through the 28-bit mask and takes the "VIF1 TADR 0" early return (STR clear, no CIS, nothing fed). | Reference behaviour; pinned by `DmacDeferredVif1`. |
| P5-5F-CHASE-GATE-VS-WALK | 5F | `IsSaneRewrite` accepts an address-bearing tag whose ADDR carries the `0x7000` prefix without the SPR bit, but the VIF walk then reads RAM at `addr & 0x01FFFFFF` (the walk honours the bit, not the prefix). | Two rules transcribed as-is; pinned by the accept table. |
| P5-5F-P3-STALE-RESET-VIA-DMA | 5F | A PATH3 IMAGE left mid-transfer by one GIF chain survives the drain's `FinishPath3` but the next GIF chain's `ReceiveData(Path3)` abandons it (5A's stale-reset rule) — a PATH3 IMAGE can only be completed by PATH1/PATH2 continuation. | Pinned by `DmacGifPath3`; no DMAC change. |
| P5-5F-M3R-DRAIN-ABANDON | 5F | With MODE.M3R set at drain time the PATH3 packet is held in the parser tail and then cleared by the drain's `FinishPath3` (the data is lost unless the mask lifts before the drain). | Pinned as the NEW-inline observable (5A parser rules); flagged for the 5G/6 owners to confirm against OLD's drain cadence once `GifRegs` is routed. |
| P5-5F-LIBDMA-ARENA | 5F | `sceDmaGetChan` hands out an invented descriptor arena at `0x01CFF000` (HACK_LEDGER libdma row). | Transcribed with the divergence stated in the header + README; bodies uninstalled; root fix = manifest-resolved symbol. |
| P5-5G-DRAIN-CTOR | 5G | The brief's `GifDrainThread(GifArbiter&, GifParser&)`: the parser reference has no use — the arbiter owns the parser and the loop calls `GifArbiter::DrainPass()`. | Ctor is `GifDrainThread(GifArbiter&)`; PLAN §2.1 row text to be aligned. |
| P5-5G-COLLECT-GATE | 5G | OLD promoted the inbox only while `vu1_async::IsRunning()`; with the drain thread up and the worker down (lazy MTVU start) submits reached the inbox but `drain()` never promoted them. | `DrainPass` runs `collectReady()` whenever `PendingSlots() != 0` (lock-free check; observably identical wherever OLD's gate held; the inline harness path never holds a slot). |
| P5-5G-PIPELINE-SEAM | 5G | `BackpressureIfNeeded` read nine `vu1_async` globals; the class must be proven before the worker exists. | `MtvuPipeline` abstract (10 methods) implemented by the worker in part 2; the fake in `VuMtvuBackpressure` advances fake time inside the waits. |
| P5-5G-FRAME-WAIT | 5G | OLD's frame-only wait is `sleep_for(500 µs)`; the tree forbids `sleep_for` in `engine`. | `MtvuPipeline::WaitFrameProgress(timeout_ns)` — the worker implements it as a timed `ps2r::Park::WaitFor` (timeout IS the OLD behaviour; the present thread may notify later). |
| P5-5G-GUARD-CLOCK | 5G | The 100 ms frame valve lived in the park loop (`frame_deadline`). | `RunaheadGuard::FramePark(declared, presented, engaged_at_ns)` owns it with the injected `MtvuClock`; `MtvuBackpressure` passes its engagement `t0`. |
| P5-5G-TSAN-TWIN | 5G | OLD `GifDrainThreadTsan` instrumented the header-inline arbiter inside the test TU; here the arbiter body is in `ps2r_gif`, so a per-test `-fsanitize=thread` row would instrument only the test. | Not added as a ctest row; a whole-tree TSAN configure is the honest twin (part 2 may run one bounded under `/tmp/p5/5G/build-tsan` and quote it). |
| P5-5G-LINT-QUOTE | 5G | `lint_one_class.py` treats C++14 digit separators (`1'000'000`) as a string quote, hiding the rest of the line from the primary-type scan (`FakeClock` in `test_runahead_guard.cpp` was not counted). | Tests nest their fakes inside the rig named after the file regardless; lint fix is a `tests/lint` row (read-only this phase). |
| P5-5G-RESUME | 5G | Part 2 was interrupted after the engine files, the tool arms and `test_mtvu_worker.cpp` were written (the test unregistered, `test_mtvu_sync.cpp` absent, no record). Inherited-code deviations found on resume: (a) `DmacMtvuFeed` existed, compiled and installed the seam but had no test and no README row; (b) the `--mtvu` ctest rows are named `VifMtvuSubmitEquivalence(Fast)` while the driver keeps the OLD arm names; (c) the worker test links `ps2r_vif`, which the `ps2r_vu_test()` helper cannot express (a second `ps2r_vu` link produced an `ld: duplicate libraries` warning → the row is written out longhand); (d) the worker's spin tier reads `ps2r::Park::NowNs()` directly (no injected clock) — a wall-clock upper bound only, never asserted. | (a) `DmacMtvuFeed` test + README rows added; (b) recorded (P5-5G-ARM-NAMES), names kept; (c) longhand rows for `VuMtvuWorker` / `VuMtvuSync`; (d) recorded, no change. |
| P5-5G-VIF-FEED-SEAM | 5G | PLAN §3.5 / FACTS §F.1 placed the worker in `engine/vif/` because it takes `Vif&`; `ps2r_vif` links `ps2r_vu` and the dispatcher hook + backpressure live in `engine/vu/tiers`. | `MtvuVifFeed` abstract in `engine/vu/tiers/`, implemented by `Vif`; the worker stays in `engine/vu/tiers/` with the rest of the MTVU classes; the tests / tool that need the real `Vif` link `ps2r_vif`. |
| P5-5G-ARM-NAMES | 5G | NEW ctest rows `VifMtvuSubmitEquivalence(Fast)` vs OLD / driver `VifReplayMtvuEquivalence(Fast)`. | Recorded; the driver (the oracle) keeps the OLD names, so the identity log reads as OLD's. PLAN §4.2 row text to be aligned at 5H. |
| P5-5G-ORACLE-GRAMMAR | 5G | The `--mtvu` arm's `[W13R3-E3]` / `[W10-EEFEED]` / `[R168-DEFER]` tokens look like session tags but are OLD output grammar the driver's line identity requires. | Kept in the tool's printf strings only; never in comments or identifiers (lint clean). |
| P5-5G-SYNC-SEAL-RACE | 5G | After `Sync` returns, whether the sealed markers are still awaiting collection depends on whether the last seal landed before or after the loop's last drain (both are correct: the stream is inline either way). | `VuMtvuSync` asserts the post-`TryEagerDrain` state (`PendingSlots() == 0`, ops == inline), not `hasSealedReady()` after `Sync`. |
| P5-5G-EESCHED-FLAKE | 5G | With two foreign `ctest` batteries + a harness leg running on the same host (the neighbouring `RecompSymbolCensus` took 231 s vs its usual tens of seconds), the read-only Phase-4 row `EeRunTokenAndEventQueue` (`tests/ee/test_ee_sched.cpp`, `sleep_for` / `yield`-paced) printed `FAIL change_priority_moves_to_tail` and then hung past its 120 s TIMEOUT in BOTH full runs (`/tmp/p5/5G/fin_ctest_full.log`, `fin_ctest_full2.log`, 150/151 each); it passes standalone 3/3, under ctest 3/3, with its neighbours 2/2 and `--repeat until-fail:5` 5/5. No `engine/ee` / `tests/ee` file is touched by 5G (`git status` clean there); part 1's full run had it at 0.07 s. | Recorded for the Phase-4 owner (a wall-clock-paced scheduler test: not fixable from this tree). The 5G rows are unaffected; the full battery must be re-run on an idle host before the phase gate. |
| P5-5E-NO-DRAIN | 5E | `Vif` never calls `GifArbiter::drain()` / `hasPending()` — after a DIRECT/DIRECTHL submit or an MSCAL→XGKICK the packets sit in the arbiter until the DMAC / boundary owner drains (OLD `vif.cpp` drained inline from the DMA path, not from the VIF). | recorded (HANDOFF `:1610`, `:1746`); the drain call belongs to `Dmac`/`BoundarySequencer` — carried-to-integration (checklist (b)). |
| P5-5E-MODE2-COL | 5E (review) | UNPACK MODE 2 carried verbatim as `+ COL[cycle&3]` (OLD); hardware MODE 2 is the "difference" mode (add ROW, then ROW := result — EE UM 6.3.5). Corpus-identical with OLD (6/6 arms). | recorded (5E REVIEW §4 (a)); BUG-KB candidate = checklist (j). |
| P5-5F-MEMCLR-SPR | 5F (review) | `LibdmaBindings::Memclr` / `ZeroGuest` store through `MainMemory::Pointer(Phys(a), w)` — every store OUTSIDE RAM (incl. a scratchpad `0x7000xxxx` destination) is DROPPED; OLD `memclr` went through `memory::write<T>`, which routes `0x7000xxxx` to the scratchpad. Zero observable while the bindings are uninstalled. | recorded (5F REVIEW §5.1); accepted-difference candidate alongside P5-SPR-STUB; the `Scratchpad` path is the fix when the manifest installs the row = checklist (d)6 / (j). |
| P5-5H-SECTIONSPEC-GAP | 5H | PLAN §6 DoD says "the module classes expose … `SectionSpec` factories"; only `Dmac::SavestateSection()` (`TAG_DMAC`) exists — no factory for `TAG_VIFS/GIFS/VU0C/VU0D/VU1C/VU1D/VU1R` (`grep SectionSpec engine/{gif,vu,vif}` → 0). The 7 layouts are raw copies (`VifRegs` 92 B ×2, `GifRegs` 13 B, VU memories, `VU1R` = the 1600-byte `CpuContext` image). | carried-to-integration — checklist (f) lists each tag with its OLD payload and restore order; 1 of 8 factories written — the 7 factories are OWNED-path work for a follow-up owned stage 5H′ (distinct from the `app/host` registration rows); the MODULE GATE SUMMARY states the DoD clause as PARTIAL. |
| P5-5H-HLE-DEFAULT-FALSE | 5H | `engine/vu/tiers/vu_tier_dispatch.h:29` keeps `bool hle_enabled = false;` although PLAN §5.1 row `PS2_VU1_HLE` (NORM) reads "`VuSettings::hle_enabled = true`"; P5-5C-HLE-DEFAULT said 5D would flip it and 5D did not. Every gate sets it explicitly (tools/tests), so no proof is affected; the composition root would inherit `false`. | left `false` in the header (the corpus tools rely on the explicit set); the Config copy MUST set `hle_enabled = (library != none)` — checklist (c) mandatory row. |
| P5-5H-CMAKE-TOOL-LINES | 5H | Root `CMakeLists.txt` carries 2 lines beyond CONTRACT §2 / PLAN §1.5's literal list: `add_subdirectory(tools/vu1_replay)` (5C) and `add_subdirectory(tools/vif_replay)` (5E) — 7 root + 4 test lines = 11 added, 0 removed, nothing else in either file. The tools are OWNED dirs; only the registration form exceeded the list (accepted by the 5C/5D/5G reviews). | recorded in the 5H G1 row; no change. |
| P5-5H-SWEEP-CTEST | 5H | `Vu1CollectSweep` needed exclusion from the default battery; `CMakePresets.json` (not OWNED) carries no label filter. | registered in `tests/vu/CMakeLists.txt` with `LABELS long TIMEOUT 18000 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE` and `DISABLED TRUE` unless `option(PS2R_VU1_SWEEP OFF)` is ON (OWNED-only; `ctest -N` → 152 rows, 151 enabled); the quoted G5 line is the hand run of the same script body over `/tmp/p5/5H/logs` (17 chunks) — the checked-in `sweep_vs_old.sh` + `aggregate_sweep.py` are the row's command. |
| P5-5H-VU0-DELETE-ROWS-UNNAMED | 5H | 5 of the 57 DELETE rows were never named by any stage's Flags section: `PS2_VU0_SHADOWGATE`, `PS2_VU0_VISCAP`, `PS2_VU0_VISCAP_MINVF`, `PS2_VU0_VISCAP_N`, `PS2_VU0_XCHECK` (`vu.cpp:4460-5041`, inside the probe forest 5B p3 dropped as a RANGE). | not written (verified at 5H: `grep -rn` of the 57 DELETE names over `engine/ tools/ games/` → 0 hits outside comments); tally stays 32/2/15/8/57. |
| P5-5H-SWEEP | 5H | The 3,293-case `vutrace_collect` sweep vs the frozen OLD tool (gate G5): `SWEEP cases=3293 same=3293 diff=0 old_fail=0 new_fail=0 chunks=17` (17:30:53 → 19:32:13 UTC, 17 chunks each `exit=0`, 0 DIFF lines). | closed-by-stage 5H — G5 ✔; no OLD-own failure, nothing masked; ctest `Vu1CollectSweep` registered (Disabled by default, P5-5H-SWEEP-CTEST). |
| P5-5H-EESCHED-LOAD | 5H | `EeRunTokenAndEventQueue` (Phase-4 row) timed out at 120 s under the sweep + `-j2` battery, then `Passed 0.07 sec` ×3 alone — the sixth sighting of P5-5G-EESCHED-FLAKE, never on Phase-5 code. | recorded for the Phase-4 owner (condition-variable wait or a larger TIMEOUT — outside OWNED); quoted in the 5H G1 row. |
| P5-5H-TSAN-NOT-RUN | 5H | The whole-tree TSAN configure of the MTVU/drain suites (P5-5G-TSAN-TWIN) was not run: the sweep held the host for the whole stage. | deferred — exact command in INTEGRATION CHECKLIST (k). |

## 8. Review notes

### 8.1 Self-review (part 3, 2026-09-27)

Verified against the sources while writing §6–§7 (read-only; no OLD binary run, no build):
- Every OLD anchor in §7's "OLD sources read" column is copied from a §2 row or FACTS §3 (all re-verified there with `grep -n` on
  2026-09-27); no new anchors were invented. Every file name in §7 appears in a §2 row with the same spelling (`gif_tag`, `gif_regs`,
  `gs_sink.h`, `register_op_stream`, `gif_parser`, `gif_arbiter`, `gif_drain_thread`; `vu1_state.h`, `vu_unit`, `decode`,
  `span_classify`, `flag_pipeline`, `stall_model`, `xgkick`, `vu_float`, `chop_round_guard`, `vu_image_hash`, `vu1_case`;
  `tiers/{interp_tier, cached_tier, tier_dispatch, mtvu_worker, mtvu_backpressure, mtvu_sync, runahead_guard}`; `hle/{vu1_hle_contract.h,
  vu1_family_signature.h, vu1_registry, vu1_image_analyzer, vu1_hle_runtime, kick_lowering, gif_packet_builder}`; `vu0/{libvu0_sincos,
  vu0_registry}`; the 13 library files of §2.5; `vif_regs.h`, `vif`, `vif_unpack`, `vif_fast_span`, `vif_unpack_bulk`; `dmac_channel.h`,
  `dmac`, `chain_walker`, `deferred_vif1`, `dmac_handlers`; the 4 tool files of §2.9). Every ctest name in §6/§7 is one of §4.3's.
- Gate numbers: 126 cases / 63 images / 61 covered / 2 uncovered (`0d20914e`, `50b7302d`) / 67 catalog lines / 3,293 sweep cases /
  17 chunks / 6-of-8 arms / 30 UNPKB cases / 140 sites / 176 hashes — all from FACTS §1.1, §3.5, §7.1–§7.3 and §1.2 above.
- Deviations from the brief, each with a §7.1 row: `gif_arbiter`'s synchronous half lands in 5A (P5-ARBITER-SPLIT — three earlier
  stages submit through it); `vif_replay`'s MTVU/runahead arms complete in 5G (their classes are 5G's) while 5E lands the UNPACK arms;
  the `gsops_seed.txt` corpus half of `GifParserEquivalence` is generated at 5C (first stage with the parser AND a running interp tier
  under the tool — §4.4 corrected inline to say 5C); `packet_template` is a recorded decision in 5A, not a file.
- Budget check: §7 has 8 stages × 1 pre-registered split (5D twice: 5D′ group B, 5D″ group C) = 18 possible stage sections; the
  heaviest rows (5B, 5D, 5G) are the ones most likely to split — their STOP points name a tested, green subset.
- Open items (not resolvable at planning): the exact `--mode … [postdata_out]` dump layout (read from `tests/vu1_replay.cpp` at 5C and
  reproduced byte-for-byte); whether clang's default `-ffp-contract` reproduces OLD's float lanes (P5-FP-CONTRACT decides at 5C/5D″);
  the pinned N/M (P5-FAMILY-COUNT at 5D″); whether any `vutrace_collect` case fails in OLD itself (`old_fail`, G5).
- Consistency pass over the whole file after appending: section numbering §1–§8 + HANDOFF contiguous; the Status header updated
  from "part 1 of 3" to complete; no session tag outside backticks (the only bracketed OLD tokens are inside quoted PASS lines and
  the `[GD1]` gate id, which is a CONSTRUCTION identifier, not a session tag); `wc -l` and the `## ` index printed in the part-3 report.

### 8.2 `PHASE5_IMPL.md` stage-section + HANDOFF template (CONTRACT §6, verbatim; one per stage incl. every split taken)

```
## 5<X> — <title> (<date>, ≈<min> min)
### Landed (files)            — paths + class per file + OLD source file:lines transplanted
### Flags applied             — NAME → outcome (NORM arm written / SETTING field / DIAG / not written)
### Tests added (ctest names) — result lines quoted
### Oracle / gate status      — commands + quoted output
### Findings (→ plan §7.1)    — P5-<NAME> rows
### HANDOFF (5<X> → 5<Y>)
**Done:** … **Not done (moved to 5<Y>):** … **Gate status:** … **Next command:** …
```

Filling rules: "Gate status" lists G1..G9 each ✔ / ✘ / – (not yet) with one evidence phrase; "Next command" starts with
`cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev` then the first OLD source / file
list of the next stage; the 5H section additionally carries the G1–G9 table and the INTEGRATION CHECKLIST (§6).

## HANDOFF (planning → stage 5A)

- **Done:** `docs/phases/PHASE5_PLAN.md` complete — §1 goal/scope + contract, §2 module table (65 rows + 4 explicit not-written,
  reconciled with CONSTRUCTION §4.5), §3 interfaces (`GsSink`, `RegisterOpStream`, tier ladder + `VuSettings`, ARCH §14.2 structs,
  MTVU seam + Settings), §4 oracle/test strategy (tool grammars verbatim, 6 arms, ported tests, `gsops_seed`, the 3,293 sweep),
  §5 the 114 flags by outcome, §6 gate G1–G9 + DoD + the integration-gate list + accepted-difference protocol, §7 stages 5A–5H with
  pre-registered splits (5A′…5H′, 5D′/5D″ by family group) + rule reminders, §7.1 ledger seeded with 27 rows (5 named + 1 ordering +
  6 FACTS §5.6 gaps + 14 FACTS §9 risks/questions), §8 self-review + the `PHASE5_IMPL` template. Facts read: `/tmp/p5/CONTRACT.md`,
  `/tmp/p5/5P/FACTS.md`, `PHASE4_PLAN.md` §6–§8/HANDOFF (format). No `/tmp/p5/5P/REVIEW.md` existed at part 3 — no review fixes to apply.
- **Not done:** no code, no CMake, no tests, no OLD binary run, no build (planning stage); the tree is unchanged except this file
  (`git status --porcelain` → `?? docs/phases/PHASE5_PLAN.md`); no commits; OLD read-only (FACTS greps only). `PHASE5_IMPL.md`
  does not exist yet — 5A creates it with its first section.
- **Next command (stage 5A):** `cmake --preset macos-dev && cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`
  (pre-flight: tree green at `78b5655`, expected 100 % incl. `CoreSingleGetenvSite`), then read OLD `gif.h:15-118, :120-202`,
  `gif.cpp:185-380, :716-735, :3183-3258, :3585-4324, :4374-4400, :4541-5224`, `gif_arbiter.h:85-112, :229-247`,
  `gif_arbiter.cpp:20-30, :579-783, :933-1808` and write `engine/gif/{gif_tag.h/.cpp, gif_regs.h/.cpp, gs_sink.h,
  register_op_stream.h/.cpp, gif_parser.h/.cpp, gif_arbiter.h/.cpp (inline half), CMakeLists.txt, README.md rows}` +
  `tests/gif/{CMakeLists.txt, counting_gs_sink.h/.cpp, test_gif_regs.cpp, test_gif_parser_equivalence.cpp}` per §2.1/§7 5A, append
  `add_subdirectory(engine/gif)   # Phase 5` after `add_subdirectory(engine/iop)` and `add_subdirectory(gif)` at the end of
  `tests/CMakeLists.txt`; expected first green line: `GifRegsStat ... Passed`; create `docs/phases/PHASE5_IMPL.md` with the 5A section.

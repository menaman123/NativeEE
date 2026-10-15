# FILEMAP — host_app shard 1 (36 files, 24,605 lines)

Shard 1 of 9. Source tree: `$PS2R_OLD_ROOT` (read-only).
Plan authorities used: ARCHITECTURE.md §3/§11/§13/§14–§18, MIGRATION_PLAN.md §1,
HOOK_RECLASSIFICATION.md, TRIAGE_CAPTURE.md, A2_CONFIG_DIAG.md (env registry),
A6_GAME_SPECIFICITY.md. Peak configuration = `scripts/run_play.sh` CANON
(PS2_GS_SCALE=2 PS2_GS_INTERP=1 PS2_FPS_CAP=60 PS2_MTVU=1 PS2_GL_FAST=1 PS2_GL_FAST2=1
PS2_VU1_HLE=1 PS2_VU1_HLE_DIRECT=1, plus PS2_VU_MODE=cached PS2_EE_TIM0=1 PS2_PERF=1 …):
everything it turns on is NORM; SCALE/INTERP/FPS_CAP are SETTINGs with those defaults.

Refactor-outcome legend: **NORM** (winning arm becomes the only code path, losing arm +
var deleted) / **SETTING** (Config field) / **DIAG** (core/diag or core/triage,
compile-time, default off) / **TEST-ONLY** (harness hook) / **DELETE** (both arms gone).
"A2 disp" = the registry's recommendation; deltas are called out where code evidence
overrides it.

---

### host_app/vu.cpp (12,475 lines)

- **Purpose**: The Vector Unit runtime — one `VU1` class instantiated twice (`g_vu0`,
  `g_vu1`) implementing VU0 macro-mode and VU1 micro-mode execution. Contains the
  interpreter tier (`RunUpper`/`RunSpecial2Upper`/`RunLower`, the full upper+lower
  instruction set), the cached tier (`DecodeProgram`/`ExecuteCached(Loop/Resume)` with
  superblock runs, stall-class specialization and span folding), the hardware flag
  pipelines (MAC/STATUS lazy ring, CLIP retire queue, Q/P/FDIV/EFU latches, FMAC/IALU
  read-stall rings), chop-rounding-mode enforcement (`VuChopRoundGuard`), XGKICK
  drain-time pacing (`XgkickTransfer`/`Op_XGKICK`/`XgkickPacketSize`), the program
  cache with hash-epoch elision, the VU1-HLE dispatch hook, and the vutrace
  trace/case-capture harness. It is also the dumping ground for ~10 one-off VU0/VU1
  investigation probes wrapped around `Execute()`.
- **Public surface**: `class VU1` (vu.h), globals `g_vu0`/`g_vu1`,
  `g_vu1_hle_try_execute` hook pointer (installed by hle/vu1_hle_runtime.cpp),
  `vuflags::g_lazy_on`/`vuring::g_runretire_on` (+`OverrideForTest`),
  `VuModeResolvedName()`, `VuBumpMicroEpoch()`, `VuBenchBundleCount()`,
  `VuBenchAotReport()`, `W15KickCensusEmitForBench()`, cutscene probe globals
  `g_cut_vu1_runs/g_cut_xgkick/g_cut_vu1_entry`, `g_pose_wb_count`. Included/called by
  vif.cpp (MSCAL/MSCNT), recompiled EE code (vcallms/CMSAR1), vu1_async.cpp (MTVU
  worker), gif_arbiter (XGKICK submits), memory.cpp (micro-epoch bumps), bench/main.
- **Classification**: GENERIC-PS2 (A6 §1: zero code-level game tokens; XGKICK
  pacing/deferral is unconditional generic behavior, title-tuned via
  TitleProfile.quirks). Subsystem: VU.
- **Refactor destination** (MIGRATION §1.3 vu.cpp row): split into
  - `engine/vu/vu_unit.{h,cpp}` — class `VuUnit` (register state, memory access,
    Reset, mode resolution as Config-driven member);
  - `engine/vu/flag_pipeline.{h,cpp}` — class `FlagPipeline` (MAC/STATUS lazy ring,
    CLIP retire queue, Q/P latches, ApplyMacToStatus/Materialize/Retire/Flush family);
  - `engine/vu/stall_model.{h,cpp}` — class `StallModel` (FMAC/IALU rings,
    ApplyFmacReadStalls + the decode-time specialized entries, absorbing
    `ApplyFmacPrunePush`);
  - `engine/vu/xgkick.{h,cpp}` — class `XgkickPacer` (drain-time copy, 2-cycle/QW
    pacing, packet-size walk, staging, arbiter submit);
  - `engine/vu/tiers/interp_tier.{h,cpp}` — class `VuInterpreterTier` (RunUpper/
    RunLower; the instrumented reference tier);
  - `engine/vu/tiers/cached_tier.{h,cpp}` — class `VuCachedTier` (DecodeProgram,
    ExecuteCached*, program cache, superblock/span machinery with winning arms
    inlined);
  - `engine/vu/vu_float.{h,cpp}` — the single-sourced PS2-float helpers (A6 §7 row A;
    ClampFloat/broadcast/mac-pack canon shared with the game VU1 library);
  - `engine/core/diag/vutrace.{h,cpp}` — the [VUTRACE] Config/EmitBundle/MaybeCapture
    harness (PS2R_DIAGNOSTICS builds; also the core/triage FallbackDetector producer
    per TRIAGE_CAPTURE §5.2).
  Duplicated decode/fast-exec bodies are deleted in favor of the header canon
  (A1 §4.2); every one-off probe bracket below is deleted, not moved.
- **Flags consumed here** (57 vars; lines are today's getenv sites):

  | var | default today | what it selects | A2 disp | refactor outcome |
  |---|---|---|---|---|
  | PS2_VU_MODE (:373) | unset→cached [M3A] | interp / cached / native(stub) tier | keep-as-setting | **SETTING** — Config `vu.mode`, default `cached`; interp stays as the instrumented reference tier ([GD1]) |
  | PS2_VU0_MODE / PS2_VU1_MODE (:726) | unset = no override | per-unit tier override (A/B hatch) | **NOT IN A2 REGISTRY (A2-GAP)** | **DELETE** — per-unit A/B lever concluded; one Config field suffices |
  | PS2_VU_FASTPACK (:443) | ON | 4-way interleaved FNV image hash vs legacy serial | delete-losing-arm | **NORM** — fast hash only |
  | PS2_VU_HASHEPOCH (:2474) | ON | micro-generation epoch elision of per-entry hashing | delete-losing-arm | **NORM** — epoch elision only |
  | PS2_VU_LAZYFLAGS (:873) | ON (NEON; forced eager under trace/scalar) | lazy vectorial MAC/STATUS ring vs eager pack-at-push | delete-losing-arm | **NORM** — lazy on NEON builds; eager remains only as the scalar-build/trace-tier arm, selected by build/diag mode not env |
  | PS2_VU_RUNRETIRE (:903) | ON (run_play revert list) | O(1) evict-on-full vs per-bundle retire scan | delete-losing-arm | **NORM** |
  | PS2_VU_SUPERBLOCK (:478) | ON | decode-time straight-line run fast path | delete-losing-arm | **NORM** |
  | PS2_VU_STALLSPEC (:497) | ON | decode-time stall-class specialization | delete-losing-arm | **NORM** |
  | PS2_VU_STALLFOLD (:510) | ON | never-stalls fold class | delete-losing-arm | **NORM** |
  | PS2_VU_SPANFOLD (:526) | ON | semi-flat span stall-dispatch elision | delete-losing-arm | **NORM** |
  | PS2_VU_SPANFOLD2 (:547) | OFF | second span body (pace hoist + bulk charge) | delete-both | **DELETE** — experiment; shipped span body stays |
  | PS2_VU_RUNFLAT (:575) | OFF | whole-region stall elision (measured wash, W13R6) | delete-both | **DELETE** — both arms; checker verdict recorded in-code |
  | PS2_VU_RUNFLAT_CENSUS (:585) | OFF | decode-end flat-coverage census | move-to-diag | **DELETE** — its subject (RUNFLAT) deletes; census has no referent (A2 delta noted) |
  | PS2_VU_PACE_NEXTDUE (:747) | ON | next-due algebra in XGKICK pace | delete-losing-arm | **NORM** |
  | PS2_VU_RUNPACE (:755) | ON | slim pace form in run body | delete-losing-arm | **NORM** |
  | PS2_VU_XGKPACE_QUANTUM (:738) | 32 | XGKICK pace quantum (cycles banked per drain visit) | delete-losing-arm | **NORM** — 32 becomes a named constant in XgkickPacer |
  | PS2_VU1_CYCLE_CAP (:458) | 1,000,000 | per-Execute bundle watchdog; raised only by the offline oracle | delete-both | **TEST-ONLY** — becomes a vu1_replay CLI option; live constant stays 1M (A2 delta: the oracle legitimately needs it) |
  | PS2_PERF (:751/:880/:905) | num-if-set (canon=1) | gate-announce printfs + pace census counters | keep-as-setting | **SETTING** (global `diag.perf`); the vu.cpp announce lines die with their gates, census → core/diag |
  | PS2_VUTRACE (:136/:875) | OFF | per-bundle VUT trace of VU1 (forces interp tier, forces eager flags) | move-to-diag | **DIAG** — core/diag/vutrace |
  | PS2_VUTRACE_FILE (:138) | vutrace_host.txt | trace output path | move-to-diag | **DIAG** |
  | PS2_VUTRACE_MAX (:140) | 200000 | bundle budget | move-to-diag | **DIAG** |
  | PS2_VUTRACE_CAPTURE (:142) | 0 | dump first N Execute entries as Vu1Case files | move-to-diag | **DIAG** (also the triage fallback-capture producer, D9) |
  | PS2_VUTRACE_PC (:144) | unset | restrict capture to one entry pc | move-to-diag | **DIAG** |
  | PS2_VUTRACE_DIR (:148) | ./vutrace_cases | case directory | move-to-diag | **DIAG** |
  | PS2_VUTRACE_MINFRAME (:150) | 0 | capture start frame | move-to-diag | **DIAG** |
  | PS2_VUTRACE_PER_IMAGE (:152) | 0 | N cases per distinct image (HLE harness collector) | move-to-diag | **DIAG** |
  | PS2_VUTRACE_IMG_GAP (:154) | 30 | frame spacing between per-image samples | move-to-diag | **DIAG** |
  | PS2_VUTRACE_NEWIMG (:246) | OFF | capture at decode, once per distinct image | move-to-diag | **DIAG** |
  | PS2_SHADOW8_KTAP (:1635/:1677/:11318) | OFF | XGKICK/data-mem shadow-strip tap (16 sites repo-wide) | move-to-diag | **DIAG** — but recommend fold into the triage GS-decline/gsdump path; session-scoped variant dies |
  | PS2_S13_PCRING (:109) | OFF | VU0 executed-pc ring | delete-both | **DELETE** |
  | PS2_S13_TBLCAP / _ENTRY / _N (:4612/:4616/:4621) | unset / 0x398 / 8 | VU0 table-builder pre/post capture | delete-both | **DELETE** ×3 |
  | PS2_S13_TBLWATCH (:1609/:4712) | OFF | VU0 data qw0-8 writer watch | delete-both | **DELETE** |
  | PS2_S15_HDRCAP / _ENTRY / _MINVF / _N (:4936-:4948) | unset/0/0/8 | arbitrary-entry VU1 pre/post capture | delete-both | **DELETE** ×4 |
  | PS2_S15_VUENT (:4913) | 0 | 3-frame VU1 entry census | delete-both | **DELETE** |
  | PS2_S26_DECAL (:4346) | OFF | decal claim-eval micro tap + histogram | delete-both | **DELETE** |
  | PS2_S26_FORCEPASS (:4381) | OFF | zero vf31.w cull verdict after decal micro (behavior-mutating A/B) | delete-both | **DELETE** |
  | PS2_S26_UTRACE (:10082) | OFF | FMAND mac-ring forensics inside FUN_001ad158 | delete-both | **DELETE** |
  | PS2_S26_VUDUMP (:4406) | OFF | one-shot VU0 code+data dump at decal call | delete-both | **DELETE** |
  | PS2_S8_FORCEVIS (:10993) | OFF | force VI6=0 at pc 0x17a0 (shadow builder cull open; behavior-mutating) | delete-both | **DELETE** |
  | PS2_S8_MTXCAP / _EERAM / _MINVF / _N (:4832-:4860) | unset | shadow-builder MSCAL 0x19e0 pre/post capture | delete-both | **DELETE** ×4 |
  | PS2_VU0_SHADOWGATE (:4460) | 0 | blob-shadow micro telemetry wrap (0xA08/0xB90) | delete-losing-arm | **DELETE** — default-OFF telemetry: the winning arm is off (A2 delta in name only) |
  | PS2_VU0_VISCAP / _MINVF / _N (:4548-:4555) | unset | full VU0 state capture at 0x1C0 | delete-losing-arm | **DELETE** ×3 — capture probe, superseded by triage/replay tooling |
  | PS2_VU0_XCHECK (:5041) | OFF | cached-vs-interp differential for every VU0 macro call | delete-both | **DELETE** — tier equivalence is owned by vu1_replay/corpus gates |
  | PS2_R198_XGKPROBE (:11408) | OFF | XGKICK arm-vs-submit packet mutation probe | delete-both | **DELETE** |
  | PS2_R198_XGK_WINDOW (:11445/:11449) | 0x2f0000:0x2f8000 | TEX0 watch window for the probe | delete-both | **DELETE** |
  | PS2_W15_KICKCENSUS (:3998) | OFF | kick-identity census bracket | delete-both | **DELETE** |

  Tally: 11 NORM, 2 SETTING, 11 DIAG, 1 TEST-ONLY, 32 DELETE.
- **Hacks / workarounds** (blunt list):
  1. **Probe forest wrapping `Execute()`** (vu.cpp:4330-5100): seven nested one-off
     capture/telemetry brackets (S26-VU0 :4346, SHADOW8-GATE :4460, S8-VISCAP :4548,
     S13-TBLCAP :4612, S13-TBLWATCH :4712, S8-MTXCAP :4832, S15-VUENT :4913,
     S15-HDRCAP :4936, VU0-XCHECK :5041) run BEFORE the mode dispatch of every VU
     entry, several re-entering `Execute()` recursively with thread-local latches.
     Correct implementation: none of this is a PS2 rule — delete; play-time capture
     belongs to `core/triage` (FallbackDetector/SavestateRing), offline capture to
     `vu1_replay`/`tools/triage lift`.
  2. **Behavior-mutating diagnostics**: [S26-FORCEPASS] zeroes vf31.w after the decal
     micro (:4381-4400); [S8-FORCEVIS] forces VI6=0 at a pinned pc/reg signature
     inside the interpreter MTIR case (:10993-11003). These are A/B levers that change
     guest-visible results. Correct: delete; the questions they answered are closed.
  3. **Scene/frame-window constants in engine code**: VuFlagTrace logs only frames
     1100-1180 (:840-869); SHADOW8-GATE only sgf>=2990 (:4520); S8-RECWATCH pins the
     shadow record regions (477+32)*16 / (632+32)*16 (:1638-1650); the r198 window
     defaults to the beach RT range 0x2f0000 (:11445). Game/scene knowledge inside
     engine — delete with the probes; any surviving trigger data is RendererProfile /
     TitleProfile territory (`lint_engine_purity` would fail these today).
  4. **Ungated hot-path logging**: qw2 WriteDataMem logging "UNGATED from process
     start" (:1697+), [VU1-MTIR] first-40 logging (:11005-11017), [VUFLAG] cutscene
     flag-read trace (:840). Correct: compile-time log categories (core/log), none in
     release builds.
  5. **Dual-path perf gates** (superblock/stallspec/stallfold/spanfold/runflat/
     fastpack/hashepoch/lazyflags/runretire/pace gates, :364-615, :738-916): every one
     keeps a verbatim legacy arm behind a static latch. Correct: Phase 4 gate purge —
     winning arm inlined, losing arm and latch deleted; equivalence pinned by the
     corpus COMPARE gates before each purge.
  6. **Duplicated stall-model code**: `ApplyFmacPrunePush` (:616-663) is two blocks
     hand-copied VERBATIM from vu_fast_exec.h:337-352/:398-412 with a comment
     inventorying the exclusions. Correct: one `StallModel` class with a decode-time
     specialized entry point; no textual twinning.
  7. **Mirror accessor of the mode gate**: `VuModeResolvedName()` (:418-433) re-derives
     `vumode::Resolve` minus the announce for bench reporting — a second copy of the
     same resolution logic. Correct: one resolution, one owner (Config), reports read
     the resolved state.
  8. **Probe state as inline header globals shared for gating**: `g_vu_ocean_active`
     ([OCEAN-ISO], set from ExecLowerFast's LQ body per :709-712 comment) exists so
     probes in vu.cpp fire from the AOT TU too. Correct: dies with the probes and the
     AOT tier.
  9. **Cutscene investigation globals**: `g_cut_vu1_runs/g_cut_xgkick/g_cut_vu1_entry`
     (:12400-12410), `g_pose_wb_count` read by the 0x194040 look-at hook (:12413) —
     cross-TU probe plumbing for concluded hunts ([CUTFLOW]/[BONEWB]). Correct: delete;
     the 0x194040 hook itself is a HOOK_RECLASSIFICATION row.
  10. **Session-tag narration throughout** ([R31], [W13-*], [S8-*], loop-N;
      hundreds of lines): banned by ARCH §11 — strip to hardware-invariant one-liners
      on move; history stays in docs/loop_state.
  11. **Real PS2 rules to KEEP (not hacks, named so the purge doesn't overreach)**:
      chop rounding (VuChopRoundGuard :53-70), CLIP/MAC/STATUS retire pipelines
      (:797-1100), FMAC/IALU/EFU/FDIV stall rings, XGKICK drain-time copy + 2-cycle/QW
      pace + wrap/cancel semantics (:11566-11800, mirrors VUops.cpp behavior), the
      MTVU EE-kick Sync redirect (:4806-4830), VU1_LINK11 companion (see
      vu_fast_exec.h). These become the owning classes above.
- **Risks**: flag-pipeline timing and XGKICK pacing are behavior-carrying; the
  cached/interp equivalence contract must survive the split. Proving tests:
  `vu1_replay` corpus (COMPARE 121/121, FAIL=0 per-entry oracle gates),
  `Vu1ReplayCachedCompare` interp-vs-cached bit-exactness, the five-state flag unit
  suite, `test_vu_clampelide`-style clamp gates, XGKick-family scene anchors
  (BUG-023 link truncation regression), full differential harness legs.

---

### host_app/ps2_scheduler_old_fiber.cpp (2,169 lines)

- **Purpose**: The superseded fiber-based PS2 thread scheduler (Windows-fiber
  cooperative kernel emulation). Replaced wholesale by `ps2_threading.{h,cpp}`.
- **Public surface**: none that is linked — zero references in host_app/CMakeLists.txt
  (verified: `grep -c ps2_scheduler_old_fiber` = 0).
- **Classification**: MIXED (DEAD) per A6 §4 table — 66 code-level EE-address hits
  inside superseded code; drops out of every count.
- **Refactor destination**: **DELETE** in Phase 1 (MIGRATION §1.1 row
  "ps2_scheduler_old_fiber.{cpp,h} (2,169 ln) + platform/*/fiber_*").
- **Flags consumed here**: none (0 getenv).
- **Hacks / workarounds**: n/a (dead). The Windows-macro undef dance at the top
  (:8-18) is the same include-order fragility syscalls.h carries — noted there.
- **Risks**: none; not compiled. Evidence of death: no CMake refs, A1 §5.1, A4 §2.3/B5.

---

### host_app/vu_fast_exec.h (1,833 lines)

- **Purpose**: Header-inline VU fast-execution bodies extracted from vu.cpp
  ([R153-TUSPLIT]) so both the cached tier (vu.cpp) and the generated AOT TUs could
  inline them: `ExecUpperFast` / `ExecLowerFast` / `ApplyFmacReadStallsFast`
  (PS2_VU_HOT_INLINE member definitions), the NEON/scalar clamp + MAC-flag pack
  helpers (`VuClampBits1/4`, `VuMacFlags1/4`, `VuWriteMaskClamp`, `CommitQStatus`,
  `Vu_fp_max/min`), `kVuLaneMask`, the mutable probe flag `g_vu_ocean_active`, and the
  BAL/JALR link-width fix `VuLinkMask()`.
- **Public surface**: the inline functions above + `kVuNoClamp` / `kVuClampElide`
  latches. Included by vu.cpp and the five vu_aot_programs_*.cpp chunk TUs.
- **Classification**: GENERIC-PS2, subsystem VU.
- **Refactor destination** (MIGRATION §1.4 VU row): split into
  `engine/vu/tiers/cached_tier.cpp` — with the AOT tier deleted (NOAOT) the second
  consumer vanishes, so the multi-TU inline-sharing rationale dies and the bodies move
  INTO the cached-tier TU (satisfying §13 "headers are declarations only"); the clamp/
  MAC-pack helpers single-source into `engine/vu/vu_float.{h,cpp}` (shared canon with
  the game VU1 library per A6 §7 row A); `VuLinkMask`'s resolved 11-bit rule inlines
  into the two link writers. RunUpper/fast-exec duplicate bodies dedupe per A1 §4.2.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disp | refactor outcome |
  |---|---|---|---|---|
  | PS2_VU_NOCLAMP (:61) | OFF | bypass VU float clamping (measurement-only; changes semantics) | delete-losing-arm | **DELETE** — winner is clamps-ON; the bypass arm and latch go (A2 category quibble only) |
  | PS2_VU_CLAMPELIDE (:83) | ON | proven-clamp elision at AOT-emitted `EL!=0` sites only | delete-losing-arm | **DELETE** — its only consumers are AOT bodies; rides out with the NOAOT tier deletion (run_play.sh documents it as acting on the AOT build) |
  | PS2_PERF (:85) | canon=1 | gate-announce printf | keep-as-setting | **SETTING** (global); this announce line dies with the gate |
  | PS2_VU1_LINK11 (:113) | ON | 11-bit BAL/JALR link mask vs historical 10-bit truncation | delete-losing-arm | **NORM** — the PS2 rule (link is an 11-bit instruction index; jump-time masking only) becomes unconditional; BUG-023 regression pins it |
- **Hacks / workarounds**:
  1. **Header-defined hot bodies** (whole file): 1,800 lines of function definitions
     in a header, forced by the AOT TU-split — the exact pattern §13 retires. Correct:
     bodies in the cached-tier .cpp once AOT dies.
  2. **Mutable inline global as probe gate**: `g_vu_ocean_active` — probe state
     mutated from the LQ hot path. Correct: delete with the [OCEAN-ISO] probes.
  3. **kVuNoClamp branch inside every clamp call** (`VuClampBits1` :117 and siblings):
     a measurement branch in the hottest arithmetic path. Correct: delete the branch.
  4. The 10-bit link truncation it fixes was OUR translation bug, not a PS2 behavior —
     the fix comment (:90-112) is the model for a `docs/bugs` entry (exists:
     BUG-023-vu1-link-truncation).
- **Risks**: clamp/MAC-flag semantics are bit-exactness-critical. Proving tests:
  corpus COMPARE both-latch-state sweeps, `tests/test_vu_clampelide.cpp` (adversarial
  raw-ACC seeds), vu1_replay --exact, NEON-vs-scalar build equivalence suite.

---

### host_app/test_scanout_preservation.cpp (1,058 lines)

- **Purpose**: Preservation property tests for the black-screen scanout fix
  (TextureDecoder RGBA decode, sprite 2→6 vertex expansion, GSVertex→GPUVertex
  conversion, insufficient-vertex batch skip). Observation-first captures of unfixed
  behavior; the campaign is concluded.
- **Public surface**: standalone `main()`; zero CMake references (verified).
- **Classification**: GENERIC-PS2 test scaffolding (GS), dead.
- **Refactor destination**: **DELETE** Phase 1 (MIGRATION §1.1 "23 host_app-root
  test_*.cpp, zero CMake refs"). The properties it pinned live on in tests/ replay
  suites (gs_replay byte-identity, sprite-expansion unit tests); anything unique is
  distilled into docs/bugs, not code.
- **Flags consumed here**: none.
- **Hacks / workarounds**: replicates production logic by copy (its stated
  methodology) — copies drift; the refactor's oracle-gated tests replace the pattern.
- **Risks**: none (not compiled). Verify the live tests/ suites cover P1-P4 before
  deletion (they do per A5's replay/coverage census; spot-check at Phase 1 gate).

### host_app/test_bug_conditions.cpp (789 lines)

- **Purpose**: Bug-condition exploration tests for four host defects (host `exit()`
  from emulated paths, scheduler spin after all-dormant, low-vertex primitive drops,
  missing diagnostics), using fork() isolation; encodes expected-fixed behavior.
- **Public surface**: standalone `main()`; zero CMake refs.
- **Classification**: GENERIC test scaffolding (EE/scheduler/GS), dead.
- **Refactor destination**: **DELETE** Phase 1 (same §1.1 row). The no-exit()-from-
  guest-paths property becomes a lint/contract in the new tree (crashring terminal
  path is the ONE sanctioned exit — recomp_seams.cpp); scheduler liveness is covered
  by ee/hle thread suite.
- **Flags consumed here**: none.
- **Hacks / workarounds**: logic-replication-by-copy (as above); fork()-based exit
  testing is macOS/POSIX-only (would never have ported).
- **Risks**: none; not compiled.

### host_app/test_vblank_preservation.cpp (688 lines)

- **Purpose**: Preservation tests for the VBlank starvation deadlock fix (non-blocking
  fast paths, timing logic, semaphore ops, DMA tag parsing).
- **Public surface**: standalone `main()`; zero CMake refs.
- **Classification**: GENERIC test scaffolding (INTC/timing/DMAC), dead.
- **Refactor destination**: **DELETE** Phase 1 (§1.1 row). VBlank/boundary behavior is
  owned by the ee/timing BoundaryProtocol tests + determinism gates in the new tree.
- **Flags consumed here**: none.
- **Hacks / workarounds**: logic replication by copy.
- **Risks**: none; not compiled.

### host_app/test_fbo_persistence_bug_condition.cpp (637 lines)

- **Purpose**: Bug-condition tests for FBO persistence loss through clear passes and
  MODULATE TFX color double-scaling ("expected to fail on unfixed code").
- **Public surface**: standalone `main()` linking gs_state/gif/texture_decoder
  directly; zero CMake refs.
- **Classification**: GENERIC test scaffolding (GS), dead.
- **Refactor destination**: **DELETE** Phase 1 (§1.1 row). The two defect classes are
  covered by corpus byte-identity + the class-renderer hatches' census tests.
- **Flags consumed here**: none.
- **Hacks / workarounds**: none beyond the copy pattern.
- **Risks**: none; not compiled.

### host_app/test_comprehensive_rendering.cpp (581 lines)

- **Purpose**: Regression tests for six rendering root causes (RT UV generation,
  scanout FBO selection via DISPFB, Z-texture skip, EOP flush state propagation,
  triangle assembly / FlushBatch / GS register parsing preservation).
- **Public surface**: standalone `main()`; zero CMake refs.
- **Classification**: GENERIC test scaffolding (GS), dead.
- **Refactor destination**: **DELETE** Phase 1 (§1.1 row). Root causes 8a-8g map to
  gs/state flush + gs/hle/present suites and the G-C1 verifier in the new tree.
- **Flags consumed here**: none.
- **Hacks / workarounds**: none beyond the copy pattern.
- **Risks**: none; not compiled.

### host_app/test_texture_shuffle.cpp (519 lines)

- **Purpose**: Property tests for GetBitsPerPixel and texture-shuffle scale
  computation, replicating a static function from opengl_backend.cpp.
- **Public surface**: standalone `main()`; zero CMake refs.
- **Classification**: GENERIC test scaffolding (GS/render), dead.
- **Refactor destination**: **DELETE** Phase 1 (§1.1 row). Shuffle detection/snap gets
  real unit tests beside `texture_shuffle_snap` (see shuf_snap.h entry) with the
  production function exported, not replicated.
- **Flags consumed here**: none.
- **Hacks / workarounds**: replicates a TU-static production function verbatim (:7-9
  admits it) — the anti-pattern one-class-per-file + real linkage removes.
- **Risks**: none; not compiled.

---

### host_app/snd_census.h (454 lines)

- **Purpose**: Env-gated sound-RPC traffic census ([S41-SND]): counts every audio
  command at the three funnels all sound flows through (EE-side `sceSdRemote`
  dispatch, LIBSD.IRX stub calls from the recompiled STREAM.IRX, SIF RPC calls by
  server id incl. a read-only decode of STREAM.IRX batch buffers), with an atexit
  summary table. Mutex-serialized across CPU + IOP worker threads.
- **Public surface**: `snd_census::{Enabled, Mtx, Counts, SdrCmd, LibSdStub, RpcCall,
  StreamBatch, VoiceTransBytes, ...}` inline hooks. Included by sce_hle/libsdr.cpp,
  iop_stubs.cpp, sif_rpc_handlers.cpp.
- **Classification**: GENERIC diagnostic mechanism with title knowledge in its decode
  (STREAM.IRX server id 0x12345, cmd types 43/46/47/48) — MIXED lean-GENERIC;
  subsystem: sound/diag.
- **Refactor destination** (MIGRATION §1.4 Audio row): `engine/core/diag/snd_census.
  {h,cpp}` — class `SndCensus`, compile-time PS2R_DIAGNOSTICS, move-to-diagnostics-
  build. The STREAM.IRX command-name/decode table is title data: parameterize from the
  title's stream_irx feed-contract module (games/…/iop/stream_irx/) so the engine diag
  stays token-free.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disp | refactor outcome |
  |---|---|---|---|---|
  | PS2_SND_CENSUS (:45) | OFF | enable the census + atexit dump | move-to-diag | **DIAG** — PS2R_DIAGNOSTICS build, Config `diag.snd_census` |
- **Hacks / workarounds**: header-defined mutable state (inline statics + mutex) —
  moves to the .cpp; hook tendrils into three TUs become registered diag probe points;
  STREAM.IRX cmd-type knowledge hardcoded in an engine header (lint_engine_purity
  candidate) — becomes registered title data.
- **Risks**: none behavioral (pure observation). Proof: diag build compiles + census
  smoke run; release build contains no trace of it (M4A-style kept-log identity).

---

### host_app/iop_loader.cpp (451 lines) + host_app/iop_loader.h (15 lines)

- **Purpose**: Loads the recompiled STREAM.IRX execution image: extracts the IRX from
  the ISO at a hardcoded LBA, parses its ELF32 sections manually, copies
  .text/.data/.bss into IOP memory, resolves the 73 import stubs, registers the RPC
  dispatch address, and runs the module entry point. Ends with printf/log
  verification blocks (.data integrity, function-map presence).
- **Public surface**: `iop_load_stream_irx()`, `g_iop_rpc_dispatch_addr` (consumed by
  the SIF RPC integration). Called once from boot (main.cpp) after the ISO opens.
- **Classification**: GAME per A6 §4 (:289 — "locates and loads THIS game's
  STREAM.IRX"); mechanism/data split per A6 §7 row G. Subsystem: IOP.
- **Refactor destination** (MIGRATION §1.4 verbatim row + §1.6 row G): split —
  - `engine/iop/module_loader.{h,cpp}` — class `IopModuleLoader`: ELF32 parse,
    section copy, import-stub resolution, entry-point invocation (generic IRX-load
    mechanism, also serves any future custom module);
  - `games/<title>/iop/stream_irx/stream_irx_plugin.cpp` — the
    `IopPlugin` descriptor carrying every pin: ISO location (today LBA 1019227, size
    116156 — replaced by ISO9660 path lookup of `DATASYS/STREAM.IRX`), entry 0x13C,
    dispatcher 0x8D28, RPC server id 0x12345, registered via
    `TitleProfile.iop_modules.custom` (§14.1).
- **Flags consumed here**: none (0 getenv).
- **Hacks / workarounds**:
  1. **Hardcoded LBA/size instead of filesystem lookup** (:24-26): the PS2 rule is
     `LoadModule("cdrom0:\\DATASYS\\STREAM.IRX")` resolved through ISO9660; the
     refactor's cdvd ISO9660 walker owns the lookup, the title supplies the path.
  2. **Hardcoded module-internal addresses** (:31-37 entry/dispatcher/server-id;
     :356-362 .data verify addresses 0x12BF0/0x1344C/0x14A90): title data → the
     IopPlugin descriptor; the verify block deletes.
  3. **Task-numbered verification narration** (:347-420 "VERIFICATION: Task 7.1/7.2"
     printf blocks) and the [S42 note] session post-mortem (:330-341): session history
     in code — delete; a boot-time load failure becomes a loud error path, not a
     passing printf census.
- **Risks**: STREAM.IRX bring-up is the sound stack's root. Proof: boot-to-title
  harness leg with sound anchors (bank ledger populated, first voice key-on), IOPM
  savestate round-trip, cmd45 memory-gate acceptance (see iop_sysmem_heap.h).

---

### host_app/gs_mipmap.h (358 lines)

- **Purpose**: The GS mipmap pipeline ([S82-MIP] + [S91-MINIF] + [S92-MOTION]):
  decodes TEX1/MIPTBP per the GS manual (LCM/MXL/MMAG/MMIN/MTBA/L/K, per-level
  TBP/TBW pairs), captures per-level private VRAM snapshot windows onto
  `BatchTextureInfo.mips`, selects GL min/mag filters mip-aware, injects the
  `GSMIP_AAF` shader define for the anti-aliasing LOD floor
  (`lod_used = max(lod_retail, lambda_footprint)`), and carries census counters.
- **Public surface**: `gsmip::{On, AafMode, AllClassOn, InjectAafDefine, MiptbpPair,
  CaptureMipChain, TrilinearOn, SelectGLFilters, BindCensus(Emit)}` + census atomics.
  Included by gif.cpp, gs_state.cpp, opengl_backend.cpp.
- **Classification**: GENERIC-PS2 (GS format + texture sourcing; the retail LOD
  formula and MIPTBP layout are hardware rules). Subsystem: GS.
- **Refactor destination** (MIGRATION §1.4 GS row: "addressing tables → gs/format;
  sourcing → gs/hle/texsrc — split"): split —
  - `engine/gs/format/mip_chain.{h,cpp}` — class `MipChain` (TEX1 field decode,
    `MiptbpPair`, level dims, K sign-extension);
  - `engine/gs/hle/texsrc/mip_sampler.{h,cpp}` — class `MipSampler` (chain capture
    through the ONE ResolveRead footprint path, GL filter mapping, LOD-floor policy);
    the `GSMIP_AAF` GLSL blocks land in render/backends/gl shader sources;
  - census counters → core/diag.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disp | refactor outcome |
  |---|---|---|---|---|
  | PS2_GS_MIPMAP (:64) | ON | whole mip pipeline vs legacy level-0-only | delete-losing-arm | **NORM** — mip pipeline unconditional |
  | PS2_GS_MIP_ALLCLASS (:128) | ON | AAF LOD floor + all-class TEX1 honoring | delete-losing-arm | **NORM** (named in the peak-config NORM list) |
  | PS2_GS_MIP_LODREF (:126) | OFF | diag arm: pure derivative lambda ("not a play mode") | delete-losing-arm | **DELETE** — the reference instrument's campaign (CAVERN_MINIF) concluded; winner is AAF=1 |
  | PS2_GS_MIP_TRILINEAR (:278) | ON | LINEAR between-level step for MMIN 2/4 | delete-losing-arm | **NORM** — documented divergence from the literal GS table (temporal-crawl fix), carried with a `// diverges:` comment per ARCH §11 |
  | PS2_MIPCEN (:187/:326) | OFF | capture/bind census prints | delete-losing-arm | **DIAG** — census counters stay (core/diag); stderr prints behind PS2R_DIAGNOSTICS (A2 delta: it is a census, not a tuning knob) |
- **Hacks / workarounds**:
  1. **MTBA=1 auto-base unimplemented** (:196-206): censused loudly and level-0
     fallback. Correct implementation: MTBA auto base-address derivation per GS manual
     in `MipChain` (this game never uses it, so it may land as a documented
     divergence + census — carry the note).
  2. **TEX1.L approximated as bias-only** (:246-254): GL LOD bias cannot scale lambda
     by 2^L. Keep as a one-line `// diverges:` with RecordId.
  3. **GL enums as numeric literals** (:262-268) to keep the header GL-free for EE
     TUs: resolves structurally when filter mapping moves behind the backend boundary.
  4. Header-defined mutable census atomics: move to .cpp/diag.
- **Risks**: minification/aliasing look (BUG-011, BUG-012, BUG-021 family) and replay
  identity. Proof: frozen gs_replay baselines replay byte-identically (DrawRec carries
  no mips — verified property), CAVERN scene anchors for the AAF floor, swizzle/decode
  equivalence tests for footprint math.

---

### host_app/loadspan.h (328 lines)

- **Purpose**: Load-screen span probe ([LOADSPAN]): defines "loadscreen_active" as
  QUEUE-ACTIVE ∧ INPUT-DEAD ∧ DRAW-CLASS, then emits START/MID/END stderr tables
  attributing the span to per-op counters (ISO read enqueue/deliver latency, gate
  holds, EE polls, CD completion events, STREAM.IRX heartbeats) bumped from hook
  sites in hle_heap/iop_stubs/main/ps2_vblank.
- **Public surface**: `loadspan::{On, DrawMax, Note*(…), OnTick, TakeSnap, EmitTable}`
  + `ctr::` counter accessors. Hooked by hle_heap.cpp, iop_stubs.cpp, main.cpp,
  ps2_vblank.cpp.
- **Classification**: GENERIC diagnostic (cdvd/loading), measurement-only.
- **Refactor destination** (MIGRATION §1.4: "loader_prof.h, loadspan.h | loader probes
  → core/diag — move-to-diagnostics-build"): `engine/core/diag/loadspan.{h,cpp}` —
  class `LoadSpanProbe`, PS2R_DIAGNOSTICS builds. Note: its streak/span machinery
  overlaps core/triage `PerfMonitor` (TRIAGE_CAPTURE absorbs slowspot); if triage's
  load-phase attribution matures, this dissolves into it — keep the migration row's
  DIAG placement until then.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disp | refactor outcome |
  |---|---|---|---|---|
  | PS2_LOADSPAN (:98) | OFF | arm the span probe | delete-both | **DIAG** — MIGRATION §1.4 keeps the file as diagnostics; A2's delete-both reflects the concluded LOADTIME campaign. Ledger call: DIAG (Config `diag.loadspan`), revisit at the Phase 4 purge with the loop_state verdict check |
  | PS2_LOADSPAN_DRAWMAX (:105/:106/:115) | 0 (classifier-only) | optional draw-count gate for span detection | delete-losing-arm | **DIAG** — becomes a diag config field beside it |
- **Hacks / workarounds**:
  1. Cross-TU `extern` counter redeclarations (:83-90, the "cuelag pattern") instead
     of an owned interface — becomes registered probe points on the diag module.
  2. Hook tendrils in four TUs (documented at :51-63) — same fix.
  3. Header-resident mutable state (function-local statics in `ctr::`, the OnTick
     span state machine) — moves to the .cpp.
  4. Depends on `chunk_gate::GetReleaseTallies` ([S60]) — that gate family is
     cdvd/hle loader territory (HOOK §3.6); the probe must read it through the
     loader's diag surface, not a namespace back-channel.
- **Risks**: none behavioral. Proof: diag-build smoke (span START/END emitted on a
  scripted load), release build free of the symbol.

---

### host_app/time_domain.h (319 lines)

- **Purpose**: The single emulated-time domain ([S94-TIMEDOM]) for every
  game-observable EE clock: a monotonic emu tick counter advanced REGIME
  fields/frame at each sceGsSyncV boundary (with carry/top-up), an adaptive
  hysteresis `RegimeDetector` (1..5 fields/frame tracking sustained loop rate, the
  retail slip shape), a lead governor (emu may lead wall by ≤1 tick; lag absorbed,
  never repaid as a burst), and 1:1 raw-tick riding outside the frame loop
  (loads/FMV/freerun/warp). Fixes the wall-clock jitter that flipped the game's
  T0-budgeted detail-overlay passes (BUG-014).
- **Public surface**: `timedom::{On, BypassNow, ForcedRegime, RegimeDetector,
  BoundaryAdvance, NoteArrival, OnRawTick, OnBoundary, ReAnchor, DomainTick}` +
  inline atomics (g_emu_count, g_regime, …). Consumers: ps2_vblank.cpp (ticker),
  eetimers (PumpAtBoundary/T0 reads), gs_hle (NoteArrival).
- **Classification**: GENERIC-PS2 mechanism (EE timing); budget/regime *parameters*
  are title-tuned via TitleProfile.quirks (A6 §4).
- **Refactor destination** (MIGRATION §1.2 verbatim row "time_domain.h (timedom) →
  ee/timing/emu_domain.*"; ARCH §3/§4.7 TimeDomain family): split for D4 —
  - `engine/ee/timing/emu_domain.{h,cpp}` — class `EmuDomain` (tick counter, lead
    governor, boundary settle, re-anchor; state moves out of the header);
  - `engine/ee/timing/regime_detector.{h,cpp}` — struct `RegimeDetector` (already
    pure + unit-tested);
  - `BoundaryAdvance` stays a pure free function beside EmuDomain.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disp | refactor outcome |
  |---|---|---|---|---|
  | PS2_TIME_EMU (:76) | ON | emulated time domain vs raw g_vblank_count legacy | delete-losing-arm | **NORM** (named in the peak-config NORM list) — emu domain becomes the only path; raw-domain arm and `DomainTick`'s legacy branch delete |
  | PS2_TIME_EMU_REGIME (:93) | 0 (adaptive) | pin the quantum at N fields/frame | delete-losing-arm | **NORM** — adaptive detector unconditional; the pin arm deletes (a regime pin can resurface as a diag-build knob if triage needs it, but the ledger closes the env) |
- **Hacks / workarounds**:
  1. Header-defined live state (inline atomics + `s_m` mutex + `s_det` at :219-231)
     and mutex-taking inline functions — violates §11 "header mutable globals = 0";
     moves into the EmuDomain class.
  2. stderr regime-change print (:284-289) → core/log category.
  3. `BypassNow()` declared here, implemented in ps2_vblank.cpp — an inverted
     dependency; the refactor gives EmuDomain an injected `TickSource`/bypass policy
     (ARCH §4.7 TickSource → EmuDomain → DerivedClocks).
  Everything else is a real model with documented contracts (this is the good kind
  of file).
- **Risks**: game speed/timing regressions (BUG-014 wall-clock, BUG-029 subtick,
  BUG-013 budget-phase interactions). Proof: `tests/test_time_domain.cpp` (detector +
  BoundaryAdvance), determinism gates, cavern scene anchors (band flips), FPS_CAP=60
  peak-config play leg.

---

### host_app/vu_aot_config.h (266 lines)

- **Purpose**: Parser/round-trip for `vu_aot.ini` ([U6-VUCTRL]): corpus_compile mode
  (auto/all/none), collection_enabled, collection_per_image — shared by the AOT
  generator, main.cpp startup injection (arms vutrace collection), the GUI
  VU-microcode panel, and a pure ctest.
- **Public surface**: `vuaotcfg::{CorpusMode, ModeName/ModeFromName, Config, Parse,
  Serialize, Load/Save}`. Consumers: recompiler_tool/vu_aot_gen.cpp, main.cpp,
  gui/vu_microcode_section.cpp, tests/test_vu_aot_config.cpp.
- **Classification**: GENERIC tooling config (VU/AOT tier).
- **Refactor destination**: **DELETE** with the AOT tier (MIGRATION §1.4 VU row:
  "vu_aot_config.h, vu_aot_status.h, vu_aot_registry.h | delete with the AOT tier
  (§1.1 row)", NOAOT decision). The one live concern it carries — arming Vu1Case
  collection — is replaced by core/triage's FallbackDetector VU1 arm + the GUI
  Triage panel (TRIAGE_CAPTURE §12); the offline collector remains reachable via the
  vutrace DIAG config.
- **Flags consumed here**: none (reads the ini file, not env).
- **Hacks / workarounds**: config-by-ini-file that `setenv()`s vutrace vars at boot
  (the main.cpp consumer) — env as an internal IPC channel; dies with the tier, and
  the Config schema replaces env plumbing generally.
- **Risks**: losing the case-collection workflow UX — accepted; triage panel is the
  replacement. Its ctest (VuAotConfig) deletes with it.

---

### host_app/gs_deposit_overlay.h (236 lines)

- **Purpose**: [TEXPOP-FIX] single-writer VRAM-mirror discipline: GFX-side deposit
  encoders (SyncRT/SyncZ/DepositAlt16Lane) write a 4 MiB shadow with per-8KB-page
  copy-on-first-touch instead of gs_vram; the EE thread commits pending pages at
  stream boundaries, arbitrating per page in EE-stream order (upload-epoch vs
  deposit-batch-epoch); GFX readers see the freshest view via ComposeRead. Closes
  the two-writer race behind the one-frame texture pops (BUG-010).
- **Public surface**: `gsovl::{On, BeginDeposit, EndDeposit, ComposeRead,
  WriteThrough, ApplyDeposits, DropAll}` + state/counter atomics. Consumers:
  opengl_backend.cpp (deposit encoders, reads), gs flush/TRXDIR/HLE-feed boundary
  sites (ApplyDeposits), savestate restore (DropAll).
- **Classification**: GENERIC-PS2 (GS VRAM coherency; PCSX2's stream-order rule
  projected onto the mirror). Subsystem: GS/VRAM.
- **Refactor destination** (MIGRATION §1.4 GS row: "absorbed by the ONE PageRecord
  rewrite (§1.5) — delete after Phase 5"): **rewritten/absorbed** — the shadow +
  page-state machine becomes the single-writer commit protocol of
  `engine/gs/vram/vram_image.{h,cpp}` (class `VramImage`) +
  `engine/gs/vram/page_record.{h,cpp}` (class `PageRecord`, ARCH §4.3); this header
  then dies. Its epoch-arbitration rule (upload stream-newer wins unconditionally)
  is the invariant PageRecord must carry forward verbatim.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disp | refactor outcome |
  |---|---|---|---|---|
  | PS2_GS_DEPOSIT_EEAPPLY (:66) | ON | single-writer EE-commit overlay vs legacy GFX direct-write | delete-both | **NORM** (named in the peak-config NORM list) — single-writer becomes the only path; legacy direct-write arm deletes. A2's delete-both is WRONG here: this is a default-ON proven fix (BUG-010), not a concluded experiment |
- **Hacks / workarounds**:
  1. Unsized `extern` array declarations against graphics_thread.cpp definitions
     (:52-55, `g_vram_page_epoch[]`) — no extent checking; PageRecord owns its arrays.
  2. Bounded busy-spin on state 3 (`if (st == 3) continue;` :101) — cross-thread
     hand-off by spin; the rewrite serializes commits through the queue boundary.
  3. Compute-raster readback path explicitly NOT routed (:44-46) — dies with the
     compute experiment deletion (§1.1).
  4. 4 MiB static shadow inside a header-local static (:76-79) — fine today, but the
     allocation belongs to VramImage.
- **Risks**: VRAM coherency regressions (texture pop, shimmer at SCALE=1). Proof:
  SNAPVERIFY mismatches=0, corpus byte-identity, the BUG-010 scene anchor
  (crystal/wall texpop), savestate restore with pending deposits (DropAll path).

---

### host_app/gif.h (216 lines)

- **Purpose**: The GIF device header: `GIFPath`/`GIFFormat` enums, the GS internal
  register-descriptor enum, `GIFTag` with its bitfield `Parse`, `GIFPacket`, and
  `class GIF` — I/O regs (CTRL/MODE/STAT), 16-QW FIFO, PATH3 mask (atomic for MTVU),
  per-tag fast-path eligibility bits ([R158-GIFFAST]), the legacy raw-packet queue to
  the graphics thread, ParseSpan/ProcessPacked/Reglist/Image decls, the decode-once
  template cache hook (TplTryServe), and the HLE typed-apply entries
  (ApplyHleDirectOps / ApplyHleDrawIR).
- **Public surface**: `class GIF`, `g_gif`, `GIFTag`, `GifEmitR158CensusForBench()`,
  `GskSimdOverrideForTest()` + SIMD census atomics. Included by dmac.cpp, vif.cpp,
  vu.cpp (XGKICK→arbiter), gif_arbiter, gs_state.cpp, graphics_thread.cpp, tests.
- **Classification**: GENERIC-PS2 (GIF). 4 VRAM-literal hits live in gif.cpp (A6 row
  C), not this header.
- **Refactor destination** (MIGRATION §1.3 gif.cpp row: "gif/parse (ParseSpan
  canonical; legacy ProcessPacked deleted) + gs/state/flush (FlushBatch merged into
  the ONE flush)"): split —
  - `engine/gif/gif_tag.{h,cpp}` — struct `GifTag` (parse + eligibility bits);
  - `engine/gif/gif_unit.{h,cpp}` — class `GifUnit` (CTRL/MODE/STAT, FIFO, PATH3
    mask, MMIO handler registered via ee/mem router);
  - `engine/gif/parse.{h,cpp}` — class `GifParser` (ParseSpan canonical; template
    cache rides with gif_template.h per §1.4);
  - the HLE typed-apply entries become the gs/hle/ir apply seam (DrawIR is the queue
    payload; the raw `GIFPacket` queue+condvar to the graphics thread is superseded
    by gs/hle/queue and deletes — A3 finding 7's "GL staging must not survive as the
    IR" applies to this raw-bytes queue equally);
  - `FlushBatch` decl merges into the ONE flush in gs/state.
- **Flags consumed here**: none (0 getenv in the header; PS2_GIF_FAST / PS2_GSK_SIMD
  are read in gif.cpp — another shard's ledger).
- **Hacks / workarounds**:
  1. `// ... many more` gap in the GS register enum (:104) — the refactor's
     gs/hle/ir + format layer enumerate the full set once.
  2. Two generations of HLE apply (`ApplyHleDirectOps` vs `ApplyHleDrawIR`) kept
     side-by-side (:174-190) — the DIRECT→DrawIR ladder collapses to DrawIR-only
     when PS2_GS_DRAWIR NORMs (that gate lives in render.h/gif.cpp; noted here
     because the dual entry points are this header's surface).
  3. Bench census export + test hook globals at namespace scope (:204-216) —
     TEST-ONLY seams; keep behind the tests/ boundary.
  4. Comment-documented cross-thread contracts on `path3_masked` — keep as §11
     one-line invariant comments.
- **Risks**: GIF parse is on every draw path. Proof: `test_gif_fast_equivalence`
  (SIMD vs scalar), gs_replay corpus byte-identity, G-C1 verifier once flush unifies.

---

### host_app/ee_timers.h (177 lines)

- **Purpose**: EE hardware timers T0-T3 in the vblank tick domain ([R195-TIM0]):
  MODE/COMP/HOLD semantics (CLKS/CUE/CMPE/OVFE/EQUF/OVFF/ZRET), counts-per-tick
  table, pending overflow/compare events dispatched to the game's recompiled ISR at
  safe boundaries (frame boundary + T0_COUNT read hook), sub-tick COUNT
  interpolation state ([S40-SUBTICK]), and the pure TIMR savestate payload codec.
  Replaces the legacy synthesized-from-SyncV counter that caused fixed-step
  slow-motion (0.73x dilation).
- **Public surface**: `eetimers::{On, Timer, EncodeTimr/DecodeTimr, EETimers (Read/
  Write/AdvanceToNow/Snapshot/Restore/SeedBootDefault), g_ee_timers, SetThreadCtx,
  PumpAtBoundary, PumpAtCountRead}`. Consumers: memory.cpp (MMIO + count-read hook),
  gs_hle.cpp (boundary pump), savestate.cpp (TIMR section), ps2_vblank.
- **Classification**: GENERIC-PS2 mechanism (EE timers); the game's boot-config
  seed and ISR knowledge are title-tuned data (A6 §4 row E budget family rides on
  this clock).
- **Refactor destination** (MIGRATION §1.3 cluster row "ee_timers/… → ee/timing/* +
  ee/intc/"; ARCH tree puts "INTC + EE timers T0-T3" in ee/intc/): split —
  - `engine/ee/intc/ee_timers.{h,cpp}` — class `EETimers` (device model, MMIO via
    ee/mem router, INTC TIM0-3 raising);
  - `engine/ee/intc/timer_codec.{h,cpp}` — the pure TIMR payload codec, registered
    as a core/savestate SectionSpec provider;
  - tick-domain reads go through ee/timing DerivedClocks (EmuDomain), the boundary
    pump folds into the ONE BoundaryProtocol (ARCH §4.7);
  - `SeedBootDefault`'s game boot config (T0_MODE=0xe82) → TitleProfile data
    (savestate-compat seed), leaving the engine seed generic.
- **Flags consumed here**:

  | var | default today | what it selects | A2 disp | refactor outcome |
  |---|---|---|---|---|
  | PS2_EE_TIM0 (:46) | ON ("DEFAULT ON since R195b", user-approved; canon sets =1) | tick-domain timer model + ISR dispatch vs legacy synthesized count | keep-as-setting | **NORM** — peak config runs it and the OFF arm is the known-bad frozen-timer legacy; the memory.cpp synthesized path deletes with it (A2 delta: not a real user choice). Note: the file-top comment still says "default OFF" — stale, the latch is authoritative |

  (PS2_T0_SUBTICK is documented here but read in ee_timers.cpp — that shard's ledger;
  its disposition is NORM per the run_play.sh proven-fix list / BUG-029.)
- **Hacks / workarounds**:
  1. **Game boot knowledge in engine comments/seed** (:1-13 T0_MODE=0xe82, ISR
     0x191980, accumulator at gp-0x7d20 = RAM 0x3097D0; SeedBootDefault): mechanism is
     generic, the seed values are title data — move to TitleProfile; keep the
     hardware-rule comments.
  2. Public raw members on the class (timers/m/dispatchable exposed; `friend
     PumpDispatch`) — tighten at the move.
  3. ISR dispatch "on a guest EE thread at safe boundaries" is a host scheduling
     approximation of an interrupt — document as `// diverges:` (real INTC preemption
     is not modeled); acceptable per the R195 adjudication.
  4. Dual maintenance paths (gate-ON module vs memory.cpp host-maintained
     accumulator) — the NORM purge deletes the legacy path and its memory.cpp mirror.
- **Risks**: game speed, music cadence, budget classifier inputs (BUG-014/BUG-019/
  BUG-029 family). Proof: `tests/test_ee_timers_timr.cpp` (codec round-trip), subtick
  unit tests, determinism gates, authored-speed play anchor ("movement feels good"
  leg), savestate old-file seed path.

---

### host_app/shuf_snap.h (149 lines)

- **Purpose**: [R175-SHUFSNAP] GS column-snap for split-texture-shuffle strip draws:
  detects PCSX2's "Offset shuffle" signature (16bpp FRAME + 16bpp TEX read, FST
  sprite quads, x-width == u-width, width multiple of 8, |x-u| ≡ 0/8 mod 16) and
  snaps strip UVs to the 8px column grid (round-half-even), so GL's pixel-center
  sampling lands on the GS-DDA-intended texel — fixing the "last column of every 8px
  group wrong" fog/DOF lattice.
- **Public surface**: `r175::{ShufSnapOn, ShufSnapOverrideForTest, SnapToColumn8,
  SnapRow, IsPsm16, DetectColumnShuffle}` + census atomics. Consumer:
  opengl_backend.cpp draw path; tests.
- **Classification**: GENERIC-PS2 (a GS sampling-convention rule vs GL rasterization;
  PCSX2 implements the same class generically). Subsystem: GS/render.
- **Refactor destination** (MIGRATION §1.4: "shuf_snap.h … purge-dissolve into
  render/backends/gl + gs/hle/targets"): the detection + snap become one class
  `TextureShuffleSnap` in `engine/gs/hle/classrdr/texture_shuffle_snap.{h,cpp}`
  applied at draw lowering (it is a generic idiom detector in the §14.3 sense —
  geometry+format keyed, no game data), with the GL-side application riding the
  backend draw path. The PS2_GL_SHUF_SNAP hatch purge-dissolves (winning arm stays).
- **Flags consumed here**:

  | var | default today | what it selects | A2 disp | refactor outcome |
  |---|---|---|---|---|
  | PS2_GL_SHUF_SNAP (:66) | ON | UV column snap vs unsnapped legacy sampling | delete-losing-arm | **NORM** — snap unconditional (A1 §5.3 expired hatch); `ShufSnapOverrideForTest` stays as the TEST-ONLY in-process hook |
- **Hacks / workarounds**:
  1. `kEps = 0.75f` snap tolerance (:112) — a heuristic threshold; document its
     derivation (≥ the 0.5 authoring bias) in the class, cover with unit cases at the
     boundary.
  2. Detection runs on the EXPANDED 6-vertex triangle stream (:118 comment) — after
     the flush unification the detector should see sprite quads pre-expansion;
     re-derive at the move.
  3. None otherwise — this is a faithful PCSX2-semantics port with sources cited.
- **Risks**: fog/DOF lattice regression on every fogged material. Proof: dedicated
  unit tests (detect + snap math, both gate states today → single-path tests after
  purge), the VIS_ROOTCAUSE2 scene anchor, corpus replay identity for non-shuffle
  draws (all-or-nothing batch rule).

---

### host_app/iop_sysmem_heap.h (148 lines)

- **Purpose**: Pure, header-only model of the IOP kernel sysmem API
  (AllocSysMemory/FreeSysMemory/QueryMaxFreeMemSize) over the 2 MB arena our runtime
  lays out for the recompiled STREAM.IRX: bump tail + first-fit free list with
  coalescing and tail reclaim, a skipped worker-stack band [0xE0000,0x100000), floor
  0x28000 above the module image, ceiling 0x1FF800 under the callback scratch
  stacks. The [S48 FIX-B] floor fix restored retail-sized largest-free blocks so the
  module's cmd45 memory gate (262,144 B, Aku Aku dialogue class) passes.
- **Public surface**: `struct IopSysmemHeap` {Alloc, Free, QueryMaxFree,
  ResetTransient, heap_ptr + maps}. Consumers: iop_stubs.cpp (sysmem stubs),
  savestate (heap_ptr only), tests/test_iop_sysmem_heap.cpp.
- **Classification**: GENERIC-PS2 (Sony sysmem semantics) with runtime-layout
  constants derived from THIS title's module footprint — MIXED lean-GENERIC.
  Subsystem: IOP.
- **Refactor destination** (MIGRATION §1.4: "iop_context.h, iop_memory.h,
  iop_sysmem_heap.h | iop/ … — verbatim"): `engine/iop/sysmem_heap.{h,cpp}` — class
  `IopSysmemHeap` (already one class; definitions move to the .cpp per §13, staying
  pure/vector-testable).
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  1. `kFloor = 0x28000` hardcodes STREAM.IRX's image ceiling (:38-40 layout table);
     correct: floor computed from the loaded IopPlugin image end at module load
     (title data in, constant out).
  2. Stack-band and scratch-stack layout constants encode OUR thread-hosting scheme
     (StartThread sp = 0x100000 - tid*0x4000; scratch tops 0x1FFA00/0x1FFC00/
     0x1FFE00) — engine exec-env policy; keep, but own them in one iop memory-plan
     class shared with iop_stubs rather than duplicated literals.
  3. Savestate contract serializes only heap_ptr; loaded states start with an empty
     free list ("conservative rebuild") — a documented approximation; carry as
     `// diverges:` + the old-state compat note.
- **Risks**: STREAM.IRX allocation failures reintroduce the silent-dialogue class.
  Proof: `test_iop_sysmem_heap.cpp` (lifecycle + band skip + QueryMaxFree), the
  cmd45 acceptance census (largest-free ≥ 0x88800 after boot), savestate old-file
  load leg.

---

### host_app/s8_inrt.h (124 lines)

- **Purpose**: [S8-INRT] pure admission + remap math for serving a PSMT8 texture
  window that lives INSIDE a live CT16 render target (PCSX2 "texture inside RT"
  region-view semantics): page-aligned window-in-footprint test returning the page
  offset, the fmt=6 `u_idx_self` bit packing, a CPU mirror of the GLSL claim-texel →
  CT16 (X,Y,lane) remap, and the CT16 halfword index extraction. Fixes the
  character-shadow decal "white cones" (stale mask reads).
- **Public surface**: `s8inrt::{On, IsCt16Psm, WindowInTarget, PackSelf6,
  UnpackPageOff, Fmt6Remap, Fmt6IndexFromRgba}`. Consumers: opengl_backend.cpp
  (fmt=6 serve), tests/test_s8_inrt.cpp.
- **Classification**: GENERIC-PS2 (a texture-cache serve rule; the game evidence in
  comments is context, not code tokens). Subsystem: GS/texsrc.
- **Refactor destination**: `engine/gs/hle/texsrc/window_in_target.{h,cpp}` — class
  `WindowInTarget` (admission + page-offset math) folded into the ONE `ResolveRead`
  owner-table tier (ARCH §4.5); the fmt=6 remap math joins the texsrc/format shader
  contract. **MIGRATION-ROW CORRECTION**: §1.4 lumps s8_inrt.h with "w10_eefeed.h,
  s8_ctxlatch.h … purge-dissolve into ee/timing + ee/hle" — an s8_-prefix
  misassignment; this file is GS texture-serve admission and belongs to the §1.5
  "17 texture-serve paths → ONE ResolveRead" rewrite (flagged in TREE-GAPS below).
- **Flags consumed here**:

  | var | default today | what it selects | A2 disp | refactor outcome |
  |---|---|---|---|---|
  | PS2_S8_INRT (:38) | unset → follows the fmt=6 class gate | window-inside-RT serve admission (explicit set wins) | delete-both | **NORM** — the serve rule becomes an unconditional ResolveRead tier; both the env and the follow-the-class default logic delete. A2's delete-both would remove a proven fix — misclassification (session-named but default-riding-ON via the class gate) |
- **Hacks / workarounds**:
  1. CPU/GLSL twin remap that "must stay bit-identical to the GLSL" (:100-104) —
     cross-language duplication by contract; correct: golden-vector tests generated
     from one table (the PsmLayout constexpr descriptors) driving both sides.
  2. Gate-follows-gate default (`On(bool fmt6_class_on)`) — a mirror of another
     file's gate; dissolves with the NORM.
  3. 6-bit page-offset pack budget (:66 `off > 63u` reject) — a uniform-encoding
     limit; document, or widen when the packing moves into the texsrc key.
- **Risks**: shadow-decal class regression. Proof: `test_s8_inrt.cpp` (remap/pack
  vectors), the /tmp/s8gate SW-dump-derived case set, beach/meadow shadow scene
  anchors, per-route ResolveRead case tests at the §1.5 rewrite.

---

### host_app/ipu.h (113 lines)

- **Purpose**: The IPU (Image Processing Unit) device class: command set
  (BCLR/IDEC/BDEC/VDEC/FDEC/SETIQ/SETVQ/CSC/PACK/SETTH), CTRL register bits,
  input/output FIFOs with DMA feed/drain, MPEG intra-macroblock decode (bitstream
  reader, DC/AC coefficients, IDCT, YCbCr→RGB32 with alpha thresholds). Used by the
  FMV pipeline and IPU-based texture decompression.
- **Public surface**: `class IPU` (WriteCmd/ReadCmd(64)/WriteCtrl/ReadCtrl/ReadBP/
  ReadTop(64), FeedInputFIFO/DrainOutputFIFO, status probes), `g_ipu`. Consumers:
  dmac.cpp (ch3/ch4), memory.cpp MMIO, mpeg_hle.cpp.
- **Classification**: GENERIC-PS2 (IPU hardware). Subsystem: IPU.
- **Refactor destination** (MIGRATION §1.4: "mpeg_hle.{cpp,h} + ipu.{cpp,h} →
  ipu/hle/mpeg_ipu.* (FMV pipeline …) — split"): split per D4 —
  - `engine/ipu/ipu_device.{h,cpp}` — class `IpuDevice` (regs, FIFOs, command
    dispatch, MMIO via router);
  - `engine/ipu/mpeg_decode.{h,cpp}` — the bitstream/DC-AC/IDCT/CSC internals as a
    concept-named unit (or classes `MpegBlockDecoder`/`ColorSpace` if they grow);
  - the sceMpeg HLE surface stays with ipu/hle per the row (mpeg_hle is another
    shard's file; PACE_AUTHORED folds into time.mode there).
- **Flags consumed here**: none (0 getenv; PACE_AUTHORED lives in mpeg_hle.cpp).
- **Hacks / workarounds**: none in the header — clean device decl. (The known IPU
  shortcuts live in ipu.cpp/mpeg_hle.cpp, other shards.)
- **Risks**: FMV decode. Proof: pss_census offline demux parity, FMV frame baseline
  replays, the `_bstag:48` benign-warning parity note (runtime-environment doc).

---

### host_app/vu_trace_case.h (93 lines)

- **Purpose**: The `Vu1Case` capture format — a 33,456-byte POD snapshot of
  everything a VU1 microprogram run depends on (VUregs, 16KB micro + 16KB data
  memory, VIF1 TOP/TOPS/ITOP/ITOPS, defensive Q/P latches, vu_cycle) captured at
  Execute entry, with inline binary Write/Read and magic/version pinning, plus
  `VuTraceForce` (setenv-based arming for offline tools).
- **Public surface**: `struct Vu1Case`, `WriteCase/ReadCase`, `VuTraceForce`.
  Consumers: vu.cpp (MaybeCapture), tests/vu1_replay corpus tooling.
- **Classification**: GENERIC diag/tooling format (VU). Subsystem: core/diag.
- **Refactor destination** (MIGRATION §1.4 verbatim row): `engine/core/diag/
  vutrace_case.h` (+ trivial .cpp if helpers grow) — verbatim; per [REV3 D9] this
  struct/writer is ALSO the fallback-capture format of core/triage
  (FallbackDetector VU1 arm, TRIAGE_CAPTURE §5.2) — one format, two producers.
  Format is FROZEN (static_asserts pin sizeof(VUregs)=608 and sizeof(Vu1Case)=33456;
  existing corpus files must keep reading).
- **Flags consumed here**: none read; `VuTraceForce` WRITES PS2_VUTRACE/
  PS2_VUTRACE_FILE via setenv.
- **Hacks / workarounds**:
  1. `VuTraceForce` arms tracing by mutating the process environment before first
     Execute (:82-92) — env as an internal API; correct: vutrace Config gains a
     programmatic injection point (offline tools construct the Config directly);
     the setenv shim deletes.
  2. Capture-semantics assumption ("pipelines drained at entry") documented with a
     version-bump escape hatch — good contract hygiene; keep.
- **Risks**: corpus compatibility. Proof: vu1_replay reads the existing case corpus
  unchanged; a format round-trip unit test pins the layout.

---

### host_app/syscalls.h (86 lines)

- **Purpose**: Declarations for the EE kernel syscall HLE surface (~50 handlers:
  threads, semaphores, event flags, SIF DMA/RPC/regs, GS IMR/CRT, INTC/DMAC handler
  management, heap/misc) plus the dynamic-decode fallback interpreter entries
  (`dynamic_decode_and_execute`, `execute_single_instruction`,
  `handle_branch_logic`) and the dispatcher `runtime_syscall_dispatcher`.
- **Public surface**: all of the above + `SifDmaTransfer_t`. Included by
  syscalls.cpp, main.cpp, recompiled shards' syscall path, runtime.h (dead).
- **Classification**: GENERIC-PS2 (EE kernel/OS surface). Subsystem: ee/hle.
- **Refactor destination** (MIGRATION §1.3: "syscalls.cpp | ee/hle/syscalls.* +
  ee/hle/dyn_interp.* (fallback interpreter out of syscalls)"): split —
  - `engine/ee/hle/syscalls.h/.cpp` — the syscall table + handlers (class
    `SyscallTable` or concept-named free-function pair; numbering frozen);
  - `engine/ee/hle/dyn_interp.{h,cpp}` — class `DynInterp` (the rabbitizer-backed
    decode-and-execute fallback);
  - `SifDmaTransfer_t` → `engine/sif/` types.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  1. **SDL.h included by a kernel header** (:16) — layering violation (core rule: no
     SDL below app/platform); nothing in the declarations needs it; drop at move.
  2. **Rabbitizer include-order + macro-undef dance** (:8-13, `#undef CONST/PURE`
     "can corrupt other headers") — quarantine rabbitizer usage inside dyn_interp.cpp
     so no public header carries it.
  3. Grab-bag include fan-out (ps2_threading, ps2_syscall_wrappers, sema, intc) makes
     every includer see the world — the split slices it per D4.
- **Risks**: syscall numbering/semantics are the boot path. Proof: boot-to-title
  harness, thread/sema unit suites, determinism gates (the dispatcher is on the
  recompiled `syscall` seam, frozen per A4 §1.3).

---

### host_app/ring_log_format.h (75 lines)

- **Purpose**: The frozen binary layout of the ring-log dump format ([E2-LOG],
  `.rlog`): 24-byte `Event` (tag/channel/flags/frame/a0/a1), `FileHeader` (48 B,
  magic PS2RLOG1, dump reason enum: crash/hotkey/exit/fatal-nojump/test),
  `RingHeader` — POD only, no host_app dependencies, shared by the in-process writer
  (ring_log.cpp) and the standalone offline decoder (tools/ring_decode.cpp).
  static_asserts pin every size.
- **Public surface**: `ringlog::{Event, FileHeader, RingHeader, Channel, EventFlags,
  DumpReason, kMagic/kVersion/kNameMax}`. Consumers: ring_log.cpp, ring_log_decode.h,
  tools/ring_decode.cpp.
- **Classification**: GENERIC (core logging). Subsystem: core/log.
- **Refactor destination** (MIGRATION §1.4 diagnostics keep-list: "ring_log.{h,cpp},
  ring_log_format.h, ring_log_decode.h, ring_tags.h | core/log/ring.* sink + decode
  tool — verbatim"): `engine/core/log/ring_format.h` — verbatim (pure declarations;
  already passes §13; concept-named PODs are the allowed adjacent-POD case).
- **Flags consumed here**: none.
- **Hacks / workarounds**: none. ("All little-endian, we only run on LE hosts" is a
  documented platform assumption — carries into platform/ notes for the Wii/GC
  research stubs, which are BE.)
- **Risks**: writer/decoder drift. Proof: the byte-identical round-trip test between
  core/log ring writer and tools/ring_decode (M4A kept-log property); DUMP_FATAL_
  NOJUMP is part of the recomp_seams terminal path contract.

---

### host_app/sif_hle.h (56 lines)

- **Purpose**: Declares the EE-side SIF HLE surface: sceSifInitRpc/BindRpc/CallRpc,
  boot-time HLE seams (WaitForVblank, InitTLB, ContextRestore,
  DoGlobalConstructors), the anti-piracy no-op `FUN_00181490`, the engine-conditional
  re-install hook `recomp_install_engine_hle_hooks` ([RECOMP-P3b]), and the
  `sif_bind_rpc` client-binding tracker (Sony RPC server-id enum, BoundClient table).
- **Public surface**: the above + `sif_bind_rpc::{RpcServerId, BoundClient,
  InitBindingTracker, RegisterBinding, FindBindingBy*, GetServerName}`. Consumers:
  sif_hle.cpp, sif_rpc_handlers.cpp, main.cpp (install), recompiled seams.
- **Classification**: MIXED (A6 §4 row: the piracy bypass is the 1 GAME hook;
  the RPC surface is GENERIC Sony API). Subsystem: SIF.
- **Refactor destination** (MIGRATION §1.4 IOP runtime row + §1.6 row E): split —
  - `engine/sif/hle/rpc_router.{h,cpp}` — class `RpcRouter` (binding tracker +
    server-id vocabulary; invoked through the 291 sce registry rows, NOT independent
    address bindings);
  - sceSif handler decls fold into the sif/hle SdkBinding surface (ARCH §4.9);
  - boot seams (WaitForVblank/InitTLB/ContextRestore/DoGlobalConstructors) →
    `engine/ee/hle/boot_env.*` / kernel boot surface;
  - `FUN_00181490` (piracy-check bypass at 0x181490) — EMULATION-BUG-WORKAROUND per
    HOOK §3.5: quarantined as a named workaround-ledger row beside cdvd/sif, retired
    at the Phase 7 adjudication; if the v2 body still needs the patch it becomes a
    `TitleProfile.patches` row (never engine code);
  - `recomp_install_engine_hle_hooks` — dies in Phase 2 with the one-engine decision
    (RECOMP_ENGINE selector deleted).
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  1. **Game-address symbol in an engine header** (`FUN_00181490`, :12) — a
     lint_engine_purity token; resolved by the HOOK §3.5 quarantine/patch-row path.
  2. **Engine-conditional dual install** (:13-19, legacy-vs-v2 re-install of the
     piracy skip) — a mirror of the RECOMP_ENGINE gate; dies in Phase 2.
  3. Fixed-size global binding table (MAX_BOUND_CLIENTS=32, extern array) — becomes
     owned state of RpcRouter.
- **Risks**: RPC bind/call is the IOP lifeline; the piracy adjudication must be
  proven per-address. Proof: per-address harness adjudication (HOOK §4), boot leg
  with disc-auth path exercised, RPC round-trip suites (pad/mc/cdvd/sound).

---

### host_app/mipsrc_diag.h (52 lines)

- **Purpose**: [S100-MIPSRC]/[S100-MIPWR] one-off diagnostics: EE-source attribution
  for HOST→LOCAL uploads into the composite pool pages 0x300000-0x3fffff (GIF IMAGE
  scan with span→guest-address resolution) and a multi-range EE-RAM write-watch
  (fastmem watch raise funneling recompiled stores through write_slow for
  intersect checks). Implementations live in dmac.cpp/memory.cpp; this is the
  interface header.
- **Public surface**: `mipsrc::{Span, On, Scan, g_vif_feed_ee, RamBase/RamSize/
  SetRamBase, g_mipwr_on, MipwrInit, MipwrStore, MipwrBulk}`. Consumers: dmac.cpp,
  memory.cpp, vif DIRECT path.
- **Classification**: GENERIC diagnostic (one-off, concluded CAVERN_MIPSRC session).
- **Refactor destination**: **DELETE** — named explicitly in MIGRATION §1.1
  "One-off diagnostics: … mipsrc_diag … | concluded sessions (A2 §4) | delete". The
  generalizable idea (armed RAM write-watch over fastmem) is a core/diag capability
  if ever re-needed; the ledger closes this instance.
- **Flags consumed here**: none read in this header — PS2_MIPSRC_DIAG / PS2_MIPWR /
  PS2_MIPSRC_RAMDUMP are consumed at the dmac.cpp/memory.cpp sites (those shards'
  ledgers; all delete-both).
- **Hacks / workarounds**: VRAM pool-page range knowledge (0x300000-0x3fffff — the
  tile-atlas band) in a diag header — game VRAM layout in engine code; moot on
  deletion. Documented MTVU limitation ("run with PS2_MTVU=0") shows the probe never
  fit the peak config.
- **Risks**: none — delete with its implementation blocks; grep for `mipsrc::` after
  removal proves closure.

---

### host_app/gs_hle.h (28 lines)

- **Purpose**: Declares the sce graphics HLE seams the recompiler injects at SDK-band
  addresses: hle_sceGsSyncV (the de-facto frame-boundary owner), SyncPath,
  ResetGraph/ResetPath, DmaSync/GifSyncDma, SendImage, SetupDisplayRegisters, the
  dead hle_InitGraphics/hle_RenderLoop, plus globals: SDL window/renderer handles
  and `g_sceGsSyncV_count` (frame counter other HLE couples to).
- **Public surface**: the 10 `hle_*` decls, `g_window`, `g_renderer`,
  `g_sceGsSyncV_count`, `g_logFile` extern. Consumers: gs_hle.cpp, main.cpp installs,
  pad_input.cpp (SyncV cadence), loadspan.h et al.
- **Classification**: MIXED — sce library surface (GENERIC) + one GAME-code seam
  (SetupDisplayRegisters = FUN_0019aff0) + platform globals. Subsystem: GS/sce.
- **Refactor destination** (the detailed MIGRATION §1.4 gs_hle row governs; the
  header dissolves with the split): SyncV → gs/hle/sce + ee/timing BoundaryProtocol
  + core/savestate quiescence hook; SyncPath/DmaSync/GifSyncDma → gs/hle/sce +
  dmac/hle sync surface; ResetGraph/ResetPath → gs/hle/sce over gs/state/privileged;
  SendImage → gs/hle/sce over gs/xfer deposit; **SetupDisplayRegisters** →
  EMULATION-BUG-WORKAROUND quarantine (HOOK §3.5; retired when recompiled
  FUN_0019aff0 runs against gs/state/privileged); **hle_InitGraphics +
  hle_RenderLoop → DELETE as dead** (never installed / commented out);
  `g_sceGsSyncV_count` → frame counter published by ee/timing, consumed by pad/hle;
  `g_window`/`g_renderer` → platform/sdl. The 3 seam-only sce addresses become
  SdkBinding manifest rows in Phase 8.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  1. **SDL types in an HLE header** — layering violation; platform handles leave
     with platform/sdl.
  2. **Frame-counter as cross-subsystem coupling** (`g_sceGsSyncV_count` documented
     as pad-cadence source) — becomes a published ee/timing clock, not a gs_hle
     global.
  3. **Dead declarations kept** (hle_InitGraphics/hle_RenderLoop) — delete with
     their bodies (zero refs in generated shards, per the migration row's evidence).
- **Risks**: SyncV is the frame boundary — the single most timing-sensitive seam.
  Proof: BoundaryProtocol ordering tests, savestate-at-boundary chain tests, pace
  ledger determinism gates, full harness legs.

---

### host_app/recomp_seams.cpp (27 lines)

- **Purpose**: [RECOMP-P3a] host implementation of the v2 runtime seam
  `recomp_fcf_miss`: when the v2 common TU's find_containing_function misses ALL
  tiers on an indirect jump, this routes to `crashring_fatal_no_jump` (dump crash
  ring + guest ctx, `_exit(1)`) — identical policy to the legacy emission, kept as a
  separate TU so the verification harness can link an oracle-policy recording twin
  instead.
- **Public surface**: `recomp_fcf_miss(uint32_t pc)`. Linked by the v2 generated
  tree; compiled into host_app today (CMake ref verified), dormant until the P3b
  swap.
- **Classification**: GENERIC (EE dispatch seam). Subsystem: ee/cpu.
- **Refactor destination** (MIGRATION §1.4: "recomp_seams.cpp (frozen A4 §1.3 seam
  surface) | ee/cpu/seams.cpp — verbatim (surface frozen until Phase 8)"):
  `engine/ee/cpu/seams.cpp` — verbatim.
- **Flags consumed here**: none.
- **Hacks / workarounds**: none — this file is the CORRECT pattern (one seam, two
  linkable policies, never both in one binary). Its terminal path writes
  DUMP_FATAL_NOJUMP through the ring-log format above.
- **Risks**: the miss policy is the last line before undefined behavior on a bad
  indirect target. Proof: tests/recompiler_verify sut_runner (the oracle twin),
  corpus-as-audit (observed jr/jalr pairs vs static tables, GAPS=0), determinism
  gates.

---

### host_app/vu_aot_programs_7.cpp · _3 · _24 · _19 · _13 (17 lines each, 85 total)

- **Purpose**: Five of the 33 fixed, checked-in VU1 AOT chunk aggregator TUs
  ([R158-TUSPLIT], K=24 sharding): each `#include`s vu.h + vu_aot_registry.h +
  vu_decode_slot.h + vu_fast_exec.h and then textually includes the generated
  `vu_aot_generated_N.inc` (a valid empty TU when programs run out), exporting the
  chunk's `g_vu_aot_chunk_descs_N` array for the master registry. Verified: chunks
  3/13/19 differ from 7 only in the index + comment phrasing; chunk 24's banner says
  "24/32" — a stale K reference.
- **Public surface**: `g_vu_aot_chunk_descs_N` (external linkage), aggregated by
  vu_aot_programs.cpp at static init. Built into the `vu_aot_lib` static (root
  CMakeLists), not the host_app source list.
- **Classification**: GAME (generated payload per A6 §6 table :263 — "generated from
  SLUS_123.45; game-specific by construction"); the aggregator shells are build
  machinery.
- **Refactor destination**: **DELETE** with the VU-AOT tier (MIGRATION §1.1:
  "VU-AOT tier: vu_aot_gen.cpp, 33 chunk TUs, vu_aot_registry consumers, 6 ctest
  gates, 233 MB inc + 351 MB lib | default-OFF per NOAOT decision record").
- **Flags consumed here**: none (0 getenv; the tier's enable lives elsewhere and is
  already default-OFF).
- **Hacks / workarounds**: the textual `.inc` include pattern itself — exactly what
  §13 bans for hand-written code (generated exempt, but the tier dies regardless);
  the "fixed checked-in TU so CMake's source list stays static" trick (R126-SHARD)
  becomes moot.
- **Risks**: none — NOAOT is the shipping default; deleting removes 233 MB inc +
  351 MB lib from the build. Proof: default build already runs with
  PS2_VU_AOT_DISABLED; cached-tier corpus gates unaffected.

---

### host_app/runtime.cpp (0 lines) + host_app/runtime.h (5 lines, rider)

- **Purpose**: **DEAD**. runtime.cpp is a 0-byte file; runtime.h declares only
  `runtime_syscall_dispatcher` (which syscalls.h also declares and syscalls.cpp:2310
  defines).
- **Public surface**: none. Evidence: runtime.cpp has ZERO CMake references (the one
  grep hit is the substring of `hle/vu1_hle_runtime.cpp`); runtime.h has ZERO
  includers (grep `#include "runtime.h"` across host_app = 0 matches).
- **Classification**: dead scaffold.
- **Refactor destination**: **DELETE** (Phase 1, rides the "dead scaffolds" §1.1
  family). **MIGRATION-ROW CORRECTION**: §1.4's row "runtime.{cpp,h} (dispatcher
  entry, update_function_impl seam) | ee/cpu/dispatch.* + ee/hle glue — split" is
  STALE — the file is empty; the dispatcher entry actually lives in syscalls.cpp
  (`runtime_syscall_dispatcher`) and the `update_function_impl` seam sites live in
  main.cpp/sce_hle_registry/manual_switch_dispatchers/libc_hle/sif_hle/anim_pose_hle
  (verified by definition-site grep). The row's intent transfers to those files'
  rows; nothing here moves. Flagged in TREE-GAPS.
- **Flags consumed here**: none.
- **Hacks / workarounds**: n/a.
- **Risks**: none. Post-delete grep for `runtime.h` proves closure.

---

## SHARD SUMMARY

Flags column counts THIS file's table rows as N→NORM/SETTING/DIAG/TEST/DELETE.

| file | lines | class | destination | shape | flags(N→N/S/D/T/X) | hacks |
|---|---|---|---|---|---|---|
| vu.cpp | 12,475 | GENERIC-PS2 | engine/vu/{vu_unit,flag_pipeline,stall_model,xgkick,vu_float} + vu/tiers/{interp,cached} + core/diag/vutrace | split | 57 → 11/2/11/1/32 | 10 |
| ps2_scheduler_old_fiber.cpp | 2,169 | MIXED (DEAD) | — | delete (Phase 1) | 0 | 0 |
| vu_fast_exec.h | 1,833 | GENERIC-PS2 | vu/tiers/cached_tier.cpp + vu/vu_float | split (header dissolves post-NOAOT) | 4 → 1/1/0/0/2 | 3 |
| test_scanout_preservation.cpp | 1,058 | GENERIC test (dead) | — | delete (Phase 1) | 0 | 1 |
| test_bug_conditions.cpp | 789 | GENERIC test (dead) | — | delete (Phase 1) | 0 | 1 |
| test_vblank_preservation.cpp | 688 | GENERIC test (dead) | — | delete (Phase 1) | 0 | 1 |
| test_fbo_persistence_bug_condition.cpp | 637 | GENERIC test (dead) | — | delete (Phase 1) | 0 | 1 |
| test_comprehensive_rendering.cpp | 581 | GENERIC test (dead) | — | delete (Phase 1) | 0 | 1 |
| test_texture_shuffle.cpp | 519 | GENERIC test (dead) | — | delete (Phase 1) | 0 | 1 |
| snd_census.h | 454 | MIXED (diag) | core/diag/snd_census (title cmd-table as data) | move-to-diagnostics-build | 1 → 0/0/1/0/0 | 3 |
| iop_loader.cpp | 451 | GAME | engine/iop/module_loader + games/…/iop/stream_irx plugin | split | 0 | 3 |
| gs_mipmap.h | 358 | GENERIC-PS2 | gs/format/mip_chain + gs/hle/texsrc/mip_sampler | split | 5 → 3/0/1/0/1 | 4 |
| loadspan.h | 328 | GENERIC (diag) | core/diag/loadspan | move-to-diagnostics-build | 2 → 0/0/2/0/0 | 4 |
| time_domain.h | 319 | GENERIC-PS2 | ee/timing/{emu_domain,regime_detector} | split (logic verbatim) | 2 → 2/0/0/0/0 | 3 |
| vu_aot_config.h | 266 | GENERIC tooling | — | delete (NOAOT tier) | 0 | 1 |
| gs_deposit_overlay.h | 236 | GENERIC-PS2 | gs/vram/{vram_image,page_record} | rewritten/absorbed (Phase 5) | 1 → 1/0/0/0/0 | 4 |
| gif.h | 216 | GENERIC-PS2 | gif/{gif_tag,gif_unit,parse} + gs/hle/ir apply seam | split | 0 | 4 |
| ee_timers.h | 177 | GENERIC-PS2 | ee/intc/{ee_timers,timer_codec} (+TitleProfile seed) | split | 1 → 1/0/0/0/0 | 4 |
| shuf_snap.h | 149 | GENERIC-PS2 | gs/hle/classrdr/texture_shuffle_snap | purge-dissolve (snap stays) | 1 → 1/0/0/0/0 | 3 |
| iop_sysmem_heap.h | 148 | MIXED lean-GENERIC | iop/sysmem_heap | verbatim (h/cpp split) | 0 | 3 |
| s8_inrt.h | 124 | GENERIC-PS2 | gs/hle/texsrc/window_in_target → ONE ResolveRead | split/fold (ROW CORRECTED) | 1 → 1/0/0/0/0 | 3 |
| ipu.h | 113 | GENERIC-PS2 | ipu/{ipu_device,mpeg_decode} | split | 0 | 0 |
| vu_trace_case.h | 93 | GENERIC (diag fmt) | core/diag/vutrace_case (also triage producer, D9) | verbatim (format frozen) | 0 | 2 |
| syscalls.h | 86 | GENERIC-PS2 | ee/hle/{syscalls,dyn_interp} | split | 0 | 3 |
| ring_log_format.h | 75 | GENERIC | core/log/ring_format.h | verbatim | 0 | 0 |
| sif_hle.h | 56 | MIXED | sif/hle/rpc_router + ee/hle/boot_env + HOOK §3.5 quarantine | split | 0 | 3 |
| mipsrc_diag.h | 52 | GENERIC (one-off diag) | — | delete (§1.1 named row) | 0 | 1 |
| gs_hle.h | 28 | MIXED | gs/hle/sce + ee/timing + platform/sdl (per §1.4 row) | split/dissolve | 0 | 3 |
| recomp_seams.cpp | 27 | GENERIC | ee/cpu/seams.cpp | verbatim (frozen) | 0 | 0 |
| vu_aot_programs_7.cpp | 17 | GAME (generated shell) | — | delete (NOAOT) | 0 | 1 |
| vu_aot_programs_3.cpp | 17 | GAME (generated shell) | — | delete (NOAOT) | 0 | 1 |
| vu_aot_programs_24.cpp | 17 | GAME (generated shell) | — | delete (NOAOT) | 0 | 1 |
| vu_aot_programs_19.cpp | 17 | GAME (generated shell) | — | delete (NOAOT) | 0 | 1 |
| vu_aot_programs_13.cpp | 17 | GAME (generated shell) | — | delete (NOAOT) | 0 | 1 |
| iop_loader.h | 15 | GAME | rides iop_loader.cpp split | split | 0 | 0 |
| runtime.cpp | 0 | DEAD | — | delete (stale §1.4 row) | 0 | 0 |

Totals: 36 files, 24,605 lines. Flag rows: 75 → **21 NORM / 3 SETTING / 15 DIAG /
1 TEST-ONLY / 35 DELETE**. Files by shape: 14 delete, 13 split, 4 verbatim,
2 move-to-diagnostics, 1 rewritten/absorbed, 1 purge-dissolve, 1 rider (iop_loader.h).

## TREE-GAPS and plan-row corrections

1. **MIGRATION §1.4 misassigns s8_inrt.h** ("purge-dissolve into ee/timing + ee/hle"
   with w10_eefeed/s8_ctxlatch, apparently by s8_ prefix). It is GS texture-serve
   admission math; correct destination `engine/gs/hle/texsrc/` inside the §1.5
   "17 texture-serve paths → ONE ResolveRead" rewrite. No new tree node needed —
   row correction only.
2. **MIGRATION §1.4 runtime.{cpp,h} row is stale**: runtime.cpp is 0 bytes with zero
   CMake refs; runtime.h has zero includers. The row's content ("dispatcher entry,
   update_function_impl seam") actually lives in syscalls.cpp (dispatcher,
   syscalls.cpp:2310) and main.cpp + the five hook TUs (seam sites). Disposition
   here: delete both files; the row's intent transfers to those files' rows.
3. **A2 registry gap**: PS2_VU0_MODE / PS2_VU1_MODE (vu.cpp:726) are absent from the
   496-var registry. Proposed ledger rows: per-unit tier override, experiment,
   DELETE.
4. **A2 disposition deltas found by code reading** (for the FLAG_LEDGER consolidator):
   PS2_GS_DEPOSIT_EEAPPLY delete-both→**NORM** (default-ON proven fix, BUG-010;
   instructions name it NORM); PS2_EE_TIM0 keep-as-setting→**NORM** (canon-ON,
   OFF arm is the known-bad frozen-timer legacy); PS2_S8_INRT delete-both→**NORM**
   (deleting both arms would remove a proven serve fix); PS2_VU1_CYCLE_CAP
   delete-both→**TEST-ONLY** (offline oracle legitimately raises it);
   PS2_MIPCEN tuning-knob→**DIAG** (it is a census); PS2_VU_RUNFLAT_CENSUS
   move-to-diag→**DELETE** (its subject deletes); PS2_LOADSPAN delete-both vs
   MIGRATION's move-to-diagnostics-build — this shard follows the MIGRATION row
   (**DIAG**), flagging the conflict for the purge-procedure loop_state check.
5. **gs_mipmap MTBA=1 unimplemented** (gs_mipmap.h:196): a real GS rule (auto
   base-address mip chains) absent from the tree's gs/format scope notes. Not needed
   by this title (censused zero) — carry as a documented divergence in
   `gs/format/mip_chain`, not a new node. TREE-GAP only in the "document it" sense.
6. **ee_timers destination ambiguity** (minor): §1.3 says "ee/timing/* + ee/intc/",
   the tree comment puts "INTC + EE timers T0-T3" under ee/intc/. This shard picks
   `engine/ee/intc/ee_timers.*` with clock reads through ee/timing DerivedClocks —
   consolidator should standardize.

## Dead code found in this shard (with evidence)

- **runtime.cpp** (0 bytes) + **runtime.h** (never included; dispatcher declared in
  syscalls.h instead) — not in any CMake source list.
- **ps2_scheduler_old_fiber.cpp** — zero CMake refs; superseded by ps2_threading
  (A1 §5.1, A4 §2.3/B5).
- **test_scanout_preservation / test_bug_conditions / test_vblank_preservation /
  test_fbo_persistence_bug_condition / test_comprehensive_rendering /
  test_texture_shuffle** (.cpp ×6) — zero CMake refs each (verified per file);
  members of the §1.1 "23 host_app-root test_*.cpp" family.
- **vu_aot_programs_{3,7,13,19,24}.cpp** — compiled into vu_aot_lib but the tier is
  default-OFF (NOAOT decision record; PS2_VU_AOT_DISABLED default); §1.1 delete row.
  Chunk 24's banner still says "24/32" (stale K=32 reference) — evidence the shells
  are generator boilerplate, not maintained code.
- **gs_hle.h riders**: hle_InitGraphics / hle_RenderLoop declarations — bodies dead
  (commented out / never installed per the MIGRATION gs_hle row's evidence).

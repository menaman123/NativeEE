# FILEMAP — host_app shard 0 (35 files, 24,611 lines)

Shard scope: `/tmp/s116/s116_shard0.txt` — host_app top-level files incl. main.cpp.
Sources: ARCHITECTURE.md §3/§11/§13/§14-§18, MIGRATION_PLAN.md §1, HOOK_RECLASSIFICATION.md,
TRIAGE_CAPTURE.md, A2_CONFIG_DIAG.md (registry CSV + §2/§6), A6_GAME_SPECIFICITY.md.
Flag-table conventions: `disposition` = A2 registry category; `refactor outcome` ∈
NORM / SETTING(Config field) / DIAG / TEST-ONLY / DELETE. Peak config = run_play.sh
CANON (PS2_GS_SCALE=2 PS2_GS_INTERP=1 PS2_FPS_CAP=60 PS2_MTVU=1 PS2_GL_FAST=1
PS2_GL_FAST2=1 PS2_VU1_HLE=1 PS2_VU1_HLE_DIRECT=1) — everything it enables is NORM.

---

### host_app/main.cpp (14,690 lines)
- **Purpose**: The composition root and everything that never found a home: process
  startup (project-root chdir, log open + TagFilterBuf install, bench/warp/savestate
  CLI parsing), `CpuThreadFunc` (boots the recompiled ELF, installs ~200 dual-hooks +
  the manual-dispatcher/sce/libc HLE layers, then enters the dispatch loop), the SDL
  main event loop (input, F1 GUI, F5/F8/F9/F10 hotkeys, resize), and the shutdown/join
  ladder. It is the single largest concentration of session archaeology in the tree.
- **Public surface**: `main()`, `CpuThreadFunc()`, `install_dual_hook()` (main.cpp:464,
  the wrapper-over-`update_function_impl` used by all 200 hook sites), `TagFilterBuf`
  (log deny-filter streambuf), `M4aPrefixDroppedImpl` (published into
  `hwlog::g_prefix_dropped_hook`), `init_cpu_context()`, `s8cnt::` per-tag hit counters,
  `BullseyeDiag::` shared probe state. Everything else is TU-local. Linked only into
  host_app; no header of its own.
- **Classification**: MIXED (A6) — app glue is GENERIC; the hook layer, budget
  constants, scene addresses (0xd8c5d0 sail nodes, 0x31b138 heap, frame windows) are
  GAME-SPECIFIC. Subsystem: app/boot + ee glue.
- **Refactor destination** (MIGRATION §1.3 main.cpp row): split into
  `app/host/main.cpp` (≤500 ln composition root; class-free), `engine/core/config/`
  (loader), `engine/core/boot.*` (boot sequence), `app/host/shutdown.cpp`
  (`ShutdownLadder`, consuming shutdown_policy.h), hotkey/GUI event handling →
  `app/gui/` + `platform/sdl/input/`; the log filter layers DELETE into the
  `engine/core/log/` compile-time category logger; `init_cpu_context` →
  `engine/ee/cpu/`. Hook sites are NOT moved — they are dispositioned by class
  (below). New classes: `BootSequence` (core/boot), `ShutdownLadder` (app/host).
- **Hook sites grouped by HOOK_RECLASSIFICATION class** (200 live `install_dual_hook(`
  call sites = 201 grep lines − the :464 definition; authoritative count per
  MIGRATION §1.3 reconciliation):
  - **DIAGNOSTIC — 195 sites → DELETE outright.** All probe/tracer/watch wrappers:
    the [SEQ54] sail-node probes, [CTRL-SM]/BullseyeDiag animation probes, [S8-REG]/
    [S16-CHAIN]/[S26-DECAL] capped tracers, [ADAF0-DIAG]/[DESER] generator-relocated
    probes, memcpy/alloc watchers, the chained read-only 0x191bd0 companion probe,
    and the 4 DISABLED rows. Only the *mechanism* (`install_dual_hook` →
    `core/diag` probe API, PS2R_DIAGNOSTICS builds) survives (HOOK §3.1).
  - **EMULATION-BUG-WORKAROUND — 4 sites → quarantine beside the defect's layer,
    retire at root fix** (HOOK §3.1 table): `0x181560` [META-ARENA] allocator-metadata
    reroute (with hle_meta_arena, beside ee/mem; root fix = boot/loader divergences);
    `0x1b36b8` [RENDINIT] double-init free (root fix = retail-faithful boot-path
    selection in ee/hle boot env); `0x268440` [SESSION-14] type-registry populate
    append (root-fixed jointly with 0x1b36b8); `0x2ad110` [SESSION-15 Option B]
    dest-preservation for oversized async reads (root fix = cdvd/ through-the-stream-
    buffer delivery; its co-resident hold branch reclassifies as timing HLE, HOOK §3.6).
  - **HARDWARE/TIMING HLE — 1 site → engine mechanism + title data**: the gated
    `0x191bd0` [S96-BUDGETEMU] frame-budget predicate (main.cpp:3254-3611 +
    budget_math.h) → `ee/timing` DerivedClocks.budget_anchor; bases/phase thresholds
    become `TitleProfile.quirks.budget` data (ARCH §14.1). The [LDR-HLE] poll-spin
    early-out inside the same wrapper → cdvd/hle/loader (completion visibility).
  - **Adjacent install layers called from main.cpp** (not dual-hooks, counted in their
    own HOOK populations): `install_manual_switch_dispatchers()` at :5177 (235 installs,
    §3.2 → retired Phase 2), `install_libc_hle()` (§3.4 → ee/hle/libc),
    `register_sce_hle_overrides()` (§3.3 → SdkBindingManifest rows), anim-pose mirrors
    (§3.2), S8-ARGV writer (§3.7 → ee/hle/boot_env.*).
- **Flags consumed here** (45 getenv sites, 44 named vars + 1 name-loop):

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_LOG_FASTFILTER (:174) | ON-unless-0 | bulk-line vs per-char log filter path | diagnostic | DELETE (whole 4-layer filter dies into core/log) |
  | PS2_LOG_ELIDE_DROPPED (:331) | ON-unless-0 | dead-format elision for dropped tags | diagnostic | DELETE (core/log compile-time categories) |
  | PS2_ARGV (:561) | retail tokens | boot argv: retail argc=1 vs `legacy` invented argc=2 | tuning-knob / delete-losing-arm | NORM — retail argv only path; string becomes TitleProfile.boot data (HOOK §3.7) |
  | PS2_SMWATCH (:795) | OFF | scene-manager watch probe | diagnostic | DIAG |
  | PS2_SND_BANKTRACE (:843) | OFF | sound-bank trace probe | diagnostic | DIAG |
  | PS2_EULERTRACE (:1050) | presence-gated | [S30] slide-jump euler probe | keep-as-setting (A2 caveat: actually diagnostic) | DIAG (core/diag/stick_trace) |
  | PS2_TICKWATCH (:1139) | OFF | tick-cadence probe | diagnostic | DIAG |
  | PS2_EDGEPROBE (:1204) | OFF | present-edge probe | diagnostic | DIAG |
  | PS2_BUDGET_EMU (:3308) | num=1 (ON) | emulated-time budget predicate vs R145 host sawtooth | tuning-knob / delete-losing-arm | NORM (named in shard brief; emu-domain predicate is the only path) |
  | PS2_BUDGET_DIAG (:3315) | 0 | per-query budget value dump | diagnostic | DIAG |
  | PS2_BUDGET_STEP (:3424) | num=0 | budget step tuning | tuning-knob | NORM — tuned value bakes into TitleProfile.quirks.budget |
  | PS2_BUDGET_BASE (:3449) | num=-1 | synthetic base (8400..9700 tuned, 8800 shipped) | tuning-knob | NORM — TitleProfile.quirks.budget data |
  | PS2_BUDGET_PHASE (:3479) | ON-unless-0 | [S112] boot-phase classifier | proven-fix-hatch-ON | NORM |
  | PS2_BUDGET_LOWDRAW (:3484) | num=0 | low-draw phase threshold | tuning-knob | NORM — TitleProfile data |
  | PS2_BUDGET_BASE_LOW (:3490) | num=-1 | low-phase base | tuning-knob | NORM — TitleProfile data |
  | PS2_LVL_DIAG (:3524) | OFF | level-load diagnostic | diagnostic | DIAG |
  | PS2_SITEB_DIAG (:3623) | OFF | site-B probe | diagnostic | DIAG |
  | PS2_PROJ_DIAG (:3705) | OFF | projection probe | diagnostic | DIAG |
  | PS2_BAKE_DIAG (:3824) | num-if-set | bake-path probe (mirror of 7 gs_state sites) | diagnostic | DIAG |
  | PS2_LOG (:4905) | unset=open | runtime log open/closed | shipping-setting | SETTING → Config dev.log_level |
  | CT_FULL_LOG (:4943) | OFF-unless-1 | bypass the tag deny-filter | diagnostic | DELETE (filter dies; categories replace it) |
  | PS2_VUAOT_INI (:5002) | unresolved | vu_aot.ini path override | tuning-knob | DELETE (AOT tier deleted, NOAOT) |
  | getenv(v) loop (:5014) | — | scans VUAOT test-var names for collection arming | test-hook | DELETE (AOT) |
  | PS2_VUAOT_COLLECT_DIR (:5020) | unresolved | AOT capture dir | tuning-knob | DELETE (AOT) |
  | PS2_GUI_OPEN (:5047) | OFF | open F1 settings menu at startup | shipping-setting | SETTING → Config dev.gui_open |
  | PS2_BOOT_WATCH (:5074) | num-if-set | boot-progress watchdog print | diagnostic | DIAG |
  | PS2_POPUP_DIAG (:10305) | OFF-unless-1 | popup/dialog probe | diagnostic | DIAG |
  | PS2_B57Q (:10474) | OFF-unless-1 | LOADTIME B57 experiment | experiment | DELETE |
  | PS2_S8_REGHOOK (:13438) | numeric-other | [S8] register-hook tracer | experiment | DELETE |
  | PS2_S8_REGHOOK_CAP (:13447) | num=400 | tracer line cap | experiment | DELETE |
  | PS2_S8_REGHOOK_BATCAP (:13453) | num=100 | tracer batch cap | experiment | DELETE |
  | PS2_S8_FORCECLAIM (:13588) | OFF | [S8] force-claim experiment arm | experiment | DELETE |
  | PS2_S16_CHAIN (:13707) | num=400 | [S16] chain tracer | experiment | DELETE |
  | PS2_S16_CHAIN_CAP (:13712) | num=400 | tracer cap | experiment | DELETE |
  | PS2_S26_DECAL (:13868) | num=400 | [S26] decal tracer | experiment | DELETE |
  | PS2_S26_DECAL_CAP (:13872) | num=400 | tracer cap | experiment | DELETE |
  | PS2_S26_KILL (:13880) | numeric-other | [S26] decal kill-switch experiment | experiment | DELETE |
  | PS2_RING_TEST_CRASH (:14199) | unset | synthetic SIGSEGV at frame N (ring-dump e2e test) | test-hook | TEST-ONLY |
  | PS2_RING_TEST_HOTKEY (:14206) | unset | headless F9 snap verification | test-hook | TEST-ONLY |
  | PS2_RING_TEST_BUNDLE (:14210) | unset | headless F10 bundle verification | test-hook | TEST-ONLY |
  | PS2_SAVE_SLOT (:14271) | num=0 | F8 quick-load slot index | tuning-knob / delete-losing-arm | NORM — slot 0 only; the savestate-browser GUI panel is the real chooser |
  | PS2_W13R7_RELOAD_EVERY (:14318) | unset | cross-leg reload instrument | experiment | DELETE |
  | PS2_W13R7_CENSUS_EVERY (:14321) | unset | cross-leg census instrument | diagnostic(one-off) | DELETE |
  | PS2_W13R7_RELOAD_PATH (:14324) | unset | reload state path | experiment | DELETE |
  | PS2_TEST_RESIZE (:14479) | unset | scripted one-shot window resize `f:WxH` | test-hook | TEST-ONLY |

- **Hacks / workarounds** (each with the correct refactor implementation):
  1. The 195 diagnostic dual-hooks with hardcoded game addresses, host addresses and
     frame windows (e.g. [SEQ54] :1530-1650, nodes 0xd8c5d0/0xd8c1a0/0xd8c130/0xd8c050,
     frames ≥1180) — session archaeology for solved bugs. Correct: delete; probe
     mechanism only in core/diag (HOOK §3.1, A2 finding 8).
  2. The 4 behavioral hooks (:3095-3108, :4184-4200, :7989-8020, :10063-10078) — each
     masks a host divergence. Correct: root-fix at ee/hle boot env + cdvd delivery
     model, then delete (HOOK §3.1; harness-gated).
  3. Budget-emu hook with tuned constants inline (:3254-3611) — timing model living in
     a hook. Correct: `ee/timing` budget_anchor mechanism + TitleProfile.quirks.budget.
  4. Four retrofitted log layers (TagFilterBuf :125, [M3A] FastOn :174, [M4A] elision
     :331, PS2_LOG master :4905) doing a leveled logger's job (A2 finding 5). Correct:
     `core/log` compile-time category logger; all four die.
  5. `install_manual_switch_dispatchers()` call at :5177 runs hand bodies even on v2
     builds. Correct: Phase 2 per-address adjudication, then the call dies (HOOK §3.2).
  6. `BullseyeDiag` shared-state namespace + `s8cnt` exit-report counters — probe
     plumbing in the composition root. Correct: core/diag registry.
  7. ISO-path resolution by "join every remaining argv" + chdir-to-project-root by
     `../../..` from the exe (:4874-4884) — fragile path convention. Correct: Config
     + TitleProfile.elf/save paths; app/host resolves explicitly.
  8. F8 slot path `save_states/ps2state_slot%d.ps2st` hardcoded (:14271) — becomes the
     savestate browser / Config-owned path scheme.
- **Risks**: the hook-retirement order is load-bearing (libc install must precede the
  MemcpyDiag wrapper; sce registry installs last and wins) — a naive move changes
  winner-at-address semantics. The boundary between "boot sequence" and "CpuThreadFunc
  glue" must keep the frozen A4 §1.3 seam surface. Proof: Phase-0 coldboot gate +
  anchor-savestate harness OLD-vs-NEW byte-identity; hook-count reconciliation table
  (200/195/4/1) re-verified per wave; per-address Phase 2 ledger to zero.

---

### host_app/ps2_threading.cpp (1,481 lines)
- **Purpose**: The pthread-backed PS2 kernel thread/semaphore/event-flag HLE — one
  host pthread per guest thread with per-thread mutex/cv, THS_* state machine,
  create/start/exit/terminate/suspend/resume, sleep/wakeup with banked wakeups,
  sema wait/signal/poll, event flags, plus `initMainThread`/`initHeap` (the CRT0
  boot environment incl. the [S8-ARGV] argc/argv writer at args=0x30a980) and host
  thread registration/join.
- **Public surface**: `namespace ps2_threading` — createThread, startThread,
  startThreadResumed (savestate resume), exitThread, terminateThread, suspend/resume,
  sleepThread/wakeupThread/iWakeupThread, createSema/waitSema/signalSema/pollSema,
  event-flag ops, initMainThread/initHeap/endOfHeap, lookupThreadInfo,
  isGuestWorkerThread, registerHostThread/joinAllHostThreads/terminateAllThreads,
  perfCensusEmitIfDue. Consumers: syscalls.cpp (the syscall table), savestate.cpp,
  sif_hle.cpp, main.cpp.
- **Classification**: GENERIC-PS2 (EE kernel HLE; SDK 3.0.1 thbase semantics). The
  argv literal and the [SESSION-16 PTC-STUB-RESERVE] top-1MB heap clamp are
  GAME-SPECIFIC data. Subsystem: ee/hle.
- **Refactor destination** (MIGRATION §1.3: "ps2_threading.{h,cpp} → ee/hle/threads.*
  — split; map leaves the header"): `engine/ee/hle/threads.h/.cpp` (class
  `ThreadManager`), `engine/ee/hle/semaphores.h/.cpp` (class `SemaphoreTable`, event
  flags with it or as `event_flags.h/.cpp`), `engine/ee/hle/boot_env.h/.cpp` (class
  `BootEnvironment`: initMainThread/initHeap + the S8-ARGV layout writer, HOOK §3.7 —
  argv tokens from `TitleProfile.boot`), sema/thread perf census → `core/diag`.
  The PTC-STUB heap reserve moves to the title module (games/…/hle) with the PTC stub
  family.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_ARGV (:825) | retail tokens | same latch as main.cpp:561 (mirror) | tuning-knob / delete-losing-arm | NORM — retail argv only; TitleProfile.boot data |

- **Hacks / workarounds**:
  1. [S8-ARGV] env-selectable "legacy" invented-argv arm (:825, via ps2_argv.h) — the
     losing arm of a solved boot defect. Correct: BootEnvironment writes the retail
     layout only (HOOK §3.7 = SDK/OS HLE, retail-faithful).
  2. [SESSION-16 PTC-STUB-RESERVE] initHeap clamps EE RAM top 1MB (0x01F00000+) for
     synthesized .ptc views — a host invention supporting the PTC stub workaround.
     Correct: dies with the PTC-stub family when the cdvd delivery model is faithful
     (workaround-ledger row; games/…/hle until then).
  3. Guest-thread unwind via `ThreadExitException` thrown through recompiled frames —
     legitimate mechanism, but scattered; becomes a documented ThreadManager contract.
  4. Sema census counters + perfCensusEmitIfDue in the hot wait path — DIAG; moves to
     core/diag (counter registry), off in release.
- **Risks**: thread lifecycle races (savestate resume-into-running-thread, banked
  wakeups, suspend-while-waiting) are subtle and currently proven by soak; the split
  into three classes must not reorder lock acquisition. Proof: existing threading
  ctests + savestate resume anchors + a boot-to-gameplay soak leg; sema/wakeup
  semantics pinned against SDK 3.0.1 docs.

---

### host_app/ps2_vblank.cpp (1,167 lines; 1,262 after [TICKSYNC 2026-09-25])
- **[TICKSYNC 2026-09-25 addendum]**: the tick BODY is now the shared static
  `FireVblankTickBody()` (extracted verbatim from the ticker loop) so the legacy
  ticker and the EE-synchronous path (`SyncTickBody`, installed as the
  `eetiming::TickSync` hook at static init) run the identical body. Under
  `PS2_TICK_SYNC=1` the ticker thread exits at once, `waitForNextVBlankTick`
  fires one tick in-line and consumes it, `NoteTickDemand` /
  `NotifyCdvdCompletionDelivered` fire in-line on a guest EE thread (deferred
  otherwise), and injected CRTC presents carry a lockstep ticket. Destination
  addendum: `engine/ee/timing/tick_source.*` keeps the generator; the body
  becomes `engine/ee/timing/tick_body.*` shared with `tick_sync.*` (ARCH §7.2).
- **Purpose**: The vblank ticker thread (`VBlankWorkerFunc`): fires the 60 Hz tick
  train (count increment, sema signals, INTC VBON→TIM2→VBOF order, cv notify),
  absolute-deadline pacing ([R131-WAITFIX]), and the free-run regime (completion-paced
  ticks with a 16.67 ms timeout floor) with release sources: tick consumption
  ([R133-LOADTICK]), VSync retire ([R132]), CDVD delivery ([R134-CDVDRTT]), warp/turbo.
  Also `waitForNextVBlankTick()` (the EE-side park every frame ends in), the
  CRTC-fallback present injector gate, EE park-depth/stall-debt accounting
  ([S93-TILEPATCH]), and three dead bullseye probe dumpers.
- **Public surface**: `g_vblank_count`, `VBlankWorkerFunc`, `waitForNextVBlankTick`,
  `VBlankFreerunOn`, `timedom::BypassNow`, `PresentEdgeOn`, `CdvdInstantOn`,
  `NotifyCdvdCompletionDelivered`, `VBlankMaxConsumed`, `EeParkEnter/Leave`,
  `T0StallDebt`. Consumers: sif_hle (WaitForVblank), hle_heap (CDVD pump), ee_timers,
  time_domain.h, bench.h, graphics_thread.
- **Classification**: GENERIC-PS2 (vblank/CRTC timing model). The bullseye dumpers
  (:41-386) are GAME-SPECIFIC dead probes. Subsystem: ee/timing + ee/intc +
  gs/hle/present.
- **Refactor destination** (MIGRATION §1.3 timing row): split into
  `engine/ee/timing/tick_source.h/.cpp` (class `TickSource`: ticker thread, WAITFIX
  pacing, freerun release sources — ARCH §4.7), `engine/ee/timing/boundary_protocol.*`
  (park/consume accounting, EePark/T0StallDebt), INTC raise order stays via
  `engine/ee/intc/`, present-injector gate → `engine/gs/hle/present/pacing.*`.
  Bullseye dumpers DELETE (MIGRATION §1.1 one-off diagnostics row).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_VBLANK_FREERUN (:411) | rides PS2_NOVSYNC | completion-paced vs wall-clock ticks | shipping-setting | SETTING → Config engine.pacing (realtime\|freerun); env inheritance chain dies (A2 finding 3) |
  | PS2_PERF (:414,:448,:472) | OFF | gate self-report announce (3 of 21 mirror latches) | announce idiom | DELETE latches — replaced by one startup config dump (SETTING dev.perf_hud owns reporting) |
  | PS2_PRESENT_EDGE (:446) | ON | edge- vs level-triggered CRTC-fallback present | tuning-knob / delete-losing-arm | NORM (edge-triggered only) |
  | PS2_CDVD_INSTANT (:469) | rides FREERUN | on-demand deferred ISO reads vs 3-pass floor | shipping-setting | SETTING → Config loads.instant_cdvd (explicit field, no inheritance) |
  | PS2_INJ_PROBE (:873) | num-if-set | injector probe | diagnostic | DIAG |

- **Hacks / workarounds**:
  1. [BULLSEYE-SLOT-DUMP]/[BULLSEYE-TEMPLATE-PERIODIC]/[TEXT-SCAN] (:41-386) — RAM
     scans at hardcoded game addresses for a concluded investigation. Correct: delete.
  2. The NOVSYNC→FREERUN→CDVD_INSTANT default-inheritance cascade (:408-486) — one
     bench var silently flips three behaviors (A2 finding 3). Correct: explicit Config
     pacing enum + dependent-field validation in core/config.
  3. PS2_PERF announce re-reads inside other gates' latches — mirror hazard. Correct:
     single Config dump at startup.
  4. [S93-TILEPATCH] stall-debt bookkeeping feeding T0 — a timing patch pending the
     TimeDomain unification. Correct: EmuDomain owns stall accounting (ARCH §4.7).
  5. Warp/turbo dynamic OR into the freerun branch (:563+ worker loop) — regime
     switching by scattered predicates. Correct: TickSource takes one `PacingRegime`
     input owned by ee/timing.
- **Risks**: tick ordering (VBON→TIM2→VBOF), sema-signal fan-out and the
  consumption-release protocol are livelock-sensitive (the R156 warp lesson is cited
  in-source). Proof: pacing regression legs (paced vs freerun), LOADTICK consumption
  tests, BUG-014 (wall-clock time domain) repro, frame-time distribution equivalence
  on the bench anchors.

---

### host_app/sif_hle.cpp (884 lines)
- **Purpose**: EE-side sceSif* HLE bodies: sceSifInitRpc (hardware-derived struct
  writes + IOP handshake fake), sceSifBindRpc (binding tracker), sceSifCallRpc
  (packet-pool bypass; direct RPC dispatch), plus three boot-era HLEs that live here
  for historical reasons: `hle_WaitForVblank` (a frame-boundary owner: MTVU sync/defer,
  arbiter drain, VSync push, INTC/CSR writes, R170 pace park, S29 shutdown unwind),
  `hle_DoGlobalConstructors` (CRT0 ctor table walk), `hle_InitTLB`, `hle_ContextRestore`,
  and the 0x181490 piracy-check bypass with its v2-engine reinstall path.
- **Public surface**: `hle_sceSifInitRpc/BindRpc/CallRpc`, `hle_WaitForVblank`,
  `hle_DoGlobalConstructors`, `hle_InitTLB`, `hle_ContextRestore`,
  `FUN_00181490` (legacy engine) / `recomp_install_engine_hle_hooks()` (v2),
  `sif_bind_rpc::` tracker. Invoked through the 291 sce registry rows (HOOK §3.5),
  not independent address bindings.
- **Classification**: MIXED — sceSif surface is GENERIC (SDK 3.0.1); the piracy
  bypass, the InitRpc struct addresses (0x2ea850/0x3caa80…) and ctor-table address
  (0x2ebf04) are game-image facts that should come from signature-resolved bindings,
  not literals. Subsystem: sif/hle + ee/timing + ee/hle.
- **Refactor destination** (MIGRATION §1.4 sif row + gs_hle row): split —
  `engine/sif/hle/sceSif.h/.cpp` (class `SifRpcHle`: Init/Bind/Call bodies, addresses
  resolved via SdkBindingManifest rows); `hle_WaitForVblank`'s boundary logic merges
  into `engine/ee/timing/boundary_protocol.*` (the ONE frame-boundary sequence,
  shared with hle_sceGsSyncV) + `gs/hle/present` pacing; `hle_DoGlobalConstructors`/
  `hle_InitTLB`/`hle_ContextRestore` → `engine/ee/hle/boot_env.*`; the 0x181490
  bypass → EMULATION-BUG-WORKAROUND ledger row quarantined beside cdvd/sif, carried
  as `TitleProfile.patches` row until Phase 7 adjudication (HOOK §3.5).
- **Flags consumed here**: none (0 getenv). Consumes gates via accessors:
  shutdown_policy::WaitSecondsCached (PS2_SHUTDOWN_WAIT), vu1_async::DeferOn/
  VbsyncGateOn (PS2_MTVU_DEFER/PS2_MTVU_VBSYNC), ee_pace::PaceOn (PS2_EE_PACE) —
  dispositioned at their owning files.
- **Hacks / workarounds**:
  1. 0x181490 piracy-check bypass (:702-724, writes 0x30a458=0) — a game-behavior
     patch. Correct: named workaround-ledger row (HOOK §3.5); root adjudication in
     Phase 7 (either retail runs it clean under faithful cdvd, or it stays an explicit
     TitleProfile patch with rationale).
  2. Dual-engine seam: `#ifndef PS2_RECOMP_ENGINE_V2` symbol definition vs
     `update_function_impl` reinstall (:710-750) — two install mechanisms for one
     behavior. Correct: one SdkBinding/patch channel after the legacy engine deletes
     (Phase 2).
  3. sceSifInitRpc writes a dozen absolute game addresses inline (:60-115) — image
     facts as literals. Correct: offsets derive from the signature-matched sceSifInitRpc
     binding (SDK struct layout), or the recompiled body runs and the HLE dies.
  4. sceSifCallRpc reads args 5-8 from t0-t3 "because the recompiler optimizes away
     sw/lw pairs" (:790+) — an ABI coupling to a recompiler quirk. Correct: v2 emission
     is stack-faithful; HLE reads the SDK ABI locations.
  5. WaitForVblank carries five tags' worth of boundary logic (TryEagerDrain,
     ChaseVif1Tail, defer branch, VBSYNC conditional, R170 park) duplicated against
     hle_sceGsSyncV — mirrored frame-boundary code. Correct: ONE BoundaryProtocol class
     both HLEs call (ARCH §4.7).
- **Risks**: frame-boundary semantics are the most regression-prone spot in the tree
  (pacing, MTVU ordering, savestate boundary). Proof: boundary-protocol equivalence
  leg (SyncV-paced vs WaitForVblank-paced scenes), MTVU marker-order tests, savestate
  at-boundary load/save anchors, piracy-row adjudication harness leg.

---

### host_app/bench.h (713 lines)
- **Purpose**: The `--bench-frames N` harness runtime: CLI parse (strips bench args,
  self-arms PS2_PERF), per-present timestamp/draw/bundle/vtick rings, window
  snapshots (perf totals, census, RSS via mach phys_footprint), a macOS `funopen`
  stderr shim that re-parses census *lines* to recover counters private to other TUs,
  SIGINT/SIGTERM best-effort report write, and JSON report assembly via bench_report.h.
- **Public surface**: `namespace bench` — ParseArgs, Enabled, OnVsyncFrame (graphics
  thread), RegisterCensusCollector/RegisterHostHooks, WriteReportAtExit,
  InstallSignalHandlers, `State& S()`. Consumers: main.cpp, graphics_thread.cpp,
  slowspot.cpp (bench-mode capture suppression).
- **Classification**: GENERIC-PS2 (host tooling; no game facts). Subsystem: core/diag.
- **Refactor destination** (MIGRATION §1.4 durable-diagnostics row): 
  `engine/core/diag/perf_bench.h/.cpp` (class `BenchHarness`) — header-only becomes a
  proper .h/.cpp pair (D4); the totals-snapshot API additionally feeds
  `core/triage` PERF bundles (perf_buckets.tsv, TRIAGE §5.7). CLI flags stay flags
  (TEST/bench surface, not env).
- **Flags consumed here**: none read; **writes** `setenv("PS2_PERF","1",0)` ([M0C]
  self-arm). Outcome: the write dies with PS2_PERF's replacement (Config dev.perf_hud
  + startup config dump); bench arms reporting through Config, not env.
- **Hacks / workarounds**:
  1. The funopen stderr shim parsing its own process's census lines (:150-250) because
     the authoritative counters live in an anonymous namespace of a "frozen TU"
     (vu1_async.cpp) — string-scraping as an API, macOS-only. Correct: counters exposed
     through the core/diag registry; shim deletes.
  2. `#undef PAGE_SIZE` include-order workaround (:60) — dies when platform/ owns mach
     includes.
  3. Signal-path report write documented as not async-signal-safe ("accepted") —
     keep, but isolate in the diag module with the same documented contract.
  4. [S22-BENCHTICK] process-local tick vs absolute vf — correct design, keep (this is
     a rule: bench indexes by local tick).
- **Risks**: the bench numbers gate every perf phase of the migration; changing the
  window/snapshot semantics breaks comparability with the frozen baselines. Proof:
  side-by-side bench_report.json on the same savestate anchors old vs new (fields
  bit-compared where deterministic).

---

### host_app/vu_decode_slot.h (648 lines)
- **Purpose**: The canonical constexpr VU slot decode ([R153-TUSPLIT]): 
  `DecodeStallStatics` (read/write sets, IALU hazards, EFU/FDIV pipe ops — the decode
  half of ApplyFmacReadStalls, mirrored statement-for-statement) and `DecodeVuSlot`
  (full VuDecodedSlot materialization) shared verbatim by vu.cpp and the (retiring)
  AOT generator so decode semantics cannot diverge between tiers.
- **Public surface**: `static constexpr DecodeStallStatics(VuDecodedSlot&)`,
  `DecodeVuSlot(...)` (internal linkage per TU by design). Included by vu.cpp,
  vu_span_classify.h, all 33 vu_aot_programs_*.cpp, recompiler_tool/vu_aot_gen.cpp.
- **Classification**: GENERIC-PS2 (VU ISA decode). Subsystem: vu/.
- **Refactor destination** (MIGRATION §1.2 explicit row): `engine/vu/decode.h` —
  verbatim move ("ONE decode" per ARCH §3 vu/ comment). Free-function concept pair;
  the `static constexpr`-per-TU trick is acceptable under the .inl/template allowance
  but should become plain `constexpr` in one header. AOT includers die with the tier.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  1. The mirror obligation itself — "MUST mirror ApplyFmacReadStalls statement-for-
     statement", enforced only by the vu1_replay --compare gate. Correct: after the
     move there is ONE decode consumed by both the interp and cached tiers directly
     (no runtime twin to drift against); the replay gate stays as the proof.
  2. VF00/empty-mask suppression comment cites PCSX2 — keep as a hardware-invariant
     comment (allowed "why" form).
- **Risks**: any decode drift breaks both tiers subtly. Proof: vu1_replay --compare
  over the vutrace corpus (FAIL=0), plus the per-entry oracle_case_ids gates once the
  library registers (ARCH §14.2).

---

### host_app/ipu.cpp (600 lines)
- **Purpose**: The IPU (Image Processing Unit) device core: command/ctrl register
  protocol (WriteCmd/ReadCmd/WriteCtrl/ReadCtrl/ReadBP/ReadTop), input FIFO feed, and
  the MPEG-adjacent decode commands — BCLR, SETIQ, SETTH, FDEC, BDEC, CSC (macroblock
  color-space conversion), IDEC (intra decode: VLC tables, zigzag, IQ, IDCT). Used by
  the FMV pipeline and boot-time texture decompression.
- **Public surface**: `class IPU` + global `g_ipu`. Consumers: memory.cpp (MMIO
  0x10002000 routing), dmac.cpp (ch3/ch4 to/from IPU), mpeg_hle.cpp (FMV).
- **Classification**: GENERIC-PS2 (hardware decoder model). Subsystem: ipu/.
- **Refactor destination** (MIGRATION §1.4: "mpeg_hle.{cpp,h} + ipu.{cpp,h} →
  ipu/hle/mpeg_ipu.* — split"): device core → `engine/ipu/ipu.h/.cpp` (class `Ipu`:
  registers, FIFO, commands), decode helpers (VLC/IDCT/CSC tables) →
  `engine/ipu/decode.h/.cpp`; the sceMpeg-facing HLE stays in `engine/ipu/hle/`
  (with mpeg_hle, other shard). MMIO registration via the ee/mem router.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  1. IDEC/BDEC are partial implementations sufficient for this game's FMV/texture use
     (per the project HLE strategy) — divergence from full IPU spec. Correct: keep the
     scope, but mark each stubbed command with a `// diverges:` comment in ipu/hle per
     ARCH §11, so the divergence census sees it.
  2. `#define M_PI` fallback ([U1-WIN]) — fine, or use a local constant.
- **Risks**: FMV regressions (the `_bstag:48` class is benign, but IDEC output feeds
  texture decompression at load). Proof: FMV frame-hash anchors + the boot-load
  texture corpus (gs_corpus) before/after the move.

---

### host_app/hle_heap.h (521 lines)
- **Purpose**: Misnamed grab-bag header (the "heap" name is historical): (a) the
  `HLEHeap` class — the EE kernel InitHeap allocator (first-fit free list + downward
  bump, savestate serialize/restore/validate); (b) the deferred async ISO read queue
  (`DeferredIsoRead` with delay ticks, DVD-model due times, causal-order stamps) and
  its completion-visibility API (`chunk_gate::` ARM-A/E/C/V release arms,
  HoldCompletionForOrder); (c) HLE_00xxxx declarations for the libc/MC families;
  (d) ISO9660 lookup (`HostIso_FindFile`), PTC-record lookup, `SndRegEnabled`.
- **Public surface**: `HLEHeap` + `g_heap`; `chunk_gate::` namespace;
  ProcessDeferredIsoReads/HasPendingDeferredReadFor/Take-ClearAsyncCallbackPending;
  HleHeap_Serialize/RestoreCdvdFiles; HLE_002c6ce0-family decls; MC HLE state
  globals. Included by 10 host_app .cpp files (main, memory, savestate,
  sif_rpc_handlers, iop_stubs, iop_loader, ps2_threading,
  ps2_scheduler_old_fiber, sce_hle/libcdvd, hle_heap itself — per grep;
  artdet_flicker does NOT include it).
- **Classification**: MIXED — the read engine/latency model/ISO walker are
  GENERIC-PS2 (cdvd hardware/timing); the chunk_gate ring addresses (0x3b01e0…,
  0x31b138, FUN_ numbers) and PTC paths are GAME-SPECIFIC. Subsystem: cdvd/ + ee/hle.
- **Refactor destination** (MIGRATION §1.4: "hle_heap.{cpp,h} → cdvd/ — split + gate
  purge"): split into `engine/cdvd/read_engine.h/.cpp` (class `CdvdReadEngine`:
  DeferredIsoRead queue + pump + latency model, consuming ee/timing clocks),
  `engine/cdvd/iso_fs.h/.cpp` (class `IsoFileSystem`: ISO9660 walker + handle
  registry), `engine/cdvd/hle/loader.h/.cpp` (class `LoaderCompletionModel`: the
  chunk_gate completion-visibility arms — HARDWARE/TIMING HLE, HOOK §3.6),
  `engine/ee/hle/heap.h/.cpp` (class `KernelHeap` = HLEHeap, savestate SectionSpec
  beside it); the game's drain-ring addresses and FUN anchors become
  `games/<title>/hle/loader_sync.cpp` data; PTC records → title
  hle (retire with the 0x2ad110 root fix); HLE_00xxxx decls die into ee/hle/libc and
  mc/hle rows.
- **Flags consumed here**: none directly (documents PS2_CHUNK_APPLY_ORDER /
  _SCOPED / _DORMANT / _VALVE_TICKS and PS2_SNDREG — all read in hle_heap.cpp /
  other shard). Outcome preview for those (per A2 + HOOK §3.6): the scoped causal
  gate is the winning arm → NORM as LoaderCompletionModel behavior; the S57-era
  pump-hold arm and the tick knobs become named constants; PS2_SNDREG → NORM.
- **Hacks / workarounds**:
  1. The chunk_gate arm machinery binds to 9 hardcoded drain-ring descriptors and
     FUN_00264948/FUN_001a1bb8 hook observations — retail's teardown→drain→apply
     contract enforced from outside the game. Correct: cdvd delivery through the
     stream buffer (0x2ad110 root fix) makes visibility retail-shaped; the model
     keeps only the timing observable (completion latency), title anchors move to
     games/…/hle/loader_sync data.
  2. `HLEHeap` logging via printf+g_logFile both (init/out-of-memory) — core/log.
  3. Async-callback pending marks replicating IOP DMA completion ordering
     (FUN_002ad110 contract) — becomes an explicit CdvdReadEngine completion FSM
     with the rule documented as a hardware invariant.
  4. Two allocators coexisting (HLEHeap downward vs sbrk upward) with a comment
     explaining the truce — keep the rule, state it on KernelHeap.
- **Risks**: load-time behavior (beach.sm2 crash class, frame-75 Hub_01 class) is
  pinned by delivery ordering; any split must preserve arm priority order. Proof:
  LOADTIME/LOADER legs (load-span duration + release tallies), savestate CDVD-files
  section round-trip test, BUG-001/BUG-004/BUG-005 repro bundles.

---

### host_app/libc_hle.cpp (495 lines)
- **Purpose**: [R138-LIBCHLE] Guarded native fast paths for the SDK 3.0.1 static-libc
  memory/string family: memcpy 0x2c6ce0 (shipping-contract), memmove 0x2c6d8c,
  memset 0x2c6e88, strlen 0x2c72d4 — each replicating the recompiled body's EXACT
  register effects incl. 128-bit residues; any argument the guard cannot prove safe
  (RAM range, vtable band 0x2f6930-0x2f6a00, overread margin) falls back to the
  captured original. Installer wraps existing dispatch entries via
  update_function_impl.
- **Public surface**: `libc_hle_native_memcpy/memmove/memset/strlen`,
  `install_libc_hle()` (libc_hle.h). Consumers: main.cpp (install), tests/libc_hle_diff.
- **Classification**: GENERIC-PS2 (toolchain-lib HLE, signature-band addresses).
  Subsystem: ee/hle. HOOK §3.4: SDK/OS HLE, 4 bindings.
- **Refactor destination** (MIGRATION §1.4: verbatim): `engine/ee/hle/libc.h/.cpp`
  (concept pair `LibcHle`); addresses become SdkBindingManifest rows
  (signature-matched, ARCH §4.9) instead of literals; guarded-fallback semantics
  survive verbatim.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_LIBC_HLE (:440) | ON-unless-0 | native fast paths vs pre-R138 dispatch entries | proven-fix-hatch-ON | NORM (A1 §5.3 expired hatch; MIGRATION §3 wave lead — install unconditional, gate + skip arm deleted) |

- **Hacks / workarounds**:
  1. The vtable-protect band [0x2f6930,0x2f6a00) fallback — a game-image fact guarding
     a diagnostic filter (VTABLE-PROTECT lives in the old HLE). Correct: when the
     write-watch diagnostics move to core/diag builds, the band guard follows them
     (release build: plain range guard only); band value, if still needed, is title
     data.
  2. [S100-MIPWR] write-watch hooks inline in the hot copy paths (default OFF) — DIAG;
     compiled out of release via core/diag.
  3. Register-residue emulation (stale-upper-half reads replicated) — not a hack; the
     shipping contract. Keep, pinned by tests/libc_hle_diff.
- **Risks**: recompiled callers may read any register after JAL — the earlier strlen
  HLE failure ("rendering stopped") is the cautionary case. Proof: tests/libc_hle_diff
  randomized register-exact diff vs recompiled bodies (existing), re-run after move.

---

### host_app/libc_hle.h (31 lines)
- **Purpose**: Declarations for the four libc natives + installer, with the install-
  order contract (after dispatch init/manual dispatchers/sce registry, before
  MemcpyDiag wrappers) documented.
- **Public surface**: the four `libc_hle_native_*` + `install_libc_hle`. Included by
  libc_hle.cpp, main.cpp, tests.
- **Classification**: GENERIC-PS2. Subsystem: ee/hle.
- **Refactor destination**: `engine/ee/hle/libc.h` — verbatim (pairs with the .cpp).
- **Flags consumed here**: none (documents PS2_LIBC_HLE; see .cpp — NORM).
- **Hacks / workarounds**: the install-order prose is a hidden dependency chain —
  becomes explicit ordering in app/host boot code (one install sequence, asserted).
- **Risks**: same as .cpp.

---

### host_app/test_alpha_blend_bug_condition.cpp (453 lines)
- **Purpose**: A root-level "expected-to-fail" bug-condition exploration test for the
  alpha-blend-equation propagation gap (RenderBatch lacked ALPHA A/B/C/D/FIX fields).
  Self-contained main() linking gs_state/gif/texture_decoder with stub externs.
- **Public surface**: standalone main(); nothing includes it.
- **Classification**: GENERIC-PS2 (test scaffold). Subsystem: tests.
- **Refactor destination**: **DELETE** (MIGRATION §1.1 row: "23 host_app-root
  test_*.cpp, zero CMake refs"). Evidence: no match in host_app/CMakeLists.txt, root
  CMakeLists.txt, or tests/CMakeLists.txt (grep 2026-09-23); only stale build-dir
  CMakeFiles reference it. The bug it explored is since fixed (blend equation
  propagation exists); live coverage belongs to tests/ replay suites.
- **Flags consumed here**: none.
- **Hacks / workarounds**: n/a (the file *is* the artifact).
- **Risks**: none — dead code. Proof: build passes without it (already does).

---

### host_app/vu1_async.h (358 lines)
- **Purpose**: Interface of the MTVU-style dedicated VU1+VIF1 worker: lifecycle
  (Start/Stop/Active), the ordered byte-segment ring producer edge (SubmitVif1,
  [W10-EEFEED] staged batching), the Sync barrier with per-site attribution
  (sync_site:: enum), eager/park drains ([R124]/[R168-DEFER] boundary slots), the
  [W13R3-E3] one-slot exec-thread handoff, VIF1_STAT busy-view, and VU1's private
  register context (`Vu1Context()` — separate file, as real hardware has).
- **Public surface**: `namespace vu1_async` — ~30 functions listed above plus
  W16 dmac-depth/pause hook slots. Consumers: dmac.cpp (feed), memory.cpp (readback
  syncs), gs_hle/sif_hle (boundary), vu.cpp, gif_arbiter, graphics_thread, bench.
- **Classification**: GENERIC-PS2 (MTVU mechanism; PCSX2-precedented). Subsystem:
  vu/tiers.
- **Refactor destination** (MIGRATION §1.4: "vu1_async.{cpp,h} →
  vu/tiers/mtvu_worker.* — split"): `engine/vu/tiers/mtvu_worker.h/.cpp` (class
  `MtvuWorker`: ring, lifecycle, submit edges), `engine/vu/tiers/mtvu_sync.h/.cpp`
  (class `MtvuSync`: Sync/site census/eager drains — census part to core/diag),
  exec-thread handoff stays inside MtvuWorker; `Vu1Context` ownership moves to the
  VU1 instance proper (vu/). VIF-submit equivalence gate stays a test.
- **Flags consumed here**: none directly (header declares accessors; all getenv in
  vu1_async.cpp, another shard). Declared-flag outcomes for cross-reference (per A2 +
  peak config): PS2_MTVU → SETTING engine.mtvu (peak=1 default true);
  PS2_MTVU_DEFER / PS2_MTVU_VBSYNC / PS2_MTVU_EAGER_DRAIN / PS2_MTVU_PATHSYNC_RELAX /
  PS2_VU1_EXEC_ASYNC → NORM (MTVU quartet = A1 §5.3 expired hatches, MIGRATION §3);
  PS2_VU1_EXEC_WAIT_SPIN_NS → NORM (constant).
- **Hacks / workarounds**:
  1. Hook-slot dependency inversions (W16DmacDepthHook, SetEagerDrainHook) so test
     binaries link without dmac/gif_arbiter — becomes constructor injection on
     MtvuWorker (explicit collaborators), not global atomic slots.
  2. Boundary census feeders declared here so gif_arbiter.h needs no perf include —
     include-graph contortion; dies with proper module layering.
  3. The "frozen TU" status of vu1_async.cpp (bench scrapes its stderr) — dies when
     counters live in the core/diag registry.
- **Risks**: ordering contract (marker queue = inline stream order) is the whole
  correctness story; the F5 self-deadlock class is documented in-header. Proof:
  vif_replay --mtvu staged iterations (BatchSegmentsProcessed asserts), marker-order
  tests, W8 U5 boundary co-promotion proof retained as a test.

---

### host_app/artdet_flicker.cpp (338 lines)
- **Purpose**: [FL3-ARTDET] In-app left-zone period-2 flicker detector: per-present
  GL readback of a 9/16-width zone, per-band |Δluma| lag-1/lag-2 stats + lag-1
  autocorrelation over a 32-present window; on fire writes a bug_reports/ bundle
  (PPM + stats txt + async savestate) with cooldown and 8-capture cap. Has a
  synthetic ABAB test mode.
- **Public surface**: `artdet_flicker::Enabled()`, `OnPresent(frame, dw, dh)`.
  Consumer: graphics_thread present path; flicker ctest suite.
- **Classification**: GENERIC-PS2 (host artifact detector; thresholds calibrated on
  this game but parameterizable). Subsystem: core/diag + core/triage.
- **Refactor destination** (MIGRATION §1.4 [REV3 D9] row): split —
  detector/metrics → `engine/core/diag/artdet.h/.cpp` (class `ArtifactDetector`,
  diag builds; flicker ctest unchanged); the FIRE/WriteBundle path (:193-250 flat
  files into bug_reports/) DELETES — fires report through `core/triage/TriageHost`
  as a spike-class bundle (TRIAGE §12; caps/cooldown become TRIAGE §3 constants —
  the 8/session cap is the cited precedent).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_ARTDET_TEST (:43) | OFF | synthetic ABAB verification clears | test-hook | TEST-ONLY |
  | PS2_GS_SCALE (:233) | read-only echo into bundle env record | shipping-setting | SETTING (video.scale=2 default; here just bundle metadata via Config dump) |
  | PS2_GS_INTERP (:234) | read-only echo into bundle | shipping-setting | SETTING (video.interp=true default; bundle metadata via Config dump) |
  | PS2_ARTDET (:255) | OFF | master detector gate | diagnostic | DIAG (diag-build detector; triage spike class covers release) |
  | PS2_ARTDET_FLICKER (:257) | rides master | flicker sub-gate | tuning-knob / delete-losing-arm | NORM — sub-gate folds into the master (one diag switch) |

- **Hacks / workarounds**:
  1. Raw glReadPixels + GL calls in a "detector" TU (backend bypass) — correct home is
     a backend-provided readback tap (render/backend.h capability) feeding the
     detector pure pixel buffers.
  2. Bundle writes flat files with epoch suffixes into bug_reports/ — replaced by
     TriageBundler layout (games/<title>/triage/spikes/…, dedup + retention §8).
  3. Env echo of PS2_GS_SCALE/INTERP into the txt — replaced by the triage bundle's
     canonical config.json (whole Config dump).
  4. Thresholds as file constants calibrated once (kD1Thresh…) — by TRIAGE §3 rule
     these stay constants in code (documented basis), not config. Keep.
- **Risks**: readback cost on the present path (measured ~ms) must stay out of release
  builds; the fire criteria are tuned. Proof: flicker ctest suite (synthetic ABAB),
  cost census line, triage self-inflicted masking test (capture must not report itself).

---

### host_app/slowspot.cpp (306 lines)
- **Purpose**: [R190-SLOWSPOT] frame-cadence watchdog glue over the pure Detector:
  two detectors (present-interval p50 and [R191-GAMESPEED] wall-time-per-game-frame),
  FMV suppression, [FIX-SLOWSTICK] capture opt-in, session dedupe by frame bucket,
  capture trio (auto-savestate request + display snap + ring bundle) with
  session-unique names, exit summary, and bug-report bundle registration.
- **Public surface**: `slowspot::OnPresent(now_us, vf, ee_frame)`, `OnSnapWritten`,
  `PrintExitSummary`. Consumer: graphics_thread present path; main exit path.
- **Classification**: GENERIC-PS2 (perf watchdog; thresholds are project-calibrated
  constants). Subsystem: core/triage.
- **Refactor destination** (MIGRATION §1.4 [REV3 D9] row): 
  `engine/core/triage/perf_monitor.h/.cpp` (class `PerfMonitor` absorbs both
  detectors + FMV/self-inflicted masking); the capture trio (state/snap/_logs naming)
  retires into `TriageBundler` bundle layout; dedupe generalizes into `TriageDedup`
  (§8); exit summary becomes triage index reporting. `dev.slowspot_capture` Config
  field is absorbed by the `triage.*` section (ARCH §4.8 amended, TRIAGE §3).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_SLOWSPOT (:36,:39,:55) | detect ON; capture only when =1 (bench: force =1) | detector + capture opt-in | shipping-setting | SETTING → Config triage.perf (master triage.enabled; capture always bundled per TRIAGE §3 — the opt-in nuance is replaced by self-inflicted masking + caps) |
  | PS2_SLOWSPOT_DEDUPE (:62) | ON-unless-0 | per-session scene dedupe | tuning-knob / delete-losing-arm | NORM — dedup is TriageDedup, always on (§8) |
  | PS2_SLOWSPOT_MS (via EnvDouble :68) | 40.0 | present-p50 threshold | tuning knob (non-literal getenv) | NORM — derived-threshold constant (TRIAGE §3: thresholds are constants, basis documented; fps_target Config field feeds the rule) |
  | PS2_SLOWSPOT_COOLDOWN_S (via EnvDouble) | 30.0 | trigger spacing | tuning knob | NORM — constant (30 s, TRIAGE §3) |
  | PS2_SLOWSPOT_GF_MS (via EnvDouble) | 33.4 | game-frame threshold (30 fps line) | tuning knob | NORM — derived from triage.fps_target |

  Consumed-elsewhere but shaping this file: PS2_SLOWSPOT_ASYNC (savestate.cpp; A1
  §5.3 expired hatch → NORM, async PPM copy only path), PS2_SLOWSPOT_SUFFIX (see
  slowspot_paths.h).
- **Hacks / workarounds**:
  1. Capture-cost-versus-detection tension patched twice ([FIX-SLOWSTICK] opt-in,
     [W13R7-A7] bench suppression) — symptoms of capture running on the graphics
     thread. Correct: TRIAGE §9 cost budget (async writer, ring states, self-inflicted
     masking) makes capture safe-by-construction; both patches dissolve.
  2. Graphics-thread-only statics + a pending-snap string handoff — becomes explicit
     PerfMonitor state with the documented single-thread contract.
  3. Frame-bucket dedupe (vf/600 in a 16-slot array) — replaced by TriageDedup keys.
  4. FMV suppression via mpeg_hle::DecodeActiveRecently(250) — keep the rule, expressed
     as a FrameAnnotator input (TRIAGE §2).
- **Risks**: the watchdog is the user's primary perf evidence channel during play;
  regressions are silent. Proof: tests/test_slowspot (detector equivalence) retarget,
  synthetic slow-frame injection leg, triage bundle-content spec check (§5.7).

---

### host_app/slowspot_detector.h (129 lines)
- **Purpose**: Pure O(1) sliding-window slow-spot decision logic: ring of present
  intervals, incremental over-threshold counter with the exact p50 equivalence
  (`p50 > T ⇔ count(>T) ≥ n − n/2`), cooldown, percentiles computed only on the cold
  trigger path, Reseed for load/warp discontinuities.
- **Public surface**: `slowspot::Config`, `slowspot::Detector` (OnPresent, P50Ms,
  P95Ms, Reseed, BruteForceP50 test hook). Consumers: slowspot.cpp,
  tests/test_slowspot.cpp.
- **Classification**: GENERIC-PS2 (pure algorithm). Subsystem: core/triage.
- **Refactor destination** (MIGRATION §1.4): `engine/core/triage/perf_monitor.*` —
  the Detector core moves verbatim as the window-p50 engine of `PerfMonitor` (TRIAGE
  §4.2 names the same O(1) core; SpikeDetector §4.3 reuses the ring). One-class rule:
  it may stay its own pair `engine/core/triage/window_median.h/.cpp` (class
  `WindowMedianDetector`) consumed by PerfMonitor — proposed shape.
- **Flags consumed here**: none (defaults 40 ms/30 s/60 documented with their A/B
  basis — stays a constants-in-code table per TRIAGE §3).
- **Hacks / workarounds**: none — this is the model file (pure, unit-pinned,
  documented basis). The A/B-derived defaults comment is the allowed "why" form.
- **Risks**: none beyond arithmetic equivalence. Proof: tests/test_slowspot brute-force
  equivalence (exists; retargets).

---

### host_app/cpu_state.h (299 lines)
- **Purpose**: The frozen `EmotionEngineState`/`CpuContext` register-file family:
  128-bit GPR union, CP0, FPU, and the VU0-macro `VUregs` (VF/VI/ACC/Q/P/I/R, flag
  regs, control-reg read/write with hardware masking rules) — plus the PS2-float
  helpers (`ps2_float_clamp`, `ps2_div_s`, `ps2_sqrt_s`, `ps2_rsqrt_s`: FE_TONEAREST
  swap around DIV/SQRT/RSQRT, single-precision, no-NaN clamp) and VR_reg operator
  overloads that clamp per PS2 semantics.
- **Public surface**: every recompiled function's signature type; included
  universally (generated shards, all subsystems, tests). `g_cpuContext` extern.
- **Classification**: GENERIC-PS2 (EE/VU architecture). Subsystem: ee/cpu.
- **Refactor destination** (MIGRATION §1.2 verbatim row): `engine/ee/cpu/cpu_state.h`
  (+ common/Pcsx2Types.h alongside) — verbatim; explicitly EXEMPT from the
  one-class-per-file lint (ARCH §13 exemption 2, "the frozen CpuContext family").
  The PS2-float free functions single-source into `engine/vu/vu_float.h/.cpp`
  (ARCH §3 vu/ row; A6 §7 row A names the collapse — the ~140 duplicated lines in
  vu1_hle_programs.cpp fold into the same home), with cpu_state.h consuming them.
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  1. `volatile float` + fesetround swap in ps2_div_s/sqrt/rsqrt — not a hack; the
     documented hardware rule (PCSX2 iFPU precedent cited). Keep verbatim; the comment
     is a model hardware-invariant "why".
  2. VR_reg operator+/-/*// clamp helpers — convenience used by VU0-macro code paths;
     under the vu_float collapse they delegate to the single source.
  3. FBRST/status-flag write masks encode manual rules inline — keep (hardware
     semantics with section citations).
- **Risks**: this type's layout is ABI for every generated shard — any change forces
  full regeneration; the seam is frozen until Phase 8. Proof: recompiled corpus builds
  + oracle spot-sweep unchanged; static_assert layout pins if touched.

---

### host_app/budget_lift.h (223 lines)
- **Purpose**: [S70 BUDGET-LIFT] Lifts the R140 256 KB/frame CDVD delivery ration
  during hands-off loading screens: a QUEUE-ACTIVE ∧ INPUT-DEAD span classifier
  (turbo::AutoEngage streak machine + 2 s input-idle veto) makes `ActiveNow()` true so
  the pump delivers at latency instead of the gameplay ration; engage/disengage/MID
  logging with per-span delivery census. Explicitly does NOT touch the causal-order
  gate or readiness floor.
- **Public surface**: `budget_lift::On/NoteInput/NoteQueue/ActiveNow/NoteLifted`.
  Consumers: hle_heap.cpp (pump charge site), main.cpp (input note).
- **Classification**: GENERIC-PS2 (load-span delivery policy). Subsystem: cdvd/.
- **Refactor destination** (MIGRATION §1.3 `budget_*` glob row points at ee/timing;
  semantically this is the CDVD delivery budget): dissolve into
  `engine/cdvd/delivery_budget.h/.cpp` (class `CdvdDeliveryBudget`: ration + lift
  policy, span classifier injected from the load-span detector). Header-inline
  logic + statics become class members. Flagging the ee/timing-vs-cdvd placement as a
  minor plan refinement, not a TREE-GAP (cdvd/ exists; the §1.3 glob was a filename
  match).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_LOAD_BUDGET_LIFT (:105) | ON-unless-0 | lift vs metered R140 path | shipping-setting (A2 baseline) | NORM — proven default-ON behavior becomes CdvdDeliveryBudget policy; not in the ARCH §4.8 ~32-field schema, and "hands-off load" is auto-detected, not a user choice |
  | PS2_LOAD_BUDGET_LIFT_GAP_MS (via EnvU32 :72) | 150 | streak gap merge | tuning knob | NORM — named constant |
  | PS2_LOAD_BUDGET_LIFT_ENGAGE_MS | 500 | engage threshold | tuning knob | NORM — constant |
  | PS2_LOAD_BUDGET_LIFT_RELEASE_MS | 750 | release threshold | tuning knob | NORM — constant |
  | PS2_LOAD_BUDGET_LIFT_INPUT_MS | 2000 | input-dead window (0=off) | tuning knob | NORM — constant |

- **Hacks / workarounds**:
  1. Whole policy implemented as header-inline function-local statics with a mutex —
     TU-latch config style (A2 §2.1). Correct: one class, config resolved once.
  2. PS2_PERF-conditional gate self-report in On() — the announce idiom; dies with the
     startup config dump.
  3. stderr engage/disengage/MID census — becomes core/log category (loads) at info
     level, and triage PERF evidence if a load exceeds budget.
- **Risks**: interacts with the causal gate arms — lifting must never bypass ARM
  ordering (S57 protection is load-bearing). Proof: load-span legs measuring load
  duration + release tallies with lift on (NORM) vs the archived metered baseline;
  BUG-004 bundle repro.

---

### host_app/vu_span_classify.h (195 lines)
- **Purpose**: [W14-W1] Decode-time VU span classification shared verbatim by the
  cached tier and the AOT generator: run-length/hardwire pass ([W13-V1]), 14-class
  stall classifier ([W13-S1]), stall-fold prover with the 3-slot read window
  ([W13-S3], static_asserts pin the latency-4 derivation), and SPANFOLD span lengths
  ([W13-S7]). Header-pure so the generator links it standalone.
- **Public surface**: `vu_span::ComputeRunLenTouchHw/ComputeStallClasses/
  ComputeFoldClasses/ComputeSpanRem` + latency constants. Consumers: vu.cpp
  DecodeProgram, recompiler_tool/vu_aot_gen.cpp (dies with AOT).
- **Classification**: GENERIC-PS2 (VU pipeline analysis). Subsystem: vu/.
- **Refactor destination** (MIGRATION §1.4 explicit: "vu_span_classify.h → vu/ —
  verbatim"): `engine/vu/span_classify.h/.cpp` (concept pair; definitions move to the
  .cpp per §13 headers-are-declarations, cached tier is the sole surviving consumer).
- **Flags consumed here**: none (PS2_VU_AOT_SPANFOLD gates the emitted bodies in
  another shard; dies with AOT → DELETE there).
- **Hacks / workarounds**:
  1. The [W13-F1] RUNFLAT exclusion note (fold_class 17 handled only in vu.cpp) —
     a cross-file protocol in comments. Correct: after AOT deletes, the classifier has
     one consumer and F1 folds into the same class (`SpanClassifier`) as an explicit
     pass list.
  2. static_asserts mirroring vu.h latency constants — keep (they ARE the honesty
     mechanism).
- **Risks**: fold soundness proofs (S3 window) gate cached-tier correctness. Proof:
  vu1_replay corpus with span folding on (the shipping default) — FAIL=0.

---

### host_app/runahead_guard.h (180 lines)
- **Purpose**: [R203-RAGUARD] Standing-runahead tripwire + the single source of truth
  for the MTVU frame-depth cap: `MtvuMaxFramesEnv` (default 2), [PERF1-B]
  band-aware dynamic depth (`MtvuEffMaxFrames` lifts 2→3 while the pacer sits in its
  lowest band; hard-capped, only when base==2), `DepthFrames` math shared by producer
  park and tripwire, and the pure edge-triggered `Tripwire` (fires after >30
  consecutive over-cap presents, ≤5 reports/run).
- **Public surface**: `raguard::MtvuMaxFramesEnv/AlarmEnabled/MtvuDynDepthEnv/
  MtvuEffMaxFrames/DepthFrames/Tripwire`, `g_pace_low_band`. Consumers:
  vu1_async.cpp (producer park), graphics_thread.cpp, present_pace.h,
  tests/test_runahead_guard.cpp.
- **Classification**: GENERIC-PS2 (pipeline-depth invariant). Subsystem: vu/tiers.
- **Refactor destination** (MIGRATION §1.4: "runahead_guard.h → vu/tiers/ —
  verbatim; mtvu_depth bound → engine.mtvu_depth config"): 
  `engine/vu/tiers/runahead_guard.h/.cpp` (class `RunaheadGuard` owning the depth
  bound from Config + the Tripwire; `g_pace_low_band` becomes a pacer-published input,
  not a header global — header-mutable-global budget is 0 per §11).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_MTVU_MAX_FRAMES (:66) | 2 | runahead depth cap (0 = unbounded park) | shipping-setting | SETTING → Config engine.mtvu_depth = 2 |
  | PS2_RUNAHEAD_ALARM (:77) | ON-unless-0 | tripwire on/off | tuning-knob / delete-losing-arm | NORM — invariant monitor always on (cost: two compares/present) |
  | PS2_MTVU_DYNDEPTH (:108) | ON-unless-0 | band-aware 2→3 lift | proven-fix-hatch-ON | NORM — lift logic unconditional when depth==2 |

- **Hacks / workarounds**:
  1. `inline std::atomic<bool> g_pace_low_band` header global written by the pacer —
     hidden cross-module channel. Correct: RunaheadGuard reads the pacer's published
     state through an interface (gs/hle/present pacing → vu/tiers input).
  2. "Moved verbatim from vu1_async.cpp's anonymous namespace so two readers can never
     disagree" — the mirror-latch fix done the header way. Correct: Config field read
     once; both consumers take the value from RunaheadGuard.
- **Risks**: depth semantics couple pacing, latency and throughput (measured trade
  curve in-file). Proof: tests/test_runahead_guard (pure Tripwire, exists) + a
  depth-sweep bench leg pinning p50 at depth 2/3.

---

### host_app/hle_meta_arena.h (176 lines)
- **Purpose**: [R144-LISTLEAK]/[R145-LOCKSTORM] The handle-heap descriptor arena: a
  host-invented 2 MB guest region at EE 0x02000000 serving the game allocator's
  ≤0x20-byte descriptors with free-side link tombstoning, FIFO reuse quarantine
  (256 blocks), O(1) double-free audit, +0x0C block-base skew (to dodge the
  memory.cpp write-watch geometry), savestate capture of allocation identity, and a
  pump-watch/budget-query telemetry API.
- **Public surface**: `hle_meta_arena::Alloc/Free/Contains/GetStats/Serialize-
  Restore-ValidateState/PumpWatchAdd/BudgetQueryNote/PumpWatchEmitIfDue/ResetForTest`
  + kBase/kSize/kSkew constants. Consumers: main.cpp (0x181560 hook), savestate.cpp,
  hle_meta_arena.cpp, tests/r144_list_leak.
- **Classification**: GAME-SPECIFIC workaround (the structure identification —
  DAT_0031b138, FUN_00202748/203358/204ef8/205120/202ac0 — is this game's allocator).
  Subsystem: ee/mem (quarantine home).
- **Refactor destination** (MIGRATION §1.4 + HOOK §3.1 0x181560 row): 
  **quarantined beside ee/mem** as a named workaround module
  (`engine/ee/mem/workarounds/meta_arena.h/.cpp`, workaround-ledger row) — **retired
  at the root fix** (eliminate the boot double-init leak family + loader delivery
  divergence that poison the free list); its META savestate section exits under the
  accepted-difference protocol with it.
- **Flags consumed here**: none.
- **Hacks / workarounds** (the file is one big documented workaround — well-formed):
  1. The arena itself — a non-hardware invention isolating metadata from the shared
     slab. Correct: root-fix upstream divergences (0x1b36b8/0x268440/0x2ad110 family),
     then delete; retail semantics need no arena.
  2. kSkew=0x0C exists to dodge *diagnostic* write-watch predicates in memory.cpp
     whose backtrace symbolication runs even with the log closed — a workaround for a
     workaround. Correct: those watches move to core/diag (diag builds only); the skew
     dies with them even before the arena retires.
  3. PumpWatch/BudgetQueryNote telemetry embedded in an allocator header — core/diag.
  4. Tombstone bits ≥0x40000 chosen because "real descriptors never set them" —
     game-image reverse-engineering as API. Documented; dies with the arena.
- **Risks**: removing it before the root fix re-opens the key-buffer corruption (the
  R144 class). Proof: workaround-ledger harness leg — retail-faithful boot flow vs
  arena-on, allocator-state divergence census; BUG-019-class boot anchors.

---

### host_app/idx16_lane.h (118 lines)
- **Purpose**: [R177-IDX16LANE] Pure serve predicate for CLASS IDX-B (PSMT8H
  composite reads over a CT16 pair render target): serve from the pair-FBO only when
  the recorded 16-bit keep-mask proves the index lane (texel high byte) is 100%
  draw-authored (`keep16 == 0x00FF`), plus the single-sourced `Fbmsk16From32`
  conversion and a fall census counter.
- **Public surface**: `r177::Idx16LaneOn/Fbmsk16From32/LaneAuthored16/
  kIndexLaneKeep16/n_idx_fall_lane` + test override. Consumers: render.h (backend
  fmt=5 dispatch), tests/test_idx16_lane.cpp.
- **Classification**: GENERIC-PS2 (GS channel-shuffle rule: RT reads bind the target,
  local memory is never round-tripped — the PCSX2 semantic is cited). The fog-map
  addresses in comments are game facts. Subsystem: gs/hle/targets + render/backends/gl.
- **Refactor destination** (MIGRATION §1.4: purge-dissolve into render/backends/gl +
  gs/hle/targets): the predicate + fbmsk math become methods of
  `engine/gs/hle/targets/render_target.*` (RenderTarget records sync_keep_mask16 per
  committed draw; `TextureSource::ResolveRead` consults `LaneAuthored16`); the
  session namespace `r177::` dies (§11 no-session-tags). File then deletes.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_GL_IDX16_LANE (:78) | ON-unless-0 | lane-aware serve vs R175 class-off legacy | tuning-knob / delete-losing-arm | NORM — lane-authored serve is the only path; the R173/R175 legacy arms delete |

- **Hacks / workarounds**:
  1. The header exists to *bypass* a structurally-starved VRAM round-trip (epoch-
     blocked deposits) — the predicate is correct GS semantics, but the surrounding
     legacy/fast dual path is the hack. Correct: RenderTarget/TextureSource implement
     the channel-shuffle read rule directly; no byte-identity fallback arm.
  2. Census atomic in a pure header — core/diag counter.
- **Risks**: fog/DOF composite regressions (left-anchored lavender/mosaic class).
  Proof: tests/test_idx16_lane (exists, retargets) + gs_replay byte-identity on the
  fog corpus cases; BUG-016 bundle.

---

### host_app/idx_epoch_guard.h (115 lines)
- **Purpose**: [R176-IDXEPOCH] Epoch guard for the indexed-decode live refresh: walk
  the snapshot range in ≤8KB page segments and re-copy from live gs_vram ONLY pages
  whose last EE-upload epoch ≤ the batch's kick epoch (same [VRAM-EPOCH] rule
  SyncRTToVRAM applies on the write side); newer pages keep kick-time snapshot bytes.
  Injected callables keep it pure/unit-testable. Fixes the one-frame "noisy texture
  patches" pop (proof chain documented: Fisher p, dose-response, kill-switch x4).
- **Public surface**: `r176::IdxEpochGuardOn/WalkRefreshSegments/GuardStats` +
  census atomics + test override. Consumers: opengl_backend.cpp,
  tests/test_idx_epoch_guard.cpp, tests/test_idx16_lane.cpp.
- **Classification**: GENERIC-PS2 (EE-order coherence rule for VRAM mirrors).
  Subsystem: gs/vram.
- **Refactor destination** (MIGRATION §1.4: purge-dissolve into render/backends/gl +
  gs/hle/targets; A1 §5.3 lists GL_IDXEPOCH as an expired hatch): the segment walk +
  epoch compare become the ONE commit rule of `engine/gs/vram/page_record.h/.cpp`
  (PageRecord epoch state machine, ARCH §4.3) consumed by `TextureSource::ResolveRead`
  — this is precisely the "ONE PageRecord rewrite" absorption (§1.5). `r176::` dies;
  file deletes.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_GL_IDXEPOCH (:60) | ON-unless-0 | epoch-guarded vs unguarded live re-copy | tuning-knob / delete-losing-arm (expired hatch, MIGRATION §3 wave lead) | NORM — guard is the only path; unguarded arm deletes |

- **Hacks / workarounds**:
  1. The guard duplicates the [VRAM-EPOCH] comparison that lives at
     opengl_backend.cpp ~9409 — two implementations of one rule. Correct: PageRecord
     owns the epoch comparison once; both read and write sides call it.
  2. Injected-callable purity (page_epoch/seg lambdas) — good test design; keep the
     shape as PageRecord's iteration API.
- **Risks**: coherence between EE uploads, GFX deposits and reads is the project's
  hardest race surface. Proof: tests/test_idx_epoch_guard byte-for-byte walk
  equivalence (exists) + BUG-010/BUG-026 bundles.

---

### host_app/shutdown_policy.h (90 lines)
- **Purpose**: [S29-SHUTDOWN] Pure bounded-shutdown policy: parse/clamp
  PS2_SHUTDOWN_WAIT (default 10 s, 0 = legacy infinite join), decide when a guest
  worker unwinds via ThreadExitException at frame-boundary HLEs, and the Warn(5s)/
  HardExit escalation ladder for main's join watcher. Fixes the zombie-process class.
- **Public surface**: `shutdown_policy::ParseWaitSeconds/WaitSecondsCached/
  ShouldUnwindGuestThread/JoinActionAt/JoinAction`. Consumers: main.cpp, sif_hle.cpp,
  gs_hle.cpp, tests/test_shutdown_policy.cpp.
- **Classification**: GENERIC-PS2 (host lifecycle). Subsystem: app/host.
- **Refactor destination** (MIGRATION §1.4: "shutdown_policy.h → app/host/shutdown.*
  — verbatim"): `app/host/shutdown.h/.cpp` (class `ShutdownPolicy`), wait bound from
  Config.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_SHUTDOWN_WAIT (:62) | 10 (0=legacy, clamp 600) | bounded-shutdown wait | shipping-setting | SETTING → Config engine.shutdown_wait_s = 10 (the 0/legacy arm deletes — unwind always armed) |

- **Hacks / workarounds**: the legacy (wait==0, infinite-join) arm exists only for
  bisect — deletes at purge; unwind-at-boundary becomes unconditional with the
  configured bound.
- **Risks**: shutdown during in-flight savestate write. Proof: test_shutdown_policy
  (pure ladder, exists) + a scripted window-close soak (no zombie, artifacts durable).

---

### host_app/sif_rpc_handlers.h (89 lines)
- **Purpose**: Declarations for the SIF RPC dispatch surface: `SifRpcContext`,
  per-module handlers (FileIO, LoadFile, Padman, McServ, Cdvd S/N-cmd, LibSd, Mtap,
  EyeToy, CustomLoader[Recompiled], IopHeap), `DispatchSifRpcCall`, the [S43] EE
  stream-callback RPC bridge (RegisterEeStreamRpcServer/EeStreamRpcRequest/
  ServiceEeStreamRpc for server 0x12344321), `StreamPollCompleteMarker` (§38 faithful
  '~' marker retire), and IopHeap_Alloc/Free.
- **Public surface**: as above. Consumers: sif_rpc_handlers.cpp, sif.cpp, sif_hle.cpp,
  hle_heap.cpp, main.cpp.
- **Classification**: MIXED — the router and stock-module handler surface are
  GENERIC (Sony RPC protocol); the STREAM.IRX bridge details (server id 0x12344321,
  recv 0x3B3000/0x3B4480, FUN_ anchors in comments) are the title's custom module
  contract (A6 §5). Subsystem: sif/hle + per-device hle.
- **Refactor destination** (MIGRATION §1.3 sif_rpc_handlers.cpp row): split —
  `engine/sif/hle/rpc_router.h/.cpp` (class `SifRpcRouter`: SifRpcContext +
  dispatch); per-module handler decls move beside their bodies into
  `engine/cdvd/hle/`, `engine/mc/hle/`, `engine/pad/hle/`, `engine/sound/hle/`;
  the EE stream-callback bridge + StreamPollCompleteMarker + feed contracts →
  `games/<title>/iop/stream_irx/` (title plugin, ARCH §14.1
  IopPlugin_StreamIrx); IopHeap allocator → `engine/iop/` (sysmem heap, one pool
  shared with the libkernl path — the stated invariant).
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  1. `HandleCustomLoader` vs `HandleCustomLoaderRecompiled` twins — legacy twin
     deletes; the survivor is HARDWARE/TIMING HLE (HOOK §3.6) in cdvd/hle/loader.
  2. The S38-era synthesized reply fallback for the stream RPC (bridge falls back to
     synth on timeout) — a stopgap under the real-server bridge. Correct: STREAM.IRX
     recomp plugin serves all replies; synth path deletes with proof.
  3. Struct-comment RE anchors (thread body 0x1E7F80 etc.) — move to the title
     module's protocol doc, not engine headers.
- **Risks**: audio-manager stream channel bring-up depends on the bridge handshake.
  Proof: STREAM wire-capture replay tests + BUG-005 (save-select livelock) bundle.

---

### host_app/display_settings.h (69 lines)
- **Purpose**: [U3-SCREENFIT] User display/screen-fit settings surface: mode/aspect/
  custom scale+offsets (clamped setters), live drawable + GS VCK geometry publication
  for viewport math, output-rect readback for the GUI, display.ini persistence with
  explicit paths for the ctest, and a probe gate.
- **Public surface**: `display::` Get/Set Mode/Aspect/CustomScale/Offsets,
  SetDrawableSize/SetVckGeometry/GetAutoAspect, PublishOutputRect, Load/SaveConfig,
  Init, name↔enum helpers, ProbeOn. Consumers: gui, opengl_backend
  ApplyScanoutViewport, main (startup ini load), tests/test_display_settings.cpp.
- **Classification**: GENERIC-PS2 (host presentation settings). Subsystem:
  core/config + gs/hle/present.
- **Refactor destination** (MIGRATION §1.4 explicit split row): persisted keys →
  `engine/core/config/` `[video]` fields (fit_mode/aspect/custom_scale_pct/
  custom_offset_x/y — already named in the A2 §6 schema); live geometry publication →
  `engine/gs/hle/present/view_state.h/.cpp` (class `PresentViewState`: drawable, VCK,
  output rect); ini round-trip test retargets the Config loader.
- **Flags consumed here** (via .cpp latch; declared here):

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_U3_VIEWPORT (ProbeOn) | OFF | viewport probe lines | diagnostic | DIAG |

- **Hacks / workarounds**: atomics-with-relaxed-ordering as the cross-thread settings
  bus (documented convention) — replaced by Config (read-once) + PresentViewState
  (single-writer graphics thread). display.ini-next-to-exe convention folds into the
  one Config file.
- **Risks**: GUI↔present-thread ordering on live geometry. Proof: display-settings
  ini round-trip ctest (retargets) + resize smoke (PS2_TEST_RESIZE successor lives in
  tests).

---

### host_app/slowspot_paths.h (66 lines)
- **Purpose**: [W14R3][FIX-C2] Pure formatter for session-unique slowspot capture
  names (`save_states/slowspot_f<vf>_s<epoch>.{ps2st,ppm,_logs}`), preventing
  cross-session capture clobbering; legacy vf-only names behind the gate.
- **Public surface**: `slowspot::SlowspotSuffixOn/FormatSlowspotPaths` + test
  override. Consumers: slowspot.cpp, tests/test_slowspot_paths.cpp.
- **Classification**: GENERIC-PS2. Subsystem: core/triage.
- **Refactor destination** (MIGRATION §1.4: "the capture trio … slowspot_paths.h
  retires into TriageBundler bundle layout"): DELETE after absorption — bundle
  naming becomes `TriageWriter`/`TriageBundler`'s `<UTC>_<id>` directory scheme under
  `games/<title>/triage/` (TRIAGE §7), which is session-unique by construction.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_SLOWSPOT_SUFFIX (:29) | ON-unless-0 | session-suffixed vs legacy vf-only names | tuning-knob / delete-losing-arm | NORM — unique naming always (then the whole scheme is superseded by TriageWriter layout) |

- **Hacks / workarounds**: fixed `save_states/` flat-dir capture location — replaced
  by the triage tree + retention (§8).
- **Risks**: none. Proof: test_slowspot_paths pins the format until deletion; after
  absorption, TriageWriter layout tests.

---

### host_app/hw_verbose.h (47 lines)
- **Purpose**: [R188-HYGIENE] One latched gate (`hwlog::VerboseOn`, PS2_VERBOSE_HW)
  re-arming the per-event hardware narration logs (DMA tag walkers, per-FlushBatch GS
  dumps, per-DIRECT VIF boxes — the 2.8 GB log class), plus the [M4A]
  `PrefixDropped` hook letting emit sites skip formatting lines the main.cpp filter
  would drop.
- **Public surface**: `hwlog::VerboseOn/PrefixDropped/g_prefix_dropped_hook`.
  Consumers: dmac.cpp, gs_state.cpp, vif.cpp, main.cpp (hook install).
- **Classification**: GENERIC-PS2 (logging infra). Subsystem: core/log.
- **Refactor destination** (MIGRATION §1.4 explicit): **DELETE** — dissolves into
  `engine/core/log/` compile-time categories (Phase 3). Per-event narration becomes
  a trace-level category compiled out of release; the standing rule (hot-path
  diagnostics default-off, in-memory census only) becomes the logger's design, not a
  per-site convention.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_VERBOSE_HW (:26) | OFF | re-arm per-event hardware narration | diagnostic | DIAG — trace category in PS2R_DIAGNOSTICS builds; env dies |

- **Hacks / workarounds**: the whole file is layer 4 of the 4-layer retrofit (A2
  finding 5). Correct: compile-time category logger.
- **Risks**: none behavioral. Proof: log-output category tests; release-build binary
  contains no narration format strings.

---

### host_app/sema.h (21 lines)
- **Purpose**: Legacy semaphore header kept "for backward compatibility": a
  `HostSemaphore` struct and `extern std::vector<HostSemaphore> g_semaphores`, both
  explicitly marked unused in the new threading model (real semas live in
  ps2_threading).
- **Public surface**: `HostSemaphore`, `g_semaphores` extern. Included by syscalls.h,
  syscalls.cpp, main.cpp (and dead main_old.cpp).
- **Classification**: GENERIC-PS2 (dead scaffold). Subsystem: ee/hle.
- **Refactor destination**: **DELETE** — dead payload. Evidence: `g_semaphores` is
  DEFINED only in main_old.cpp (itself a MIGRATION §1.1 deletion row); no live TU
  defines or touches it, so any live ODR-use would fail to link — the includes are
  vestigial. MIGRATION §1.4 lists "sema.h → ee/hle — verbatim"; the file's own text
  and the definition census contradict a move — flagging as a plan correction
  (delete, remove the three stale includes).
- **Flags consumed here**: none.
- **Hacks / workarounds**: the file is one ("kept for backward compatibility").
- **Risks**: none. Proof: build after include removal.

---

### host_app/vu_aot_programs_1.cpp · host_app/vu_aot_programs_9.cpp · host_app/vu_aot_programs_15.cpp · host_app/vu_aot_programs_20.cpp · host_app/vu_aot_programs_26.cpp · host_app/vu_aot_programs_31.cpp (17 lines each, 102 total)
- **Purpose**: [R158-TUSPLIT]/[W12R2] Six of the 33 fixed checked-in VU1-AOT chunk
  aggregator TUs (K=24 split + extras): each includes vu.h, vu_aot_registry.h,
  vu_decode_slot.h, vu_fast_exec.h and then the generated `vu_aot_generated_N.inc`
  (always emitted, possibly empty, so CMake's source list stays static). The chunk's
  `g_vu_aot_chunk_descs_N` array registers with the master AOT registry.
- **Public surface**: `g_vu_aot_chunk_descs_N` (external linkage), aggregated by
  vu_aot_programs.cpp at static init.
- **Classification**: GENERIC-PS2 mechanism carrying game-generated payloads.
  Subsystem: vu (AOT tier).
- **Refactor destination**: **DELETE** with the whole AOT tier (MIGRATION §1.1 row:
  "VU-AOT tier: vu_aot_gen.cpp, 33 chunk TUs, vu_aot_registry consumers, 6 ctest
  gates, 233 MB inc + 351 MB lib — default-OFF per NOAOT decision record").
  Build evidence: with PS2_VU_AOT=OFF, `vu_aot_lib` is an empty INTERFACE placeholder
  (host_app/CMakeLists.txt:405-415, :605-606) — these TUs are not even compiled in
  the shipping configuration. The `.inc` textual include is exactly the pattern the
  D4 rule bans for hand-written code (generated exemption applies until deletion).
- **Flags consumed here**: none (the AOT runtime gates PS2_VU_AOT* live in
  vu_aot_config/vu.cpp — all DELETE with the tier).
- **Hacks / workarounds**: the always-emit-empty-inc trick to keep CMake static —
  moot after deletion.
- **Risks**: none — default-OFF tier; cached interpreter + VU1-HLE (peak config) are
  the shipping paths. Proof: NOAOT decision record + shipping run_play.sh env.

---

### host_app/runtime.h (7 lines)
- **Purpose**: Declares `runtime_syscall_dispatcher(uint32_t, CpuContext&)` (defined
  in syscalls.cpp:2310) and includes cpu_state.h/syscalls.h. Historical seam header.
- **Public surface**: the one declaration.
- **Classification**: GENERIC-PS2 (dead scaffold). Subsystem: ee/hle.
- **Refactor destination**: **DELETE** — dead code. Evidence: zero
  `#include "runtime.h"` anywhere (grep 2026-09-23 across host_app + generated
  recompiled.h); its sibling runtime.cpp is **0 bytes**; neither appears in
  host_app/CMakeLists.txt. The MIGRATION §1.4 row "runtime.{cpp,h} (dispatcher entry,
  update_function_impl seam) → ee/cpu/dispatch.* + ee/hle glue — split" describes a
  concept that actually lives in generated recompiled.cpp (update_function_impl) and
  syscalls.cpp (dispatcher) — flagging as a plan correction: the row's *content*
  arrives from those files; runtime.{h,cpp} themselves just delete.
- **Flags consumed here**: none.
- **Hacks / workarounds**: n/a.
- **Risks**: none. Proof: build without it (nothing includes it).

---

## SHARD SUMMARY

flags(N→…) = getenv rows documented here → NORM/SETTING/DIAG/TEST/DELETE.

| file | lines | class | destination | shape | flags(N→N/S/D/T/X) | hacks |
|---|---|---|---|---|---|---|
| main.cpp | 14,690 | MIXED | app/host/main.cpp + core/{config,boot} + app/host/shutdown; hooks per HOOK ledger (195 DIAG del / 4 workaround / 1 timing HLE) | split | 45→8/2/12/4/19 | 8 |
| ps2_threading.cpp | 1,481 | GENERIC-PS2 | ee/hle/{threads,semaphores,boot_env} + core/diag census | split | 1→1/0/0/0/0 | 4 |
| ps2_vblank.cpp | 1,167 | GENERIC-PS2 | ee/timing/{tick_source,boundary_protocol} + ee/intc + gs/hle/present/pacing | split (+delete probes) | 5→1/3/1/0/0 | 5 |
| sif_hle.cpp | 884 | MIXED | sif/hle/sceSif + ee/timing boundary_protocol + ee/hle/boot_env + workaround ledger (0x181490) | split | 0 | 5 |
| bench.h | 713 | GENERIC-PS2 | core/diag/perf_bench (feeds core/triage PERF) | verbatim → .h/.cpp pair | 0 (setenv PS2_PERF write dies) | 3 |
| vu_decode_slot.h | 648 | GENERIC-PS2 | vu/decode.h | verbatim | 0 | 1 |
| ipu.cpp | 600 | GENERIC-PS2 | ipu/{ipu,decode} + ipu/hle | split | 0 | 1 |
| hle_heap.h | 521 | MIXED | cdvd/{read_engine,iso_fs} + cdvd/hle/loader + ee/hle/heap + title loader_sync data | split | 0 | 4 |
| libc_hle.cpp | 495 | GENERIC-PS2 | ee/hle/libc.cpp (SdkBinding rows) | verbatim | 1→1/0/0/0/0 | 3 |
| test_alpha_blend_bug_condition.cpp | 453 | GENERIC-PS2 | — | delete (dead) | 0 | 0 |
| vu1_async.h | 358 | GENERIC-PS2 | vu/tiers/{mtvu_worker,mtvu_sync} | split | 0 | 3 |
| artdet_flicker.cpp | 338 | GENERIC-PS2 | core/diag/artdet + triage spike path (bundle writer deletes) | split | 5→1/2/1/1/0 | 3 |
| slowspot.cpp | 306 | GENERIC-PS2 | core/triage/perf_monitor (capture trio → TriageBundler) | split | 5→4/1/0/0/0 | 4 |
| cpu_state.h | 299 | GENERIC-PS2 | ee/cpu/cpu_state.h (lint-exempt); ps2-float → vu/vu_float | verbatim (+helper extraction) | 0 | 1 |
| budget_lift.h | 223 | GENERIC-PS2 | cdvd/delivery_budget | rewritten as class (purge-dissolve) | 5→5/0/0/0/0 | 3 |
| vu_span_classify.h | 195 | GENERIC-PS2 | vu/span_classify.h/.cpp | verbatim | 0 | 1 |
| runahead_guard.h | 180 | GENERIC-PS2 | vu/tiers/runahead_guard | verbatim → class | 3→2/1/0/0/0 | 2 |
| hle_meta_arena.h | 176 | GAME-SPECIFIC | ee/mem/workarounds/meta_arena (quarantine; retire at 0x181560 root fix) | quarantine-then-delete | 0 | 4 |
| slowspot_detector.h | 129 | GENERIC-PS2 | core/triage/window_median (PerfMonitor core) | verbatim | 0 | 0 |
| idx16_lane.h | 118 | GENERIC-PS2 | gs/hle/targets/render_target + texsrc ResolveRead | purge-dissolve | 1→1/0/0/0/0 | 2 |
| idx_epoch_guard.h | 115 | GENERIC-PS2 | gs/vram/page_record (the ONE epoch rule) | purge-dissolve | 1→1/0/0/0/0 | 1 |
| shutdown_policy.h | 90 | GENERIC-PS2 | app/host/shutdown | verbatim | 1→0/1/0/0/0 | 1 |
| sif_rpc_handlers.h | 89 | MIXED | sif/hle/rpc_router + per-device hle + games/…/iop/stream_irx | split | 0 | 3 |
| display_settings.h | 69 | GENERIC-PS2 | core/config [video] + gs/hle/present/view_state | split | 1→0/0/1/0/0 | 1 |
| slowspot_paths.h | 66 | GENERIC-PS2 | absorbed by TriageWriter layout | delete after absorption | 1→1/0/0/0/0 | 1 |
| hw_verbose.h | 47 | GENERIC-PS2 | core/log categories | delete (dissolve) | 1→0/0/1/0/0 | 1 |
| libc_hle.h | 31 | GENERIC-PS2 | ee/hle/libc.h | verbatim | 0 | 1 |
| sema.h | 21 | GENERIC-PS2 | — | delete (dead payload) | 0 | 1 |
| vu_aot_programs_1/9/15/20/26/31.cpp | 6×17 | GENERIC-PS2 | — | delete (NOAOT tier) | 0 | 1 |
| runtime.h | 7 | GENERIC-PS2 | — | delete (dead orphan) | 0 | 0 |

Totals: 35 files / 24,611 lines; 77 flag rows documented → 26 NORM / 10 SETTING /
16 DIAG / 5 TEST-ONLY / 19 DELETE (+1 mirror: PS2_ARGV ×2 sites; PS2_PERF here is
3 of its 21 announce mirrors).

## TREE-GAPs and plan corrections

No hard TREE-GAP (every destination exists in the ARCH §3 tree). Three plan
corrections and one refinement, flagged inline above:

1. **runtime.h / runtime.cpp are dead** (runtime.cpp is 0 bytes; zero includers;
   not in CMake). MIGRATION §1.4's "runtime.{cpp,h} → ee/cpu/dispatch.* split" row
   should be re-pointed at the real owners of that content (generated
   recompiled.cpp's update_function_impl seam + syscalls.cpp dispatcher); the two
   files themselves are §1.1-class deletions.
2. **sema.h is dead payload**, not a verbatim ee/hle move: `g_semaphores` is defined
   only in main_old.cpp (already a §1.1 deletion). Delete sema.h and its three stale
   includes.
3. **PS2_LOAD_BUDGET_LIFT**: A2 baseline says keep-as-setting, but it is absent from
   the ARCH §4.8 ~32-field schema and the span is auto-detected — proposed NORM
   (policy of CdvdDeliveryBudget), sub-knobs to named constants. Needs the §3
   gate-purge record check to confirm.
4. **budget_lift.h placement**: MIGRATION §1.3's `budget_*` filename glob points at
   ee/timing; the semantic home is cdvd/ (it modulates the CDVD delivery ration).
   Proposed `engine/cdvd/delivery_budget.*`.

## Dead code found in this shard (with evidence)

- `host_app/test_alpha_blend_bug_condition.cpp` — no reference in any source
  CMakeLists (host_app/, root, tests/); only stale build-dir CMakeFiles. Matches the
  MIGRATION §1.1 "23 dead root test_*.cpp" row.
- `host_app/runtime.h` + `host_app/runtime.cpp` — 0 includers, runtime.cpp 0 bytes,
  not in CMake.
- `host_app/sema.h` — payload (`HostSemaphore`, `g_semaphores`) defined only in dead
  main_old.cpp; header self-describes as legacy compat.
- `host_app/vu_aot_programs_{1,9,15,20,26,31}.cpp` — not compiled in the shipping
  configuration (PS2_VU_AOT=OFF ⇒ vu_aot_lib is an empty INTERFACE placeholder,
  host_app/CMakeLists.txt:405-415/:605); NOAOT decision retires the tier.
- Inside live files: ps2_vblank.cpp bullseye dumpers (:41-386, concluded probes);
  main.cpp [SEQ54]-class hook bodies (solved-bug archaeology, HOOK §3.1);
  sif_hle.cpp legacy `FUN_00181490` symbol arm (dies with the legacy engine).

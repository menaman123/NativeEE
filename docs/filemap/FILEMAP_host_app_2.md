# FILEMAP — host_app shard 2 (37 files, 24,606 lines)

File-by-file mapping of shard 2 (`/tmp/s116/s116_shard2.txt`) of the current
Ps2Recomp tree into the refactor plan (ARCHITECTURE.md §3 tree, MIGRATION_PLAN.md
rows, HOOK_RECLASSIFICATION.md dispositions, A2 env registry, A6 classification,
TRIAGE_CAPTURE.md). Every file was read; every getenv/latch site is tabled.
Flag "refactor outcome" legend: NORM / SETTING(field) / DIAG / TEST-ONLY / DELETE
per the shard instructions; the run_play.sh peak config (PS2_GS_SCALE=2,
PS2_GS_INTERP=1, PS2_FPS_CAP=60, PS2_MTVU=1, PS2_GL_FAST=1, PS2_GL_FAST2=1,
PS2_VU1_HLE=1, PS2_VU1_HLE_DIRECT=1) is NORM/SETTING as directed.

---

### host_app/sif_rpc_handlers.cpp (6,131 lines)

- **Purpose**: The EE↔IOP RPC HLE hub. `DispatchSifRpcCall()` routes every
  `sceSifCallRpc` by server id to per-module handlers: FILEIO (0x80000001),
  IOP heap (0x80000003), LOADFILE (0x80000006), padman (0x80000100/101),
  mcserv (0x80000400), cdvdman S/N-cmd + search + disk-ready (0x80000592–59A,
  0x8000059c), libsd (0x80000701), mtap (0x80000901-905), EyeToy (0x80001400),
  and the game's custom STREAM.IRX server (0x00012345). The 0x12345 path
  (`HandleCustomLoaderRecompiled`, :2935–4849, 1,915 ln) marshals the RPC batch
  into recompiled STREAM.IRX code (send buffer @IOP 0x1344C, result @0x14A90,
  main_handler @0x8D28) under the single-core guest monitor, with two wire-level
  batch shapers (S46 bank governor, S65 pause-defer). Also hosts the S43
  EE-stream-callback RPC bridge (STREAM.IRX → EE server sid 0x12344321), the
  `iso_scanner` ISO9660 file-table scan, the `streaming_delivery` engine that
  delivers game loader file data (cmd49 EE-dest latch, "7e" job tracking),
  `StreamPerformIsoRead`, and stream-state savestate serialize/restore/validate.
- **Public surface**: `DispatchSifRpcCall`, `Handle*` per-module handlers,
  `RegisterEeStreamRpcServer` / `EeStreamRpcPending` / `EeStreamRpcRequest` /
  `ServiceEeStreamRpc` (called from gs_hle.cpp frame boundary + syscall edge),
  `IopHeap_Alloc/Free`, `StreamPollCompleteMarker`,
  `SifRpc_{Serialize,Restore,Validate}StreamState`. Included via
  `sif_rpc_handlers.h` by gs_hle.cpp, syscalls.cpp, iop_stubs.cpp, main.cpp.
- **Classification**: MIXED (A6 §5): Sony module RPC semantics (pad/mc/cdvd/
  libsd/fileio/loadfile) are GENERIC-PS2 by API; the STREAM.IRX `lti_` protocol,
  the 0x12345 server, the IOP-layout constants (0x1344C/0x14A90/0x8D28,
  engine-state words 0x159d4/0x12f2c/0x12f30, slot recs stride 0x278), the EE
  recv buffer 0x3B3000 family, and the EE state peeks (0x309720/0x309508/
  0x309BF4/0x309AE4/0x309B00/0x309BD0) are GAME-SPECIFIC.
- **Refactor destination** (MIGRATION §1.3 row, verbatim): SPLIT into
  - `engine/sif/hle/rpc_router.{h,cpp}` — class `RpcRouter` (DispatchSifRpcCall
    + server-id table; handlers registered per subsystem, not switch-cased);
  - `engine/pad/hle/padman_rpc.{h,cpp}` (`PadmanRpc`), `engine/mc/hle/
    mcserv_rpc.{h,cpp}` (`McservRpc`), `engine/cdvd/hle/cdvd_rpc.{h,cpp}`
    (`CdvdRpc` — S/N-cmd, search, disk-ready), `engine/sound/hle/
    libsd_rpc.{h,cpp}` (`LibsdRpc`), `engine/iop/hle/fileio_rpc.{h,cpp}`,
    `engine/iop/hle/loadfile_rpc.{h,cpp}`, `engine/iop/hle/iop_heap.{h,cpp}`
    (`IopHeap`), mtap/EyeToy stubs → `engine/iop/hle/`;
  - `engine/cdvd/hle/loader.{h,cpp}` — `HandleCustomLoaderRecompiled`'s
    completion-visibility model is HARDWARE/TIMING HLE (HOOK §3.6); class
    `LoaderCompletionModel`;
  - `engine/cdvd/iso9660.{h,cpp}` — `iso_scanner` → class `Iso9660Scanner`
    (merges with hle_heap.cpp's walker per MIGRATION — ONE walker);
  - `games/<title>/iop/stream_irx/` — the `lti_`/0x12345
    protocol glue: dispatch bracket constants, `streaming_delivery` (class
    `StreamDelivery`), `StreamPerformIsoRead`, the S46 `BankChainGovernor` and
    the EE-RPC bridge (`EeStreamRpcBridge`, sid 0x12344321) — all keyed to this
    title's recompiled module layout;
  - `games/.../savestate/stream_sections.cpp` — the STREAM SectionSpec provider.
  Legacy twin `HandleCustomLoader` (:5094–5990, 897 ln, `#if USE_IOP_RECOMPILER`
  = 1 makes it unreachable) — DELETED (A1 §4.4 near-twin row).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_MUSQ (:3158,:3473,:3543,:3717) | OFF-unless-1 | music-stream forensics prints (MUSQ7-GOV, batch decode) | move-to-diagnostics-build | DIAG (core/diag probe kit; category SND) |
  | PS2_SND_PAUSE_DEFER (:3199) | OFF-unless-1 | hold wire cmd-10 pause during mid-prime stream group | delete-both (experiment) | DELETE (both arms; the save-select wedge owns a root fix in sound/) |
  | (via `SndRegEnabled()`, latched in hle_heap.cpp:685 PS2_SNDREG) | ON | S43/S46 fidelity family incl. dispatch-thread impersonation + bank governor | keep-as-setting per A2, but reads here are gate mirrors | NORM — the S43/S46 arm becomes the only path; the legacy pre-S43 arm deletes; PS2_SNDREG hatch retires |
- **Hacks / workarounds** (blunt):
  1. `USE_IOP_RECOMPILER` compile-time switch + 897-line legacy twin
     `HandleCustomLoader` (:33, :5094) — two generations of the loader in one
     TU. Correct: v2-only; delete the twin and the macro.
  2. S46-BANKGOV batch rewrite (:3010–3170) — holds bank-chain records so the
     single-job engine sees one chain per dispatch round. This is a MODEL of
     retail IOP pacing implemented as wire surgery with host-side carry that a
     mid-carry savestate loses. Correct: the PS2 rule is "RPC batches arrive at
     CD-latency spacing"; owning class = `engine/cdvd/hle` latency model +
     title `StreamDelivery` (pace at the completion model, not by rewriting
     payload bytes).
  3. S65 PAUSE-DEFER (:3195–3290) — order-massaging experiment, default OFF.
     Correct: delete; root fix is sound-core prime/pause semantics.
  4. EE game-state peeks at 0x309720/0x309508/0x309BF4/… (:2986–3000) — game
     addresses in an engine file, log-only. Correct: delete with log purge;
     any needed observability becomes a title triage probe.
  5. EE-RPC 120 ms timeout → synthesized reply fallback (:118–140) — masks a
     missed service point. Correct: BoundaryProtocol guarantees the service
     point; timeout becomes a loud triage event, not silent synth.
  6. `VerifyRamContent` (:291) + [STREAM EE-STATE] diag reads — session
     archaeology; delete.
- **Risks**: largest MIXED file in the shard; the engine/game cut must keep the
  guest-monitor bracket semantics (single-core exclusion) intact or STREAM.IRX
  races appear. Proving tests: sound corpus (SOUND_P8 acceptance runs), boot →
  island load anchor savestates, `bug_reports/` save-select repro, EQUIVALENCE
  audio-hash leg; RPC replay of a captured batch stream (M2 RpcCase per
  TRIAGE_CAPTURE) proves handler-by-handler moves.

---

### host_app/vif.cpp (3,609 lines)

- **Purpose**: VIF0/VIF1 command processor. FIFO/latch assembly
  (`WriteToLatch2`), the command state machine (`ProcessFifo`,
  `ExecuteCommand`: NOP/STCYCL/OFFSET/BASE/ITOP/STMOD/MSKPATH3/MARK/FLUSH*/
  MSCAL*/STMASK/STROW/STCOL/MPG/DIRECT/DIRECTHL), the full UNPACK engine
  (`ExecuteUnpack`: all formats, masks, CL/WL cycle stealing, double buffer),
  a whole-span fast path (`ProcessDataFast`, PS2_VIF_FAST), MTVU integration
  (worker-private VU1 context via `Vif_VuCtx`, W13-E3 exec-idle wait points,
  UNPACK shadow staging + `CommitStagedUnpacks`), and MMIO `Read`/`Write`.
- **Public surface**: `VIF` class + global `g_vif`; KICKCNT per-frame counters
  (`g_kc_*`, `KickCount_FrameTick`); consumed by dmac.cpp (chain feed),
  memory.cpp (MMIO), vu1_async (worker replay), vif_replay test.
- **Classification**: GENERIC-PS2 (A6: zero code-level game tokens; the
  s8viftap window constants are diag-only — see hacks). Subsystem: VIF.
- **Refactor destination** (MIGRATION: "vif.{cpp,h} | vif/ — verbatim + gate
  purge"): `engine/vif/` split one-class-per-file:
  `vif.{h,cpp}` (class `Vif`: registers, state machine, command dispatch),
  `vif_unpack.{h,cpp}` (`VifUnpack`: ExecuteUnpack + mask/row/col logic),
  `vif_fast_span.{h,cpp}` (`VifFastSpan`: the span parser — merged as the ONLY
  parser once PS2_VIF_FAST is NORM, decline ladder folded in),
  `vif_mtvu_seam.{h,cpp}` (`VifMtvuSeam`: wait points + unpack shadow commit).
  KICKCNT counters → core/diag census. mipsrc spans → core/diag.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_VIF_FAST (:801) | ON-unless-0 | span fast path vs word-at-a-time latch/FIFO | keep-as-setting (canon) | NORM — fast span becomes the only parser; slow word loop kept ONLY as the fast path's per-command decline target, not env-selectable |
  | PS2_UNPKT_CENSUS (:344) | OFF-unless-nonzero (+frame≥1990 hardcode) | UNPACK class census emit | move-to-diagnostics-build | DIAG |
  | PS2_VIF_ALIGN_LOG (:1515,:1730,:1789) | OFF (num-if-set) ×3 mirror latches | UNPACK phase logging | move-to-diagnostics-build | DIAG (one latch via config; mirrors die) |
  | PS2_SHADOW8_KTAP (:2172,:2250,:2468) | OFF (num-if-set) — 3 of 16 tree-wide mirrors | shadow-ordering taps | move-to-diagnostics-build | DIAG |
  | PS2_S8_VIFTAP (:2553) | OFF (minvf-gated) | header-window unpack tap | delete-both (one-off) | DELETE |
  | PS2_VIF_WTRACE (:3456) | OFF | ocean-color W-lane write trace | delete-both (one-off) | DELETE |
  | (vif.h, not this file: PS2_VIF_UNPACK_SIMD, PS2_VIF_UNPKBX) | ON | SIMD unpack lanes | delete-losing-arm | NORM (noted for the vif.h shard) |
- **Hacks / workarounds**:
  1. s8viftap hardcoded VU1-data header windows `{0x110,0x130},{0x173,0x193}`
     qw + header bases 0x11a/0x17d (:2568–2575) — game VU1 memory-layout
     constants inside a diagnostic. Correct: delete; if ever needed again it is
     RendererProfile/Vu1Library data consumed by a declared probe.
  2. `w10unpkt::On()` hardcodes `frame >= 1990` (:351; namespace at :341) —
     scene-keyed census window. Correct: delete (frame windows are triage-tool
     arguments).
  3. Three independent latches of PS2_VIF_ALIGN_LOG and three of
     PS2_SHADOW8_KTAP — mirror-drift idiom (A2 §2.3). Correct: single Config
     read; here they simply delete as DIAG.
  4. [KICKCNT] prefix chosen to dodge main.cpp's TagFilterBuf denylist (:100)
     — logging system fighting itself. Correct: core/log category VIF.
  5. UNPACK shadow refresh = full 16 KB memcpy per exec-idle point (:79–86,
     self-documented 0.14 ms/f) — a cost accepted to keep the staging rule
     simple. Correct: keep semantics; document as the MTVU seam contract in
     `vif_mtvu_seam` (candidate for dirty-range refresh later, out of scope).
- **Risks**: UNPACK edge cases (CL/WL, masks, V3-16 phase) are the classic
  regression field; the fast/slow unification must be proven by
  `tests/vif_replay` over the corpus plus `test_gif_fast_equivalence`-class
  A/B (in-process equivalence idiom, A5 §1.2). MTVU wait-point moves proven by
  the MTVU soak legs + vu1_replay.

---

### host_app/syscalls.cpp (2,496 lines)

- **Purpose**: The EE kernel syscall surface: `runtime_syscall_dispatcher()`
  (:2310) switches ~120 syscall numbers to handlers — thread/sema wrappers
  (delegated to ps2_syscall_wrappers/ps2_threading), event flags, INTC/DMAC
  handler add/remove/enable/disable, SetGsCrt + GsPutIMR/GetIMR + OSD config,
  sceSif{SetDma,DmaStat,SetDChain,GetReg,SetReg,StopDma}, FlushCache, file
  write/open/exit, Deci2. Also contains the EE **dynamic interpreter**
  (`dynamic_decode_and_execute`, `execute_single_instruction`,
  `handle_branch_logic` — a rabbitizer-driven fallback interpreter used for
  code the recompiler didn't emit, e.g. RPC callback thunks), `handle_mtc0_write`
  COP0 semantics, and the deferred-RPC-callback queue (g_pending_rpc_callbacks
  + mutex/cv) serviced by the CPU maintenance loop.
- **Public surface**: `runtime_syscall_dispatcher` (called by every recompiled
  `syscall`), individual handlers (SetGsCrt reused by gs_hle), globals `g_sif`,
  `g_gs_regs`, `g_video_state`, `g_file_io_handles`, `g_pending_rpc_callbacks`
  (+`g_rpc_cb_mtx/cv`), `custom_syscall_addresses`. Included by main.cpp,
  gs_hle.cpp, ps2_threading.
- **Classification**: GENERIC-PS2 (SDK 3.0.1 kernel semantics). Subsystem:
  EE kernel HLE.
- **Refactor destination** (MIGRATION: "syscalls.cpp | ee/hle/syscalls.* +
  ee/hle/dyn_interp.* (fallback interpreter out of syscalls)"): SPLIT —
  `engine/ee/hle/syscalls.{h,cpp}` (class `SyscallDispatcher`; table-driven,
  ARCH §12 row 8), `engine/ee/hle/dyn_interp.{h,cpp}` (class `DynInterp` —
  the rabbitizer interpreter + branch/delay-slot logic + mtc0),
  `engine/sif/hle/sceSif_regs.{h,cpp}` (GetReg/SetReg/SetDma/DmaStat bodies —
  the SdkBinding rows ride libkernl per HOOK §3.5), `engine/ee/hle/
  rpc_callback_queue.{h,cpp}` (class `RpcCallbackQueue`). Globals `g_gs_regs`/
  `g_video_state` definitions move to `engine/gs/state/privileged.cpp`;
  `g_sif` to `engine/sif/sif_state.cpp`. Alarm syscalls (_SetAlarm etc., today
  NotImplemented) get real ee/timing-backed bodies or documented cuts.
- **Flags consumed here**: none — zero getenv (confirmed by grep). The
  [R131-WAITCHAIN] consumer-wait mechanism is gated by PS2_WAITFIX read in
  timeline.h (not this file) → NORM there.
- **Hacks / workarounds**:
  1. `NotImplemented_Syscall` logs then `return; exit(1);` — dead exit after
     return (:89–93). Correct: a triage-visible unimplemented-syscall counter.
  2. `KERNEL_SYSCALL_TABLE_BASE 0x800002E0` + `custom_syscall_addresses[256]`
     (SetSysCall) — fine as SDK semantics, but the table should live in
     `SyscallDispatcher` state, not file-scope globals.
  3. Dynamic-interpreter g_logFile narration per branch (E18 in
     TRIAGE_CAPTURE) — becomes the EE fallback arm of `FallbackDetector`
     (core/triage), producing `EeFunctionCase` bundles instead of log lines.
  4. `read_string_from_guest` unbounded strlen on guest pointer (:85) —
     correct: bounded read via ee/mem accessor.
  5. sceSifSetDma (~300 ln at :482) interprets RPC command structs inline —
     splits into `sif/hle` per D3 (the SIF DMA-command decode is SIF's).
- **Risks**: the syscall numbers/behaviors are savestate-coupled (thread ids,
  sema state); dispatcher moves must keep handler addressing identical.
  Proven by: boot-to-menu anchors, test_ps2_argv, scheduler soak
  (ps2_threading tests), EQUIVALENCE frame-hash legs; dyn-interp extraction
  proven by forcing map-miss paths in tests/recomp_semantics.

---

### host_app/iop_stubs.cpp (2,422 lines)

- **Purpose**: HLE of the IOP kernel + device libraries that recompiled
  STREAM.IRX imports: thbase (Create/Start/Delete/Sleep/Wakeup Thread),
  thsemap (semas with host cv), sysclib/stdio bits, timrman hard timers (the
  module's 60 Hz heartbeat), libsd's 16 ordinal stubs mapped onto the host
  sound core (`sound::*`), cdvdman (sceCdInit/Read/Callback + deferred CD
  completion), and the S48 FIX-C deferred-event engine (counted queue of CD +
  SPU-voice-transfer completions with poll-retire) plus the single-core
  "guest monitor" that serializes all module code like the real one-core IOP.
- **Public surface**: `iop_stubs::` namespace — kernel stub entry points bound
  to import ordinals by iop_recompiled bindings; `GuestMonitorAcquire/Release`,
  `GuestMonitorYieldScope`, `SwapCurrentIopThreadId`, `RaiseStreamCdCompletion`,
  `StopHardTimers`, park census hooks. Called from sif_rpc_handlers (dispatch
  bracket), iop_loader, main.
- **Classification**: MIXED (A6 §5 row: "thbase/sifman/sysclib-shaped IOP
  kernel stubs (generic Sony API semantics) with stack/callback conventions
  tuned to the STREAM.IRX worker threads"). The kernel/libsd/cdvdman semantics
  are GENERIC; the module-layout constants (gp=0x13328, RPC-tid word 0x13328,
  sema word 0x12f18, engine words 0x12f1c/0x12f44/0x12f2c/0x12f30/0x132ec/
  0x133c0/0x159d4, scratch stacks 0x1FFA00/0x1FFC00/0x1FFE00, per-thread stack
  formula) are GAME/module-specific.
- **Refactor destination** (MIGRATION: "iop_stubs.{cpp,h} | iop/hle/modules.*
  — verbatim"; A6 §7 row G refines): `engine/iop/hle/` one-class-per-file —
  `thbase.{h,cpp}` (class `IopThreads`), `thsemap.{h,cpp}` (`IopSemas`),
  `timrman.{h,cpp}` (`IopHardTimers` incl. tick-clocked heartbeat),
  `libsd_stubs.{h,cpp}` (`LibsdStubs` over engine/sound),
  `cdvdman_stubs.{h,cpp}` (`CdvdmanStubs`), `deferred_events.{h,cpp}`
  (`IopDeferredEvents` — the FIX-C counted queue; the latency model is
  HARDWARE/TIMING HLE per HOOK §3.6), `guest_monitor.{h,cpp}` (`GuestMonitor`).
- **[IOPK 2026-09-24] UPDATE — cooperative kernel landed**: the thbase/thsemap
  /timrman/FIX-C semantics now ROUTE to `host_app/iop_kernel/` (IopKernel /
  IopClock / SifRpcBridge / IopModuleTable, hatch `PS2_IOP_KERNEL` default ON
  [R1B: shipped default OFF, BUG-026 — realtime boot stall, PHASE0_IMPL §12.2;
  profile pins `=1`/frame];
  ARCHITECTURE §7.1). What remains in iop_stubs.cpp after NORM: the ordinal
  binding surface, the POD savestate mirror (IOPM layout unchanged), guest
  glue closures (IopContext setup, stacks, $gp), the `PS2_IOP_KERNEL=0`
  legacy arm (per-guest-thread std::threads + cv waits + wall heartbeat
  ticker + FIX-C event thread — the delete-losing-arm set), and the
  `PS2_IOPSCHED_DIAG` trace. Destination table above still applies; the
  kernel classes themselves land as `engine/iop/` per MIGRATION.
  The STREAM.IRX-specific constants (gp, RPC-tid address, engine-state words,
  stack conventions) move to `games/<title>/iop/
  stream_irx/module_layout.h` as data consumed by the engine classes.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_HB_TICKCLOCK (:1175) | ON-unless-0 | heartbeat clocked on vblank ticks vs wall sleep | delete-losing-arm | NORM (tick-clocked only; wall branch deletes — ARCH §4.7 heartbeat row) |
  | PS2_IOPQ (:1231) | OFF-unless-1 | 1/s CD-job engine state dump | delete-both | DELETE |
  | PS2_MUSQ (:1386,:1413,:1450,:1514,:1569) | OFF-unless-1 (5 mirror latches) | ENVX/KON/SSA/VT/ProcBatch probes | move-to-diagnostics-build | DIAG (probe kit; single gate) |
  | PS2_MUSQ_VFMIN (:1390,:1418) | 0 | probe arm frame | delete-losing-arm | DELETE (tool argument, not env) |
  | PS2_NAXQ (:1473) | OFF-unless-1 | NAX poll sampling | delete-both | DELETE |
  | PS2_SND_PROCBATCH (:1506) | ON-unless-0 | real sceSdProcBatch semantics vs legacy drop | delete-losing-arm (proven-fix-ON) | NORM (real semantics only; hatch deletes) |
  | PS2_SND_KONLOG (:1558,:1658) | OFF (2 of 4 mirrors) | VT/CD-CB sequence prints | move-to-diagnostics-build | DIAG |
  | PS2_CD_LAT_US (:1804) | 3000 µs | deferred CD completion latency | delete-both per A2 (experiment) | NORM as a **constant** of the cdvd latency model (value 3000 µs pinned; env dies). Flag as model parameter in `cdvd/hle` docs |
  | PS2_SPU_LAT_US (:1813) | 300 µs | SPU voice-trans completion latency | delete-both | NORM as a model constant (300 µs) |
  | PS2_VTQ (:1894) | OFF-unless-1 | voice-trans completion protocol probe | delete-both | DELETE |
  | (via SndRegEnabled — PS2_SNDREG, hle_heap.cpp) | ON | S43+ fidelity family vs legacy flag-forcing | keep-as-setting per A2 | NORM — the S43/S48 arm (guest monitor, deferred events, bare SignalSema) is the only path; every `if (!SndRegEnabled())` legacy branch deletes |
- **Hacks / workarounds**:
  1. `RaiseStreamCdCompletion` legacy arm force-writes module error/tick flags
     0x12f44/0x12f1c and runs the CD callback on a zeroed context (:1660–1700)
     — the documented WRONG behavior kept as bisect hatch. Correct: delete arm
     with PS2_SNDREG; the SignalSema-only path is the implementation.
  2. Heartbeat sanity clamps `usec` to [1000,100000] (:718) — silently
     rewrites the module's timer request. Correct: keep as documented timrman
     divergence comment (`// diverges:`) or honor exact period.
  3. Per-thread stack formula `0x100000 - tid*0x4000` and scratch stacks
     0x1FFA00/0x1FFC00/0x1FFE00 — invented host conventions in module RAM.
     Correct: title module_layout data with a documented reservation map.
  4. IOPQ dump reads a dozen raw module addresses + SPU peeks — archaeology;
     delete (triage owns future capture).
  5. Handler lookup fallback `handler & 0xFFFF` module-relative then absolute
     (:770) — masks a binding ambiguity; correct: iop_recompiler emits one
     canonical keying.
- **Risks**: timing model (CD/SPU latency, heartbeat clock) is load-bearing
  for boot audio and save-select; changes proven by SOUND corpus + boot
  anchors + the IOPM savestate section round-trip. Guest-monitor moves proven
  by TSAN soak + stream-start/stop repros in bug_reports/.

---

### host_app/gif_arbiter.cpp (1,808 lines)

- **Purpose**: Orders GIF packets from PATH1 (VU1 XGKICK), PATH2 (VIF1
  DIRECT), PATH3 (GIF DMA) into one delivery stream. `submit()` copies payload
  into per-source routes (MTVU marker fill on worker/exec threads, inbox on EE,
  legacy m_queue when MTVU off); `drain()` (the single-drain-owner, EE side)
  promotes sealed markers, dispatches R168 frame-boundary slots in stream
  position (VSync push via `SetBoundaryHook`), applies typed VU1-HLE packets
  (`hle_ops` DIRECT / `hle_ir` DrawIR) at their exact slots, and feeds raw
  bytes to `g_gif.ReceiveData`. Also hosts three ordering experiments (r198
  PATH3-defer, s8 tailfix hold, s8 ctxlatch bracket) and the E2-MEM pending
  bytes ledger.
- **Public surface**: class `GifArbiter` + global `g_gifArbiter`; used by
  vu.cpp (XGKICK submit), vif.cpp (DIRECT), dmac.cpp/gif.cpp (PATH3), gs_hle
  (openBoundary/noteFeedForBoundary/drain), vu1_async (markers).
- **Classification**: MIXED — arbitration mechanism GENERIC-PS2 (GIF PATH
  priority is real hardware); the s8/async-VU1 constants (FRAME 0x70000 vs
  0x230000, ZBP 0x150000, XYOFFSET 1792/1824, staging window 0x2f0000) are
  game VRAM layout (A6 §3.3 counts gif_arbiter 2 code hits).
- **Refactor destination** (MIGRATION: "gif_arbiter.{h,cpp} | gif/arbiter.*"):
  `engine/gif/arbiter.{h,cpp}` — class `GifArbiter` (submit/drain/boundary/
  marker routing, pending ledger), `engine/gif/boundary_slot.{h,cpp}` if the
  R168 slot logic warrants its own class. The three ordering experiments do
  NOT move (see hacks); the game constants they carry go to
  `games/.../renderer_profile.cpp` VramLayout only if a surviving mechanism
  needs them (none does — all default-OFF or dead).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_R198_PATH3DEFER (:55) | 0 = OFF | PATH3 staging-upload hold modes 1/2/3 | delete-both | DELETE (both arms). The real PS2 rule — PATH3 IMAGE only runs in PATH1-idle windows — becomes a documented GIF-arbiter invariant; if ever needed it is a real arbitration state machine in `GifArbiter`, not a byte-scanning hold |
  | PS2_R198_PATH3DEFER_BASE (:62) | 0x2f0000 | staging window base | delete-both | DELETE |
  | PS2_SHADOW_TAILFIX (:160) | OFF (superseded) | hold-model shadow fix | delete-both | DELETE (proven never to engage — 0 holds; superseded by ctxlatch) |
  | PS2_SHADOW8_KTAP (:299,:520) | OFF | submit-tap + pass dumps | move-to-diagnostics-build | DIAG (mechanism only) |
  | PS2_S8_PKTDUMP (:1496) / PS2_S8_PKTDUMP_MINVF (:1504) | OFF | raw drain-pass dumps | delete-both (one-off) | DELETE |
  | (s8ctx::On() in s8_ctxlatch.h — PS2_SHADOW_ORDERFIX/PS2_SHADOW_CTXLATCH) | OFF | PATH1 ctx bracket | delete-both per A2 | DELETE here as consumer; the ROOT FIX is the deferred-VIF1 walk reading ring slots after rewrite (the [DEFER-VIF1] divergence) — owned by `engine/dmac` chain walk + `ee/timing` (walk when kicked, not when observed). The bracket masks that defect (HOOK-class workaround) |
  | (gif_arbiter.h:48 PS2_GIF_DRAIN_THREAD) | ON-unless-0 | drain-thread arm | delete-losing-arm | NORM (noted; header is another shard's file) |
- **Hacks / workarounds**:
  1. Dead `if (false && g_cutscene_active …)` async-VU1 cutscene block
     (:700–840) — 140 lines of unreachable code installing hardcoded main-pass
     GS state (FRAME 0x70000, GEQUAL, ZBP 0x150000, XYOFFSET 1792/1824,
     scissor 511×447) around held PATH1 packets. Correct: DELETE outright
     ("dead code retained for one validation run" — the run happened).
  2. s8ctxlatch `DeliverPath1` synthesizes an A+D packet from an accumulated
     PATH1 shadow and save/restores 20 GSState fields by direct member copy
     (:250–500) — queue-adjacent state surgery keyed on byte signatures
     (wrapped-z rows, tone RGBA). Correct: root-fix the deferred-VIF1
     stale-ring-read divergence in dmac (walk at kick time / snapshot ring at
     kick), then delete; adjudicated per HOOK §4 ladder.
  3. s8tailfix + r198fix3 held-packet vectors are file-statics that `clear()`
     does not drop (self-documented caveat) — lifecycle hole accepted for an
     experiment. Correct: delete with the experiments.
  4. [ARB-SUBMIT] 200-line PATH2 dump (:590–650) — bullseye-session
     archaeology in the hot submit path. Correct: delete; replay tooling covers
     it.
  5. `s8arb::Note` FBP matcher hardcodes 0x038/0x118 page values (0x70000/
     0x230000) (:545) — game constants in a diag; delete.
- **Risks**: ordering is THE GS contract (draw order must be preserved).
  Removing the ctxlatch before the dmac root fix regresses character shadows
  (kicksum fb70 test); sequence: root fix + delete in one commit, gated by
  gs_corpus replay byte-identity on bad_shadows + s57_walk captures, plus the
  boundary-order tripwire census staying 0.

---

### host_app/gs_hle.cpp (1,113 lines; ~1,300 after [TICKSYNC 2026-09-25])

- **[TICKSYNC 2026-09-25 addendum]**: `Ps2TickSyncBoundary()` runs the fixed
  8-step sequence of `ee_timing/boundary_sequencer.h` (rendezvous → tick →
  drain → maint → IOP dispatch → EE stream RPC → savestate → verify) under
  `PS2_TICK_SYNC=1`; `Ps2LockstepPresentRendezvous()` is the ticket park
  (external linkage for the sif_hle mirror); the VSync push stamps
  `RenderJob::lockstep_ticket`. The first `PS2_FRAME_LOCKSTEP` block (R170
  counter pair) was deleted (it wedged after a load). Destination addendum: the
  step bodies → `engine/ee/hle/frame_boundary.*`, the order → `ee/timing/
  boundary_sequencer.*` (ARCH §7.2).
- **Purpose**: The sce libgraph/libdma sync surface and the de-facto frame
  boundary. `hle_sceGsSyncV` (:294) is the once-per-frame edge: tail-chase the
  VIF1 ring, deliver/enqueue the boundary VSync (defer/legacy branches), MTVU
  sync or ordered-boundary drain, runahead parks (R170 pace park or legacy
  MAX_FRAME_LAG=1 bound with WAITFIX/legacy-poll variants), vblank tick wait,
  timer pump, EE-stream RPC service, savestate hook, CSR field toggle.
  `hle_sceGsSyncPath`/`hle_sceDmaSync`/`hle_sceGifSyncDma` (report-idle +
  flush/sync/drain), `hle_sceGsResetGraph` (CSR reset + GsGParam @0x2e7ff0 +
  SetGsCrt + BIOS-like DISPFB/DISPLAY defaults), `hle_sceGsResetPath`,
  `hle_sceGsSendImage` (IPU-bypass image feed into the GS transfer engine),
  `hle_SetupDisplayRegisters` (game fn 0x19aff0: CRTC regs from the game
  struct @0x3D4AE0 with Crash-NTSC fallback), dead `hle_InitGraphics` and
  commented-out `hle_RenderLoop`. Owns `g_window`/`g_renderer` SDL globals and
  `g_sceGsSyncV_count`.
- **Public surface**: the eight `hle_*` handlers (bound by recompiler seam
  emission at fixed sce addresses; 5 of 8 SDK-band addresses superseded by the
  sce registry — HOOK §3.5), `Ps2R170BoundaryPark` (external linkage for
  sif_hle's vblank mirror), `g_sceGsSyncV_count` (pad cadence coupling).
- **Classification**: MIXED — sce sync/reset/SendImage semantics GENERIC-PS2;
  `hle_SetupDisplayRegisters` is GAME code replacement (workaround class);
  boundary orchestration is engine timing.
- **Refactor destination** (MIGRATION row, applied verbatim): SPLIT —
  `engine/gs/hle/sce/gs_sync.{h,cpp}` (`GsSceSync`: SyncV surface + SyncPath/
  ResetGraph/ResetPath), `engine/dmac/hle/dma_sync.{h,cpp}` (sceDmaSync/
  sceGifSyncDma), `engine/ee/timing/boundary_protocol.{h,cpp}` (the ONE
  boundary sequence: NoteArrival → pace park → tick wait → timer pump → ISR/
  RPC service → savestate hook — lifted from SyncV's body), `engine/gs/hle/
  present/` (pacing + CSR field), `engine/gs/hle/sce/gs_send_image.{h,cpp}`
  (SendImage over gs/xfer with the MTVU PRIVPUSH fence), core/savestate
  quiescence hook. `hle_SetupDisplayRegisters` → EMULATION-BUG-WORKAROUND
  quarantine (HOOK §3.5): retired when recompiled FUN_0019aff0 runs against
  gs/state/privileged; its NTSC fallback constants delete. `hle_InitGraphics`
  + `hle_RenderLoop` + `Ps2RenderNode` → DELETE (dead; MIGRATION explicit).
  `g_window/g_renderer` → `engine/platform/`. `g_sceGsSyncV_count` → frame
  counter published by ee/timing, consumed by pad/hle.
- **Flags consumed here**: none directly (zero getenv). Consumes latched gates
  from other TUs — for the record, their outcomes at THIS call site:
  vu1_async DeferOn (PS2_MTVU_DEFER→NORM: defer branch becomes the only
  boundary shape; legacy Sync branch deletes), tl::WaitFixOn (PS2_WAITFIX→
  NORM: cv-park only; 500 µs poll branch deletes), ee_pace::PaceOn
  (PS2_EE_PACE→NORM per ee_pace row below), eetimers::On (PS2_EE_TIM0→NORM:
  timer pump unconditional at boundary), PathSyncRelaxOn (PS2_MTVU_PATHSYNC_
  RELAX→DELETE: superseded by DeferOn joining the skip disjunct),
  shutdown_policy (PS2_SHUTDOWN_WAIT→SETTING `engine.shutdown_wait_s`).
- **Hacks / workarounds**:
  1. `hle_sceGsResetGraph` writes "BIOS-like" DISPFB/DISPLAY defaults (:646–
     700) to mask the first-frame scanout race — a real boot-order model
     problem. Correct: the PS2 rule is "the BIOS leaves CRTC configured before
     game entry"; owning class = `ee/hle` boot environment (BIOS CRTC state as
     part of boot env, like S8-ARGV), not a reset-time patch.
  2. `hle_SetupDisplayRegisters` re-implements game fn 0x19aff0 with struct
     offsets AND a hardcoded Crash-NTSC fallback (:1000–1113) — HOOK §3.5
     workaround row; retire at the gs/state/privileged root fix.
  3. `hle_sceGsSendImage` fallback path assumes PSMCT32/512-wide/dest 0x70000
     when no transfer is active (:900–960) — game VRAM guess in engine code.
     Correct: the transfer must be active (BITBLTBUF set) — make the fallback
     a loud triage decline; constants die.
  4. Four-deep branch nest of pacing eras (PaceOn/WaitFix/Defer/legacy-poll)
     in SyncV — resolves to ONE BoundaryProtocol after NORM collapses.
  5. 100 ms valves (parks fall through unpaced) — keep as the documented
     BoundaryProtocol safety valve, but count releases (already censused).
- **Risks**: this file IS the frame boundary; any ordering slip shows as
  double-presents, stale frames, or savestate divergence. Proven by:
  EQUIVALENCE frame-hash + boundary-order tripwire, anchor savestate
  round-trips (the [E2-SAVE] consume-and-return path must survive), pad
  cadence tests (SyncV-count coupling), loop_hist distribution unchanged.

---

### host_app/gs_class_renderer.h (585 lines)

- **Purpose**: The Phase-D per-class native-renderer framework: 10-class
  taxonomy (`world-tristrip` … `raw-other`), the `ClassifyFields`/`Classify`
  classifier (strstr over this game's VU1-HLE program names + S9 signature +
  ≥256×128 blit threshold — verbatim gs_replay's ClassOf), the `ClassRenderer`
  interface (pure `Accept` / all-or-nothing `Draw` / `WarmUp`), registry,
  master + per-class env hatches, always-on routing counters, `Dispatch()`
  (caller-runs-fallback contract), `WarmUpAll`, `EmitCounters`, the R121-ATTR
  attribution slots, and two bolt-on diagnostic draw filters (s103drawlim
  ordinal limiter, s105skip clause-spec ablation filter).
- **Public surface**: `classrdr::` everything (Dispatch/Classify/Register/
  Cnt/On/ClassEnabled/WarmUpAll/EmitCounters/CurDrawClass/CurS9Path),
  `s103drawlim::Drop`, `s105skip::{Parse,Match,EnvOn,Emit}`. Consumed by
  graphics_thread.cpp, tests/gs_replay.cpp, test_gs_classrdr.cpp, the six
  `*_native.inc` renderers, opengl_backend demand-flush attribution.
- **Classification**: MIXED (A6 §3.1 — THE exemplar): framework GENERIC,
  taxonomy/classifier GAME-derived (keys on this game's program names and
  census shapes).
- **Refactor destination** (MIGRATION §1.6 row, verbatim): SPLIT —
  `engine/gs/hle/classrdr/class_renderer.{h,cpp}` (interface `ClassRenderer`),
  `engine/gs/hle/classrdr/class_registry.{h,cpp}` (`ClassRegistry`: registry +
  Dispatch + counters + warmup), `engine/gs/hle/classrdr/class_classifier.
  {h,cpp}` (`ClassClassifier` — mechanism that reads RendererProfile data);
  the strstr stamp→class chain, the S9 raw signature (test_date / frame_psm==
  10 / tpsm==27), and the blit threshold become `games/.../renderer_profile.
  cpp` `{stamps, raw}` DATA (ARCH §14.3). s103drawlim + s105skip → the triage/
  replay tool layer (`tools/triage` ablation arguments; s105skip's Parse
  already serves gs_replay `--skip`) — out of the runtime header.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_GS_CLASSRDR (:242) | ON-unless-0 (GD9) | class-renderer routing vs raw register flow | delete-losing-arm | NORM — Dispatch is the only flow; the =0 short-circuit deletes (fallback ladder REMAINS as the decline path, which is the [GD1] contract, not a flag) |
  | PS2_GS_CLASSRDR_<TOKEN> ×10 (:ClassEnabled) | ON | per-class disable | delete-losing-arm | DELETE as env; per-class enable survives ONLY as the RendererProfile row being present/absent (data), plus test override hooks (TEST-ONLY) |
  | PS2_S103_DRAWLIM (:440) | OFF | per-frame draw-ordinal limiter | delete-both | DIAG→tool (replay argument; env form deletes) |
  | PS2_S105_SKIP (:536) | OFF | clause-spec draw ablation | delete-both | DIAG→tool (gs_replay --skip keeps Parse; env form deletes) |
- **Hacks / workarounds**:
  1. `ClassifyFields` strstr on `hle_name` (:152–186) — game program names
     hardwired in engine code. Correct: RendererProfile.stamps lookup.
  2. S9 signature constants (frame_psm==10 CT16S, tpsm==27 T8H, DATE) and the
     256×128 threshold — game census as code. Correct: RawClassifierSpec data.
  3. `ClassOverrideForTest` static int[10] + env cache struct — 11 gate
     mirrors in one header. Correct: Config/profile + one test hook.
  4. Attribution slots `CurDrawClass/CurS9Path` are relaxed atomics written
     per draw for an env-gated counter elsewhere (PS2_R121_ATTR) — probe
     plumbing in the hot path. Correct: PS2R_DIAGNOSTICS-only GenProbe.
- **Risks**: classifier moves must keep census language identical (the corpus
  CENSUS_MASTER rows key on class names). Proven by: gs_replay full-corpus
  routing census equality (seen/native/declined per class unchanged), G-D1
  byte-identity per class, [GD1] empty-profile leg renders.

### host_app/gs_page_tracker.h (517 lines)

- **Purpose**: [GB2] 8-KiB-page VRAM residency tracker: per-page author
  (cold/CPU/GPU-target+fbp), epoch, stale bit, dirty row band; census-seeded
  reader page set (S9 CLUT feedback band); hazard detection (`ReaderTouch`
  classes CLUT/snap/local-local/save), GPU deposit/elide notes (linear + rect
  walks via vramcensus::PagedGeom), scanout-resolve for native present
  ([GB3]), save-time stale reconcile ([GB4]), and the B4 deferred-PBO sub-gate.
  All header-inline relaxed atomics + counters, `Summary()` report.
- **Public surface**: `gsresident::` — On/PboOn/Init, NoteCPUWrite,
  NoteGPUDepositRect, NoteGPUElided(Rect), ReaderTouch(Rect), ScanoutResolve,
  ReconcileStaleAtSave, NeedsMirror, counters. Consumed by opengl_backend,
  gs_state, render.h, graphics_thread, savestate.
- **Classification**: MIXED — page-residency mechanism GENERIC; the seeded
  reader band table (`kBand` pages 0x161–0x1ab = bytes 0x2C2000–0x357FFF, the
  S9 CLUT feedback) is game census data.
- **Refactor destination** (MIGRATION §1.5: "absorbed by the ONE PageRecord
  rewrite — delete after Phase 5"): ABSORBED/REWRITTEN into
  `engine/gs/vram/vram_image.{h,cpp}` (`VramImage`) + `engine/gs/vram/
  page_record.{h,cpp}` (`PageRecord` — author/epochs/state/write_bits/
  dirty_band/reader_flag per ARCH §4.3). This header does not survive as a
  file; its counters become VramImage counters, its reader-band seed becomes
  `games/.../renderer_profile.cpp` VramLayout data (census-seeded read-hot
  pages), its ScanoutResolve becomes VramImage/present logic.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_GS_RESIDENT (:72) | ON (unset=ON since GB5; "0" escape) | texture-only residency vs eager per-frame SyncRTToVRAM | delete-both per A2 (miscategorized "experiment"); reality: proven-fix-ON | NORM — residency IS the VramImage design; the eager legacy path deletes (it survives only as MaterializePages demand flush) |
  | PS2_GS_RESIDENT_PBO (:102) | ON under master | deferred PBO readback vs eager reader-band deposits | delete-both per A2; reality proven-ON | NORM — ReadbackEngine staging rings behind backend caps.staging (ARCH §4.4); sync glReadPixels remains the caps-false fallback, not an env arm |
- **Hacks / workarounds**:
  1. `SeedReaderPages` hardcoded band table (:171–180) — game data in engine.
     Correct: RendererProfile/VramLayout seed consumed by VramImage.
  2. Stale over-mark + save-time `ReconcileStaleAtSave` (:207) — repairing the
     tracker's own over-approximation at save time. Correct: PageRecord keeps
     precise write_bits ([CX8]) so the reconcile pass is unnecessary;
     `AssertNoStaleAtSave` stays as the invariant check.
  3. Linear-span vs rect-walk duality (NoteGPUElided vs NoteGPUElidedRect,
     self-documented over-marking) — two geometries for one event. Correct:
     ONE PsmLayout page walk (gs/format) used everywhere.
  4. Mutable inline atomics arrays in a header (g_author[512] etc.) — header
     globals (banned; A1). Correct: VramImage members, one TU.
- **Risks**: last-writer arbitration and save-time zero-stale are savestate-
  critical ([GB4] c_save_stale==0). Proven by: anchor save/load round-trips,
  TEXPOP/shadow-repaint corpus scenes (BUG-007/BUG-010 regressions), the
  gs_corpus replay with residency counters compared old-vs-new.

### host_app/gs_deswizzle.h (196 lines)

- **Purpose**: A standalone PS2 GS VRAM address deswizzle: page/column tables
  for PSMCT32/CT16/T8, `addr_psmct32/16/psmt8` address functions, a CSM1 CLUT
  index unswizzle, and `read_pixel` (VRAM snapshot → RGBA with CLUT + PS2
  alpha doubling). Derived from PCSX2/TellowKrinkle's swizzle visualizer.
- **Public surface**: `gs_deswizzle::` free functions; consumers are snapshot/
  dump tooling paths (render.h snapshot code, offline analysis).
- **Classification**: GENERIC-PS2 (GS format layer). Subsystem: GS format.
- **Refactor destination** (MIGRATION: "gs_deswizzle.h | gs/format — one of
  the 4 swizzle families collapsing (Phase 3 rewrite row)"): DELETED as a
  family — its address math collapses into `engine/gs/format/psm_layout.
  {h,cpp}` (`PsmLayout` — texture_decoder's GetAddressPSM* family is the
  canonical one, A1 §7 concept 1); `csm1_unswizzle` collapses into
  `engine/gs/format/clut.{h,cpp}` (ONE `Csm1Swizzle` — A1 §2 flags this file's
  row-order {0,2,1,3} formula as a DIFFERENT expression of the same
  permutation, a maintainer trap); `read_pixel` becomes a thin tool helper
  over the format layer (tests/tools only).
- **Flags consumed here**: none.
- **Hacks / workarounds**:
  1. Duplicate swizzle family #3 of 4 (A1 §2.2) — same math as
     texture_decoder.h:196-252, different tables. Correct: one PsmLayout.
  2. PSMT8 column-table comment thread ("Wait - … Actually, looking at the
     PCSX2 visualizer code more carefully:") (:118–137) — in-code derivation
     narrative with a dead `table_offset` variable. Correct: delete narrative;
     the format layer carries a one-line invariant + a golden test.
  3. `read_pixel` hardcodes alpha `a<=128 → a*2` doubling inline — the 0–128
     domain rule belongs to `gs/format/alpha.h` (one home).
- **Risks**: none if collapsed via equivalence tests: the existing
  swizzle-equivalence test family (tests/swizzle) proves table equality
  exhaustively over x,y,bw before deletion.

### host_app/gs_glbatch.h (121 lines)

- **Purpose**: [GS-C2] consumer-side adjacent-only DrawBatch merging on the
  graphics thread: `Eligible` (non-empty DrawBatch in RUNDRAW class OPAQUE/
  GLBLEND), `CanMerge` (adjacency guaranteed by caller + equal vram_epoch +
  ≤65,536 verts + field-exact state equality via r160::RundrawKeyFieldsEqual),
  `Merge` (plain vertex append, absorbed job's payload dropped). Default OFF.
- **Public surface**: `glbatch::{On,Eligible,CanMerge,Merge,kMergedVertCap,
  OnOverrideForTest}`; consumed by graphics_thread.cpp's run collector.
- **Classification**: GENERIC-PS2 (order-inviolable merge over GS state
  equality). Subsystem: GS render queue consumption.
- **Refactor destination** (MIGRATION: "gs_glbatch.h (GL staging RenderBatch)
  | render/backends/gl/ internal — split (must NOT survive as the IR, A3
  finding 7)"): the merge predicate logic folds into the GL backend's draw
  submission (`engine/render/backends/gl/gl_draw_merge.{h,cpp}`, class
  `GlDrawMerge`) operating on DrawIR records — explicitly NOT part of the IR
  contract. Since PS2_GS_GLBATCH is default OFF and the producer-side RUNDRAW
  merge (default ON) is the proven one, the honest disposition is: producer
  RUNDRAW → NORM (lives at the DrawIR producer flush); this consumer-side
  duplicate → DELETE unless Phase-6 profiling shows the queue-resident pair
  win matters (then GlDrawMerge, backend-internal).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_GS_GLBATCH (:68) | OFF-unless-nonzero | consumer-side adjacent merge | delete-losing-arm per A2 | DELETE (default-OFF experiment never promoted; the producer merge is the shipped semantics). If kept, SETTING is wrong — it is an internal backend optimization, no user meaning |
- **Hacks / workarounds**: none beyond being a second copy of the producer
  merge predicate (the header itself argues its own equivalence). Correct:
  one merge, one place.
- **Risks**: deleting is behavior-neutral by default-OFF; prove with a
  gs_corpus replay run confirming byte-identity and no perf cliff on the
  slowspot capture.

### host_app/gs_crtc_class.h (112 lines)

- **Purpose**: [GB3] CRTC merge classification for native scanout: `Classify()`
  reads PMODE/DISPFB1/2/DISPLAY1/2 from `g_gs_regs`, detects the dual-circuit
  1-px-DY flicker-filter shape (`kDualIdentical1px`), packs {valid, class,
  FBP, FBW, PSM} into ONE relaxed atomic u32 the GFX thread loads at present;
  `IsNativeConfig` additionally pins the census-observed scanout tuple
  (FBP bytes 0x70000, 512 px, PSMCT32).
- **Public surface**: `gscrtc::{Classify,Load,IsNativeConfig,Pack,ClassOf,
  FbpBytes,FbwPx,Psm,kNative*}`; called from gs.cpp privileged writes,
  savestate restore, GS_Reset; read by graphics_thread present.
- **Classification**: MIXED — dual-circuit classification is GENERIC CRTC
  semantics; the `kNativeFbpBytes=0x70000/512/CT32` pin is game census.
- **Refactor destination** (MIGRATION: "gs_crtc_class.h | gs/hle/present/ —
  verbatim"): `engine/gs/hle/present/crtc_classifier.{h,cpp}` (class
  `CrtcClassifier` — snapshot mechanics + kDualIdentical1px promotion rule,
  which is the paraLLEl-GS-documented generic move). The native-config pin
  becomes either (a) generalized away — promote ANY kDualIdentical1px config
  (the classifier already proves single-source) — or (b) VramLayout data
  (`display` row). Prefer (a) with the loud non-native fallback retained;
  TREE-GAP: none (present/ exists).
- **Flags consumed here**: none directly; every call site is
  `gsresident::On()`-gated (PS2_GS_RESIDENT → NORM ⇒ Classify becomes
  unconditional at privileged-write sites).
- **Hacks / workarounds**:
  1. Census-pinned single config (0x70000/512/CT32) — game data as constexpr.
     Correct: classify generically; the census obligation ("every other config
     is the loud fallback") becomes a triage counter, and the constants move
     to VramLayout if the pin survives.
- **Risks**: FMV vs gameplay PMODE differences (MMOD/ALP) must keep promoting;
  proven by fmv_attract + boot_menu corpus scanouts and the scanout
  native/nonnative counter ratio unchanged.

### host_app/foglane_live.h (85 lines)

- **Purpose**: [S107-FOGLANE-LIVE] gate + counters for the live-attachment
  rescue serve: a PSMT8H composite read that the CLASS IDX chain declined is
  rescued into the fmt=5 pair serve when both targets are provably
  GPU-resident and lane-authored THIS frame (lane-fresh predicate). Pure
  header: `FoglaneLiveOn` (default ON), `S107LogOn`, three atomic counters,
  test override. The actual serve logic lives in opengl_backend.
- **Public surface**: `s107::{FoglaneLiveOn,S107LogOn,n_idx_hit16lane,
  n_lane_author,n_rescue_declined,FoglaneLiveOverrideForTest}`; consumed by
  opengl_backend.cpp.
- **Classification**: MIXED (A6 §3.2 supporting row: "generic live-attachment
  serve for PSMT8H composites, motivated by the game's fog lane" — trigger
  data 0x280000/0x2F0000 lives at the consumer).
- **Refactor destination** (MIGRATION §1.6: "foglane_live.h (row B) | generic
  live-attachment serve → engine/gs/hle/texsrc/; fog-lane trigger data →
  profile vram"): the gate/counters dissolve into
  `engine/gs/hle/texsrc/texture_source.{h,cpp}` — the lane-fresh predicate is
  the `lane_author_seq/surface_author_seq` coherence fields of RenderTarget
  (ARCH §4.4) consumed by `ResolveRead`'s fmt=5 case; this header does not
  survive as a file. Counters → texsrc serve-census (DIAG build).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_GL_FOGLANE_LIVE (:64) | ON-unless-0 | lane-live rescue vs legacy mirror decode | delete-losing-arm | NORM (explicitly named in the shard instructions) — rescue becomes the ResolveRead rule; legacy stale-mirror decode arm deletes (it was the BUG-016 mechanism) |
  | PS2_S107_LOG (:72) | OFF | per-rescue proof log | delete-both | DELETE |
- **Hacks / workarounds**: none in-file (the header is deliberately pure);
  the class-predicated-not-address-keyed design note is the correct rule and
  carries into ResolveRead documentation.
- **Risks**: cavern fog composite scenes (cavern_fog/gas captures, BUG-016);
  proven by gs_replay byte-identity on those captures with the serve counters
  (hit16lane>0, rescue_declined accounted).

### host_app/texture_cache.h (52 lines)

- **Purpose**: A small GPU-texture-id LRU cache keyed on
  {tbp,tbw,psm,tw,th,cbp,cpsm} with backend invalidation callbacks
  (`TextureCache`: Lookup/Insert/InvalidateAll/InvalidateRegion/Clear,
  MAX_ENTRIES=128). An early-era texture cache.
- **Public surface**: `TextureCache`, `TextureCacheKey(+Hash)`. **Effectively
  dead**: grep shows NO TU includes texture_cache.h except its own
  texture_cache.cpp; it is compiled into the target (CMakeLists :271-272) but
  nothing constructs it — the live caches are r118/r120 in texture_decoder.h/
  render.h/opengl_backend (A1 §7 concept 3 names texcache_key.h as the key
  home).
- **Classification**: GENERIC-PS2 (would-be texsrc cache); DEAD CODE in
  practice (evidence: zero external includers; live path uses texcache_key.h).
- **Refactor destination**: DELETE (dead-code purge, Phase 1). The MIGRATION
  row "texture_cache.{h,cpp} key logic → gs/hle/texsrc/content_key.*" is
  satisfied by texcache_key.h's logic (another shard); nothing from this pair
  is load-bearing. Flag to consolidator: MIGRATION's row conflates the two —
  reverse-map content_key.* to texcache_key.h, not this file.
- **Flags consumed here**: none.
- **Hacks / workarounds**: none (it is simply superseded).
- **Risks**: none — removing from CMake + delete; build proves it.

### host_app/ee_pace.h (367 lines)

- **Purpose**: [R170-PACE] depth-responsive EE pacing primitives: the corrected
  runahead bound (declared EE vsync pushes vs gfx-processed EE frames, park
  while `my_ee > processed + Lag()`), env latches (Lag/SafetyCap/MaxJobs/
  MaxBytes/PaceOn with the MTVU+DEFER prerequisite demotion), park/depth/
  vspush/vspop histograms, release-cause + safety-trip counters, the queued-
  payload-bytes ledger, and the 1 s census emitter. Header-only, dependency-
  free by design (unit-tested headless).
- **Public surface**: `ee_pace::` — PaceOn, Lag, SafetyCap, MaxJobs, MaxBytes,
  ShouldPark(At), OnEEFrameProcessed, NoteBoundaryDepthSample, NoteParkExit,
  Note*Depth, PaceEmitDue/PaceEmit, g_ee_frames_processed,
  g_queued_payload_bytes, test overrides. Consumed by gs_hle (boundary park),
  render.h (queue ledger + guards), graphics_thread (processed bump), sif_hle.
- **Classification**: GENERIC-PS2 (host pacing of the EE↔GFX pipeline).
  Subsystem: EE timing / render queue.
- **Refactor destination** (MIGRATION: "ee_timers/ps2_vblank/quantum/turbo/
  warp/ee_pace/present_pace/budget_* | ee/timing/* + gs/hle/present/pacing.*"):
  SPLIT — the park predicate + lead governor become fields/methods of
  `engine/ee/timing/time_domain.{h,cpp}` (EmuDomain `lead_governor
  {max_ahead}`, ARCH §4.7) invoked from `boundary_protocol.{h,cpp}`; the
  queue ledger + safety caps move to `engine/gs/hle/queue/render_queue.{h,cpp}`
  (`RenderQueue` bounds); the census histograms → core/diag (DIAG builds).
  This header does not survive; zero mutable header globals per §11.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_EE_PACE (:169) | OFF (requires MTVU+DEFER; demotes with warning) | corrected boundary park vs legacy MAX_FRAME_LAG bound | delete-losing-arm | NORM — the corrected declared/processed bound becomes THE boundary park (the legacy g_vsync_frame bound was proven dead, F2); the prerequisite cascade dies with MTVU→NORM |
  | PS2_EE_RUNAHEAD (:118) | 2, clamp [1,4] | runahead depth | delete-losing-arm per A2; ARCH §4.7 keeps it | SETTING → Config `engine.ee_runahead` (default 2) — ARCH names it a surviving knob |
  | PS2_RQ_SAFETY (:131) | 8 (floor Lag+3) | queue safety cap | delete-losing-arm | NORM (constant of RenderQueue; floor rule kept as code) |
  | PS2_RQ_MAXJOBS (:144) | 65536 | job-count guard | delete-losing-arm | NORM (constant) |
  | PS2_RQ_MAXMB (:156) | 256 MB | payload-bytes guard | delete-losing-arm | NORM (constant; Vita target may lower it via PlatformCaps, not env) |
  | (reads PS2_MTVU / PS2_MTVU_DEFER inside PaceOn) | — | prerequisite check | mirrors of vu1_async latches | NORM — mirrors die with the env-inheritance cascade (A2 finding 3) |
  | (reads PS2_PERF for announce) | — | gate self-report | keep-as-setting | DIAG (one startup config dump replaces 21 announce latches) |
- **Hacks / workarounds**:
  1. Env-level mirror of vu1_async::DeferOn's latch inputs (:151–160,
     self-documented "without linking vu1_async") — the mirror-drift idiom
     accepted for testability. Correct: Config dependency validation
     (`mtvu_depth needs mtvu`), no re-parsing.
  2. Mutable inline atomics in a header (hist arrays, ledger) — banned shape;
     correct: class members.
  3. Test overrides that bypass the SafetyCap floor (documented) — keep as
     ctor injection in RenderQueue tests, not global atomics.
- **Risks**: pacing regressions are input-lag/stutter class (BUG-027);
  proven by test_ee_pace.cpp (kept, retargeted), depth-histogram invariant
  (depth ≤ LAG+1) in the deterministic profile, and the R139-THREADS wait
  decomposition staying explained (residual tripwire).

### host_app/quantum.h (171 lines)

- **Purpose**: [P1-QUANTUM] Mode Q: decouples EE loop admission from the
  present grid (boundary parks on gfx-depth + the 60 Hz vblank tick — the
  tick wait IS the quantum, 60/30 slip like retail) and turns PS2_FPS_CAP
  into present-side decimation (`PresentDecimator`: absolute-grid skip-forward
  subsample, never debt; FMV bypass parity). Includes the 5 s census.
- **Public surface**: `quantum::{On,DecimationOn,CapPeriodNs,PresentDecimator,
  G,NoteDecim}`; consumed by present_pace.h/graphics_thread (present path)
  and by the vblank/boundary wait path.
- **Classification**: GENERIC-PS2 (retail frame-quantization semantics).
  Subsystem: EE timing + present policy.
- **Refactor destination**: SPLIT — the loop-admission rule is absorbed by
  `engine/ee/timing/boundary_protocol.{h,cpp}` (tick wait = quantum; ARCH
  §4.7 names QUANTUM as "becomes the one shipped mode"); `PresentDecimator`
  (pure, unit-pinned) moves verbatim to `engine/gs/hle/present/
  present_decimator.{h,cpp}` (class `PresentDecimator`) consumed by
  PresentPolicy. Census → DIAG.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_QUANTUM (:73) | ON-unless-0 | Mode Q vs legacy present-grid admission | delete-losing-arm | NORM (named in the shard instructions) — Mode Q is the only loop shape; the consume-path pacer sleep arm deletes |
  | (PS2_FPS_CAP via presentpace::FpsCapEnv) | 0/unset; peak runs 60 | decimation grid | keep-as-setting | SETTING → Config `video.fps_cap` default 60 (peak config) |
  | (PS2_PACE_FMV_BYPASS via Pacer::FmvBypassAllowed) | OFF | FMV decimation bypass | delete-losing-arm | NORM (authored-30fps bypass rule becomes PresentPolicy code) |
  | (PS2_PERF announce) | — | census | keep-as-setting | DIAG |
- **Hacks / workarounds**:
  1. `PS2_PACE_SMOOTH`/`PS2_CAP_STEPDOWN` declared "inert under Mode Q" but
     still latched in present_pace.h — cross-file dead-gate coupling. Correct:
     both delete when quantum is NORM (they are present_pace's rows; noted for
     that shard).
- **Risks**: audio-desync class (BUG-002) and the 35 Hz cadence bug the
  header documents; proven by test_present_pace.cpp (decimator pins kept),
  loop_hist 60/30 bimodality on the anchor set, audio-hash leg.

### host_app/memory.h (284 lines)

- **Purpose**: The EE memory API: extern declarations for main RAM /
  meta-arena / VU memories / gs_vram vectors + `g_vu1_micro_extgen`;
  `QuadWord`; and the [R130-FASTMEM] inline fast path — read/write/read_quad/
  write_quad short-circuit plain-main-RAM accesses to memcpy (guard derivation
  documented against the slow path), with `*_slow` fallbacks in memory.cpp,
  the every-256th-write fallthrough that keeps the [KEY-REDIRECT] scavenger
  ticking, the PS2_MEMWATCH probe-gauntlet gate, watchpoint force-slow
  counter, and the [KEY-OVERLAP] bulk-write check hook + g_watched_functions
  / g_current_pc diagnostics.
- **Public surface**: `memory::{read,write,read_quad,write_quad,get_pointer,
  translate_address,initialize,fastmem_*,memwatch_*,key_overlap_check,
  g_key_buf_addr}`; the memory vectors; included by nearly every TU.
- **Classification**: GENERIC-PS2 mechanism with one GAME-coupled hook (the
  key-overlap/key-redirect family targets the game's level-key buffer).
  Subsystem: EE memory.
- **Refactor destination** (MIGRATION: "memory.h … | ee/mem/ — verbatim"):
  `engine/ee/mem/` — `memory.{h,cpp}` façade + `fastmem.{h,cpp}` (class
  `Fastmem`: the guarded fast path, no env), `mmio_router` (memory.cpp's
  concern). [KEY-REDIRECT]/[KEY-OVERLAP] → named workaround module quarantined
  beside ee/mem (HOOK §3.8), deleted at the allocator-corruption root fix.
  `g_current_pc` — steering says it never updates — DELETE. `QuadWord` stays
  beside CpuContext (frozen family). meta_arena vector dies with the 0x181560
  workaround (HOOK §3.1).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_FASTMEM (:146) | ON-unless-0 | inline main-RAM fast path vs full probe gauntlet | delete-losing-arm | NORM — fastmem is the only access path; the probe gauntlet ceases to exist as a "path" (probes become PS2R_DIAGNOSTICS GenProbes), so the =0 diagnostic mode is replaced by the diagnostics build |
  | PS2_MEMWATCH (:169) | OFF-unless-1 | restore watch/trace probe blocks verbatim | move-to-diagnostics-build | DIAG (probe mechanism in core/diag; the 3,300-line gauntlet itself deletes with memory.cpp's split) |
- **Hacks / workarounds**:
  1. The every-256th-fast-write fallthrough to write_slow (:203–215) exists
     ONLY to keep the [KEY-REDIRECT] pointer-repair sampling — a cadence
     coupling between the fast path and a workaround. Correct: delete with
     KEY-REDIRECT's root fix; fastmem becomes unconditional.
  2. [KEY-REDIRECT]/[KEY-OVERLAP] (level-key buffer 0xbf2c00 default,
     "Levels\Earth\Hub\Beach") — EMULATION-BUG-WORKAROUND (HOOK §3.8);
     quarantine then delete at the boot-double-init/loader-delivery root fix.
  3. Latch-on-first-use atomics (`g_fastmem_mode` -1/0/1) — env read inside a
     hot-path helper. Correct: Config load before threads.
  4. `g_current_pc` [IO-TRACE] — non-functional per steering; delete.
  5. `g_watched_functions` unordered_set at header scope — DIAG registry.
- **Risks**: the fast/slow guard equivalence is the recompiler's data plane;
  any change proven by tests/recomp_semantics mem shims + full boot + the
  KEY500 watch scenario until the root fix lands (then the retail-faithful
  key stability test pins it).

### host_app/iop_memory.h (57 lines)

- **Purpose**: The IOP (R3000A) 2 MB RAM: inline `iop_ram[2MB]`, templated
  read/write with masking + out-of-range printf, `get_pointer`.
- **Public surface**: `iop_memory::{read,write,get_pointer,IOP_RAM_SIZE}`;
  used by iop_stubs, sif_rpc_handlers, iop_recompiled code, savestate (IOPM).
- **Classification**: GENERIC-PS2. Subsystem: IOP.
- **Refactor destination** (MIGRATION: "iop_context.h, iop_memory.h … |
  iop/ — verbatim"): `engine/iop/iop_memory.{h,cpp}` — class `IopMemory`
  (the 2 MB buffer moves out of the header: `inline uint8_t iop_ram[2MB]` is
  a 2 MB mutable header global, the exact §11 banned shape; storage lands in
  one TU, accessors stay inline).
- **Flags consumed here**: none.
- **Hacks / workarounds**: out-of-range accesses printf-and-zero silently —
  correct: count via triage (IOP fallback class), keep semantics.
- **Risks**: none beyond include-graph churn; IOPM savestate section
  round-trip proves it.

### host_app/sif.h (31 lines)

- **Purpose**: `SifState` POD: mailbox regs (MSCOM/SMCOM/MSFLAG/SMFLAG/CTL),
  the three sys sub/main/rpcinit words, a pending-RPC list (client addr +
  completion sema), last DMA id; `extern g_sif` + `SifCompletePendingRpcs()`.
- **Public surface**: `SifState`, `g_sif` (defined in syscalls.cpp),
  `SifCompletePendingRpcs` (sif.cpp); included by syscalls, sif.cpp, sif_hle,
  dmac, savestate, memory, main.
- **Classification**: GENERIC-PS2. Subsystem: SIF.
- **Refactor destination** (MIGRATION: "sif.{cpp,h} + sif_hle.{cpp,h} | sif/
  (mailbox regs) + sif/hle/ — split"): `engine/sif/sif_state.{h,cpp}` — class
  `SifState` (g_sif definition moves here from syscalls.cpp, ending the
  cross-TU global ownership oddity); pending-RPC completion belongs to
  `sif/hle/rpc_router`.
- **Flags consumed here**: none.
- **Hacks / workarounds**: none.
- **Risks**: none; savestate SIF section + RPC boot flow prove it.

### host_app/cdvd_stream.cpp (425 lines)

- **Purpose**: CDVD streaming HLE (sceCdSt* family) over direct ISO reads:
  Init/Start/Seek/Stop/Read/Stat/Pause/Resume + DiskReady(2); the shared
  stream cursor; `cdvd_stream::PumpSectors` (FMV read-ahead feeding mpeg_hle
  from the SAME cursor); cached ISO size ([R128-EEFIX]); [E2-SAVE] cursor
  serialize/restore/validate.
- **Public surface**: `hle_sceCdSt*`, `hle_sceCdDiskReady(2)` (SdkBinding
  handlers), `cdvd_stream::{PumpSectors,SerializeState,RestoreState,
  ValidateState}`; consumed by mpeg_hle, sce bindings, savestate.
- **Classification**: GENERIC-PS2 (libcdvd streaming semantics; scope-cut:
  ring buffer modeled as always-full). Subsystem: CDVD.
- **Refactor destination** (MIGRATION: "cdvd_stream.{cpp,h} | cdvd/ streaming
  reader over ISO — verbatim"): `engine/cdvd/stream_reader.{h,cpp}` — class
  `CdvdStreamReader` (state struct + cursor + PumpSectors); the `hle_*`
  wrappers register as SdkBinding rows in `engine/cdvd/hle/`. SectionSpec
  provider declared beside it (core/savestate registry).
- **Flags consumed here**: none (perf scopes are PS2_PERF-latched elsewhere).
- **Hacks / workarounds**:
  1. `hle_sceCdStStat` always reports a full ring ("data always available";
     `available=16` fallback) (:333–343) — collapses the ring-buffer timing
     surface. Today load-bearing and correct-for-this-game; document as a
     `// diverges:` libcdvd note in cdvd/hle; the HOOK §3.6 completion-
     visibility model is the future owner if a title needs honest fill levels.
  2. [CDVD-STREAM-WATCH] overlap probe against DMA tag 0x450180 (:238–243) —
     game-address diagnostic; DELETE.
  3. `memory::key_overlap_check` call (:245) — part of the KEY workaround
     family; deletes with HOOK §3.8.
  4. Byte-by-byte fallback write loop when get_pointer fails (:301–320) —
     unreachable-in-practice defensive arm; fold into ee/mem bulk write.
- **Risks**: menu-music cursor continuity across savestates (the header's own
  E2-SAVE rationale); proven by save/load during menu music + FMV playback
  anchors; PumpSectors/GetPicture pacing by the FMV corpus leg.

### host_app/anim_pose_hle.cpp (308 lines)

- **Purpose**: [R129-FMVHLE] native mirror of the hot skeletal-anim pose
  subtree: 9 statement-for-statement copies of recompiled functions
  (FUN_00293ec0/294300/294480/294b18/297ba8/297b40/297d88/297e68/297cc0, in
  anim_pose_hle_bodies.inc) with two mechanical swaps — fm_* direct-memory
  layer (main RAM + scratchpad fast path, slow fallback) and direct intra-set
  C calls. Installed over the recompiled bodies via `update_function_impl`;
  `anim_pose_fm_selftest` validates fm_* routing.
- **Public surface**: `install_anim_pose_natives()` (main.cpp),
  `anim_pose_fm_selftest()` (tests/anim_pose_diff).
- **Classification**: GAME-SPECIFIC (9 game-function mirrors; A6 §4 row
  anim_pose_hle_bodies 2,027 ln GAME).
- **Refactor destination** (MIGRATION §1.1 + HOOK §3.2, binding): **RETIRED in
  Phase 2** — emission-quality (performance) workaround whose root fixes are
  ARCH §5.2 flat dispatch + fastmem emission; each of the 9 addresses (8
  beyond the manual-235; 0x294480 overlaps) is harness-adjudicated
  v2-emitted-body vs mirror, then DELETED. Nothing moves to games/ — a
  workaround never becomes title code (HOOK §6). The fm_* layer itself is
  superseded by memory.h fastmem (already NORM).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_FMV_NATIVE (:200) | ON-unless-0 | native mirrors vs recompiled bodies | delete-losing-arm per A2 | DELETE (whole file retires; neither arm survives — the v2 emitted body + flat dispatch is the single path). A2's "keep winner" is superseded by the HOOK §3.2 retirement gate |
- **Hacks / workarounds**: the file IS the workaround (per-address hand
  mirrors of recompiled code + a private memory fast path duplicating
  fastmem). Correct implementation: v2 emission + flat dispatch + fastmem;
  proof = tests/anim_pose_diff run as the adjudication harness (v2 body vs
  mirror, full-32MB + CpuContext compare), then delete file + .inc + installer.
- **Risks**: FMV-window frame spikes regress if v2 dispatch is still slow at
  retirement time — the Phase-2 gate order (emission changes FIRST) covers
  this; perf proven by the FMV spike bench leg (frames ~1200–1360).

### host_app/savestate_writer.cpp (185 lines)

- **Purpose**: [W16R1] async capture writer: one dedicated thread servicing a
  one-slot state-write job (tmp+rename atomic write off the EE boundary, RSS
  bound by freeing the ~90 MB payload before clearing busy) and a one-slot
  PPM copy job; TryEnqueue/Flush/JoinForExit/Busy API. Pure module (format +
  std threads only), unit-tested.
- **Public surface**: `savestate::{CaptureWriterTryEnqueueState,
  CaptureWriterEnqueuePpmCopy,CaptureWriterFlush,CaptureWriterJoinForExit,
  CaptureWriterBusy}`; used by savestate.cpp (slowspot/named captures) and
  slowspot machinery.
- **Classification**: GENERIC-PS2 (host service). Subsystem: savestate/triage.
- **Refactor destination** (MIGRATION: "savestate_writer.{cpp,h} |
  core/savestate/writer.* — verbatim"; TRIAGE_CAPTURE adds the consumer):
  `engine/core/savestate/capture_writer.{h,cpp}` — class `CaptureWriter`
  (thread + slots as members, not file statics). It also becomes the
  throttled background writer core/triage rides (TRIAGE §"one throttled
  background writer thread").
- **Flags consumed here**: none in the .cpp (its header savestate_writer.h:55
  latches PS2_SLOWSPOT_ASYNC, default ON — that row: delete-losing-arm →
  NORM, async writer is the only capture path; sync arm deletes; noted for
  the header's shard).
- **Hacks / workarounds**:
  1. Hardcoded `"save_states"` dir in ServicePpmCopy (:118) — path policy in
     a pure module. Correct: destination passed by caller (triage bundle dir).
  2. `CaptureWriterTestHoldMs()` test hook via header — fine; becomes ctor
     injection.
- **Risks**: shutdown join ordering (A4 exit-with-capture rule); proven by
  test_savestate_writer.cpp (kept) + slowspot capture soak.

### host_app/perf_profiler.h (610 lines)

- **Purpose**: [R116-PERF] env-gated wall-time profiler: 31 subsystem buckets
  (ee…p2copy) with RAII `Scope`/`PERF_SCOPE`, 1 s `[PERF]` emitter +
  exit summary, the [R127-BENCH] monotonic `TakeSnapshot` API the bench
  harness diffs, the pause-aware `EEGameSlice`/`EEGamePause` per-thread slot
  system with incremental flushing + the `[R139-THREADS]` census (busy/wait
  split, rqbp/pacepark/gfxidle buckets), and the [R128-EEIO] streaming-pump
  census (`IoScope`, 8 buckets).
- **Public surface**: `perf::` — Enabled, Scope, FrameTick, ExitSummary,
  TakeSnapshot, EEGameSlice/EEGamePause, EeThreadsMaybeEmit, RqBp*/PacePark*/
  GfxPopWait* accessors, IoScope/IoCensus, `PERF_SCOPE`. Consumed by ~30 TUs
  (cdvd_stream, vif, gif_arbiter, ee_pace, mpeg_hle, render.h, …).
- **Classification**: GENERIC-PS2 diagnostics. Subsystem: core/diag.
- **Refactor destination** (MIGRATION: "perf_profiler.h, bench.h,
  bench_report.h | core/diag/perf.* — [REV3 D9] totals-snapshot API
  additionally feeds core/triage bundle files"): SPLIT —
  `engine/core/diag/perf_profiler.{h,cpp}` (class `PerfProfiler`: buckets,
  scopes, emitters — PS2R_DIAGNOSTICS builds) and the totals-snapshot feed to
  `engine/core/triage/perf_monitor.{h,cpp}` (`PerfMonitor` — compiled into
  ALL builds per TRIAGE §3/§12; the perf-buckets.tsv bundle writer consumes
  TakeSnapshot). IoCensus rides perf_profiler; the EEGameSlice pause hooks
  remain macros/classes whose bodies compile out of Release.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_PERF (:Enabled) | OFF (num-if-set); THE 21-site mirror var | all timing/census on | keep-as-setting per A2 | DIAG — becomes the PS2R_DIAGNOSTICS build (compile-time); the announce-idiom mirrors all die. The always-on share (what triage needs: FrameTick counters + TotalsSnapshot for PerfMonitor) is unconditional in all builds per TRIAGE §12 |
- **Hacks / workarounds**:
  1. One header = profiler + thread census + IO census + bench feed —
     four responsibilities (D4 violation). Correct: split per destination.
  2. `Enabled()` re-latched per TU via inline function — the PERF mirror
     family (A2 finding 2). Correct: compile-time category.
  3. `buf[1024]` line-size coupling to "the bench stderr shim's 1023 cap" —
     cross-tool contract in a comment. Correct: structured emit (tsv), no
     line-reassembly contract.
- **Risks**: bench pipeline continuity during migration (A5 anchors);
  proven by running the bench harness old-vs-new on one anchor and diffing
  report keys (bench_report contract below).

### host_app/bench_report.h (781 lines)

- **Purpose**: [R127-BENCH] pure report rendering: data structs (MtvuSites
  parse vocabulary, the ~90-counter GL `Census` + `CensusDelta`, Gates,
  WorstFrame, Derived stats), snprintf-only stable-key-order JSON renderer
  (`RenderReport`/`RenderSummaryLine`), and stderr-line accumulators
  (`AccumulateMtvuLine`, `ParseGfxresDrawsLine`) that scrape the per-second
  census lines out of the frozen TUs' stderr.
- **Public surface**: `bench::` structs + renderers; consumed by bench.h
  (runtime glue, another shard) and tests/test_bench_report.cpp.
- **Classification**: GENERIC-PS2 diagnostics (report layer). core/diag.
- **Refactor destination** (MIGRATION row shared with perf_profiler):
  `engine/core/diag/bench_report.{h,cpp}` (class `BenchReport`) —
  PS2R_DIAGNOSTICS builds; signal-safe snprintf contract kept (SIGINT write).
  The stderr-scraping accumulators DELETE once counters are readable directly
  (the "frozen TU" constraint dies with the refactor — vu1_async counters
  become queryable), which is the honest fix for the parse-vocabulary
  coupling. perf-buckets tsv for triage comes from PerfMonitor, not here.
- **Flags consumed here**: none (pure).
- **Hacks / workarounds**:
  1. Scraping its own process's stderr census lines because counters live in
     anonymous namespaces of frozen TUs (:30–44 self-documented) — IPC with
     yourself via log lines. Correct: exported counter structs.
  2. The GL Census struct mirrors ~90 loose counters by name — schema
     duplication with opengl_backend; correct: one census registry
     (core/diag) that both report and backend reference.
- **Risks**: the optimization-loop diffing contract (stable key order);
  proven by test_bench_report byte-for-byte fixture.

### host_app/bug_report.h (459 lines)

- **Purpose**: [U5-BUGREPORT] pure bundling logic: UTC stamp/naming +
  collision disambiguation, deterministic manifest.txt generation, tail-N
  log extraction, PERF-window line filter (±600 frames), retention selection
  (keep newest 20), slowspot cap (1/run) + pending-probe tick verdicts.
  Runtime glue lives in bug_report.cpp (another shard).
- **Public surface**: `bugreport::` — GenerateManifest, TailLastLines,
  FilterPerfLines, BundleDirBaseName/disambiguation, RetentionSelectDeletions,
  SlowspotCapAllows, SlowspotTick, Os/ArchName, UTC formatters. Consumed by
  bug_report.cpp, gui/bug_report_section, tests/test_bug_report.cpp.
- **Classification**: GENERIC-PS2 (host capture service). Subsystem: triage.
- **Refactor destination** (MIGRATION [REV3 D9]: "bug_report.{h,cpp} |
  core/triage/{triage_bundler,bundle_manifest}.*"): SPLIT —
  `engine/core/triage/triage_bundler.{h,cpp}` (class `TriageBundler`:
  naming/retention/cap logic; crash+manual become trigger kinds) and
  `engine/core/triage/bundle_manifest.{h,cpp}` (class `BundleManifest`:
  manifest.txt format verbatim + the new AI-contract MANIFEST.md per TRIAGE
  §6). Bundle root moves bug_reports/ → games/<title>/triage/. Compiled into
  ALL builds (product capture, not diag — ARCH §9 amendment).
- **Flags consumed here**: none in the header (bug_report.cpp latches
  PS2_BUGREPORT / PS2_BUGREPORT_SLOWSPOT(_MAX) — that shard; outcome per
  TRIAGE §3: replaced by Config `triage.*` fields, SETTING).
- **Hacks / workarounds**: none — this is the cleanest file in the shard
  (deliberately pure, tested). The `manifest.txt`-is-inventory-only gap (M8)
  is filled by BundleManifest's MANIFEST.md, per TRIAGE.
- **Risks**: retention/cap semantics guard disk budgets; test_bug_report.cpp
  retargets verbatim (MIGRATION says exactly this).

### host_app/ring_log.h (232 lines)

- **Purpose**: [E2-LOG] always-on per-thread lock-free binary event rings:
  24-byte POD events, SPSC ring per thread (overwrite-oldest), channel masks
  latched at static init (PS2_LOG_CHANNELS/VERBOSE/RING_EVENTS), Emit/EmitV
  hot path (one mask test + plain stores), signal-safe dumps (crash/hotkey/
  bundle/exit), text decode, `WriteBundle(To)` evidence-dir primitive, MEM
  gauge census (15 gauges + 1 Hz sampler), and the C-linkage crashring
  boundary the GENERATED code calls (crashring_enabled/record_indirect/
  fatal_no_jump).
- **Public surface**: `ringlog::` everything + the extern-C trio + g_rings;
  consumed by main.cpp (hotkeys/exit), slowspot, bug_report, graphics_thread,
  generated recompiled.cpp (frozen signatures).
- **Classification**: GENERIC-PS2 (logging infrastructure). core/log.
- **Refactor destination** (MIGRATION: "ring_log.{h,cpp} … | core/log/ring.*
  sink + decode tool — verbatim"): `engine/core/log/ring_sink.{h,cpp}` (class
  `RingSink`), `ring_decode` stays a tools/ binary; the MEM gauges →
  `engine/core/diag/mem_gauges.{h,cpp}`; the crash/bundle dump arms feed
  `core/triage` (crash bundles land in triage/crash per TRIAGE §7). The
  extern-C crashring boundary is a FROZEN generated-code contract — keep
  signatures verbatim (regeneration not required), body routed to RingSink;
  under ARCH §5.2 the generator eventually stops emitting diagnostics into
  generated code, at which point the boundary migrates to the GenProbe
  channel (Phase-2 recompiler work, not this move).
- **Flags consumed here** (latches live in ring_log.cpp but are this API's):

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_LOG_CHANNELS (ring_log.cpp:98) | "all" | channel mask | move-to-diagnostics-build | SETTING → Config `dev.log_categories[]` (ARCH §4.8 names this field) |
  | PS2_LOG_VERBOSE (:101) | "none" | verbose tier mask | move-to-diagnostics-build | SETTING (same field family, verbose level) |
  | PS2_LOG_RING_EVENTS (:109) | 65536 | per-thread capacity | move-to-diagnostics-build | NORM (constant; triage ring sizing comes from triage.* Config) |
  | PS2_CRASHRING (:104) | legacy alias | clears EE channel | move-to-diagnostics-build | DELETE (alias) |
- **Hacks / workarounds**:
  1. `extern std::atomic<uint32_t> g_vsync_frame;` spliced between namespace
     blocks to bind the global (:96) — include-order surgery. Correct: a
     frame-stamp provider interface.
  2. thread_local `t_ring` + fixed 32-slot registry with slots-exhausted
     counter — fine; becomes RingSink members.
  3. The [R187] absorption notes (g_last_ctx/g_boot_obj boot-object address
     published by a main.cpp hook) — game-coupled crash context; the
     boot-obj probe is a DIAG hook that dies; last-ctx stays generic.
- **Risks**: crash-path signal safety (open/write/fsync only) must survive
  the move; proven by PS2_RING_TEST_CRASH/HOTKEY/BUNDLE test hooks
  (TEST-ONLY, kept in the harness) and a kill -SEGV soak.

### host_app/call_census.h (99 lines)

- **Purpose**: [R141-CALLCENSUS] per-function call census over the generated
  dense-id dispatch: `call_census_enter/exit` bracket every recomp dispatch
  when `g_call_census_enabled` (env-latched plain bool the GENERATED code
  re-declares), fixed atomic counter array by dense id, depth-0 root-time
  attribution, [R142] root-filter direct-child mode, 2 s top-40 dumps +
  final dump; test hooks.
- **Public surface**: the extern-C-shaped global trio (g_call_census_enabled,
  call_census_enter/exit — declared identically in generated recompiled.h)
  + `call_census::{EmitIfDue,EmitFinal,CountFor,…}`; consumed by generated
  dispatch, main.cpp maintenance loop, tests/r141_call_census.
- **Classification**: GENERIC-PS2 diagnostics (dispatch-level). core/diag.
- **Refactor destination** (MIGRATION: "call_census.{cpp,h}, mid_census.h |
  core/diag census family"): `engine/core/diag/call_census.{h,cpp}` (class
  `CallCensus`), PS2R_DIAGNOSTICS builds. The generated-code coupling (three
  re-declared symbols) is exactly the ARCH §4.9 GenProbe channel — in the
  target, the enter/exit calls are emitted only in diagnostics builds via the
  declared probe manifest; Release generator emits nothing (deletes the
  one-bool-load tax).
- **Flags consumed here** (latched in call_census.cpp, this API's gates):

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_CALL_CENSUS (call_census.cpp:232) | OFF | census on | move-to-diagnostics-build | DIAG (GenProbe manifest entry) |
  | PS2_CALL_CENSUS_ROOT (:235) | unset | root-filter EE address | move-to-diagnostics-build | DIAG (probe argument — guest-address keys allowed only in the probe manifest) |
  | PS2_CALL_CENSUS_SLOWSEG (:242) | unset | slow-segment capture | move-to-diagnostics-build | DIAG |
- **Hacks / workarounds**:
  1. Symbol re-declaration pact with generated code ("the generated header
     cannot include this file") — mirrored declarations that must never
     drift. Correct: GenProbe emission owns the declaration, one source.
  2. Root-time single-root caveat (tid=1 nests its whole life) — documented
     measurement limitation; carries into the class doc.
- **Risks**: none at Release (compiles out); DIAG-build parity proven by
  tests/r141_call_census retargeted.

### host_app/debug_budget.h (69 lines)

- **Purpose**: `DebugBudget(tag, per_frame_limit)` — shared per-tag per-frame
  log budgeting with lazy "suppressed N lines" summaries; mutex + std::map of
  tag → {frame, count, suppressed}.
- **Public surface**: one inline function; included by gs_state.cpp (and
  intended for any probe TU).
- **Classification**: GENERIC-PS2 diagnostics. core/log.
- **Refactor destination** (MIGRATION: "timeline.h, stick_trace.h,
  debug_budget.h | core/diag/"): ABSORBED — ARCH §9 says budget primitives
  (`first_n`, `every_nth`, `frame_window`) are core/log library calls; this
  header's tag-budget becomes `core/log` rate-limit policy on LOG(cat,…) and
  the file deletes (no separate class needed).
- **Flags consumed here**: none.
- **Hacks / workarounds**: string-keyed map + mutex on a "low-frequency"
  assumption in paths that later became hot (the pattern M4A punished).
  Correct: compile-time category + counter-based budget, no strings/mutex.
- **Risks**: none.

### host_app/loader_prof.h (347 lines)

- **Purpose**: [LDR-PROF] loader-FSM wall-time attribution profiler: a
  hardcoded 12-row table of the game's serial chunk-load FSM functions
  (0x2aac80 requester … 0x191bd0 slice-checker), self/incl attribution via a
  thread_local stack, and a drain-pass census that READS the game's drain
  object rings (obj+0x24 slots, dyn ring array, 9 static rings at
  0x3B6BA0+k*0xC00) before/after FUN_00264948 runs; 2 s + final emits.
  Hooks installed LAST by main.cpp when PS2_LOADER_PROF=1.
- **Public surface**: `loader_prof::{On,Enter,Exit,DrainCensus,DrainResult,
  EmitIfDue,EmitFinal,kTargets}`; consumed by main.cpp dual-hook installers.
- **Classification**: GAME-SPECIFIC diagnostics (12 game EE addresses + the
  drain-ring memory layout).
- **Refactor destination** (MIGRATION: "loader_prof.h, loadspan.h | loader
  probes → core/diag — move-to-diagnostics-build" — but A2 rows say
  delete-both): resolve as DELETE. The MECHANISM it needs (address-keyed
  enter/exit probes + guest-memory census) is the core/diag probe kit +
  GenProbe channel; the 12-address TABLE and ring-layout constants are
  session inputs a future investigation would supply as probe-manifest
  arguments (title-scoped). Nothing of this file's code is worth carrying;
  flag the MIGRATION/A2 disagreement to the consolidator (recommend A2:
  delete-both — the loader investigation concluded; LOADER_HLE landed).
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_LOADER_PROF (:88) | OFF-unless-nonzero | install the 12 FSM hooks | delete-both | DELETE (mechanism survives in core/diag probe kit; this instance dies) |
- **Hacks / workarounds**: the whole file is a scene-specific probe rig
  (game addresses + guest struct layouts in a header). Correct: probe
  manifest + triage `perf` attribution (TRIAGE §10 `triage perf`).
- **Risks**: none (default OFF; delete is inert). 

### host_app/bullseye_trace.cpp (132 lines)

- **Purpose**: Bullseye-scene trace helper: `active()` gates on PS2_BTRACE
  AND the game scene-state word `DAT_00309718 == 3`; capped per-(stage,key)
  log lines + guest hex dumps into g_logFile.
- **Public surface**: `bullseye_trace::{active,log,hex_dump,reset}`; called
  from gs_state.cpp, dmac.cpp, gif.cpp, main.cpp, opengl_backend.
- **Classification**: GAME-SPECIFIC diagnostics (scene-state address
  0x309718, bullseye scene). 
- **Refactor destination** (MIGRATION §1.1: "One-off diagnostics:
  bullseye_trace, cuelag_probe, foglane_live[gate], loop_hist, pacebrk,
  mipsrc_diag, SEQ54-class hooks | concluded sessions (A2 §4) | delete"):
  DELETE (file + all call sites). The bullseye text bug it served is closed;
  future scene traces are triage probes.
- **Flags consumed here**:

  | var | default today | what it selects | disposition (A2) | refactor outcome |
  |---|---|---|---|---|
  | PS2_BTRACE (:59) | OFF (re-arm hatch after [FIX-SLOWSTICK]) | trace on | move-to-diagnostics-build per A2 CSV | DELETE (MIGRATION §1.1 overrides: concluded one-off; the [FIX-SLOWSTICK] history is exactly why it must not ride in hot paths) |
- **Hacks / workarounds**: scene gate reads game RAM per call (memory::read
  of 0x309718 in `active()`); the default-ON era measurably caused the
  "held stick goes slow" bug. Correct: delete; lesson already encoded as the
  cheap-gate-first rule.
- **Risks**: none (default OFF).

### host_app/recomp_test.cpp (24 lines)

- **Purpose**: Phase-1 "hello world" scaffold — `GetPlayerMaxHealth_recomp`
  writes 100 into $v0 on a context. Historical proof-of-concept (README
  Phase 1).
- **Public surface**: one function + recomp_test.h (9 ln).
- **Classification**: DEAD (A1 §5.1: referenced by no CMake target; "dead
  phase-1 test scaffold").
- **Refactor destination** (MIGRATION §1.1: "main_old.cpp, recomp_test.cpp,
  memory_test.cpp | dead scaffolds | delete"): DELETE (+ recomp_test.h).
- **Flags consumed here**: none. **Hacks**: none. **Risks**: none (not built
  — evidence: absent from host_app/CMakeLists.txt and root CMakeLists).

### host_app/test_vblank_bug_condition.cpp (676 lines)

- **Purpose**: Self-contained fork-isolated property test for the VBlank
  starvation deadlock in WaitSema/SleepThread (encodes the FIXED behavior;
  expected to fail on unfixed code). Replicates decision logic rather than
  linking the runtime.
- **Public surface**: standalone main-style test; NOT in any CMake target
  (A1 §5.1 dead-test list, 676 ln; pairs with test_rendering_bug_condition,
  37 duplicated windows).
- **Classification**: DEAD test scaffold (the bug it pinned is fixed and the
  live scheduler tests cover it).
- **Refactor destination**: DELETE with the 23-file dead-test pile (A1 §4.4:
  "deleting the pile removes ~330 duplicate windows in one stroke"). The
  covered invariant (a WaitSema(count==0)/SleepThread(wakeup==0) block must
  not deadlock the vblank pump) is re-expressed as ONE named invariant test
  in engine/ee/hle tests if not already covered by ps2_threading's suite.
- **Flags consumed here**: none. **Hacks**: logic-replication drift risk is
  the reason to delete rather than port. **Risks**: none (not built —
  evidence: A1 §5.1 "referenced by no CMake target").

### host_app/vu_aot_programs_0.cpp, vu_aot_programs_2.cpp, vu_aot_programs_8.cpp, vu_aot_programs_14.cpp, vu_aot_programs_25.cpp, vu_aot_programs_30.cpp (17 lines each)

- **Purpose**: Six of the 32 fixed checked-in VU1-AOT chunk aggregator TUs:
  each is four includes (vu.h, vu_aot_registry.h, vu_decode_slot.h,
  vu_fast_exec.h) + `#include "vu_aot_generated_N.inc"` so vu_aot_gen's
  output compiles with a static CMake source list (R126-SHARD principle).
  Diff-verified identical modulo index (banner text drift only: "N/24" vs
  "N/32").
- **Public surface**: `g_vu_aot_chunk_descs_N` external-linkage desc arrays
  aggregated by vu_aot_programs.cpp at static init. Built only under the
  CMake option PS2_VU_AOT (default OFF per NOAOT).
- **Classification**: GENERIC mechanism / GAME payload (the generated .inc
  is this game's microcode); moot — the tier is deleted.
- **Refactor destination** (MIGRATION §1.1: "VU-AOT tier: vu_aot_gen.cpp, 33
  chunk TUs, vu_aot_registry consumers, 6 ctest gates, 233 MB inc + 351 MB
  lib | default-OFF per NOAOT decision record | delete"; ARCH §5.3 records
  the considered-and-rejected note): DELETE — all six files (and their 26
  siblings, other shards) go with the AOT tier.
- **Flags consumed here**: none in-file. Related rows for the ledger:
  PS2_VU_AOT env (vu_aot_programs.cpp:37, proven-fix-ON per A2) → DELETE
  (tier gone; A2's delete-losing-arm is overridden by the NOAOT decision —
  the "winner" here is the HLE+cached-interpreter ladder, not the AOT arm);
  CMake option PS2_VU_AOT → removed.
- **Hacks / workarounds**: the "include dance" (constexpr + always_inline
  member definitions required in-TU) is the textual-include pattern D4 bans —
  moot on deletion. **Risks**: none; NOAOT already measured no correctness
  win; deletion proven by default build + vu1_replay full corpus.

---

## SHARD SUMMARY

flags(N→…) counts every env var whose latch site is in the file (per-file rows
above; family gates read-but-latched-elsewhere are not counted here).
hacks(N) = numbered hack/workaround items above.

| file | lines | class | destination | shape | flags(N→NORM/SETTING/DIAG/TEST/DELETE) | hacks(N) |
|---|---:|---|---|---|---|---:|
| sif_rpc_handlers.cpp | 6,131 | MIXED | sif/hle + pad/mc/cdvd/sound/iop hle + cdvd/hle/loader + games/…/iop/stream_irx | split | 3→1N/0S/1D/0T/1X | 6 |
| vif.cpp | 3,609 | GENERIC-PS2 | engine/vif/{vif, vif_unpack, vif_fast_span, vif_mtvu_seam} | split | 6→1N/0S/3D/0T/2X | 5 |
| syscalls.cpp | 2,496 | GENERIC-PS2 | ee/hle/{syscalls, dyn_interp, rpc_callback_queue} + sif/hle/sceSif_regs | split | 0 | 5 |
| iop_stubs.cpp | 2,422 | MIXED | iop/hle/{thbase, thsemap, timrman, libsd_stubs, cdvdman_stubs, deferred_events, guest_monitor} + games/…/iop/stream_irx/module_layout | split | 11→4N/0S/2D/0T/5X | 5 |
| gif_arbiter.cpp | 1,808 | MIXED | engine/gif/arbiter | split (experiments deleted) | 6→0N/0S/1D/0T/5X | 5 |
| gs_hle.cpp | 1,113 | MIXED | gs/hle/sce + ee/timing/boundary_protocol + dmac/hle + gs/hle/present + platform | split | 0 (consumes others) | 5 |
| bench_report.h | 781 | GENERIC-PS2 | core/diag/bench_report | moves + scrapers rewritten | 0 | 2 |
| test_vblank_bug_condition.cpp | 676 | DEAD | — | deleted | 0 | 1 |
| perf_profiler.h | 610 | GENERIC-PS2 | core/diag/perf_profiler + core/triage/perf_monitor | split | 1→0N/0S/1D/0T/0X | 3 |
| gs_class_renderer.h | 585 | MIXED | gs/hle/classrdr/{class_renderer, class_registry, class_classifier} + games/…/renderer_profile | split | 13→1N/0S/2D/1T/9X (10 per-class tokens→X, test hooks→T) | 4 |
| gs_page_tracker.h | 517 | MIXED | gs/vram/{vram_image, page_record} (absorbed) | rewritten | 2→2N | 4 |
| bug_report.h | 459 | GENERIC-PS2 | core/triage/{triage_bundler, bundle_manifest} | split | 0 | 0 |
| cdvd_stream.cpp | 425 | GENERIC-PS2 | cdvd/stream_reader + cdvd/hle bindings | moves verbatim | 0 | 4 |
| ee_pace.h | 367 | GENERIC-PS2 | ee/timing/time_domain + gs/hle/queue/render_queue + core/diag | split | 7→4N/1S/1D/0T/1X | 3 |
| loader_prof.h | 347 | GAME-SPECIFIC (diag) | — | deleted | 1→0N/0S/0D/0T/1X | 1 |
| anim_pose_hle.cpp | 308 | GAME-SPECIFIC | — (HOOK §3.2 Phase-2 retirement) | deleted | 1→0N/0S/0D/0T/1X | 1 |
| memory.h | 284 | GENERIC-PS2 (+1 game hook) | ee/mem/{memory, fastmem} + quarantined key workaround | split | 2→1N/0S/1D | 5 |
| ring_log.h | 232 | GENERIC-PS2 | core/log/ring_sink + core/diag/mem_gauges + triage feed | split | 4→1N/2S/0D/0T/1X | 3 |
| gs_deswizzle.h | 196 | GENERIC-PS2 | gs/format/{psm_layout, clut} (collapsed) | rewritten (family merge) | 0 | 3 |
| savestate_writer.cpp | 185 | GENERIC-PS2 | core/savestate/capture_writer | moves verbatim | 0 | 2 |
| quantum.h | 171 | GENERIC-PS2 | ee/timing/boundary_protocol + gs/hle/present/present_decimator | split | 2→1N/1S | 1 |
| bullseye_trace.cpp | 132 | GAME-SPECIFIC (diag) | — | deleted | 1→0N/0S/0D/0T/1X | 1 |
| gs_glbatch.h | 121 | GENERIC-PS2 | — (producer RUNDRAW is the survivor) | deleted | 1→0N/0S/0D/0T/1X | 1 |
| gs_crtc_class.h | 112 | MIXED | gs/hle/present/crtc_classifier | moves + pin generalized | 0 | 1 |
| call_census.h | 99 | GENERIC-PS2 | core/diag/call_census (GenProbe channel) | moves | 3→0N/0S/3D | 2 |
| foglane_live.h | 85 | MIXED | gs/hle/texsrc/texture_source (absorbed) | rewritten | 2→1N/0S/0D/0T/1X | 0 |
| debug_budget.h | 69 | GENERIC-PS2 | core/log rate-limit policy (absorbed) | deleted | 0 | 1 |
| iop_memory.h | 57 | GENERIC-PS2 | iop/iop_memory | moves (storage out of header) | 0 | 1 |
| texture_cache.h | 52 | DEAD | — | deleted | 0 | 0 |
| sif.h | 31 | GENERIC-PS2 | sif/sif_state | moves verbatim | 0 | 0 |
| recomp_test.cpp | 24 | DEAD | — | deleted | 0 | 0 |
| vu_aot_programs_{0,2,8,14,25,30}.cpp | 17×6 | GAME payload aggregators | — (NOAOT) | deleted | 0 (tier env rows die with the tier) | 0 |

**Totals**: 37 files, 24,606 lines. Flag latch sites in this shard: 66 →
NORM 17 / SETTING 4 / DIAG 15 / TEST 1 / DELETE 29.

## TREE-GAPs

None hard. Two soft notes for the consolidator:
1. `engine/gif/boundary_slot.*` — the R168 boundary-slot machinery is big
   enough to be its own class; ARCH's tree shows only `gif/` with "hle/
   reserved". Not a gap (fits under gif/), but the INDEX should name it.
2. `engine/render/backends/gl/gl_draw_merge.*` — only if the consumer-side
   merge survives profiling (recommended: it does not; delete).

## Dead-code findings (with evidence)

| file | evidence |
|---|---|
| recomp_test.cpp/.h | A1 §5.1 dead-scaffold list; no CMake reference (grep of both CMakeLists) |
| test_vblank_bug_condition.cpp | A1 §5.1 "DEAD in-tree test harness (not in any CMake target)" |
| texture_cache.h/.cpp | in CMake (host_app/CMakeLists.txt:271-272) but NO other TU includes texture_cache.h (grep: only its own .cpp); no `TextureCache` instantiation anywhere; live caches are texture_decoder/render.h r118-r120 families |
| gs_hle.cpp: hle_InitGraphics, hle_RenderLoop (+Ps2RenderNode) | never installed / body commented out; MIGRATION + HOOK §3.5 mark both DELETE |
| gif_arbiter.cpp `if (false && …)` async-VU1 block (~140 ln) | compile-time-dead guard, self-labeled "removed in cleanup" |
| sif_rpc_handlers.cpp HandleCustomLoader (897 ln) | unreachable under USE_IOP_RECOMPILER=1 (compile-time constant since the recomp path shipped) |
| vu_aot_programs_* | built only under CMake option PS2_VU_AOT (root CMakeLists:115), default OFF per NOAOT |

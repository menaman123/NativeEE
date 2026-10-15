# PHASE4_PLAN — `engine/iop/` + `sif` + `cdvd` + STREAM.IRX + the IOP-bus devices: CONSTRUCTION_PLAN §4.4 made executable

Status: planning complete (step 4 of 4). §1–§7 are written; each step ended with a review (§8.1–§8.4 collect them) and the
plan-level findings are §7.1's first three rows. Stage 4A starts from the HANDOFF at the end of this file.

Sources for step 1, and nothing else was read: `CONSTRUCTION_PLAN.md` §0.2 (D-SEQ-1/2/3) and §4.4,
`ARCHITECTURE.md` §7.1, `PHASE3_IMPL.md` §3F′ HANDOFF. Step 2's sources are listed at the head of §2.
`STAGE_RULES.md` binds every stage below
(bounded runs, OLD read-only and never built or `git`-touched, no commits, tree green at every stop).

## 1. Goal and scope (D-SEQ-1/2/3)

### 1.1 Start state — the SIF park (Phase 3 gate PASSED; 3F′ handoff commands are the pre-flight check)

- Cold boot: `python3 verify/run_leg.py --binary build/app/host/nativeee --tree new --anchor coldboot
  --profile deterministic --frames 1500 --out /tmp/p4/cb` → `run_leg: OK`,
  `park=sif_rpc_absent park_pc=0x2cdadc park_tid=1`. The EE main thread (tid 1) is parked in the caller
  of the `sceSifSetRpcQueue` / `sceSifBindRpc` / `sceSifCallRpc` rows with nothing on the IOP side.
  **Correction (4C, §7.1 P4-PARK-IS-SBRK):** that coldboot park was NOT a SIF row. `KernlSifRpc` owns 8
  non-SIF libkernl rows under the `sif_rpc_absent` reason and the parking row was `sbrk@0x2cf930`
  (`sdk_rows_hit_list`); once `KernlSbrk` owned it the EE ran on to libcdvd (`sceCdReadClock`,
  `0x2c47a0`) BEFORE its first SIF call. The coldboot leg reaches `sceSifInitRpc` / `sceSifBindRpc`
  only after 4D (libcdvd rows + `CdvdReader`); the SIF-park wording above stands for the anchor legs
  (tid 2's `sceSifSetRpcQueue → RegisterRpc → RpcLoop`), which 4C released as predicted.
- Anchors (11 legs, `--frames 0`, e.g. `--anchor save_states/ps2state_slot0_f57292.ps2st --out
  /tmp/p4/a1`) → `park=sif_rpc_absent park_pc=0x1e7fa4 park_tid=2 relaunch=2`: restored
  (`restore.mhash` 11/11), relaunched through `ThreadRelauncher`, parked in the anchors' RPC caller on
  tid 2.
- What Phase 3 deliberately left for us (D-SEQ-2): the boundary OWNER stub (`engine/gs/hle/sce/`) and
  `MmioSink`s over the SIF and SPU2 register windows. Phase 4 reaches the boundary WITHOUT editing
  Phase-3 files; it displaces those two sinks with real register models (`sif/sif_state`,
  `sound/sound_core`) so that neither range reports in `UnhandledRanges()` at the end of the phase.

### 1.2 End state — from the park to the boundary stream

- Cold boot runs from `0x2cdadc` through the IOP reboot handshake (IOPRP300), loads the 8 disc IRX
  (7 stock Sony stubs + the recompiled STREAM.IRX plugin), binds RPCs, passes the memory-card check
  (libmc rows), streams the first files (CDVD + STREAM.IRX), and reaches boundary 0 — the first
  `sceGsSyncV` — and beyond, as far as the stopping rule in §1.3 lets the leg be claimed.
- Each of the 11 anchor legs restores its IOP state (IOPM section), re-parks the STREAM.IRX threads
  boot-symmetrically, restores SNDC/SDRX, and runs ≥ 60 boundaries past `0x1e7fa4` with the first
  frame's pad read, music streaming and RPC traffic all served.
- Both dispatch policies (§3) exist. `iop.dispatch=frame` is the proof path for every hash claim;
  `iop.dispatch=realtime` boots to the title screen in a bounded human-play run (BUG-026 closed;
  recorded, not hash-gated).

### 1.3 Lanes claimable in this phase (and the stopping rule that bounds each claim)

- `mem` — RAM + SPAD + meta arena hashed at each SyncV boundary (`mem.mhash`). Claimable at checkpoint 0
  (boundary 0) and per checkpoint after it. The claim is IDENTICAL to the frozen OLD baseline
  (`verify/baseline/`) through the LAST checkpoint whose RAM content does not yet depend on a sink this
  phase has not transplanted. Sinks still open after Phase 4: (a) DMAC transfers that write RAM/SPAD
  (SPR channels; the Phase-3 sink swallows the kicks), (b) VU1 data-memory results copied back to RAM,
  (c) GS local→host readbacks, (d) IPU decode output. Per leg `PHASE4_IMPL.md` records the index of the
  last claimed checkpoint, the first divergent checkpoint, and its ATTRIBUTION (which sink, found by
  memory-checkpoint bisection + diag census). A divergence not attributable to (a)–(d) is a construction
  bug and blocks the phase. Sink-attributed divergences are the next phase's work, never accepted
  differences.
- `audio` — claimable from this phase (D-SEQ-1), because the mixer is tick-clocked at the boundary
  (OLD `sound::TickClocked()` → `Mixer` pump as a `BoundarySequencer` step, `PS2_SND_TICKCLOCK` NORM
  under `time.tick_sync`) and needs no host device to produce the window hashes. Compared window by
  window under the same stopping rule. The first two claims: coldboot window 0 `0x0a6fb8af536edb25`,
  the f46203 anchor's window 0 `0xbed6341167ff6373`.
- `frames` — NOT claimable (no GS state / render until Phase 6). The boundary owner keeps its Phase-3
  stub form; only its slot table gains the IOP dispatch and mixer steps (§3.2).

### 1.4 In scope (D-SEQ-1 pulls the IOP-bus devices forward from Phase 7)

- `engine/iop/`: kernel + coroutines + host thread + clock + RPC bridge + module table; IOP memory,
  context, sysmem heap, `IopLoader` (IRX load / reboot handshake; module locations from
  `TitleProfile.iop_modules`); the stock Sony kernel surface one dir per module
  (`iop/hle/{thbase,thsemap,thevent,thmsgbx,timrman,sifman,sifcmd,sysclib,stdio,ioman,loadcore}`) plus
  `iop/hle/cdvdman` (an IOPRP300 post-reboot kernel module, not a disc IRX — §2.3/§8.6 item 5); the
  disc IRX stubs (`iop/hle/{sio2man,padman,mcman,mcserv,libsd,sdrdrv,cdvdstm}`).
- `engine/sif/`: mailbox registers over the SIF MMIO range, `RpcRouter` (bind/call, IOP→EE marshal
  `Wait::Rpc`, `CompletePending`), scesif handler bodies, netcnfif bindings after a reachability check.
- `engine/cdvd/`: ISO9660 walker, deferred async read engine, latency model (`cdvd.latency_model`,
  `cdvd.latency_scale`), stream reader, delivery meter/budget, the completion-visibility loader model
  (HARDWARE/TIMING HLE, HOOK §3.6), libcdvd bindings.
- `games/<t>/iop/stream_irx/`: the STREAM.IRX recompiled OUTPUT from the frozen
  `OLD/build-v2/iop_recompiler_tool` (invocation recorded like §A.1; regen determinism is an oracle),
  plugin registration, module layout, `lti_` protocol handlers (recv buffer 0x3B3000), and the
  `feed_contract` completed here together with the SPU2 core.
- `engine/sound/` (D-SEQ-1): `SpuCore` (voices, ADSR, VOLX/NAX/ENDX readbacks — generic; its registers
  displace the Phase-3 SPU2 sink), voice, VAG decoder, tick-clocked `Mixer` (no host device), fmv_stream,
  sound state codec (SNDC/SDRX), bank ledger; `sound/hle` (sdr_remote, sd_block_trans, libsdr rows).
- `engine/pad/hle` (D-SEQ-1): `PadProtocol` (scePad wire words, slot phase/seq stamps at the boundary,
  `PS2_MAINT_EEPACED` NORM); input SOURCE = `InputLog` replay or neutral under the profile; libpad rows.
- `engine/mc/hle` (D-SEQ-1): host store, command model (`cdvd.mc_latency_polls` documented divergence),
  libmc rows — the boot's memory-card check runs through them.
- Title side: the four quarantined workarounds with ledger rows — `piracy_bypass` (0x181490, HOOK §3.5),
  `rendinit_double_init` (0x1b36b8), `type_registry_append` (0x268440), `async_read_dest_preserve`
  (0x2ad110) (HOOK §3.1), `allocator_reentry` (0x181560); `games/<t>/data/archive_table` (DATA.BD/BH
  layout, TREE-GAP #11). Diag builds: `core/diag/{snd_census,loadspan,stick_trace}`.
- OLD work items: BUG-026 (§3.4) and BUG-038 (scripted cold-boot title load stall under the run token).
  The coldboot leg IS the gate, so BUG-038 is closed here or the phase does not land.
- Carried from the 3F′ handoff, scheduled in §7: `battery.py` Phase-3 mode (coldboot ×3 + 11 anchors ×3,
  `--restore-only`); P3-MANIFEST-12 disposition; P3-BOOT-WAITSEMA compare with the OLD boot trace
  (WaitSema(68) before any CreateSema); `KeyRedirect::NoteWrite` binding decision (arbiter = level-leg
  `mem.mhash`); HEAP section; `hle/budget` / `hle/loader_sync` data pairs; P3-RAMIMAGE-2 adjudication
  when a leg reaches `FUN_002d18f8`; P4FIX-F1 `p4fix_obj=` probe (EE[sp+0x10] at FUN_001f4730 entry) on
  the first post-SIF leg, NEW vs OLD `--tree v2`, bounded 60 frames.

### 1.5 Out of scope (and where it goes)

- Host DEVICE halves — `platform/sdl/audio_sink`, `platform/sdl/input/*`, gamepad/kbm backends,
  `default_bindings.ini`, play-mode audio latency → Phase 7 (D-SEQ-3). `audio.enabled` exists as a
  SETTING, but no device is opened in this phase; the mixer only fills the hashed windows.
- Sinks (a)–(d): dmac/vif/gif/vu → Phase 5; gs/render → Phase 6; ipu/FMV → Phase 7. The coldboot leg's
  first FMV is sink (d): the claim stops there by rule, not by tolerance.
- The `PS2_IOP_KERNEL=0` hosting (per-guest-thread `std::thread` bodies, `GuestMonitorYield`) is never
  written — `PS2_IOP_KERNEL` is NORM ON.

## 2. Module table

Sources for this step (nothing else was read): CONSTRUCTION §4.4 table + §A.1 STREAM.IRX paragraph;
`docs/filemap/INDEX.md` rows for cdvd/dmac/iop/mc/pad/sif/sound + the reverse map; OLD `iop_stubs.h`
(192 ln) and the `host_app/iop_kernel/` file list (6 pairs, `iop_clock.h` header-only); OLD line anchors
re-verified today with `grep -n` (STAGE_RULES: OLD read-only); `FLAG_LEDGER.md` rows for the flags named
in §4.4 (source-file attribution only — dispositions and counts are §5, step 3); `HOOK_RECLASSIFICATION`
§3.1/§3.5/§3.6 rows for the retirement gates; NEW `boundary_sequencer.h` and `syscall_table.h` for what
Phase 3 already provides.

Conventions: one primary type per `.h/.cpp` pair, file = snake_case(type) (ARCHITECTURE §11; this
renames two §4.4 file names — `loader` → `iop_loader`/`loader_model`, noted in the rows). `*_bindings.cpp`
TUs are the established SDK-binding form (no class; addresses come from `games/<t>/hle/sdk_bindings.tsv`).
"Flags applied" lists NAMES only, at the row where the OLD `getenv` site lives (ledger `source` column);
a flag whose OLD site spans rows is named on each (§5 counts it once). Line anchors are today's OLD
lines; where CONSTRUCTION §4.4 quotes a different anchor the drift is recorded in §8.2.

### 2.1 IOP kernel — 6 rows (OLD `host_app/iop_kernel/*`, verbatim; ARCHITECTURE §7.1)

| New file | Class | OLD source | Flags applied |
|---|---|---|---|
| `engine/iop/iop_kernel.h/.cpp` | `IopKernel` — states Dormant/Ready/Run/Wait, waits Sleep/Delay/Sema/EventFlag/Mbx/Rpc, `Dispatch()` = injections → due events/timers → Ready by priority (FIFO among equals); the dispatch-generation rule (an event scheduled in pass k fires at k+1 — the counted-queue contract of §3.1 line 7) | `iop_kernel/iop_kernel.{h,cpp}` (`iop_kernel.h:34` class; `kMaxThreads 32`, `kMaxSemas 64`; hooks `SetThreadRunHooks/SetWakeHook/SetTraceHook` kept; OLD's `SetDispatchGuard`/`SetContextSwitchHooks` exist only for the deleted `GuestMonitor` and are NOT written — P4-GUARD-HOOKS-DROPPED, §7.1; no `GuestMonitor*` exists, §1.5) | `PS2_IOP_KERNEL` (kernel arm only), `PS2_IOPSCHED_DIAG` |
| `engine/iop/iop_coroutine.h/.cpp` | `IopCoroutine` — one per guest thread over the tree's platform fiber layer (the EE scheduler's) | `iop_kernel/iop_coroutine.{h,cpp}` | — |
| `engine/iop/iop_host_thread.h/.cpp` | `IopHostThread` — the ONE realtime dispatcher: `Dispatch()` then condvar-sleep to next deadline / EE poke (§3.3) | `iop_kernel/iop_host_thread.{h,cpp}` | `PS2_IOP_DISPATCH` (realtime arm) |
| `engine/iop/iop_clock.h/.cpp` | `IopClock` — Realtime = steady_clock µs; Frame = virtual µs, `OnFrameBoundary()` +16,667 µs, `AdvanceToUs()` forced advance. **Closes §3.2's open bound:** forced advances are capped at `FrameCeilingUs()` = one frame quantum past the current frame base (`iop_clock.h:63`), and `PumpFromParkLoop` advances at most one quantum per call (`iop_module_table.h:61`) — the next boundary's tick, as §3.2 guessed | `iop_kernel/iop_clock.h` (header-only; the `.cpp` holds the non-trivial members). `GetSystemTime`/`USec2SysClock` do NOT route here (per-call counters are guest-visible, `iop_clock.h:15-16`) → `thbase` row | `PS2_IOP_DISPATCH` (frame arm) |
| `engine/iop/sif_rpc_bridge.h/.cpp` | `SifRpcBridge` — EE→IOP `RunOnRpcThread` (body enqueued, module's own RPC-loop coroutine woken; FRAME: the EE drives `Dispatch()` until the ticket completes), `RpcServiceLoop`, IOP→EE `WaitEeReplyUs`/`NotifyEeReply` (wakes only a `Wait::Rpc` parkee; late reply dropped) | `iop_kernel/sif_rpc_bridge.{h,cpp}` | — |
| `engine/iop/iop_module_table.h/.cpp` | `IopModuleTable` — facade: kernel + bridge + host thread, `OnModuleLoaded(rpc_tid)` (every started thread runs to its first block before the EE continues), pump points `PumpAtBoundary` (→ `BoundarySequencer::Step::IopDispatch`, enum index 4 = the fifth slot: §3.2's "slot 5" is the 1-based count — CONFIRMED), `PumpAfterEeService` (→ `Step::EeStreamRpc`, index 5), `PumpFromParkLoop` (EE park loops that reach no boundary); the `iop.dispatch` selection | `iop_kernel/iop_module_table.{h,cpp}` (`:11` `PS2_IOP_KERNEL` latch, `:19` `PS2_IOP_DISPATCH` latch — both become `Config` reads, no `getenv`) | `PS2_IOP_KERNEL`, `PS2_IOP_DISPATCH` (→ `iop.dispatch`) |

### 2.2 IOP memory / context / heap / loader — 4 rows

| New file | Class | OLD source | Flags applied |
|---|---|---|---|
| `engine/iop/iop_memory.h/.cpp` | `IopMemory` — 2 MB IOP RAM, `read<T>/write<T>`, storage out of the header; the IOPM savestate section's byte source | `iop_memory.h` (57 ln) | — |
| `engine/iop/iop_context.h/.cpp` | `IopContext` — the R3000 register file the recompiled STREAM.IRX runs on (`gpr[]`, results in `$v0`); the global instance moves to the `.cpp` | `iop_context.h` (19 ln) | — |
| `engine/iop/sysmem_heap.h/.cpp` | `SysmemHeap` — `AllocSysMemory/FreeSysMemory/QueryMaxFreeMemSize` arena (also the `0x80000003` IOP-heap RPC face, `sif_rpc_handlers.cpp:700 HandleIopHeap`) | `iop_sysmem_heap.h` (148 ln) | — |
| `engine/iop/iop_loader.h/.cpp` (§4.4 says `loader.h/.cpp`; renamed for file = snake_case(type)) | `IopLoader` — IRX section load + relocation, the reboot handshake (`sceSifRebootIop` with `IOPRP300.IMG`: pre-reboot skipped, post-reboot module set = the §2.3 dirs), the `0x80000006` LOADFILE RPC face (`sif_rpc_handlers.cpp:761 HandleLoadFile`) through which the EE loads the 8 disc IRX; module locations and the two STREAM.IRX constants (`gp = 0x13328`, RPC tid read from `[0x13328]`, `iop_loader.cpp:299,:311-314`) come from `TitleProfile.iop_modules` / `module_layout.h` (§2.10) — no game constants in `engine/` | `iop_loader.{h,cpp}` (451+15 ln; `iop_load_stream_irx()` `:100`) | — |

### 2.3 `engine/iop/hle/` — Sony libraries, one dir + one class per module: 18 rows

The set is EXACTLY iop_stubs.h's 12 libraries imported by STREAM.IRX (`iop_stubs.h:5`: sysmem, intrman,
ioman, sifcmd, sifman, stdio, sysclib, thbase, thsemap, timrman, libsd, cdvdman) ∪ the 7 disc-side
stock modules (sio2man, padman, mcman, mcserv, libsd, sdrdrv, cdvdstm; the 8th disc IRX is the title's STREAM.IRX,
§2.10) = 18 dirs — `cdvdman` is an IOPRP300 (post-reboot kernel) module, not a disc IRX (`runtime-environment.md`; it is
already in the imported 12, so the union is unchanged). §4.4's list names
`thevent`, `thmsgbx`, `loadcore` (NOT imported by STREAM.IRX and not disc modules — `IopKernel` already
carries the thevent/thmsgbx semantics for unit tests) and omits `sysmem`, `intrman` (imported) — §8.2
item 1; the 18 below follow the evidence. Each dir = `iop/hle/<module>/<module>_module.h/.cpp`, class
`<Module>Module`, holding (a) the import stubs the recompiled STREAM.IRX calls (kernel stubs route to
`IopKernel`, nothing else: the `PS2_IOP_KERNEL=0` `std::thread` bodies and `GuestMonitor*` are not
written) and/or (b) the module's SIF RPC-server face (server ids from `sif_rpc_handlers.cpp:379-420`),
delegating to the device engines of §2.5–§2.9. Deferred completions (`iop/hle/deferred_events` in
§4.4's OLD column) dissolve into `IopKernel::ScheduleEventUs` calls made by rows (k) and (l) — no
separate class.

| New file | Class | OLD source | Flags applied |
|---|---|---|---|
| (a) `iop/hle/sysmem/sysmem_module.h/.cpp` | `SysmemModule` — `AllocSysMemory/FreeSysMemory/QueryMaxFreeMemSize/Kprintf` (idx 4/5/7/14) over `SysmemHeap`; `0x80000003` IOP-heap RPC face | `iop_stubs.cpp:262-320`; `sif_rpc_handlers.cpp:700` | — |
| (b) `iop/hle/intrman/intrman_module.h/.cpp` | `IntrmanModule` — `CpuSuspendIntr/CpuResumeIntr` (idx 17/18): no-ops with the saved-state word (single dispatcher; no interrupts to mask) | `iop_stubs.cpp:321-331` | — |
| (c) `iop/hle/ioman/ioman_module.h/.cpp` | `IomanModule` — `open/close/read/lseek` (idx 4/5/6/8) over `IsoFs` (§2.5); `0x80000001` FILEIO RPC face | `iop_stubs.cpp:332-382`; `sif_rpc_handlers.cpp:441 HandleFileIO` | — |
| (d) `iop/hle/sifcmd/sifcmd_module.h/.cpp` | `SifcmdModule` — `sceSifInitRpc/BindRpc/CallRpc/RegisterRpc/SetRpcQueue/RpcLoop` (idx 14/15/16/17/19/22); `RpcLoop` = `SifRpcBridge::RpcServiceLoop` (the module's RPC-dispatch coroutine parks here); `sceSifCallRpc` from the IOP side = the IOP→EE marshal (`Wait::Rpc`) | `iop_stubs.cpp:383-604` (`sceSifRpcLoop :560`) | — |
| (e) `iop/hle/sifman/sifman_module.h/.cpp` | `SifmanModule` — `sceSifInit/SetDma/DmaStat/CheckInit` (idx 5/7/8/29): IOP-side SIF DMA = memcpy into EE RAM + mailbox update through `SifState` | `iop_stubs.cpp:605-652` | — |
| (f) `iop/hle/stdio/stdio_module.h/.cpp` | `StdioModule` — `printf` (idx 4) → `core/log` | `iop_stubs.cpp:653-662` | — |
| (g) `iop/hle/sysclib/sysclib_module.h/.cpp` | `SysclibModule` — the ordinal→function map `{12 memcpy, 14 memset, 20 strcat, 23 strcpy, 27 strlen, 29 strncmp, 34 strstr}` (the S44-corrected table, `iop_stubs.h:53-73`); the five pre-S44 mis-bound names (memcmp/prnt/strchr/index/strrchr) are declared in OLD "harmless, unit-tested" — written only if the generated output references them (grep at transplant) | `iop_stubs.cpp:663-769` | — |
| (h) `iop/hle/thbase/thbase_module.h/.cpp` | `ThbaseModule` — `Create/Delete/Start/TerminateThread, ChangeThreadPriority, GetThreadId, Sleep/Wakeup/DelayThread, GetSystemTime, USec2SysClock` (idx 4/5/6/10/14/20/24/25/33/34/39) → `IopKernel`; `ChangeThreadPriority` stays the documented NO-OP (`iop_stubs.h:82-93`: one call site `0x1115c`; routing it changes dispatch order under the frozen profile — a §7 re-freeze item, BUG-026 record); `GetSystemTime`/`USec2SysClock` keep their per-call-counter bodies verbatim | `iop_stubs.cpp:770-1171` (`DelayThread :1095`, `GetSystemTime :1143`, `USec2SysClock :1159`) | `PS2_IOP_KERNEL` (kernel routing arm), `PS2_IOPSCHED_DIAG` |
| (i) `iop/hle/thsemap/thsemap_module.h/.cpp` | `ThsemapModule` — `Create/Delete/Signal/iSignal/WaitSema` (idx 4/5/6/7/8) → `IopKernel` semaphores (FIFO order) | `iop_stubs.cpp:1172-1347` (`CreateSema :1187`, `WaitSema :1286`; the retired S42 CD-worker hold `:1278` is not transplanted) | `PS2_IOP_KERNEL`, `PS2_IOPSCHED_DIAG` |
| (j) `iop/hle/timrman/timrman_module.h/.cpp` | `TimrmanModule` — `Alloc/FreeHardTimer, SetTimerHandler, SetOverflowHandler, SetupHardTimer, StartHardTimer` (idx 4/6/20/21/22/23): the ONE hard timer the title starts (the STREAM.IRX 60 Hz heartbeat, handler `0x136c`) becomes `IopKernel::StartPeriodicTimerUs` on the kernel clock (§3.2 emu ticks / §3.3 wall) — the `PS2_HB_TICKCLOCK` arm §8.1 deferred to §5 | `iop_stubs.cpp:1348-1677` (`AllocHardTimer :1401`, `StartHardTimer :1440`, kernel periodic timer `:1466`) | `PS2_HB_TICKCLOCK` |
| (k) `iop/hle/libsd/libsd_module.h/.cpp` (landed 4E″ it.2) | `LibsdModule` — the 16 libsd import stubs (`SdInit`… idx per `iop_stubs.cpp:1740+`) HLE'd onto `SpuCore` (§2.7): voice-transfer completions are deferred kernel events (FIX-C), status polls answered from `SpuCore` readbacks (VOLX/NAX/ENDX) | `iop_stubs.cpp:1678-2003` | `PS2_SND_PROCBATCH`, `PS2_SPU_LAT_US`, `PS2_MUSQ`, `PS2_MUSQ_VFMIN`, `PS2_NAXQ`, `PS2_VTQ`, `PS2_SND_KONLOG` |
| (l) `iop/hle/cdvdman/cdvdman_module.h/.cpp` | `CdvdmanModule` — `sceCdInit/Read/GetError/SearchFile/Sync/GetDiskType/DiskReady/Callback/Mmode` (idx 4/6/8/10/11/12/13/37/75) over `ReadEngine` + `LatencyModel`; CD-read completion = deferred kernel event fired inside `Dispatch()` (the §3.4 "meet at a defined boundary" point); the EE→IOP nudge (`RaiseStreamCdCompletion :2066`) = `InjectSignalSema` on the CD-worker sema; PLUS the EE-facing RPC faces `0x80000592/593/595/597/59A/59C` (retail: cdvdfsv, the RPC face of cdvdman — one dir per §4.4) | `iop_stubs.cpp:2004-2807` (`sceCdInit :2010`, `sceCdRead :2543`); `sif_rpc_handlers.cpp:1155-1550, :6126` (`HandleCdvdInit/SCmd/NCmd/SearchFile/DiskReady`, `HandleCdDiskReady`) | `PS2_CD_LAT_US`, `PS2_IOPQ` |
| (m) `iop/hle/sio2man/sio2man_module.h/.cpp` | `Sio2manModule` — no import stubs, no RPC server; exists as the load-order/`RegisterLibraryEntries` dependency of padman/mcman (module table entry only) | — (runtime-environment: stock Sony 2.03) | — |
| (n) `iop/hle/padman/padman_module.h/.cpp` | `PadmanModule` — RPC faces `0x80000100/0x80000101` → `PadProtocol` (§2.8) | `sif_rpc_handlers.cpp:875 HandlePadman` | — |
| (o) `iop/hle/mcman/mcman_module.h/.cpp` | `McmanModule` — no RPC server of its own (mcserv fronts it); module-table entry + the `McHostStore` ownership | — | — |
| (p) `iop/hle/mcserv/mcserv_module.h/.cpp` | `McservModule` — RPC face `0x80000400` → `McCommandModel` (§2.9) | `sif_rpc_handlers.cpp:1035 HandleMcServ` | — |
| (q) `iop/hle/sdrdrv/sdrdrv_module.h/.cpp` | `SdrdrvModule` — RPC face `0x80000701` (LIBSD REMOTE) → `SdrRemote`/`SdBlockTrans` (§2.7) | `sif_rpc_handlers.cpp:1619 HandleLibSd` (`:1551` banner) | `PS2_SND_PAUSE_DEFER` |
| (r) `iop/hle/cdvdstm/cdvdstm_module.h/.cpp` | `CdvdstmModule` — the stream-read face behind `sceCdSt*` → `StreamReader` (§2.5) | `cdvd_stream.cpp` IOP-side half | — |

Not transplanted from `sif_rpc_handlers.cpp`'s table: `HandleMtap` (`0x80000901-905`, `:1743`) and
`HandleEyeToy` (`0x80001400`, `:1811`) — no MTAPMAN/EyeToy IRX is on the disc, so the game cannot bind
them; same reachability rule as `netcnfif` (§2.4). `HandleUnknownServer` → a `core/diag` census line.

### 2.4 `engine/sif/` — 5 rows

| New file | Class | OLD source | Flags applied |
|---|---|---|---|
| `engine/sif/sif_state.h/.cpp` | `SifState` — MSCOM/SMCOM/MSFLAG/SMFLAG (+ the `sceSifSetReg/GetReg` words) registered into `MmioRouter` over `0x1000F200–0x1000F2FF`, displacing the Phase-3 `MmioSink` (D-SEQ-2; the range must vanish from `UnhandledRanges()`) | `sif.h` (31 ln) | — |
| `engine/sif/hle/rpc_router.h/.cpp` | `RpcRouter` — bind (client record ↔ server id, `sif_bind_rpc::RegisterBinding`), call → the owning module's RPC face (server-id table above) or `SifRpcBridge::RunOnRpcThread` for `0x12345`; `CompletePending` = end-callback delivery at `Step::MaintDuties` | `sif_rpc_handlers.cpp:370-424` (dispatch), `sif_rpc_handlers.h` (89 ln), `sif.cpp` (19 ln, `CompletePending`), `sif_hle.cpp` bind body (`:727` region) | `PS2_MAINT_EEPACED` (end-callback duty) |
| `engine/sif/hle/sce_sif.h/.cpp` | `SceSif` — the `sceSif*` handler bodies (Init/InitRpc/BindRpc/CallRpc/CheckStatRpc/RegisterRpc/SetRpcQueue/GetNextRequest/RpcLoop/SetDma/DmaStat/SetReg/GetReg) REBINDING Phase 3's `KernlSifRpc` park rows (`ee/hle/kernl_sif_rpc.h`: "Phase 4 rebinds these rows through sif/hle registration") via the manifest; `sceSifSetDma` = descriptor decode + memcpy into `IopMemory` (no DMAC register traffic — §2.6) | `sif_hle.{h,cpp}` (884 ln minus `:745-778` bypass and the Phase-3 boundary/boot halves), `syscalls.cpp` sceSifSetDma/DmaStat decode | — |
| `engine/sif/hle/ee_rpc_server.h/.cpp` | `EeRpcServer` — the game's registered EE-side RPC server (sid/func/buff) and the IOP→EE marshal service (`EeStreamRpcRequest` → park `Wait::Rpc`; `ServiceEeStreamRpc` runs the EE callback at `Step::EeStreamRpc` / the syscall edge with the `ra` sentinel `0x80001000`, then `NotifyEeReply`) | `sif_rpc_handlers.cpp:60-200` (`EeStreamRpcServer :60`, `RegisterEeStreamRpcServer :78`, `EeStreamRpcRequest :94`, `ServiceEeStreamRpc :170`) | — |
| `engine/sif/hle/netcnfif_bindings.cpp` | (bindings TU) — written ONLY if the Phase-4 boot BOUND census shows a netcnfif binding (INDEX f7: "may be unreachable") | `sce_hle/netcnfif.cpp` (23 ln) | — |

### 2.5 `engine/cdvd/` — 8 rows (delivery meter + budget = the completion-visibility model's pacing half)

| New file | Class | OLD source | Flags applied |
|---|---|---|---|
| `engine/cdvd/iso_fs.h/.cpp` | `IsoFs` — ISO9660 walker: path → LBA/size, positioned sector reads (same sector math as OLD; byte-oracle on `SLUS_123.45`, `DATASYS/*.IRX`, `DATA.BH`, §4) | `hle_heap.cpp:41 PositionedIsoRead`, `:3933 HostIso_FindFile` | — |
| `engine/cdvd/read_engine.h/.cpp` | `ReadEngine` — deferred async reads: queue per fs-object, chunked apply, delivery at `Step::MaintDuties` (deferred-ISO duty), completion handed to `CdvdmanModule` as a kernel event | `hle_heap.cpp:1333 DeliverIsoReadData, :1490 CompleteDeferredReadForObj, :2002/:2011 Peek/HasPendingDeferredReadFor, :2079 ProcessDeferredIsoReads, :2441, :3979-3990` | `PS2_CHUNK_APPLY_SCOPED`, `PS2_CHUNK_APPLY_DORMANT`, `PS2_CHUNK_APPLY_VALVE_TICKS`, `PS2_CHUNK_APPLY_ORDER`, `PS2_CHUNKQ`, `PS2_MAINT_EEPACED` (delivery duty) |
| `engine/cdvd/latency_model.h/.cpp` | `LatencyModel` — DVD seek/transfer latency + the 3-pass floor (HOOK §3.6 names it part of the completion-visibility model); `instant`/`modelled` × `latency_scale` | `hle_heap.cpp` latency arms (A4 §4.1), `iop_stubs.cpp` CD/SPU latency sites (`:1804`, `:1813` region) | `PS2_CD_LAT_US`, `cdvd.latency_model`, `cdvd.latency_scale` |
| `engine/cdvd/stream_reader.h/.cpp` | `StreamReader` — `sceCdSt*` ring (Init/Start/Seek/Stop/Read/Stat/Pause/Resume), `PumpSectors` | `cdvd_stream.{h,cpp}` (425+48 ln; `PumpSectors` `cdvd_stream.h:47`) | — |
| `engine/cdvd/delivery_meter.h/.cpp` | `DeliveryMeter` (TREE-GAP #2) — per-epoch delivery-bandwidth budget in bytes; refill only on epoch edge (never mid-iteration, `ps2_vblank.h:100-101`); the "how much becomes visible per frame" half of completion visibility | `ps2_vblank.h:65-175` | `PS2_CDVD_BUDGET_KB` (NORM per ledger `:307`; absent from §4.4's 42 — §8.2 item 3) |
| `engine/cdvd/delivery_budget.h/.cpp` | `DeliveryBudget` (TREE-GAP #3) — load-budget lift: engage/gap/release/input-idle veto as a class (purge-dissolve) | `budget_lift.h` (223 ln; `:70-206`) | `PS2_LOAD_BUDGET_LIFT`, `PS2_LOAD_BUDGET_LIFT_ENGAGE_MS`, `PS2_LOAD_BUDGET_LIFT_GAP_MS`, `PS2_LOAD_BUDGET_LIFT_INPUT_MS`, `PS2_LOAD_BUDGET_LIFT_RELEASE_MS` (ledger `:491-495`) |
| `engine/cdvd/hle/loader_model.h/.cpp` (§4.4 says `hle/loader.h/.cpp`; renamed for file = snake_case(type)) | `LoaderModel` — the completion-visibility model (HARDWARE/TIMING HLE, HOOK §3.6): the single point at which a read becomes visible to the EE loader's poll (`FUN_002ad110`-class polls), the poll-spin sync-wait HLE, `LOADER_ORACLE` trace emission; title parameters come from `games/<t>/hle/loader_sync.cpp` (§2.11 row 6) | `hle/loader_sync_hle.h` mechanism half (224 ln; `:99`), `sif_rpc_handlers.cpp:2993 HandleCustomLoaderRecompiled` completion-visibility part, the hold branch inside the 0x2ad110 hook (`main.cpp:6936-7234`), `hle/loader_oracle.h` (`:55`, `:70`), `loadspan.h` → `core/diag/loadspan` | `PS2_LOADER_HLE`, `PS2_SNDREG` (`hle_heap.cpp:685`), `PS2_LOADER_ORACLE`, `PS2_LOADER_ORACLE_DIR`, `PS2_LOADSPAN`, `PS2_LOADSPAN_DRAWMAX` |
| `engine/cdvd/hle/libcdvd_bindings.cpp` | (bindings TU) — libcdvd rows + the `hle_sceCdSt*`/`DiskReady` rows (`cdvd_stream.h:13-22`; their EE addresses `0x2c4860…` are `sdk_bindings.tsv` data) | `sce_hle/libcdvd.cpp` (402 ln), `cdvd_stream.h:13-22` | — |

### 2.6 DMAC page `0x10008000` — 1 row (a register model, no transfer engine; the census is the assertion — reworded 4C‴ per §7.1 P4-DMAC-FILE)

| New file | Class | OLD source | Flags applied |
|---|---|---|---|
| `engine/dmac/dmac.h/.cpp` (written in 4C as the stage brief asked; this section originally said "no file"). `Dmac` registers the page `0x10008000–0x1000EFFF` + `D_ENABLER`/`D_ENABLEW` on the `MmioRouter` (displacing the Phase-3 sink row, `mmio_sink.cpp:28`) as a REGISTER MODEL: the ten channels' CHCR/MADR/QWC/TADR/ASR0/ASR1/SADR words store and read back; a `CHCR.STR=1` write completes synchronously (STR cleared, QWC 0, `D_STAT` CIS bit) — modelled for D5/D6/D7 (`SifKicks`), COUNTED for every other channel (`dmac_unmodelled_kicks=<n>:<chmask>`; no data moves — the GIF/VIF/IPU/SPR transfer engines stay Phase 5+, §4.5). SIF DMA itself is still the `sceSifSetDma`/`sceSifDmaStat` SYSCALL decode (§2.4 → memcpy into `IopMemory`), IOP→EE data is `SifmanModule`'s memcpy through `MainMemoryEeRamSink`, and the DMAC channel-5 handler the game registers with `AddDmacHandler` is invoked by `RpcRouter::CompletePending` — never by a D_STAT write. The original's D6 kick ran its SIF command-packet interpreter (`dmac.cpp:137-230`); NEW interprets nothing, so **`dmac_sif_kicks=0` is a leg invariant** (a non-zero value is a park-class finding). | `dmac.h:60-80`, `dmac.cpp:1045-1131, :1152-1250, :1554-1560, :2851-2859` | The assertion is the per-leg census: `unhandled_mmio_pages` contains no `0x10008000` page (coldboot: empty), no SIF (`0x1000F2xx`) or SPU2 page (= a Phase-4 construction bug), only GS-priv/GIF/VIF/FIFO/IPU pages (Phase 5–7 ranges) may appear; `dmac_unmodelled_kicks=` names the Phase-5 engines a leg touched (coldboot `1:0x1`, anchor `2:0x6`) | — |

### 2.7 `engine/sound/` (+ `sound/hle/`) — 10 rows (D-SEQ-1; no host device — `platform/sdl/audio_sink` is Phase 7)

| New file | Class | OLD source | Flags applied |
|---|---|---|---|
| `engine/sound/spu_core.h/.cpp` (the plan's `sound_core` name; file = snake_case(`SpuCore`) per the one-class lint — landed 4E″) | `SpuCore` — 2 cores × 24 voices, ADSR, KON/KOFF, VOLX/NAX/ENDX readbacks, DMA-transfer completion (deferred); its register file is registered over the SPU2 window (`0x1F900000`-side as the IOP code addresses it) displacing the Phase-3 SPU2 sink (D-SEQ-2) | `sound/sound_core.{h,cpp}` (1,168+171 ln; `:187` `PS2_SOUND`, `:237` selftest, `:337/:473` KONLOG, `:353` eager-hold) | `PS2_SOUND` (→ `audio.enabled`), `PS2_SND_EAGER_HOLD`, `PS2_SND_KONLOG`, `PS2_SND_SELFTEST` |
| `engine/sound/voice.h/.cpp` | `Voice` — per-voice state machine, ADSR envelope, pitch, loop flags | `sound/voice.{h,cpp}` (245+199 ln; `voice.h:88` is a feed-contract point → §2.10) | — |
| `engine/sound/vag_decoder.h/.cpp` | `VagDecoder` — ADPCM block decode (test vectors ported) | `sound/vag_decoder.{h,cpp}` (56+56 ln) | — |
| `engine/sound/mixer.h/.cpp` | `Mixer` — `MixInto` + the tick-clocked pump registered as a `BoundarySequencer` step (OLD `sound::TickClocked()`); ORDER relative to `Step::IopDispatch` — RESOLVED in §4.5 by OLD's boundary order (`gs_hle.cpp:721-744` + `verify_boundary.cpp:59-84`): the pump runs LAST, inside the `Step::Verify` body (pump → audio push → mem checkpoint), after IopDispatch/EeStreamRpc/Savestate; it composes into the existing Verify slot, `MaintDuties` is untouched and no slot is added; the audio window hashes remain the arbiter at transplant | `sound/mixer.{h,cpp}` engine half (85+29 ln; the SDL half stays in OLD → Phase 7) | `PS2_SND_TICKCLOCK` (NORM under `time.tick_sync`) |
| `engine/sound/fmv_stream.h/.cpp` | `FmvStream` — the FMV audio ring the IPU/MPEG side (Phase 7) will feed; created here because `sound_core.cpp` splits into it | `sound/sound_core.cpp` FMV half | `PS2_SND_FMVPACE`, `PS2_SND_FMVQ_DIV` (their sites are `libsdr.cpp:143/:153` → also named on `sdr_remote`) |
| `engine/sound/sound_state_codec.h/.cpp` (landed 4E‴; built into `ps2r_sound_hle` — it owns SDRX over `SdBlockTrans`) | `SoundStateCodec` — SNDC/SDRX savestate sections (restored on every anchor leg, §1.2); registered after TIMR, SDRX then SNDC | `sound/sound_core.cpp:1000-1178` codec half, `libsdr.cpp:400-440` | — |
| `engine/sound/spu_register_window.h/.cpp` (landed 4E‴) | `SpuRegisterWindow` — the SPU2 register window (`0x1F900000`, 0x800) over `SpuCore`, served from `IopMemory::SetBusWindow` (checked on the physical address before the RAM mask; KSEG1 alias); the range leaves the IOP unrouted census | — (the bus contract; the title never touches it directly) | — |
| `engine/sound/bank_ledger.h/.cpp` | `BankLedger` — mechanism half of the bank ledger (the IOP table half is title data → §2.10 `feed_contract.h`) | `snd_bank_ledger.{h,cpp}` (148+49 ln) | — |
| `engine/sound/hle/sdr_remote.h/.cpp` | `SdrRemote` — libsdr remote-command model (the EE side of `0x80000701`) | `sce_hle/libsdr.cpp` (438 ln) | `PS2_SDR_BLOCK_MS` (`:113`), `PS2_SND_FMVPACE` (`:143`), `PS2_SND_FMVQ_DIV` (`:153`) |
| `engine/sound/hle/sd_block_trans.h/.cpp` | `SdBlockTrans` — block-transfer (SPU DMA) command model + completion | `sce_hle/libsdr.cpp` transfer half | — |
| `engine/sound/hle/libsdr_bindings.cpp` | (bindings TU) | `sce_hle/libsdr.cpp` binding rows | — |
| (diag) `core/diag/snd_census` — `PS2_SND_CENSUS` (`snd_census.h:45`, the title cmd-table as data); listed for completeness, counted under §4.4's `core/diag` row | | `snd_census.h` (454 ln) | `PS2_SND_CENSUS` |

### 2.8 `engine/pad/hle/` — 2 rows (D-SEQ-1; input SOURCE = `InputLog` replay or neutral)

| New file | Class | OLD source | Flags applied |
|---|---|---|---|
| `engine/pad/hle/pad_protocol.h/.cpp` | `PadProtocol` — scePad wire words (DualShock 2 analog/lock/actuator frames), slot phase/seq stamps at the boundary (`Step::MaintDuties` pad-stamp duty), `scePadSetActAlign`/`SetMainMode` state; consumed by `PadmanModule`'s RPC face; the SDL backends are Phase 7 | `pad_input.{h,cpp}` protocol half (1,007+81 ln; `:476` script, `:553`, `:604`, `:653`, `:700`), `sce_hle/libpad.cpp` model half (933 ln; `:374`, `:416`) | `PS2_MAINT_EEPACED` (slot-stamp duty), `PS2_PAD_SCRIPT` (→ `InputLog` replay), `PS2_HEADWATCH`, `PS2_POSWATCH`, `PS2_U32WATCH`, `PS2_U32POKE`, `PS2_INLAT_ANALOG`, `PS2_STICKTRACE` (→ `core/diag/stick_trace`) |
| `engine/pad/hle/libpad_bindings.cpp` | (bindings TU) — libpad rows incl. `scePadSetActAlign`, `scePadSetMainMode` (both guaranteed among the 22 EE imports) | `sce_hle/libpad.cpp` binding rows | — |

### 2.9 `engine/mc/hle/` — 3 rows (D-SEQ-1; the boot's memory-card check runs through these)

| New file | Class | OLD source | Flags applied |
|---|---|---|---|
| `engine/mc/hle/mc_host_store.h/.cpp` | `McHostStore` — host directory `BASLUS-12345` round-trip (files, dir entries, timestamps), persistence policy | `sce_hle/libmc.cpp` store half (887 ln; `:96`) | `PS2_MC_PERSIST` |
| `engine/mc/hle/mc_command_model.h/.cpp` | `McCommandModel` — mcserv command state machine (GetInfo/Open/Read/Write/Close/Dir/Delete/Format) with the poll-count latency (`cdvd.mc_latency_polls`: documented divergence — real MCSERV is multi-frame) | `sce_hle/libmc.cpp` command half (`:196`), `hle_heap.cpp` MC half | `PS2_MC_LATENCY` (→ `cdvd.mc_latency_polls`) |
| `engine/mc/hle/libmc_bindings.cpp` | (bindings TU) — libmc rows | `sce_hle/libmc.cpp` binding rows | — |

### 2.10 `games/<t>/iop/stream_irx/` — 8 rows (frozen `iop_recompiler_tool` regen + byte oracle, §A.1)

| New file | Class | OLD source | Flags applied |
|---|---|---|---|
| `iop_recompiled.h/.cpp` (GENERATED, never hand-edited, 5.3) | — | regenerated by `OLD/build-v2/iop_recompiler_tool` (md5 `32a0c90ce604ddaaf1ba089bf2489c08`) from `OLD/game_files/STREAM.IRX` (md5 `d24082fd3560959a77d18ae9c43db51b`, 116,156 bytes), cwd `NEW/build/regen_iop`, bounded 600 s (§A.1 command verbatim); BYTE ORACLE: `iop_recompiled.h` md5 `c95059f165f8b68de442169ac932f871`, `iop_recompiled.cpp` md5 `b2102157c7dfa8285f21eadbb42c41a4` (= `build-v2` = `build-v2-nog`); regen determinism = two runs byte-identical | `PS2_STRMQ` (5 `getenv` sites EMITTED by the frozen tool; DELETE — grep-verified OFF-unless-set dead arms; `CoreSingleGetenvSite` dated exemption for this one file until 9.1) |
| `INVOCATION.md` + `MANIFEST.sha256` (data) | — | provenance block: tool md5, STREAM.IRX md5, the two output md5s, cwd, command, date — checked by `tools/regen_from_frozen.sh --check --iop` | — |
| `abi/host_app/{iop_context.h, iop_memory.h, iop_stubs.h}` (3 include-compat headers; lint-exempt with the generated file) | — | the frozen output includes `"../host_app/iop_context.h"`, `"../host_app/iop_memory.h"`, `"../host_app/iop_stubs.h"` by RELATIVE PATH (`build-v2/iop_recompiled.h:3-5`) — a directory literally named `host_app` must exist beside the generated file's include root; each header forwards to `engine/iop/iop_context.h`, `engine/iop/iop_memory.h`, and the `stub_surface` row | — |
| `stub_surface.h/.cpp` | `StubSurface` — the `iop_stubs::` free-function names the generated code calls (iop_stubs.h's 12 libraries, names verbatim: they are part of the generated ABI), each forwarding to the §2.3 module class; the ONLY place the OLD names survive; the one-class rule holds (one class + the ABI free functions it owns, like Phase 3's `recomp_abi`) | `iop_stubs.h` (192 ln) declaration set | — |
| `stream_irx_plugin.h/.cpp` | `StreamIrxPlugin` — `IopPlugin` registration: module entry, thread creation through `ThbaseModule`, `OnModuleLoaded(rpc_tid)` handoff, boot-symmetric re-park on savestate restore (IOPM section) | `iop_loader.cpp:100-330` plugin half, `iop_stubs.cpp:2810-2905` module glue (`:2845`, `:2902`) | — |
| `module_layout.h` (constants only) | — | `gp 0x13328`, RPC tid at `[0x13328]` (`iop_loader.cpp:299,:314`), server id `0x00012345`, EE recv buffer `0x3B3000`, IOP request `0x14A00` / result `0x14A90` / reply `0x15310` (`iop_stubs.h` STREAM-RPC-HLE note; `sif_rpc_handlers.cpp:2990, :5006`), heartbeat handler `0x136c`, `ChangeThreadPriority` call site `0x1115c`, the cd-read RPC thunk `0x128a8` | — |
| `lti_protocol.h/.cpp` | `LtiProtocol` — the `0x12345` RPC face: `HandleCustomLoaderRecompiled` glue (send-buffer copy → recompiled `main_handler` → retire → result copy-back to `0x3B3000`), the batch command table (e.g. cmd 115 `slot_multi_config`, 9 params, `:2103/:2135`), `hle_stream_cd_read_rpc` (the real body for the empty CD-read RPC thunk at IOP `0x128a8`, emitted as a call by the frozen tool; `iop_stubs.h:33-39`), `RegisterEeStreamRpcServer` use | `sif_rpc_handlers.cpp:2981-2999+` (`HandleCustomLoaderRecompiled :2993`), `:1854-2980` legacy HLE streamer (NOT transplanted — `USE_IOP_RECOMPILER 1`, `:37`), `:3199` | `PS2_SND_PAUSE_DEFER` (`:3199`) |
| `feed_contract.h` | — (contract constants + the IOP bank table) | the five contract points `sound_core.cpp:98/:105/:810/:1122`, `voice.h:88`; `snd_bank_ledger.cpp` IOP table half — completed HERE with `SpuCore` (§2.7) | — |

### 2.11 `games/<t>/hle/workarounds/` — 6 rows, each with its retirement gate (ledger rows extend the README table)

| New file | Class / hook | OLD source | Flags | Retirement gate |
|---|---|---|---|---|
| `piracy_bypass.h/.cpp` | `PiracyBypass` — `0x181490` (GAME code, below the SDK band → HOOK §3.5 forbids an SdkBind row): body writes 0 to the "check failed" flag; installed through the manifest's address-keyed TITLE lane like `free_list_0x181560` | `sif_hle.cpp:745-778` (`hle_piracy_skip_body :747`, install `:772`; §4.4 cites `:710-732` — drift, §8.2) | — | with `IsoFs`/`CdvdmanModule` faithful (disc type, `sceCdGetDiskType`, sector reads), run coldboot with the hook UNINSTALLED: the flag word stays 0 and `mem.mhash` is unchanged → retire (EQUIVALENCE §7 record if the stream moves) |
| `rendinit_double_init.h/.cpp` | `RendinitDoubleInit` — `0x1b36b8`: before the SECOND boot-time run of the render/DMA-packet init, free the first run's ~2 MB of DMA-tag arrays via the game's `FUN_00181590` | `main.cpp:4315-4350` (§4.4 cites `:4184-4200` — drift); HOOK §3.1 row | — | root fix at the `ee/hle` boot-environment layer (ONE init path, as retail — PCSX2 ground truth: `FUN_001b36b8` runs once); retires TOGETHER with `type_registry_append` when a coldboot leg with both hooks uninstalled runs `0x1b36b8` exactly once and the leg re-freezes under EQUIVALENCE §7 |
| `type_registry_append.h/.cpp` | `TypeRegistryAppend` — `0x268440`: append the game's own populate call (`FUN_0025c6f8`, "Particle" prefix) after the ELSE branch (bit `0x10000` rb-mode) | `main.cpp:8096-8140` (§4.4 cites `:7989-8020` — drift); HOOK §3.1 row | — | joint with the row above (the appended call is one of the two RENDINIT invocations): retire when the populate fires without the hook under the retail-faithful boot path |
| `async_read_dest_preserve.h/.cpp` | `AsyncReadDestPreserve` — `0x2ad110`, dest-preservation HALF ONLY: save/restore the destination across the original completion body for oversized reads (330 KB atlas through the 64 KB stream buffer); the hold branch co-resident in OLD's hook is `LoaderModel` (§2.5), not this file | `main.cpp:6936-7234` (`install_dual_hook(0x2ad110) :6987`; §4.4 cites `:10063-10078` — drift); HOOK §3.1 row | — | when `ReadEngine` delivers THROUGH the stream buffer, chunked as retail (incl. the oversized-read overflow semantics, HOOK §3.9), run the bullseye-atlas leg with the hook uninstalled: dest bytes equal → retire |
| `allocator_reentry.h/.cpp` | `AllocatorReentry` — `0x181560` family: the re-entry guard around the pool allocator from the loader's windowed-read completion path (~935 re-entries per boot); composes with Phase 3's `free_list_0x181560` on the same address (install ORDER is a ledger item, fixed by `mem.mhash`) | `main.cpp:3187-3251` META-ARENA install region + `hle_heap.cpp:1816, :3039-3050` re-entry notes (§4.4 cites `hle_heap.cpp:719-768`, which now reads `ForwardStreamRegisterFile` — the exact body is pinned at transplant, §8.2) | — | retires with the META-ARENA rows (README ledger row 1): boot + level legs with arena, free-list hooks and this guard disabled match OLD `mem.mhash` |
| `../loader_sync.cpp` (title-data half of the completion-visibility model — HARDWARE/TIMING HLE per HOOK §3.6, NOT a workaround; carried in this ledger because the task lists it and its parameters are the game's) | — (data: polled addresses of the `FUN_002ad110` class, hold thresholds, the module-mirror hold) | `hle/loader_sync_hle.h` title half; `sif_rpc_handlers.cpp` `HandleCustomLoaderRecompiled` title constants | `PS2_LOADER_HLE` (named on `LoaderModel`) | shrinks to the latency model alone when `ReadEngine`+`DeliveryMeter` are retail-faithful: the `tests/oracle` loader trace (`tools/loader_oracle_check.py` port) equal with the title hold parameters removed → row closes |

### 2.12 Row count per group

| Group | Rows |
|---|---:|
| 2.1 IOP kernel | 6 |
| 2.2 IOP memory / context / heap / loader | 4 |
| 2.3 `iop/hle` Sony libs (12 imported ∪ 7 disc-side stock modules = 18 dirs; `cdvdman` is IOPRP300, not disc-side) | 18 |
| 2.4 sif | 5 |
| 2.5 cdvd (incl. delivery_meter + delivery_budget) | 8 |
| 2.6 DMAC page `0x10008000` (assertion, no file) | 1 |
| 2.7 sound + sound/hle (+1 diag pointer) | 10 (+1) |
| 2.8 pad/hle | 2 |
| 2.9 mc/hle | 3 |
| 2.10 `games/<t>/iop/stream_irx` | 8 |
| 2.11 `games/<t>/hle/workarounds` (+ loader_sync) | 6 |
| **Total** | **71 (+1 diag pointer)** |

Also landing in this phase but owned by other §4.4 rows (not counted above): `core/diag/{snd_census,
loadspan, stick_trace}` (3 diag pairs, PS2R_DIAGNOSTICS builds), `games/<t>/data/archive_table.h/.cpp`
(DATA.BD/BH layout — TREE-GAP #11; the `hle_heap.cpp` file table), and `tests/{iop,sif,cdvd,sound,pad,mc}`
(step 3 lists them with the oracles).

## 3. IOP dispatch policy

### 3.1 The shape (ARCHITECTURE §7.1, informed by OpenGOAL, restated)

1. The IOP is native code under a cooperative kernel. There is no IOP CPU emulation anywhere in the tree.
2. The Sony kernel surface (thbase / thsemap / thevent / thmsgbx / sifman / sifcmd) is ONE small
   cooperative scheduler, `IopKernel`, over coroutines (`IopCoroutine`, the platform fiber backend the EE
   scheduler already uses).
3. Guest IOP threads are coroutines run by one dispatcher at a time; they park exactly where the real
   single-core kernel switches: SleepThread / DelayThread / WaitSema / RpcLoop / the EE marshal.
4. The title's custom IRX (STREAM.IRX) is statically recompiled native code bound through the same stub
   surface as the stock HLE modules (`iop/hle/<module>/`); OpenGOAL hand-ports theirs (`game/overlord`)
   — same shape, different provenance.
5. RPC is a memcpy plus a wakeup, in both directions. EE→IOP commands run on the module's OWN
   RPC-dispatch coroutine (the thread parked in sceSifRpcLoop); no thread-id impersonation.
6. IOP→EE marshals park in `Wait::Rpc` with a deadline and are serviced at the EE's frame/syscall edges
   (`SifRpcBridge`).
7. Deferred completions (CD reads, SPU voice transfers) and the module heartbeat are kernel events/timers
   fired inside `dispatch()` under the counted-queue contract: N schedules fire N callbacks, never inside
   the issuing pass.
8. ONE host thread at most touches the IOP, and the dispatch policy (`iop.dispatch`) is the only
   host-dependent choice: kernel, modules, RPC and the savestate layout (IOPM section, cross-mode
   compatible; boot-symmetric coroutine stacks) are identical under both policies.

### 3.2 FRAME (`iop.dispatch=frame`) — the proof path

- The EE thread owns dispatch: boundary slot 5 of the `BoundarySequencer` calls `IopKernel::dispatch()`
  once per SyncV boundary. No IOP host thread exists in this mode.
- The IOP clock is the frame-derived virtual clock (`iop_clock.h` over the Phase-3 `TickSource` /
  `EmuDomain`): DelayThread, kernel timers and `Wait::Rpc` deadlines are expressed in emu ticks, never
  wall time. When every runnable thread is time-parked, `dispatch()` performs discrete-event forced
  advances of the virtual clock to the next deadline, so the kernel never idles on the wall clock. (How
  far one boundary's dispatch may advance — the natural bound is the next boundary's tick — is fixed in
  §2 with the `iop_clock` row, by reading OLD's kernel; it is not decided here.)
- Consequence: guest-visible IOP scheduling is a pure function of the EE frame stream, which is what
  makes the `mem` and `audio` lanes hash-gatable under `--profile deterministic` (the profile pins
  `iop.dispatch=frame`). Any IOP scheduling-order difference vs OLD under `frame` is a construction bug
  (the OLD kernel is the same code) — never an accepted difference.
- This is the ONLY policy the §6 gate hashes.

### 3.3 REALTIME (`iop.dispatch=realtime`) — designed for play

- ONE IOP host thread (`IopHostThread`) runs the OpenGOAL iop_runner loop: `dispatch()`, then
  condvar-sleep until an EE wakeup (an RPC call or SIF mailbox write from the EE side) or the next
  deadline on the wall clock. Whether the run-token handoff is also a wakeup edge is the BUG-026
  proposal in §3.4, not a §7.1 fact.
- Low audio latency; non-deterministic by design; never hash-gated. Its evidence is a bounded human-play
  run recorded in `PHASE4_IMPL.md`.
- It becomes the play default only when BUG-026 is closed (§3.4). Until then the shipped `Config` default
  stays `frame`, and the flip is a named §7 stage item.

### 3.4 BUG-026 root-cause item (`docs/bugs/BUG-026-iop-worker-nondeterminism.md`)

- Recorded signature (CONSTRUCTION §4.4): IOP kernel REALTIME boot stall — the RPC-dispatcher tid 1
  `DelayThread(20 µs)` poll ⇄ the EE loader spin holding the run token. The IOP side polls for CD
  completion in 20 µs delays; the EE side spins on completion visibility while holding the token;
  neither reaches a point at which the other is allowed to make progress.
- Root-cause work (a diagnosis, not a patch): with the `PS2_IOPSCHED_DIAG` census on both policies,
  establish WHICH progress edge is missing under realtime — the CD worker's delay-poll deadline is not
  serviced while the token is held, or the EE spin never reaches the syscall/frame edge at which
  marshals are serviced and dispatch may run. Record the edge in the bug entry.
- The plan: make the CD worker delay-poll and the EE loader spin MEET AT A DEFINED BOUNDARY. The
  completion-visibility model (`cdvd/hle/loader`, HOOK §3.6) names the single point at which a read
  becomes visible to the EE. The CD worker's poll is a kernel timer whose deadline lives in the kernel
  clock (§3.2 emu ticks / §3.3 wall clock) and fires inside `dispatch()`. The EE loader spin is a wait on
  that same point — a syscall/frame edge that services `Wait::Rpc` and lets dispatch run — rather than a
  busy spin under the token. Under `frame` the two meet at boundary slot 5; under `realtime` they meet at
  the token-handoff/wakeup edge. One code path serves both: the policy only chooses who calls
  `dispatch()` and which clock the deadline is in.
- Proof of closure: (i) all `frame` legs unchanged (lanes identical to before the fix); (ii) a REALTIME
  cold boot reaches the title screen in a bounded human-play run; (iii) the BUG-026 entry updated with
  the root cause and the missing edge. Closing BUG-026 flips the `iop.dispatch` default to `realtime`
  (§3.3).

### 3.5 Pre-registered decisions for §3

- `PS2_IOP_KERNEL` NORM ON (only the kernel arm is written). `PS2_IOP_DISPATCH` → `iop.dispatch` SETTING
  with values `{realtime, frame}`; dispositions of the other §4.4 rows are listed in §5.
- Thread parks are the guest's own wait points; no host-side yield primitive is added for the kernel's
  convenience (the OLD `GuestMonitorYield` is not transplanted).
- Delays are never wall-clock under `frame`; a `DelayThread` that would need wall time to make progress
  is a construction bug, found by the forced-advance rule, not by a timeout.

## 4. Oracle and test strategy

Sources for this step (nothing else was read): `STAGE_RULES.md`; `FLAG_LEDGER.md` (the P4 master-table
rows + the SETTINGS section) and `FLAG_PHASE_COUNTS.txt` (P4 block); `tools/flag_phase_counts.py` (the
attribution rule) and `tools/gen_settings_table.py` (`LEDGER_PARENT`), `docs/filemap/settings_table.tsv`;
OLD `tests/` listing + `tests/CMakeLists.txt` `add_test` names + the header comments of the Phase-4 test
files; OLD `verify/{verify_boundary.cpp,audio_hasher.h,memory_checkpointer.h}`, `gs_hle.cpp:690-744`
(boundary order), `iop_stubs.cpp` heartbeat/FIX-C sites, `sound/sound_core.cpp:196`, `sce_hle/libsdr.cpp:113`;
NEW `verify/{run_leg.py,diff_legs.py,battery.py,PROFILE.md}`, `verify/baseline/*` (12 legs), `engine/core/
verify/*`, `engine/ee/timing/boundary_sequencer.h`, `engine/core/config/config.h`, `tests/core/
check_getenv_sites.py`, `tools/regen_from_frozen.sh`, `PHASE3_IMPL.md` (DerivedClocks table), CONSTRUCTION
§4.4 proof gate + §A.1 STREAM.IRX paragraph; OLD binary/file md5s re-verified today (`md5 -q`, read-only).

Principle (STAGE_RULES): transplant = same observable behaviour; the proof is the harness (`verify/`) and
the oracles below, never resemblance to OLD code. Every oracle here is one of: (a) an OLD unit test ported
by file, (b) a frozen byte oracle (regen md5s, ISO extraction), (c) a frozen hash stream (`verify/baseline/`),
(d) a paired OLD-vs-NEW diagnostic census under `frame`. A difference where OLD is wrong follows
EQUIVALENCE §7 — never a tolerance.

### 4.1 OLD test ports, by file (`tests/{iop,sif,cdvd,sound,pad,mc}`, ctest names kept where OLD had one)

OLD `tests/` holds 116 entries; the Phase-4 ones are exactly the files below (found by grepping OLD
`tests/` for iop/sif/cdvd/iso/sound/vag/snd/pad/libmc/loader symbols). Three groups the task names have
NO OLD unit test — sif, cdvd/iso, pad protocol — and get NEW tests (marked NEW); the reason is stated so
nobody looks for a port that does not exist.

| OLD file (ln) | OLD ctest | NEW file → ctest | What it pins / what changes |
|---|---|---|---|
| `tests/test_iop_kernel.cpp` (352) — links `iop_kernel/{iop_kernel,iop_coroutine}.cpp` + the fiber source | `IopKernelUnits` | `tests/iop/test_iop_kernel.cpp` → `IopKernelUnits` | 8 cases verbatim: `TestPriorityOrder` (lower number first, FIFO among equals), `TestWakeupBanking`, `TestSemaFifoVsPrio` (FIFO-vs-priority wake order, direct handoff), `TestEventFlagModes` (AND/OR/clear-on-match, single-waiter attr), `TestMbx` (send/poll/receive, blocking receive), `TestDelayAndEvents` (DelayThread order under forced virtual-clock advance; counted-queue events never fire in the issuing pass), `TestPeriodicTimer` (rearm semantics), `TestFrameDeterminism` (two fresh kernels, same scripted workload under the FRAME clock → identical effect sequences; `RunScriptedWorkload`). Links `IopKernel` + `IopCoroutine` over the tree's platform fiber layer. The thevent/thmsgbx cases justify keeping those APIs on `IopKernel` with no module dir (§8.2 item 1). |
| `tests/test_iop_sysmem_heap.cpp` (194) | `IopSysmemHeap` | `tests/iop/test_sysmem_heap.cpp` → `IopSysmemHeap` | Vector 1 boot replay (the measured boot allocation sequence ends with largest-free ≥ 559,104 = retail `DAT_12ef0 0x88800`; floor `0x28000`; module `.bss` end `0x20C80`; the worker-stack band `[0xE0000, 0x100000)` never allocated), vector 2 dialogue-class start (2×`0x40000` ring pair post-boot). Header-only OLD → `SysmemHeap` class, same numbers. |
| `tests/test_iop_sysclib_map.cpp` (107) — reads the GENERATED `.cpp` via `IOP_RECOMPILED_CPP_PATH` | `IopSysclibMap` | `tests/iop/test_stream_irx_abi.cpp` → `IopSysclibMap` + `StreamIrxStubSurface` | S44 pin on the committed generated file: ordinals `{12,14,20,23,27,29,34}` = `{memcpy,memset,strcat,strcpy,strlen,strncmp,strstr}`, none of the five misbound names (`memcmp/prnt/strchr/index/strrchr` — grep-verified absent today), no `UNKNOWN STUB` marker. NEW half (§8.2 item 2): every `iop_stubs::` name the `.cpp` references (74 distinct today, `libsd_stub_0..15`, kernel/sysclib/ioman/cdvdman/sifcmd/sifman names, `hle_stream_cd_read_rpc`) resolves in `StubSurface` (compile-time: the TU links; plus a name-list check against `stub_surface.h`), and exactly 5 `getenv` sites (all `PS2_STRMQ`) — the lint exemption count. |
| `tests/test_libmc.cpp` (521) — links real `libmc.cpp` + `memory.cpp` + `fastmem_stubs.cpp` | `LibMcSaveFlow` | `tests/mc/test_mc_save_flow.cpp` → `LibMcSaveFlow` | Replays the captured in-game save sequence (`FUN_002a6b68`/`FUN_002a4b30`) against a fresh temp card root: phase 0 unbound gate (−100) + idle `sceMcSync` (−1); phase 1 `sceMcGetInfo` watchdog (first −1 then 0; mode-1 busy holds = `cdvd.mc_latency_polls`, mode-0 blocking); phase 2 the failing trail on an empty root (7× `sceMcGetDir("/BASLUS-12345/*")` → −4 …); phase 3 new-save continuation (Mkdir 0 → GetDir 1 …); phase 4 reopen/read-back (Open RDONLY, Read 17 byte-exact). Links Phase-3 `Memory` + `McHostStore` + `McCommandModel` + `libmc_bindings`; `mc_dir` = the temp root. |
| `tests/test_vag_decoder.cpp` (236) | `VagDecoder` | `tests/sound/test_vag_decoder.cpp` → `VagDecoder` | Synthetic ADPCM blocks vs the documented scheme (`F0={0,60,115,98,122}`, `F1={0,0,-52,-55,-60}`, low nibble first, s16 clamp). Verbatim. |
| `tests/test_sound_voice.cpp` (359) | `SoundVoice` | `tests/sound/test_voice.cpp` → `SoundVoice` | Pitch stepping, block/loop mechanics, ENDX latch, LSAX auto-latch (LOOP_START), envelope phases, NAX/ENVX readbacks the stream pacer polls; the `voice.h:88` feed-contract point (§2.10 `feed_contract.h`) is pinned here. |
| `tests/test_sound_state.cpp` (381) — needs SDL lock stubs in OLD | `SoundState` | `tests/sound/test_sound_state_codec.cpp` → `SoundState` | `Voice` Export/Import bit-exact across every envelope phase and mid-block state; `SoundStateCodec` full-core round trip (2 MB SPU RAM + 48 voices + shadows + latches + handlers) bit-exact; reject truncation / bad guard / trailing garbage; the FMV FIFO (gate, BVOL scaling, consumption accounting, flush) → `FmvStream`. No SDL in NEW: the stubs vanish. |
| `tests/test_sound_ring.cpp` (157) | `SoundRing` | `tests/sound/test_command_ring.cpp` → `SoundRing` | The bounded MPSC command ring (`sound/ring.h`, on the P4 pattern): Pop never blocks / never tears under N producers (checksummed payloads); overflow drops the OLDEST and counts; FIFO for the kept prefix. In NEW it carries `SpuCore` commands from the `IopHostThread` under `realtime`; under `frame` it drains inline (single thread). One class `CommandRing`, `engine/sound/command_ring.h/.cpp` (+1 row to §2.7 at transplant, recorded in `PHASE4_IMPL`). |
| `tests/test_snd_wire_governor.cpp` (274) | `SndWireGovernor` | split: `tests/sound/test_snd_census_walk.cpp` (diagnostics build) + `tests/iop/test_lti_protocol.cpp` → `SndWireGovernor` | (1) `snd_census` StreamBatch record walk: cmds `0x101..0x105` are real (pre-dispatcher `FUN_00012504`), decoded AND the walk continues; a genuinely out-of-range cmd stops it → `core/diag/snd_census` (compiled under `PS2R_DIAGNOSTICS`, so this half is a diag-build test). (2) the S46 bank-chain governor split policy and the cmd-1 wire decode pin → `LtiProtocol`/`BankLedger` (product code). |
| `tests/r140_budget/test_r140_budget.cpp` (`CdvdBudgetMeter`, `ps2_vblank.h`) | `R140Budget` | `tests/cdvd/test_delivery_meter.cpp` → `R140Budget` | 9 cases verbatim: `TestRefill`, `TestExhaustNotReadyTransitions`, `TestNoCarryover`, `TestOversizeLiveness`, `TestMonotonicGuard`, `TestUnmetered`, `TestParse`, `TestLiveWrapperEpoch` (refill only on the epoch edge, never mid-iteration), `TestFifoPassModel` → `DeliveryMeter` with `kCdvdKbPerTick = 256` from `DerivedClocks` (clock 8, §4.3). |
| `tests/test_savestate_format.cpp` §11 (IOPM section: `TAG_IOPM` v1 required, RAM guard + skip, kThreads/kSemas constants "must match iop_stubs.cpp") | (Phase 1 ctest) | `tests/iop/test_iopm_section.cpp` → `IopmSection` | The IOPM codec against `IopMemory` + kernel statics (32 threads / 64 semas), boot-symmetric coroutine re-park after restore (`StreamIrxPlugin`), cross-policy compatibility (a state written under `frame` restores under `realtime` and back). SNDC/SDRX ride `SoundState`. |
| `host_app/input/test_pad_buffer.cpp` (R182 T4; lives OUTSIDE `tests/`, P7 pattern `input/`) | `PadBuffer` | `tests/pad/test_pad_protocol.cpp` → `PadProtocol` (protocol half only) | The only OLD pad test; its SDL/L2-mapping half is Phase 7. Ported half: `PS2PadState` → 128-byte PADMAN slot image byte-for-byte (ACTIVE-LOW inversion; the `+0x02`/`+0x03` byte-order fix; mode ids `0x79`/`0x73`; button-mask bytes `FF FF 03 00`; `+0x58` sequence counter written LAST, the zero pass skips it; READY→READY re-stamps skip the zero pass; buttons as ONE aligned u16), the DISCONN→CONFIG→READY staging timeline (thresholds 8 / 214 in boundaries, never wall), the script overlay applied after the mapping (`InputLog` replay). NEW: `InputLog` round-trip (CONSTRUCTION gate item: record → replay → identical slot images). |
| — (no OLD unit test; `test_e1_dispatch.cpp` only checks `sceSifInitRpc` is in the dispatch table = Phase 3) | — | NEW `tests/sif/test_rpc_router.cpp` → `SifRpcRouter`; `tests/sif/test_sif_state.cpp` → `SifState`; `tests/sif/test_sce_sif_dma.cpp` → `SceSifDma` | Router: bind (client record ↔ sid), call → the owning module face for every sid of the §2.3 table and `SifRpcBridge::RunOnRpcThread` for `0x12345`, `CompletePending` delivers end-callbacks at `MaintDuties` (never inside the issuing call), IOP→EE marshal (`EeRpcServer` park `Wait::Rpc`, `NotifyEeReply` wakes only a parkee, late reply dropped), unknown sid → census line. `SifState`: MSCOM/SMCOM/MSFLAG/SMFLAG + Set/GetReg words through `MmioRouter`; after registration `UnhandledRanges()` excludes `0x1000F200-0x1000F2FF`. `SceSifDma`: descriptor decode → exact byte image in `IopMemory` (and the reverse memcpy of `SifmanModule::SetDma`). |
| — (no OLD ISO test) | — | NEW `tests/cdvd/test_iso_fs.cpp` → `IsoFsByteOracle` | Byte oracle (CONSTRUCTION §4.4): `IsoFs` extracts `SLUS_123.45`, `DATASYS/STREAM.IRX` (md5 `d24082fd3560959a77d18ae9c43db51b`, 116,156 B), `DATASYS/CDVDSTM.IRX`, `DATA.BH` from the ISO and byte-compares against `OLD/game_files/*` (+ md5 pins); positioned sector reads at file start / mid / tail-partial equal the whole-file slices. The ISO path comes from `--game-files`' default (the frozen `OLD/game_files/`), so the ctest is registered only when CMake finds the ISO at configure time (never a red tree on a machine without it); `PHASE4_IMPL` records the run. |
| — (no OLD `StreamReader`/`ReadEngine` test) | — | NEW `tests/cdvd/test_read_engine.cpp` → `CdvdReadEngine`; `tests/cdvd/test_loader_model.cpp` → `LoaderModel` | Deferred reads: chunked apply in schedule order, delivery only at `MaintDuties`, oversized read through the 64 KB stream buffer (HOOK §3.9 overflow semantics), completion as a kernel event (fires next pass). `LoaderModel`: a scripted enq/dlv/rel/cns/yld scenario whose trace satisfies the ported checker (row below). |
| OLD `tools/loader_oracle_check.py` (S57 invariants i–iv on `loader_trace.txt`) | — | `tests/oracle/loader_oracle_check.py` (+ ctest `LoaderOracleSelftest` on a pinned good/bad trace pair) | Invariants: (i) `dlv < rel < cns` per read, (ii) ARM-A releases preceded by a teardown-drain pass after the read's enqueue, (iii) no consume without a delivery mark since the enqueue, (iv) every yield names a genuinely blocked blocker. Consumed by `test_loader_model.cpp` (`PS2_LOADER_ORACLE` TEST row, §5.4). |
| `tests/test_fmv_pace.cpp` (`mpeg_hle::PaceDeadline`), `tests/test_defer_boundary_order.cpp` (GifArbiter) | `FmvPaceDeadline`, `DeferBoundaryOrder` | NOT Phase 4 | Phase 7 (mpeg) and Phase 5 (gif) respectively — listed so the grep hit is not mistaken for a missing port. |

Also landing under `tests/`: `tests/iop/test_iop_clock.cpp` → `IopClockFrame` (NEW: `OnFrameBoundary` +16,667 µs, forced advance capped at `FrameCeilingUs()`, `PumpFromParkLoop` ≤ one quantum — the §3.2 bound closed in §2.1), `tests/iop/test_module_table.cpp` → `IopModuleTable` (NEW: `OnModuleLoaded` runs every started thread to its first block before returning; `PumpAtBoundary` is a no-op under `realtime`; `PumpAfterEeService` resumes exactly the woken `Wait::Rpc` parkee), and — for the §2.12 "also landing" row `games/<t>/data/archive_table` (DATA.BD/BH layout, TREE-GAP #11) — `games/<title>/tests/test_archive_table.cpp` → `ArchiveTableLayout` (NEW: the table parsed from the `IsoFs`-extracted `DATA.BH` equals the constants OLD `hle_heap.cpp`'s file table hard-codes: entry count, per-entry offset/size; runs only with the ISO present, like `IsoFsByteOracle`).

### 4.2 STREAM.IRX byte-oracle regen (`games/<title>/iop/stream_irx/`)

- Command (CONSTRUCTION §A.1, verbatim; cwd pinned to a NEW scratch dir for hygiene — the tool has no
  cwd-relative I/O): `mkdir -p "$NEW/build/regen_iop" && cd "$NEW/build/regen_iop"` then
  `"$NEW/verify/run_bounded.sh" 600 "$PWD/iop_gen.log" -- "$OLD/build-v2/iop_recompiler_tool" --input
  "$OLD/game_files/STREAM.IRX" --output-header "$NEW/games/<title>/iop/stream_irx/
  iop_recompiled.h" --output-cpp ".../iop_recompiled.cpp"`.
- Frozen provenance, ALL re-verified today with `md5 -q` (OLD read-only): tool `32a0c90ce604ddaaf1ba089bf2489c08`
  (1,022,288 B); input `d24082fd3560959a77d18ae9c43db51b` (116,156 B); outputs `iop_recompiled.h`
  `c95059f165f8b68de442169ac932f871` (11,546 B) and `iop_recompiled.cpp` `b2102157c7dfa8285f21eadbb42c41a4`
  (2,491,658 B) — identical in `build-v2` and `build-v2-nog`. A tool/input md5 mismatch means the oracle
  moved: stop, do not emit (the `check_input` rule of `tools/regen_from_frozen.sh`).
- Mechanism: `tools/regen_from_frozen.sh` gains an `--iop` mode (same shape as the Phase-3 EE mode): two
  bounded runs into `build/regen_iop/{a,b}`, `diff -r` empty (regen determinism), then `a/` is copied
  into `stream_irx/` and `MANIFEST.sha256` + `INVOCATION.md` (tool md5, input md5, both output md5s, cwd,
  command, date, the "generated code is never hand-edited" rule) are written. ctest
  `IopRecompManifestIntact` = `regen_from_frozen.sh --check --iop` (re-hash against the manifest); the
  two output md5s are ALSO asserted literally in `test_stream_irx_abi.cpp` so a manifest regenerated
  from a moved oracle cannot pass silently.
- `PS2_STRMQ`: the frozen tool EMITS 5 `getenv("PS2_STRMQ")` arms (`iop_recompiled.cpp:1201, 44389,
  51898, 53720, 55859`), each OFF unless the variable is set. `tests/core/check_getenv_sites.py`
  `GENERATED_EXEMPT` gains `"games/<title>/iop/stream_irx/iop_recompiled.cpp": 5`
  (the dated-exemption form Phase 3 used for `recompiled.cpp: 6`; removed by the in-tree IOP recompiler,
  9.1). The lint stays exact: a 6th site anywhere fails.
- Include compat: the generated header's `#include "../host_app/{iop_context,iop_memory,iop_stubs}.h"`
  resolves through `stream_irx/abi/host_app/` (§2.10). The build itself is the oracle (the TU compiles
  against `StubSurface` with the OLD names); `LintOneClass` exempts the generated pair and the three
  forwarding headers by path, nothing else.

### 4.3 Clocks 7–9 of the Phase-3 `DerivedClocks` table go LIVE here

Phase 3 built the accessors and pinned their purity (`DerivedClocksPurity`, 15 checks); nothing consumed
clocks 7–9 yet. Phase 4 wires the consumers; each gets a unit oracle plus the lane that observes it.

| # | Clock (Phase-3 accessor) | Phase-4 consumer (the live path) | Oracle |
|---|---|---|---|
| 7 | IOP heartbeat — `IopHeartbeatBeats(usec)`, `HeartbeatTicksFor(usec) = max(1, round(usec/16667))` | `TimrmanModule::StartHardTimer` (OLD `iop_stubs.cpp:1440`): compare ticks → `usec = compare*1000/36864`, clamped `[1000, 100000]`; then `IopKernel::StartPeriodicTimerUs(usec, beat)` on `IopClock` (the kernel-timer arm OLD takes under `KernelOn()`, `:1466`). FRAME: `IopClock` advances +16,667 µs per boundary, so the 60 Hz heartbeat (`0x136c`) fires once per boundary — exactly `HeartbeatTicksFor(16667) = 1` (over N boundary PUMPS with the inclusive ceiling of `DrainQuantum` the count is `floor((N+1)·16667/period)` — P4-CEILING-INCLUSIVE); REALTIME: steady µs. Beat body = fresh `IopContext` (`$sp 0x001FFC00`, `$gp 0x13328` from `module_layout.h`), rearm iff `$v0 != 0` | `tests/iop/test_iop_kernel.cpp::TestPeriodicTimer` (rearm) + `test_iop_clock.cpp` (beats over N boundaries = `N / HeartbeatTicksFor(usec)` for usec ∈ {16667, 33333, 100000}); leg-side: the `iopsched` paired census (§4.7) — `hb_beat` count and virtual-clock stamps equal OLD's under `frame`; the `mem` lane observes it indirectly (the heartbeat drives the CD-job engine's per-frame duties). |
| 8 | CDVD latency / refill epoch — `CdvdEpochTick()`, `CdvdBudgetKbSince()`, `kCdvdKbPerTick = 256` | `DeliveryMeter` (refill only on the epoch edge, `ps2_vblank.h:100-101` rule) meters what `ReadEngine` may make visible per frame; `LatencyModel` (`cdvd.latency_model` instant/modelled × `cdvd.latency_scale`) times the DVD seek/transfer stall; the IOP-side completion due-times (`kCdReadCompletionUs = 3000`, `kSpuTransCompletionUs = 300`, §5.1) are kernel events on `IopClock`. Under the deterministic profile: `latency_model=instant`, `latency_scale=0`, `mc_latency_polls=0` (settings_table det column) — the modelled arm is exercised by unit tests and the play smoke only | `R140Budget` (9 cases, §4.1); `tests/cdvd/test_latency_model.cpp` → `CdvdLatencyModel` (instant = 0 stall; modelled × scale monotone; scale 0 ≡ instant for the stall term while the completion protocol order is preserved — the OLD `hle_heap.cpp:2765` comment's contract); leg-side: `mem.mhash` (delivery visibility is RAM content). |
| 9 | audio — Phase 3 recorded `AudioRateMasterIsDevice() == true` (NOT derived; the device clock never enters the domain, Phase 7) | Refined, not contradicted: under `time.tick_sync` there is NO device (`platform/sdl/audio_sink` is Phase 7) and the `Mixer` is pumped 800 stereo frames per boundary (`48000/60`) inside the `Step::Verify` slot (§4.5) — the audio lane is a pure function of the boundary count in THIS phase. The play path (device master) is Phase 7's; `AudioRateMasterIsDevice()` keeps describing it | `audio.ahash` windows (§4.5); `tests/sound/test_mixer_pump.cpp` → `MixerTickPump` (800 frames per pump; 6 pumps = one 4800-frame window; a silent core hashes to `0x0a6fb8af536edb25` = the coldboot silence window — the FNV-1a64 of 4800×2 zero floats, checked against `AudioHasher` directly). |

### 4.4 `mem.mhash` — coldboot checkpoint 0 + per-checkpoint anchors vs `verify/baseline/`

- Definition (unchanged from Phase 1's `MemoryCheckpointer`, Phase 3's spans): at SyncV boundary `n`
  with `n % 60 == 0`, FNV-1a64 over main RAM (32 MB) + SPAD (16 KB) + the HLE meta arena (2 MB @
  `0x02000000`), line `n,0x<hash>`. The frozen streams (`README_FREEZE.md`, 2026-09-25/26, env_hash
  `0x9a0f815a02d994d4`, 3-rep stable) have 17 checkpoints for coldboot (`0,60,…,960`; `frames=1494`) and
  10 per anchor (`0,…,540`; `frames=600`) — 12 legs: coldboot + 8 `ps2state_slot0_f*` + 2 `slowspot_*` +
  `artdet_f37152`. Checkpoint 0 = the FIRST boundary: for coldboot that is boundary 0 of §1.2 (the first
  `sceGsSyncV`, RAM after IOP reboot + module loads + MC check up to the first frame) — the phase's first
  claim; coldboot `0,0xeec37d056857657b`. For an anchor it is the first boundary after restore, one frame
  of execution past `restore.mhash` (already 11/11 in Phase 3).
- Claim procedure per leg (the §1.3 stopping rule made mechanical):
  1. `verify/run_leg.py --binary build/app/host/nativeee --tree new --anchor <A> --profile deterministic
     --frames <1500 | 600> --out /tmp/p4/<leg>` (bounded by `run_leg`'s own timeout; `RB_STALL_S=60`).
  2. Pre-claim admissibility from `run.meta`: `finalized=1`, `park=none`, `frames ≥ 0.5×requested`
     (`check_meta`'s non-park path — the park exemption must NOT be what makes the leg pass), for anchors
     `restore_mhash_ok=1` and `relaunch=2` (the Phase-3 value), `unhandled_mmio_pages` ⊆ the allowed
     Phase-5–7 pages (no `0x1000F2xx`, no SPU2 page — §2.6).
  3. `verify/diff_legs.py --cross-engine --lanes mem verify/baseline/<leg> /tmp/p4/<leg>` → either
     `IDENTICAL` (claim = all checkpoints, 17 or 10) or `DIVERGENT` with the FIRST divergent checkpoint
     `N` (length mismatch counts at the shorter length): claim = checkpoints `< N`.
  4. Attribution of `N` (mandatory, §1.3): the `memdiff` diagnostics-build category writes a per-64 KB-page
     FNV list at each checkpoint (`<leg>/mem.pages` under `--diag memdiff`), the same leg on OLD's binary
     cannot emit it (frozen), so the NEW page list at `N` is compared with the NEW page list at `N−60` and
     the pages that changed are mapped to their WRITER: SPR DMA destination (sink a), VU1 copy-back
     destination (b), GS local→host destination (c), IPU output (d) — the writer is found by the Phase-3
     `MmioSink` census of the kicks swallowed in that window plus the diag census of §4.7. `PHASE4_IMPL`
     records per leg: last claimed checkpoint, `N`, attribution. Not attributable to (a)–(d) → construction
     bug, phase blocked. Attributable → next phase's work, not an accepted difference.
  5. `diff_legs.py` gains `--claim mem=<checkpoint>,audio=<window>` (exit 0 iff each named lane is
     IDENTICAL or its first divergence index ≥ the claimed index; the verdict line prints the claim), so
     the §6 gate is one command per leg and a shrunk claim is visible in the command line, never in a
     tolerance. `battery.py` Phase-4 mode = coldboot ×3 + 11 anchors ×3 with `--lanes audio,mem` and the
     per-leg claims from `PHASE4_IMPL` (ADMISSIBLE = reps identical; the cross-engine column reports the
     first divergence index).
- Run length vs claim length (§8.1): anchors RUN 600 frames (≥ 60 boundaries past `0x1e7fa4` is the
  §1.2 floor; the baseline has 600) and CLAIM the checkpoint prefix; coldboot RUNS 1500 (baseline 1494
  presents) and claims its prefix. A run shorter than the baseline's line count is DIVERGENT by the
  length rule — a short run is never a short claim.

### 4.5 `audio.ahash` — windows, the Verify-slot pump, and the coldboot silence prefix

- Definition (Phase-1 `AudioHasher`): one window = 4800 stereo frames (100 ms @ 48 kHz) = 6 boundaries ×
  800; FNV-1a64 over the little-endian bytes of the interleaved float pairs; `Finish()` emits a partial
  tail with its frame count folded in. Baseline: coldboot 162 windows, each anchor 100.
- Pump ORDER — the §8.1/§8.2 open item is CLOSED by reading OLD `gs_hle.cpp:696-744`: `eetimers::
  PumpAtBoundary` → (dead `PumpHeartbeatAtBoundary`) → `maint::RunGuestDuties` + `PadInput_Update` →
  `iop_kernel PumpAtBoundary` → `ServiceEeStreamRpc` + `PumpAfterEeService` → `savestate::AtFrameBoundary`
  → `verify::OnEeBoundary()`, and INSIDE `OnEeBoundary` (`verify_boundary.cpp:59-84`) the order is
  `sound::MixInto(800)` → `hasher.Push` → `mem.MaybeHash`. So the mixer pump is LAST, in the Verify slot,
  after IopDispatch (5), EeStreamRpc (6) and Savestate (7), and before the memory hash. NEW form: the
  composition root's `Step::Verify` body is `mixer.Pump(800)` then `VerifyHost::OnBoundary(mixer.Output(),
  800, spans)` (audio push, then the checkpoint) — the `Mixer` composes INTO the existing Verify slot;
  `MaintDuties` is untouched and no slot is added. Two consequences OLD fixes: the pump runs whenever
  `time.tick_sync` is on, armed harness or not (OLD `snd_tick = TickClocked() && Enabled()` is independent
  of `Armed()` — the NAX/ENVX/ENDX readbacks the game polls depend on it), and with `audio.enabled=false`
  the lane is empty (no pump), as in OLD.
- Coldboot: windows 0–141 hash `0x0a6fb8af536edb25` = 4800 frames of silence (a trivial claim, stated as
  such); window 142 (`0x6e5c320f7c150766`) is the first non-silent window and MUST be attributed before it
  is claimed: SPU2 voices keyed by STREAM.IRX (claimable: EE+IOP+SPU2 work) vs the FMV PCM FIFO
  (`FmvStream` fed by the MPEG side = sink (d): the claim stops at 141 by rule). The `snd_census`/`konlog`
  diag on the NEW leg says which (a KON before window 142 with no FMV-FIFO write = voices). `PHASE4_IMPL`
  records the attribution and the resulting claim.
- Anchors: non-silent from window 0 on all 11 (checked today), so the claims are real from the first
  window — f46203 `0,0xbed6341167ff6373`, `1,0x67c6373ea7333e9d`, `2,0x71b3c3c9fad15b04`, …; the same
  stopping rule and `--claim audio=<window>` apply; a window that diverges before the `mem` lane does is
  attributed the same way (voice state written by an untransplanted sink, or a `Mixer` order bug).

### 4.6 `run.meta` park keys — retired by phase end; two census keys added

- Phase-3 keys (`app/host/leg_closer.cpp`, `ee_bootstrap.cpp`): `park`, `park_pc`, `park_tid`,
  `park_detail`, `unhandled_mmio`, `unhandled_mmio_pages`, `restore_mhash`, `restore_mhash_ok`,
  `relaunch`. The reason `sif_rpc_absent` (`KernlSifRpc::ParkReason()`) and `KernlFileIo::ParkIopDependent`
  (`sceOpen/sceClose/write/get_iob`) are the parks Phase 4 exists to remove: `SceSif` rebinds every
  `KernlSifRpc` row, `IomanModule`'s FILEIO face (`0x80000001`) + the libcdvd/ioman bindings rebind the
  file-I/O rows. By phase end: every one of the 12 gate legs reports `park=none` (with the closer's normal-end `park_pc`/`park_tid`/
  `park_detail` values — `guest_returned`, `relaunched_threads_exited`, or the frame-cap end), `run_leg.py`'s `is_park_leg` exemption is exercised by NO gate leg (a leg that still parks fails
  §6 — the frames-floor path applies), and `unhandled_mmio_pages` contains no SIF/SPU2 page. The KEYS stay
  in the writer (a later-phase park such as an unresolvable VU microprogram still needs them); what
  retires is every Phase-4 park VALUE. The Phase-3 park classes stay in `engine/ee/hle` untouched
  (D-SEQ-2); rebinding is by manifest registration, not by editing them.
- Added keys (the §8.2 item-5 census oracle): `sif_bound=<sorted sids>` (every `sceSifBindRpc` sid the
  leg bound, from `RpcRouter`) and `iop_modules=<names in load order>` (from `IopModuleTable`). Rule:
  `netcnfif` (§2.4), `HandleMtap` (`0x80000901-905`), `HandleEyeToy` (`0x80001400`) are written only if
  a sid outside the §2.3 server table appears in the coldboot or an anchor leg's `sif_bound`; a sid that
  no disc IRX can serve appearing there is a construction bug (the disc module set is fixed:
  `runtime-environment.md`). OLD cross-check where an OLD runtime log already exists: `[SIF-BIND-HLE]
  server_id=` lines (`sif_hle.cpp:694`); no OLD leg is run with logging on for this (OLD `logs/` is not
  written by us).

### 4.7 Paired diagnostic censuses (construction oracles under `frame`, never gate lanes)

- `iopsched`: OLD `PS2_IOPSCHED_DIAG=1` emits one stderr line per IOP scheduling event (`[IOPSCHED]
  vf=<n> <evt> a b t=<virtual µs>`: thread start/park/wake, sema wait/signal, delay, `hb_beat`, CD
  schedule/fire, RPC dispatch, EE marshal; `iop_stubs.cpp:144-175`, kernel `iop_kernel.cpp:512+`). NEW
  emits the same grammar from the `iopsched` category. `run_leg.py` gains `--diag iopsched` (legacy tree:
  the env var; new tree: `--diag iopsched`; stderr captured to `<leg>.diag`). Under `frame` the two
  streams must be line-identical for the claimed prefix (the OLD kernel is the same code, §3.2); the
  first differing line names the construction bug. A diag leg's `env_hash` differs from the baseline by
  construction (the var is in `CollectEnv`), so diag legs are compared with each other, never with
  `verify/baseline/` (the README_FREEZE precedent: "EESCHED 1224 / IOPSCHED 1088 lines md5-identical").
- `snd_census` (+ `konlog`, `musq`) and `loadspan`: NEW-only censuses used for the attributions of §4.4
  step 4 and §4.5 (which voice keyed when; which load span was open at checkpoint `N`).
- P3-BOOT-WAITSEMA (3F′ handoff): the OLD boot trace shows `WaitSema(68)` before any `CreateSema`; the
  NEW coldboot leg's `iopsched`/`eesched` streams are read for the same order at the same vf — recorded in
  `PHASE4_IMPL`, adjudicated by the `mem` claim (a semaphore-order difference that moves RAM is a bug).

## 5. Flags consumed (46 rows: 17 NORM, 4 SETTING, 12 DIAG, 3 TEST, 10 DELETE — the 42 of the plan + the four §5.6 ledger gaps added by 4A′)

The heading count is `FLAG_PHASE_COUNTS.txt` P4 — `4 | 17 | 4 | 12 | 3 | 10 | 0 | 46` since 4A′ added the four §5.6
ledger rows (`PS2_SND_TICKCLOCK` NORM, `PS2_FIXC_TICKCLOCK` DELETE, `PS2_HB_BOUNDARY` DELETE, `PS2_IOPK_WATCH` DIAG); at
planning time the attributed row read `4 | 16 | 4 | 11 | 3 | 8 | 0 | 42`, which is what the tables of §5.1–§5.5 list (
reproduced today by running the tool's own attribution over the ledger (`/tmp/s3/p4_touched.py`, a helper
over `tools/flag_phase_counts.py`): the 42 names below are exactly the `P4 NORM/SETTING/DIAG/TEST/DELETE`
lists of that file. §8.2 item 3 asked whether the heading moves — it does NOT: the flags §2 names beyond
these 42 are attributed to Phases 1/3 (consumed there, only READ here) or have no ledger row at all; both
groups are §5.6, outside the count. "OLD site" is TODAY's line (re-grepped, OLD read-only); where the
ledger's line differs it is in parentheses (line drift, §8.3). "Applied form" is the STAGE_RULES form:
NORM → only the winning arm is written; SETTING → the `Config` field (peak default / deterministic value
from `settings_table.tsv`); DIAG → a `core/diag` category under `PS2R_DIAGNOSTICS`; TEST → harness/test
channel only; DELETE → not transplanted. Zero new `getenv` sites (the ONE site stays `ConfigLoader`).

### 5.1 NORM — 16 (write the winning arm only)

| Var | OLD site (today) | Winning arm → applied form | §2 row |
|---|---|---|---|
| `PS2_IOP_KERNEL` | `iop_kernel/iop_module_table.cpp:21` (ledger `:11`) | ON: the cooperative `IopKernel` is the only hosting; the per-guest-thread `std::thread` bodies, `GuestMonitorYield`, the S43 dispatch-bracket impersonation and the `KernelOn()` latches are not written (§1.5, §3.5) | 2.1 `iop_kernel`, `iop_module_table`; 2.3 (h)(i) |
| `PS2_HB_TICKCLOCK` | `iop_stubs.cpp:1542` (ledger `:1175`) | The kernel periodic timer (OLD's `KernelOn()` arm at `:1466`): `TimrmanModule::StartHardTimer` → `IopKernel::StartPeriodicTimerUs(usec)` on `IopClock` (FRAME virtual µs = clock 7, §4.3; REALTIME steady µs). The tick-clocked ticker THREAD (`beat_ticks`, `:1542-1560`) and the wall thread it replaced are both dead under the kernel arm — neither is written. Closes §8.1's open arm | 2.3 (j) `timrman` |
| `PS2_SNDREG` | `hle_heap.cpp:685` | ON (S43): registration + the CD-job engine live — heartbeat timer armed, real CD completion; the `=0` "heartbeat inert" bisect hatch (`iop_stubs.cpp:1447`) is not written | 2.5 `loader_model` (site), 2.3 (j) |
| `PS2_SND_PROCBATCH` | `iop_stubs.cpp:1878` (ledger `:1506`) | ON: batched libsd command processing in the `LibsdModule` batch stub (`libsd_stub_8` path); the per-command arm is not written | 2.3 (k) `libsd` |
| `PS2_SND_EAGER_HOLD` | `sound/sound_core.cpp:363` (ledger `:353`) | ON: the eager-KON hold (`eager_kon[vi]++` before the NAX store) — the pre-fix clobber window is not written | 2.7 `sound_core` |
| `PS2_SPU_LAT_US` | `iop_stubs.cpp:2204` (ledger `:1813`) | `LibsdModule::kSpuTransCompletionUs = 300` (constexpr): voice-transfer completion = `IopKernel::ScheduleEventUs(now + 300)`; the env override arm deleted | 2.3 (k) `libsd` |
| `PS2_CD_LAT_US` | `iop_stubs.cpp:2195` (ledger `:1804`) | `CdvdmanModule::kCdReadCompletionUs = 3000` (constexpr): CD-read completion event due on the kernel clock; env override deleted. Distinct from `cdvd.latency_model` (the DVD stall model in `LatencyModel`) — both live | 2.3 (l) `cdvdman`, 2.5 `latency_model` |
| `PS2_CHUNK_APPLY_SCOPED` | `hle_heap.cpp:1644` | ON: the scoped EE-side ordering gate for chunk apply; the S57 per-read pump-hold arm is not written | 2.5 `read_engine` |
| `PS2_CHUNK_APPLY_DORMANT` | `hle_heap.cpp:1694` | ON (`RingCheckOn`): the dormant-ring check during chunk apply; the `0 = strict` bisect arm deleted | 2.5 `read_engine` |
| `PS2_CHUNK_APPLY_VALVE_TICKS` | `hle_heap.cpp:1709` | `ReadEngine::kPumpValveTicks = 90` (the default; OLD's clamp `[8, 1200]`/0 parsing deleted with the env) — in emu ticks, never wall | 2.5 `read_engine` |
| `PS2_CHUNK_APPLY_ORDER` | `hle_heap.cpp:2069` | `ReadEngine::kApplyOrderBoundaries = 1` (fix ON, one boundary); the 0..16 knob deleted | 2.5 `read_engine` |
| `PS2_LOADER_HLE` | `hle/loader_sync_hle.h:99` | ON: the poll-spin sync-wait HLE (yield the slice when the only blocker is gate-held/in-flight; drain-first untouched) = `LoaderModel`'s mechanism; the polled `ra`/addresses (`0x17d724` class) are `games/<t>/hle/loader_sync.cpp` data. The `=0` baseline arm is not written (its behaviour is reproduced by the retirement gate of §2.11 row 6, not by a flag) | 2.5 `loader_model`, 2.11 row 6 |
| `PS2_SDR_BLOCK_MS` | `sce_hle/libsdr.cpp:113` | `SdBlockTrans::kVirtBlockUs = 8000` on the EMU/kernel clock — OLD measures the 8 ms on `steady_clock` (`libsdr.cpp:244-258`), the one wall term in the row's family; NEW expresses it in emu µs (8,000 < 16,667, so "complete at the first poll one frame later" is preserved). If a leg ever polls inside the same frame the streams move: EQUIVALENCE §7 record, never a wall clock | 2.7 `sd_block_trans`, `sdr_remote` |
| `PS2_SND_FMVPACE` | `sce_hle/libsdr.cpp:143` | ON: FMV completion pacing by ring quota — the unpaced arm deleted (the FMV feed itself arrives in Phase 7) | 2.7 `fmv_stream`, `sdr_remote` |
| `PS2_SND_FMVQ_DIV` | `sce_hle/libsdr.cpp:153` | `FmvStream::kQuotaDiv = 2` (quota = size/2 per completion); the {1,2,4} knob deleted | 2.7 `fmv_stream`, `sdr_remote` |
| `PS2_MC_PERSIST` | `sce_hle/libmc.cpp:96` | ON: `McHostStore` persists under `paths.mc_dir`/`BASLUS-12345`; the ephemeral `=0` arm deleted (tests use a temp `mc_dir`) | 2.9 `mc_host_store` |

### 5.2 SETTING — 4 (`Config` fields; all four already exist in `config.h` / `settings_table.tsv`)

| Var | OLD site (today) | Field (peak / deterministic) | Note |
|---|---|---|---|
| `PS2_IOP_DISPATCH` | `iop_kernel/iop_module_table.cpp:29` (ledger `:19`) | `iop.dispatch` enum `{realtime, frame}` (`realtime` / `frame`) | §3.3 rule: the shipped default is `frame` until BUG-026 is closed, then flips to `realtime`. `config.h:75` today says `Realtime` (Phase 1 wrote the ledger's peak): the stage that lands `IopModuleTable` sets `Frame` and the BUG-026 closure stage sets `Realtime` back — both one-line `config.h` edits plus `settings_table.tsv` regen (`SettingsTableReproducible`). The deterministic profile pins `frame` either way. Consumed by `IopModuleTable` only |
| `PS2_SOUND` | `sound/sound_core.cpp:187` | `audio.enabled` bool (`true` / `=`) | `SpuCore`/`Mixer` constructed and pumped iff on; off = empty audio lane (as OLD). No device in this phase regardless (§1.5) |
| `PS2_DVD_LATENCY_SCALE` | `hle_heap.cpp:2765` | `cdvd.latency_scale` float (`1.0` / `0`) | OLD's mode default ("explicit wins, else freerun → 0.0") is already `ConfigValidator`'s rule (`engine.pacing=freerun` → instant unless explicit). Consumed by `LatencyModel` |
| `PS2_MC_LATENCY` | `sce_hle/libmc.cpp:196` | `cdvd.mc_latency_polls` int (`4` / `0`) | The documented divergence (real MCSERV is multi-frame). Consumed by `McCommandModel` (mode-1 busy holds = polls) |

### 5.3 DIAG — 11 (`core/diag` categories, `PS2R_DIAGNOSTICS` builds; enabled by `--diag <cat>[=spec]`)

| Var | OLD site (today) | Category → applied form | §2 row |
|---|---|---|---|
| `PS2_IOPSCHED_DIAG` | `iop_stubs.cpp:152`, `iop_kernel/iop_kernel.cpp:519` | `iopsched`: the `[IOPSCHED] vf=… evt a b t=…` event stream (same grammar as OLD — it is the paired-leg oracle of §4.7) + the kernel's rate-limited `[IOPK-EVQ]` probe | 2.1, 2.3 (h)(i) |
| `PS2_SND_CENSUS` | `snd_census.h:45` | `snd_census`: the STREAM.IRX cmd census (StreamBatch record walk, §4.1 `SndWireGovernor` half 1); title cmd table = data | 2.7 diag pointer |
| `PS2_SND_KONLOG` | `iop_stubs.cpp:1930, :2030` (ledger `:1558/:1658`), `sound/sound_core.cpp:347, :483` (ledger `:337/:473`) | `snd_census` sub-probe `konlog`: KON/KOFF log with vf stamps (the §4.5 window-142 attribution instrument) | 2.3 (k), 2.7 `sound_core` |
| `PS2_MUSQ` | `iop_stubs.cpp:1758, :1785, :1822, :1886` (ledger `:1386…`) | `snd_census` sub-probe `musq`: ENVX-poll trace of the music-stream voices (throttled, read-only); the `_VFMIN` floor is dropped (§5.5) | 2.3 (k) |
| `PS2_LOADSPAN` | `loadspan.h:98` | `loadspan`: load-span census (heartbeat quantum `NoteHeartbeat`, span open/close) → `core/diag/loadspan` | 2.5 `loader_model` |
| `PS2_LOADSPAN_DRAWMAX` | `loadspan.h:105, :106, :115` | `loadspan` parameter `drawmax` (its default from `loadspan.h`; `--diag loadspan=drawmax:<n>`) — no separate category | 2.5 `loader_model` |
| `PS2_STICKTRACE` | `stick_trace.h:69` | `stick_trace`: analog-stick trace → `core/diag/stick_trace` | 2.8 `pad_protocol` |
| `PS2_INLAT_ANALOG` | `sce_hle/libpad.cpp:416` | `stick_trace` sub-probe `inlat`: analog-onset input-latency arm (press→served→presented) | 2.8 `pad_protocol` |
| `PS2_HEADWATCH` | `pad_input.cpp:640` (ledger `:604`) | `padwatch`: guest-RAM watch list (head/pos/u32 specs, ≤ 8 entries, spec grammar verbatim) — one category, three spec kinds | 2.8 `pad_protocol` |
| `PS2_POSWATCH` | `pad_input.cpp:589` (ledger `:553`) | `padwatch` (pos kind) | 2.8 |
| `PS2_U32WATCH` | `pad_input.cpp:689` (ledger `:653`) | `padwatch` (u32 kind); read-only — the poke twin is deleted (§5.5) | 2.8 |

### 5.4 TEST — 3 (harness / test channel; no product `getenv`)

| Var | OLD site (today) | Applied form | §2 row |
|---|---|---|---|
| `PS2_PAD_SCRIPT` | `pad_input.cpp:478` (ledger `:476`), `sce_hle/libpad.cpp:374` | The script grammar survives as `InputLog` replay through `harness.inputs` (Phase-1 field; `PS2_PAD_SCRIPT_FILE` is its `LEDGER_PARENT` sub-key, `gen_settings_table.py`) and the recorder `harness.record_input`; the inline env-string form is not written. Round-trip test in `tests/pad/test_pad_protocol.cpp` (§4.1) | 2.8 `pad_protocol` |
| `PS2_LOADER_ORACLE` | `hle/loader_oracle.h:55` | `LoaderModel::SetTraceSink(fn)` (engine API, no config) → the trace grammar `seq kind fs=… [arm=|src=|blk=]`; consumed by `tests/cdvd/test_loader_model.cpp` through the ported checker `tests/oracle/loader_oracle_check.py` (§4.1). Leg-side capture, when wanted, is the `loadspan` diag category writing `<leg>/loader_trace.txt` — no `harness.*` field is added | 2.5 `loader_model` |
| `PS2_SND_SELFTEST` | `sound/sound_core.cpp:247` (ledger `:237`) | The self-test body (synthetic looping ADPCM tone at `0x6000`, filter 0 / shift 4, voice 0 keyed, ~857 Hz at pitch `0x1000`, non-zero RMS windows) becomes `tests/sound/test_spu_core_selftest.cpp` → `SpuCoreSelftest`; no product hook | 2.7 `sound_core` |

### 5.5 DELETE — 8 (not transplanted: var, gate and subject arm)

| Var | OLD site (today) | What goes | §2 row |
|---|---|---|---|
| `PS2_IOPQ` | `iop_stubs.cpp:1603` (ledger `:1231`) | 1/s dump of the STREAM.IRX CD-job engine state words (S43/S48 archaeology addresses) — session probe | 2.3 (l) |
| `PS2_NAXQ` | `iop_stubs.cpp:1845` (ledger `:1473`) | NAX-poll trace for the music-stream voices (every 64th call) — concluded S48 probe | 2.3 (k) |
| `PS2_VTQ` | `iop_stubs.cpp:2319` (ledger `:1894`) | Voice-transfer completion-protocol trace (schedule/fire/poll-retire) — the save-select-freeze discriminator, closed | 2.3 (k) |
| `PS2_MUSQ_VFMIN` | `iop_stubs.cpp:1762, :1790` (ledger `:1390/:1418`) | The vf floor of the `musq` probe; the probe itself is DIAG without a floor | 2.3 (k) |
| `PS2_CHUNKQ` | `hle_heap.cpp:1663` | Chunk-apply trace lines (teardown-drain-first evidence) — the invariant is now the `loader_oracle_check.py` (ii) rule | 2.5 `read_engine` |
| `PS2_SND_PAUSE_DEFER` | `sif_rpc_handlers.cpp:3271` (ledger `:3199`) | OFF-unless-1 experiment: hold the cmd-10 pause record until the group's prime completes. The default (immediate, retail order) is the only arm written; the held-record vector and round budget go | 2.10 `lti_protocol`, 2.3 (q) |
| `PS2_LOADER_ORACLE_DIR` | `hle/loader_oracle.h:70` | Output-directory knob of the oracle capture — the sink API (§5.4) owns the path | 2.5 `loader_model` |
| `PS2_U32POKE` | `pad_input.cpp:736` (ledger `:700`) | Guest-RAM POKE list (writes) — never in product or diag; the read-only watches are DIAG (§5.3) | 2.8 |

### 5.6 Outside the count (named in §2 or found by the site census; none changes the 42)

Reconciles §8.2 item 3 and the today's-sites census (`/tmp/s3/p4_getenv_census.py`: 54 distinct vars read
by the Phase-4 OLD files):

- Attributed to EARLIER phases by the tool (consumed there; Phase 4 only reads the result):
  `PS2_CDVD_BUDGET_KB` (P3 NORM, `ps2_vblank.h:163` → `DerivedClocks::kCdvdKbPerTick = 256`, PHASE3_IMPL
  clock 8) and the five `PS2_LOAD_BUDGET_LIFT*` rows (P3 NORM, `budget_lift.h` → engine constants;
  `DeliveryBudget` §2.5 is their consumer, its constants are already fixed); `PS2_STRMQ` (P3 DELETE by the
  `recompiler.cpp` pattern — applied here as the 5-site lint exemption, §4.2, nothing else);
  `PS2_CDVD_INSTANT` (P3 SETTING → `cdvd.latency_model`, consumed by `LatencyModel`); `PS2_TICK_SYNC` (P3
  SETTING `time.tick_sync` — the mixer pump and `IopClock::OnFrameBoundary` key on it) and
  `PS2_MAINT_EEPACED` (P1 SETTING `time.maint_eepaced`, `main.cpp` site; the ledger prose cells mention
  cdvd, so the tool shows both as "touched P4" — the 44-vs-42 difference in the `touched` table). §2 lists
  `PS2_MAINT_EEPACED` as SETTING on the `rpc_router`/`read_engine`/`pad_protocol` rows: correct per ledger
  `:510`; §4.4's "NORM" is a CONSTRUCTION doc delta. Under the deterministic profile it is `on`; the
  MaintDuties slot runs its three duties + the pad stamp at the boundary (`gs_hle.cpp:702-716` order).
- No ledger row at all at planning time (ledger GAPS — the census found their `getenv` sites). **Rows added by 4A′
  (2026-09-27)**; `FLAG_PHASE_COUNTS` regenerated to P4 = 46: 17 NORM / 4 SETTING / 12 DIAG / 3 TEST / 10 DELETE
  (`FlagPhaseCountsReproducible` green; the §5 heading updated):
  - `PS2_SND_TICKCLOCK` (`sound/sound_core.cpp:196-201`; P0 pin, in the frozen profile) → **NORM** under
    `time.tick_sync`: the boundary pump of §4.5 is the only path in this phase; the device path is Phase 7's
    `time.tick_sync=false` arm. No field of its own.
  - `PS2_FIXC_TICKCLOCK` (`iop_stubs.cpp:2187`; P0 pin, in the frozen profile) → **DELETE**: OLD's
    `PumpDeferredEvtsTickClock` is a no-op under `KernelOn()` (`:2459` "events ride the kernel queue");
    deferred completions are kernel events (§3.1 line 7) — there is no other arm to choose from.
  - `PS2_HB_BOUNDARY` (`iop_stubs.cpp:1379`; PROFILE.md "available but not in profile") → **DELETE**: dead
    under the kernel arm (`PumpHeartbeatAtBoundary` `:2485` returns at once); the heartbeat is a kernel timer.
  - `PS2_IOPK_WATCH` (`iop_kernel/iop_module_table.cpp:65`; 5 s kernel-state watchdog on stderr) →
    **DIAG**, folded into the `iopsched` category as its `watch` line (realtime stalls, BUG-026 diagnosis).
- Sub-key already handled by the tooling: `PS2_PAD_SCRIPT_FILE` (`pad_input.cpp:486`) inherits TEST from
  `PS2_PAD_SCRIPT` (`LEDGER_PARENT`) and is `harness.inputs` (settings_table row 25).
- Announce sites of a P1 SETTING inside Phase-4 files: `PS2_PERF` at `ps2_vblank.h:164` and
  `sound/sound_core.cpp:174` — per ledger reconciliation 7 every per-site announce latch deletes; the
  `DiagRegistry` startup dump replaces them.
- Line drift between the ledger and today's OLD (recorded, not a disposition change): every `iop_stubs.cpp`
  row is +367..+425 lines, `iop_module_table.cpp` +10, `sound_core.cpp` +10 (`PS2_SOUND` `:187` unchanged),
  `pad_input.cpp` +36 (`PS2_PAD_SCRIPT` +2), `sif_rpc_handlers.cpp` +72 — the ledger predates the freeze; the table above carries today's lines.

## 6. Proof gate and Definition of Done (CONSTRUCTION §4.4, restated as checkable items)

Sources for this step (nothing else was read): §1–§5 above; `PHASE3_PLAN.md` §6–§7 (format); CONSTRUCTION §4.4 proof
gate + DoD + stopping rule; EQUIVALENCE §7 (five items) + §7.1 (row form); `docs/bugs/BUG-038-coldboot-title-load-runtoken.md`,
`BUG-026-iop-worker-nondeterminism.md` (headers); `PHASE3_IMPL.md` 3F′ gate table + HANDOFF; NEW `boundary_sequencer.h:46-48`
(`Step` enum: `MaintDuties`=3, `IopDispatch`=4, `EeStreamRpc`=5, `Savestate`=6, `Verify`=7 → 1-based slots 4/5/6/7/8),
`mmio_sink.cpp:20-40` (sink table: SIF row `0x1000F200/0x100`, DMAC row `0x10008000/0x7000`, NO SPU2 row), `leg_closer.cpp:24-31`
(`park*`/`unhandled_mmio_pages` writer), `ctest -N` (74 names); OLD `main.cpp:10465` (`[BOOT-SM-EDGE]` grammar), `:4210`
(`install_dual_hook(0x2b4738)`) — read-only greps.

The gate runs the `frame` policy only (§3.2); `realtime` evidence is recorded, never hashed. One command pair per leg (§4.4
step 5): `python3 verify/run_leg.py --binary build/app/host/nativeee --tree new --anchor <A> --profile deterministic
--frames <1500|600> --out /tmp/p4/<leg>` then `python3 verify/diff_legs.py --cross-engine --lanes audio,mem
--claim mem=<K>,audio=<W> verify/baseline/<leg> /tmp/p4/<leg>` (exit 0 = the claim holds; the verdict line prints the claim).
"Adjudicated" below means an EQUIVALENCE §7 record (all five items) filed as a §7.1 row `P4-<NAME>` BEFORE the lane is claimed,
OLD unpatched, the baseline re-frozen through the ledger — never a tolerance. RUN length ≠ CLAIM length (§4.4, §8.1): coldboot
RUNS 1500 frames (baseline 1494 lines / 17 checkpoints / 162 windows), each anchor RUNS 600 (≥ 60 boundaries past `0x1e7fa4` =
the baseline's 10 checkpoints / 100 windows); a run shorter than the baseline's line count is DIVERGENT by the length rule and
is never reported as a short claim.

| # | Gate item | Check |
|---|---|---|
| G1 | STREAM.IRX regen determinism + generated ABI | two bounded runs of the frozen `iop_recompiler_tool` (§4.2 command) → `diff -r` empty; output md5s `c95059f165f8b68de442169ac932f871` / `b2102157c7dfa8285f21eadbb42c41a4` asserted literally in `test_stream_irx_abi.cpp`; `MANIFEST.sha256` + `INVOCATION.md` written; ctests `IopRecompManifestIntact`, `IopSysclibMap`, `StreamIrxStubSurface` (74 `iop_stubs::` names resolve, none of the five misbound sysclib names, exactly 5 `PS2_STRMQ` sites); `CoreSingleGetenvSite` (`tests/core/check_getenv_sites.py`, the getenv lint — there is no separate `LintNoGetenv` ctest) with `GENERATED_EXEMPT` = exactly `recompiled.cpp: 6` + `iop_recompiled.cpp: 5` and still ONE product site |
| G2 | Unit / oracle ports | §4.1 table 100 % green: `IopKernelUnits` (8 cases), `IopSysmemHeap`, `IopClockFrame`, `IopModuleTable`, `IopmSection`, `SifRpcRouter`, `SifState`, `SceSifDma`, `CdvdReadEngine`, `CdvdLatencyModel`, `LoaderModel` + `LoaderOracleSelftest`, `R140Budget` (9 cases), `VagDecoder`, `SoundVoice`, `SoundState`, `SoundRing`, `SpuCoreSelftest`, `MixerTickPump`, `SndWireGovernor` (both halves; the census half in the diagnostics build), `LibMcSaveFlow`, `PadProtocol` (+ `InputLog` round trip); the ISO-gated `IsoFsByteOracle` + `ArchiveTableLayout` registered when CMake finds the ISO, run here and quoted in `PHASE4_IMPL` (byte-identical to `OLD/game_files/*`, md5s) |
| G3 | Coldboot reaches boundary 0 | `run_leg … --anchor coldboot --frames 1500` → `run_leg: OK`, `finalized=1`, `park=none`, `frames ≥ 750`; `mem.mhash` line 0 present and `== 0,0xeec37d056857657b` (IDENTICAL) **or** adjudicated (§7.1 row from the suspects list below); claim `K` = the checkpoint prefix per §4.4 steps 3–4; the first divergent `N` (if any) ATTRIBUTED to sink (a)–(d) with the `memdiff` page list + writer census — not attributable → construction bug, phase blocked; 3 reps identical (`battery.py` Phase-4 mode ADMISSIBLE) |
| G4 | Anchors 11/11, ≥ 60 boundaries | each `--frames 600` leg: `restore_mhash_ok=1`, `relaunch=2`, `park=none`, `finalized=1`, `frames ≥ 300`, all 10 `mem.mhash` lines present (≥ 60 boundaries past `0x1e7fa4`); `mem` IDENTICAL per checkpoint through the claim on 11/11 (or adjudicated / attributed as in G3); the f57292 and f59991 legs also carry the P4FIX-F1 `p4fix_obj=` probe (EE[sp+0x10] at `FUN_001f4730` entry, NEW vs OLD `--tree v2`, bounded 60 frames, oracle use only) with its verdict in `docs/bugs/P4FIX-F1-callback-slot-zero.md` |
| G5 | Audio identical where claimable | coldboot: windows 0–141 IDENTICAL (`0x0a6fb8af536edb25`, silence — stated as trivial), window 142 (`0x6e5c320f7c150766`) ATTRIBUTED before any claim past 141 via `snd_census`/`konlog` (SPU2 voices keyed by STREAM.IRX → claimable; FMV PCM FIFO = sink (d) → the claim stops at 141 by rule); anchors: window 0 IDENTICAL on 11/11 (f46203 `0xbed6341167ff6373`) and the prefix through each leg's audio claim `W`; a window diverging before the `mem` lane is attributed the same way (§4.5); `audio.enabled=false` yields an empty lane (as OLD) — not a gate configuration |
| G6 | Parks retired, MMIO clean, census keys | all 12 gate legs `park=none` (closer's normal-end `park_pc`/`park_tid`/`park_detail`); `run_leg.py`'s `is_park_leg` exemption exercised by NO gate leg (a leg that still parks fails — the frames-floor path applies); `unhandled_mmio_pages` per leg contains NO SIF page (`0x1000F2xx`) and NO SIF DMAC channel page (D5/D6/D7 = `0x1000C000` / `0x1000C400` / `0x1000C800` — SIF DMA is the syscall decode, §2.6; such a page = an unbound `sceSif*` row, `sif_bound=` names it); new key `iop_unrouted=0` (no IOP-bus write left unrouted by `IopMemory` — the SPU2 window `0x1F900000` is served by `SpuCore`; the Phase-3 "SPU2 sink" of §1.1 lives on the IOP bus, not in `mmio_sink.cpp`'s EE table — §8.4); pages that MAY appear are Phase-5–7 ranges (DMAC D0–D4 / D8–D9 / the global page `0x1000E000`, GIF, VIF, FIFO, IPU, GS-priv) and are RECORDED per leg; `UnhandledRanges()` no longer lists SIF (`AddressMapAndMmioSinkIdleValues` updated); `sif_bound=<sids>` and `iop_modules=<load order>` present on every leg; `netcnfif`/`HandleMtap`/`HandleEyeToy` written iff `sif_bound=` names a sid outside the §2.3 table (§4.6) |
| G7 | BUG-026 root-caused, BUG-038 closed | **BUG-026**: entry updated with the missing progress edge (paired `iopsched` census under both policies, §3.4), the fix (CD-worker deadline and EE loader spin meet at the completion-visibility point — one code path, the policy only chooses the caller of `dispatch()` and the clock), and the three proofs: (i) every `frame` leg unchanged (G3–G5 re-run identical after the fix); (ii) a headless `iop.dispatch=realtime` coldboot, bounded 300 s wall, `--frames 3200`, reaches the boot-SM `6 -> 7` edge (title-scene load complete; the `bootsm` diag line with OLD `main.cpp:10465`'s grammar `[BOOT-SM-EDGE] #n case a -> b … vf=`; addresses title-side) — recorded, not hashed; the VISUAL title screen is Phase 6's; (iii) `config.h` `iop.dispatch` default → `Realtime` + `SettingsTableReproducible` regen. **BUG-038**: a `frame`-policy coldboot `--frames 3200` (reachability only — no baseline beyond 1494) reaches `6 -> 7` with `finalized=1 frames=3200 park=none`; the entry's "regression test" row is met by `battery.py --frames 3200 --require-bootsm 7` on the coldboot leg; both entries' `Root cause` fields filled (NEW never writes the `PS2_IOP_KERNEL=0` hosting BUG-038's hypothesis names, so the closure states which class survived and which did not) |
| G8 | Workaround rows adjudicated | `games/<t>/hle/workarounds/README.md` gains the 5 Phase-4 rows + `loader_sync` (§2.11) with each retirement gate RUN and its verdict: RETIRED (hook uninstalled, leg IDENTICAL, or a §7.1 record) or RETAINED (the failing gate command quoted); install ORDER on `0x181560` (`allocator_reentry` vs `free_list_0x181560`) fixed by `mem.mhash`; carried adjudications recorded: P3-RAMIMAGE-2 (`FUN_002d18f8` reached → the `0x2e804c` patch retires or stays), P3-MANIFEST-12 (regen with the 12 starts, or `no-hook` TSV rows with reasons), P3-BOOT-WAITSEMA (OLD-vs-NEW boot order at the same vf, §4.7), `KeyRedirect::NoteWrite` binding (arbiter = level-leg `mem.mhash`), HEAP section, `ChangeThreadPriority` no-op kept (routing = post-gate re-freeze item) |
| G9 | Lints, ledgers, paired censuses, battery | `LintOneClass` 0 (generated pair + 3 forwarding headers exempt by path), `LintEnginePurity` 0 (every game constant in `games/<t>/`: `module_layout.h`, `loader_sync.cpp`, `archive_table`, the `bootsm`/`loadspan` address tables), `CoreSingleGetenvSite` (G1), `FlagPhaseCountsReproducible` with the four ledger rows of §5.6 added (P4 = 46: 17 NORM / 4 SETTING / 12 DIAG / 3 TEST / 10 DELETE; §5 heading updated in the same stage), `SettingsTableReproducible` after each `iop.dispatch` flip; `iopsched` paired census (OLD `PS2_IOPSCHED_DIAG=1` bounded run vs NEW `--diag iopsched`, `frame`) line-identical for the claimed prefix on coldboot + the f46203 anchor; `battery.py` Phase-4 mode (coldboot ×3 + 11 anchors ×3, `--lanes audio,mem`, per-leg claims from §6.1) → 12/12 ADMISSIBLE |
| DoD | all of G1–G9 | cold boot runs through the IOP reboot handshake, loads the 8 disc IRX (7 stock stubs + the STREAM.IRX plugin — `iop_modules=` in load order), binds RPCs (`sif_bound=`), passes the MC check (libmc rows), streams the first files and reaches boundary 0 and beyond; `audio` + `mem` IDENTICAL to OLD through each leg's claim with every divergence attributed to sink (a)–(d); both bug entries updated with root causes; the workaround ledger complete; the SIF range gone from `UnhandledRanges()` and the SPU2 window served; the boundary owner still the Phase-3 stub with slots 4/5/6 filled and the `Mixer` pump composed into slot 8 (`frames` untouched); `PHASE4_IMPL.md` complete with per-stage handoffs and the §6.1 claim table |

Accepted-difference protocol (what may and may not become a §7.1 row):
- MAY — pre-registered suspects, each needing the five-item record BEFORE the claim: `PS2_SDR_BLOCK_MS` moved from `steady_clock`
  to 8,000 emu µs (§5.1; §8.3 finding 3); P3-RAMIMAGE-2 (`0x2e804c`: retail `1`, OLD image `0`) once `FUN_002d18f8` is reached;
  P3-BOOT-WAITSEMA if the kernel error path differs from OLD's; `cdvd.mc_latency_polls` (documented divergence — `0` under the
  profile in both trees, so no row is expected); `ChangeThreadPriority` routed instead of the no-op — ONLY after the gate, as a
  re-freeze item, never inside a gate leg.
- MAY NOT: IOP scheduling order under `frame` (same kernel → construction bug); anything one of the five transplanted workarounds
  explains (they are transplanted, not adjudicated); sink-attributed divergences (the next phase's work, recorded as such); a
  shortened RUN standing in for a shortened CLAIM.

### 6.1 Per-leg claim table (the `PHASE4_IMPL.md` template — one row per gate leg, 12 rows; filled by 4F/4F′)

| Leg | Run (frames / checkpoints) | Admissible (`finalized` / `park` / `frames≥` / `restore_ok` / `relaunch` / mmio pages / `iop_unrouted`) | `mem`: claimed `K` / first divergent `N` / attribution | `audio`: claimed `W` / first divergent / attribution | `iopsched` paired (lines identical / first diff) | `--claim` exit | reps ×3 |
|---|---|---|---|---|---|---|---|
| `coldboot` | 1500 / 17 | 1 / none / 750 / – / – / `<pages>` / 0 | `K=` / `N=` / (a)–(d) or IDENTICAL | `W=` (≥ 141 trivially; 142 attributed) / … | … | 0 | identical |
| `ps2state_slot0_f36935` … `slowspot_f40298_s1789693059` (the 11 anchors of §4.4) | 600 / 10 | 1 / none / 300 / 1 / 2 / `<pages>` / 0 | … | … | … (f46203 only) | 0 | identical |

Filling rules: `K`/`W` are indices (checkpoint = boundary/60; window = 100 ms), never percentages; an attribution names the sink
AND the evidence (`memdiff` page → the writer's census line); "IDENTICAL" means the whole baseline length; a row with `N` set
and an empty attribution is a blocker, not a gate result; the `--claim` command line is quoted verbatim per row.

## 7. Stages (≤ 45 min each; every stage ends with the tree building and `ctest --preset macos-dev` green)

Order = dependency order: kernel → modules/IRX → SIF (the park releases) → CDVD (files flow) → SPU2/pad/MC (the boot's remaining
devices) → boot + gate. Five stages are pre-registered to split (4A′, 4B′, 4D′, 4E′, 4F′ — the 3B′/3E′/3F′ precedent; §8.6 item 4 added 4A′/4D′; 4B split twice, 4B′ then 4B″ — §7.1 P4-4B-SCOPE): the STOP rule of
STAGE_RULES 3 applies at ~35 min, the tree stays green, the named remainder moves to the primed stage. Every device-facing module
class created before its engine exists (4B before 4D/4E) answers with the DOCUMENTED IDLE result and is completed in the engine's
stage — recorded in the module's row of `PHASE4_IMPL`, never left implicit. "Flags" names the §5 rows applied in the stage.

| Stage | Scope | OLD sources read | Green criterion (own) |
|---|---|---|---|
| **4A** IOP kernel + coroutine + clock + memory + tests | `engine/iop/{iop_kernel, iop_coroutine, iop_host_thread, iop_clock(+.cpp), sif_rpc_bridge, iop_module_table, iop_memory, iop_context, sysmem_heap}` (§2.1–§2.2 minus `iop_loader`); `IopKernel::{ScheduleEventUs, StartPeriodicTimerUs, InjectSignalSema}` (the §3.1 line-7 contract); `IopClock::FrameCeilingUs` bound; `IopModuleTable` bound to `Step::IopDispatch` (slot 5) / `Step::EeStreamRpc` (slot 6) as no-ops with no modules; `core/diag/iopsched` (grammar of §4.7 + the `[IOPK-EVQ]` probe + the `watch` line); `config.h:75` `iop.dispatch` default → `Frame` + `settings_table.tsv` regen; the FOUR ledger rows of §5.6 (`PS2_SND_TICKCLOCK` NORM, `PS2_FIXC_TICKCLOCK` DELETE, `PS2_HB_BOUNDARY` DELETE, `PS2_IOPK_WATCH` DIAG) + `FLAG_PHASE_COUNTS.txt` regen (P4 = 46) + the §5 heading updated; tests `IopKernelUnits` (8 cases), `IopSysmemHeap`, `IopClockFrame`, `IopModuleTable`, `IopmSection` (codec over `IopMemory` + kernel statics, cross-policy). Flags: `PS2_IOP_KERNEL`, `PS2_IOP_DISPATCH`, `PS2_IOPSCHED_DIAG`, `PS2_IOPK_WATCH` | `iop_kernel/*` (6 pairs; `iop_kernel.h:34`, `iop_clock.h:15-16, :63`, `iop_module_table.h:61`, `.cpp:21/:29`), `iop_memory.h`, `iop_context.h`, `iop_sysmem_heap.h`, `tests/{test_iop_kernel,test_iop_sysmem_heap}.cpp`, `test_savestate_format.cpp` §11 (IOPM), `docs/SAVESTATE_SCHEMA.md` IOPM | 5 tests green incl. `TestFrameDeterminism` and `TestDelayAndEvents` (no event fires in its issuing pass); `sizeof`/layout of the IOPM section = the schema (`kThreads 32`, `kSemas 64`); `SettingsTableReproducible` + `FlagPhaseCountsReproducible` green after both regens; `CoreSingleGetenvSite` still 1; lints 0; the two Phase-3 park legs unchanged (`park=sif_rpc_absent` at `0x2cdadc`/`0x1e7fa4` — nothing rebound yet). **Pre-registered split (4A′):** at ~35 min STOP with `iop_kernel`, `iop_coroutine`, `iop_clock`, `iop_memory`, `iop_context`, `sysmem_heap`, the `iopsched` category, the `iop.dispatch` → `Frame` flip + `settings_table` regen, and the tests `IopKernelUnits`/`IopSysmemHeap`/`IopClockFrame` green; hand `iop_host_thread`, `sif_rpc_bridge`, `iop_module_table` (+ its slot 5/6 no-op binding and the `PumpFromParkLoop` quantum rule), `IopModuleTable` + `IopmSection` tests, the FOUR §5.6 ledger rows + `FLAG_PHASE_COUNTS.txt` regen (P4 = 46) + the §5 heading update, and the `watch` line of `iopsched` to **4A′** |
| **4B** IOP HLE modules + module table + STREAM.IRX regen / oracle / link | `tools/regen_from_frozen.sh --iop` (+`--check --iop`), two bounded regen runs (§4.2), `games/<t>/iop/stream_irx/{iop_recompiled.h/.cpp, MANIFEST.sha256, INVOCATION.md, abi/host_app/*.h, stub_surface, module_layout.h, stream_irx_plugin, lti_protocol (0x12345 face + `hle_stream_cd_read_rpc` body), feed_contract.h (constants; the IOP bank table)}`; `engine/iop/iop_loader` (IRX section load, reboot handshake, LOADFILE `0x80000006` face); the 18 `iop/hle/<module>/` dirs of §2.3 — kernel-facing COMPLETE (`sysmem, intrman, ioman [file ops idle until 4D], sifcmd, sifman [SetDma memcpy into EE RAM via a `SifState` interface stubbed until 4C], stdio, sysclib, thbase, thsemap, timrman [`StartHardTimer` → `StartPeriodicTimerUs`, clock 7]`), device-facing CREATED with idle results (`libsd, cdvdman, cdvdstm, sio2man, padman, mcman, mcserv, sdrdrv`); `check_getenv_sites.py` `GENERATED_EXEMPT` +5; `LintOneClass` path exemptions (generated pair + 3 headers); CMake `ps2r_title_stream_irx` linked into `nativeee`; tests `IopSysclibMap`, `StreamIrxStubSurface`, `IopRecompManifestIntact`, NEW `StreamIrxLoadsAndParks` (load through `IopLoader` → module entry → `OnModuleLoaded(rpc_tid)` → RPC thread parked in `RpcServiceLoop`, heartbeat timer armed with `HeartbeatTicksFor(16667)=1`, sysmem largest-free ≥ 559,104; PLUS the IOPM boot-symmetric re-park half of §4.1's `IopmSection`: encode the parked module through the IOPM codec, restore into a fresh kernel + `IopMemory`, `StreamIrxPlugin` re-parks its threads boot-symmetrically — RPC thread back in `RpcServiceLoop`, heartbeat re-armed — and the two `iopsched` streams from that point are identical; written as an `IopmSection` extension in `tests/iop/test_iopm_section.cpp`, linked with the plugin). Flags: `PS2_HB_TICKCLOCK`, `PS2_SNDREG` (registration arm), `PS2_SND_PROCBATCH` (batch stub path), `PS2_SPU_LAT_US`, `PS2_CD_LAT_US` (the two constexprs), `PS2_STRMQ` (exemption), `PS2_HB_BOUNDARY`/`PS2_FIXC_TICKCLOCK` (not written), `PS2_IOPQ`/`PS2_NAXQ`/`PS2_VTQ`/`PS2_MUSQ_VFMIN` (deleted), `PS2_MUSQ`/`PS2_SND_KONLOG` (libsd sites → `snd_census` sub-probes, category lands 4E) | `iop_loader.{h,cpp}` (`:100`, `:299`, `:311-314`), `iop_stubs.h` (192 ln), `iop_stubs.cpp` ranges per §2.3 rows (a)–(l) + `:2810-2905`, `sif_rpc_handlers.cpp:379-420` (sid table), `:441`, `:700`, `:761`, `:2981-2999`, `:2103/:2135`, `build-v2/iop_recompiled.h:3-5`, `tests/test_iop_sysclib_map.cpp`, CONSTRUCTION §A.1 | G1 items; `StreamIrxLoadsAndParks` green (the recompiled module runs to its first block under the kernel with NO EE involvement); `ps2r_title_stream_irx` links into `nativeee` (unused by the boot until 4C); `iopsched` stream of the unit run recorded; lints 0 (exemption counts exact). **Pre-registered split:** if the 18 dirs + regen pass 35 min, STOP after regen + manifest + `stub_surface` + the 10 kernel-facing dirs + `StreamIrxStubSurface`, and hand the 8 device-facing dirs + plugin + `module_layout` + `lti_protocol` + `StreamIrxLoadsAndParks` to **4B′**. **Split taken (2026-09-27, §7.1 P4-4B-SCOPE):** the STOP came EARLIER, at the regen/census boundary — 4B landed regen + manifests + `IopRecompManifestIntact` + the exemption + the import census (74 names = 73 imports + the `hle_stream_cd_read_rpc` thunk) + sysmem/intrman/stdio/sysclib + `IopSysclibMap`; **4B′** = `abi/{anchor,host_app}` + `stub_surface` + `iop_loader` + the 9 remaining kernel-facing dirs (ioman, sifcmd, sifman, thbase, thsemap, timrman, loadcore, thevent, thmsgbx) + `ps2r_title_stream_irx` link + `StreamIrxStubSurface` (census 74/74, unlisted = 0, plus OLD `test_iop_sysclib_map.cpp`'s artifact pin); **4B″** = the pre-registered device-facing remainder above (8 dirs + `stream_irx_plugin` + `module_layout.h` + `lti_protocol` + `feed_contract.h` + `StreamIrxLoadsAndParks` + the IOPM re-park oracle). G1 is claimable only after 4B′ (the generated TU compiles and links). **4B′ landed (2026-09-27):** `ps2r_title_stream_irx` compiles and links; `StreamIrxStubSurface` 74/74/0 + the artifact pin; `IopLoader` over OLD's `game_files/STREAM.IRX` (text 76744 / data 1272 / rodata 616 / bss 55360); 6 module classes (loadcore/thevent/thmsgbx have no generated references → module-table rows only, 4B″ plugin); the `SifRpcBridge` flake fixed at root (pass counted on kernel entry). |
| **4C** SIF + the DMAC-page assertion → the SIF park releases | `engine/sif/{sif_state, hle/rpc_router, hle/sce_sif, hle/ee_rpc_server}` (§2.4); `SifState` registered on `MmioRouter` over `0x1000F200–0x1000F2FF` displacing the Phase-3 sink row; `SceSif` rebinds every `KernlSifRpc` row through the manifest (no edit to `ee/hle/kernl_sif_rpc.*`, D-SEQ-2); `sceSifSetDma`/`DmaStat` syscall decode → memcpy into `IopMemory`; `RpcRouter::CompletePending` bound to `Step::MaintDuties` (slot 4) as its RPC end-callback duty (OLD duty order `gs_hle.cpp:702-716`; the deferred-ISO and pad-stamp duties are hooks filled in 4D/4E′); `EeRpcServer` service at `Step::EeStreamRpc`; `run.meta` keys `sif_bound=`, `iop_modules=`; the §2.6 DMAC-page assertion RECORDED (per-leg `unhandled_mmio_pages`, the D5/D6/D7 rule of G6); tests `SifRpcRouter`, `SifState`, `SceSifDma`; `AddressMapAndMmioSinkIdleValues` updated (SIF no longer in `UnhandledRanges()`). Flags: `PS2_MAINT_EEPACED` (P1 SETTING, read) | `sif.{h,cpp}`, `sif_hle.{h,cpp}` (minus `:745-778` and the Phase-3 halves; bind `:727` region, `[SIF-BIND-HLE]` `:694`), `sif_rpc_handlers.{h,cpp}:60-200, :370-424`, `syscalls.cpp` sceSifSetDma/DmaStat decode, `gs_hle.cpp:702-716`, `sce_hle/netcnfif.cpp` (23 ln, read for the reachability decision only) | 3 tests green; `UnhandledRanges()` lists no SIF range; **the SIF park releases on both leg kinds**: coldboot `run_leg … --frames 1500` no longer reports `park=sif_rpc_absent`/`0x2cdadc` — it runs the reboot handshake + 8 module loads (`iop_modules=` complete, `sif_bound=` non-empty, `IopModuleTable` dispatching at slot 5) and parks at the FIRST unbound device point (expected: a libcdvd/ioman file read or the MC check — the new `park=`/`park_pc`/`park_tid` quoted in `PHASE4_IMPL`; a `sif_reg_spin`-class park is recorded, not worked around); every anchor: `0x1e7fa4`/tid 2 released, `sif_bound=` shows the anchors' RPC setup, new park quoted; `unhandled_mmio_pages` has no `0x1000F2xx` and no D5/D6/D7 page; lints 0 |
| **4C′** (pre-registered continuation, split taken 2026-09-27 — §7.1 P4-4C-SPLIT) | `engine/sif/hle/ee_rpc_server` (slot 6); `sceSifSetDma` syscall → memcpy into `IopMemory`; `StreamIrxPlugin` booted from the composition root at OLD's point (composition time, before the EE starts — `main.cpp:5381-5387`); `EeRamSink` → `MainMemory`; `RpcRouter` faces: `0x12345` → `SifRpcBridge::RunOnRpcThread` + `LtiProtocol`, LOADFILE → `IopLoader::LoadFileRequest`, IOPHEAP → `SysmemHeap`, CDVD sids → `CdvdmanModule`; `SceSif::IopHeap` bound; run.meta `sif_bound=`/`iop_modules=` filled; tests `SceSifDma`, `StreamIrxServesFirstRpc`, `EeRpcServer`; `PS2_HB_TICKCLOCK`; doc deltas (§1.1 attribution, §2.6 reword) | `sif_rpc_handlers.cpp:60-200` (EE server), `syscalls.cpp:484-800` (SetDma decode), `main.cpp:5381-5387`, `iop_loader.cpp` | 3 tests green; `iop_modules=` = the 30 rows, `sif_bound=` non-empty on the anchor leg; the first `main_handler 0x8D28` call returns through the bridge (quoted); lints 0 |
| **4D** CDVD / ISO + completion model + maintenance slot 4 | `engine/cdvd/{iso_fs, read_engine, latency_model, stream_reader, delivery_meter, delivery_budget, hle/loader_model, hle/libcdvd_bindings.cpp}` (§2.5); `CdvdmanModule`/`CdvdstmModule`/`IomanModule` bodies completed (CD-read completion = `ScheduleEventUs(now + 3000)`, the EE→IOP nudge = `InjectSignalSema`, `sceCdSt*` ring); `Step::MaintDuties` (slot 4) deferred-ISO delivery duty bound in OLD's order (delivery → RPC end-callbacks → pad stamp); `games/<t>/data/archive_table`, `games/<t>/hle/{loader_sync.cpp, budget}` data pairs; workarounds `async_read_dest_preserve` (0x2ad110, dest half) + `allocator_reentry` (0x181560 family; install order vs `free_list_0x181560` fixed by the leg) with README rows; `core/diag/loadspan` (+`drawmax`); `LoaderModel::SetTraceSink` + `tests/oracle/loader_oracle_check.py`; tests `IsoFsByteOracle` (ISO-gated), `CdvdReadEngine`, `CdvdLatencyModel`, `LoaderModel`, `LoaderOracleSelftest`, `R140Budget`, `ArchiveTableLayout` (ISO-gated). Flags: `PS2_CHUNK_APPLY_{SCOPED,DORMANT,VALVE_TICKS,ORDER}`, `PS2_LOADER_HLE`, `PS2_SNDREG` (CD-job arm), `PS2_LOADER_ORACLE` (TEST), `PS2_LOADSPAN`, `PS2_LOADSPAN_DRAWMAX`, `PS2_CHUNKQ`/`PS2_LOADER_ORACLE_DIR` (deleted); read: `cdvd.latency_model`, `cdvd.latency_scale`, `PS2_CDVD_BUDGET_KB`/`PS2_LOAD_BUDGET_LIFT*` constants | `hle_heap.cpp` (`:41`, `:685`, `:1333`, `:1490`, `:1644-1709`, `:1816`, `:2002-2079`, `:2441`, `:2765`, `:3039-3050`, `:3933-3990`), `cdvd_stream.{h,cpp}`, `ps2_vblank.h:65-175`, `budget_lift.h:70-206`, `hle/loader_sync_hle.h`, `hle/loader_oracle.h`, `loadspan.h`, `sce_hle/libcdvd.cpp` (402 ln), `main.cpp:6936-7234, :3187-3251`, `iop_stubs.cpp:2004-2807` (cdvdman bodies), `tests/r140_budget/*`, `tools/loader_oracle_check.py` | 7 tests green (+2 ISO-gated, run and quoted); the coldboot leg proceeds past 4C's park through the first file reads and the disc check (`IsoFs` byte oracle IDENTICAL) to the NEXT unbound device point (expected: the MC check or the first libsd/libsdr call — quoted); `loadspan` census shows the boot's load spans opening/closing; `DeliveryMeter` refills only on epoch edges (`TestLiveWrapperEpoch`); lints 0 (`LintEnginePurity`: every polled address in `games/<t>/hle/loader_sync.cpp`). **Pre-registered split (4D′):** at ~35 min STOP after `iso_fs`, `read_engine`, `stream_reader`, `delivery_meter`, `hle/loader_model` (+ `SetTraceSink`, the oracle checker port), `libcdvd_bindings.cpp`, the module bodies, the slot-4 delivery duty, `loader_sync.cpp`/`budget` data and the tests `IsoFsByteOracle`/`CdvdReadEngine`/`LoaderModel`/`LoaderOracleSelftest`/`R140Budget`; hand `latency_model` (+ `CdvdLatencyModel`), `delivery_budget`, `core/diag/loadspan` (+`drawmax`), the two workarounds `async_read_dest_preserve` + `allocator_reentry` (README rows, install order on `0x181560`), and `games/<t>/data/archive_table` + `ArchiveTableLayout` to **4D′** |
| **4E** sound core / mixer / VAG / FMV + libsdr + pad / MC | **4E (SPU2 half):** `engine/sound/{sound_core, voice, vag_decoder, mixer, fmv_stream, sound_state_codec, command_ring}` (§2.7 + §4.1's `CommandRing`); `SpuCore` registers served over the IOP-bus SPU2 window through `IopMemory` (`iop_unrouted=` census key); `Mixer` pump composed into `Step::Verify` (slot 8: `mixer.Pump(800)` → `VerifyHost::OnBoundary(...)`, §4.5) under `time.tick_sync`, gated by `audio.enabled`; `LibsdModule` bodies completed (FIX-C deferred voice-transfer completions, NAX/ENDX/VOLX readbacks); SNDC/SDRX sections; `core/diag/snd_census` (+`konlog`, `musq`); tests `VagDecoder`, `SoundVoice`, `SoundState`, `SoundRing`, `SpuCoreSelftest`, `MixerTickPump` (silence window = `0x0a6fb8af536edb25`), `SndWireGovernor` census half (diag build). Flags: `PS2_SOUND`→`audio.enabled`, `PS2_SND_TICKCLOCK`, `PS2_SND_EAGER_HOLD`, `PS2_SND_KONLOG`, `PS2_MUSQ`, `PS2_SND_CENSUS`, `PS2_SND_SELFTEST` (TEST), `PS2_SND_FMVPACE`, `PS2_SND_FMVQ_DIV`. **4E′ (pre-registered continuation — libsdr + pad + MC):** `engine/sound/{bank_ledger, hle/sdr_remote, hle/sd_block_trans, hle/libsdr_bindings.cpp}`, `stream_irx/feed_contract.h` completed with `SpuCore` (the five contract points), `SdrdrvModule` face (`0x80000701`), `LtiProtocol` bank-chain half; `engine/pad/hle/{pad_protocol, libpad_bindings.cpp}` + `PadmanModule` face + the slot-stamp duty of slot 4 + `InputLog` replay/record (`harness.inputs`/`record_input`); `engine/mc/hle/{mc_host_store, mc_command_model, libmc_bindings.cpp}` + `McservModule` face; `core/diag/{stick_trace(+inlat), padwatch}`; tests `SndWireGovernor` product half (`tests/iop/test_lti_protocol.cpp`), `PadProtocol` (+ `InputLog` round trip), `LibMcSaveFlow`. Flags: `PS2_SDR_BLOCK_MS` (8,000 emu µs — first-run suspect), `PS2_SND_PAUSE_DEFER` (deleted), `PS2_PAD_SCRIPT` (TEST), `PS2_STICKTRACE`, `PS2_INLAT_ANALOG`, `PS2_HEADWATCH`/`PS2_POSWATCH`/`PS2_U32WATCH` (DIAG), `PS2_U32POKE` (deleted), `PS2_MC_PERSIST`, `PS2_MC_LATENCY`→`cdvd.mc_latency_polls` | 4E: `sound/{sound_core,voice,vag_decoder,mixer}.{h,cpp}` (`sound_core.cpp:98/:105/:187/:196-201/:247/:347/:363/:483/:810/:1122`, `voice.h:88`, `mixer` engine half), `sound/ring.h`, `iop_stubs.cpp:1678-2003` (libsd), `snd_census.h`, `verify/verify_boundary.cpp:59-84`, `tests/{test_vag_decoder,test_sound_voice,test_sound_state,test_sound_ring,test_snd_wire_governor}.cpp`. 4E′: `sce_hle/libsdr.cpp` (438 ln; `:113`, `:143`, `:153`, `:244-258`), `snd_bank_ledger.{h,cpp}`, `sif_rpc_handlers.cpp:1619` (+`:3271`), `pad_input.{h,cpp}` protocol half (`:478-736`), `sce_hle/libpad.cpp` (`:374`, `:416`), `sce_hle/libmc.cpp` (`:96`, `:196`), `hle_heap.cpp` MC half, `host_app/input/test_pad_buffer.cpp`, `tests/test_libmc.cpp`, `sif_rpc_handlers.cpp:875, :1035` | 4E: 7 tests green; `MixerTickPump` silence hash equals the coldboot window-0 baseline value; a unit `IopMemory` write to the SPU2 window reaches `SpuCore` (no unrouted write); the coldboot leg proceeds past 4D's park through the first libsd calls to the NEXT unbound point (expected: the MC check — quoted); lints 0. **STOP rule:** at ~35 min hand everything listed under 4E′ to 4E′ with the tree green. 4E′: 3 tests green; `LibMcSaveFlow` against a temp `mc_dir`; the coldboot leg passes the MC check (libmc rows hit in `sdk_rows_hit_list`) and either reaches boundary 0 or parks at a point quoted for 4F; `InputLog` record → replay → identical slot images; lints 0 |
| **4F** boot to the first boundary + anchors + BUG-026 + gate — **4F taken 2026-09-28 (Option A, libgraph release): boundary 0 reached, checkpoint 0 DIVERGENT unattributed (P4-COLDBOOT-CKPT0-DIVERGENT), the run parks at frame 60 on a loader file-IO seam (P4-LOADER-SEAM-PARK), anchors 11/11 restore but park on VU micro (Phase 5), the rest → 4F′** | **4F:** composition root wiring (`IopLoader` in the boot path after `SdkBindingManifest::Install`; `IopModuleTable` in slots 5/6; `SifState`/`SpuCore` registrations; the `Mixer` in slot 8; the three boot workarounds `piracy_bypass` (0x181490), `rendinit_double_init` (0x1b36b8), `type_registry_append` (0x268440) installed through the manifest's TITLE lane with README rows); harness additions `diff_legs.py --claim mem=K,audio=W`, `run_leg.py --diag <cat>` (legacy tree: env var), `battery.py` Phase-4 mode (+ the Phase-3 `--restore-only` mode carried from 3F′) and `--require-bootsm`; `core/diag/{memdiff, bootsm}` (addresses title-side); the coldboot leg → boundary 0: checkpoint 0 `0xeec37d056857657b` (G3) and the prefix claim with attribution; the 11 anchors (G4) with claims; audio claims (G5) incl. the window-142 attribution; G6 keys/pages; the `iopsched` paired census on coldboot + f46203; the §6.1 table filled; P3-BOOT-WAITSEMA + P3-RAMIMAGE-2 + P3-MANIFEST-12 + `KeyRedirect::NoteWrite` + HEAP + P4FIX-F1 `p4fix_obj=` recorded. **4F′ (pre-registered continuation):** BUG-026 diagnosis (paired `iopsched` under `frame` and `realtime`, the missing edge named) + the meet-at-the-boundary fix + proofs (i)–(iii) + `iop.dispatch` → `Realtime` flip + `settings_table` regen; BUG-038 `--frames 3200` runs (`frame` reachability; `realtime` headless bounded); the five workaround retirement-gate runs + `loader_sync` shrink check (G8 verdicts); `battery.py` ×3 over 12 legs; the gate table + verdict in `PHASE4_IMPL` | `main.cpp:5225-5300` (composition order, re-read), `:4210` + `:10465` (boot-SM census), `sif_hle.cpp:745-778`, `main.cpp:4315-4350, :8096-8140`, `gs_hle.cpp:690-744`, OLD binary bounded runs as ORACLES only: `--tree v2` for P4FIX-F1 W2's twin and `PS2_IOPSCHED_DIAG=1` for the paired census (through `run_bounded.sh`; OLD `logs/` never written by us) | 4F: coldboot `run_leg: OK`, `park=none`, `frames ≥ 750`, `mem.mhash` line 0 `== 0,0xeec37d056857657b` or a §7.1 row filed first; 11/11 anchors `park=none` with 10 checkpoints and claims; `--claim` exit 0 per leg; 74+N ctests green; lints 0. **STOP rule:** at ~35 min the claims made so far are recorded in §6.1 and BUG-026/BUG-038/retirement gates/battery move to 4F′. 4F′: G7–G9 evidenced; `battery.py` 12/12 ADMISSIBLE; **Phase 4 gate: PASSED** written only when G1–G9 + DoD are each quoted, else the exact failing command is the handoff |

### 7.1 Findings ledger (recorded, never silently tolerated; ids `P4-<NAME>`, filled during the stages)

| Id | Stage | Finding | Disposition |
|---|---|---|---|
| P4-SPU2-BUS | plan (step 4) | §1.1/§2.7 speak of a Phase-3 "SPU2 sink" to be displaced; NEW `mmio_sink.cpp`'s EE idle table has NO SPU2 row (SIF `0x1000F200/0x100` and DMAC `0x10008000/0x7000` are there). The SPU2 window (`0x1F900000`) is an IOP-bus range the recompiled IRX reaches through `IopMemory`, so the "displacement" is an `IopMemory` routing rule, observed by the new `iop_unrouted=` key — not by `UnhandledRanges()`. | Wording kept as intent; the check placed where the range lives (G6). CONSTRUCTION §4.4 DoD ("the SIF and SPU2 MMIO ranges no longer report as `UnhandledRanges()`") gets a doc delta with this note. |
| P4-DMAC-WORDING | plan (step 4) | The step-4 brief says "`unhandled_mmio` empty of IOP/SIF/DMAC pages"; §2.6 (step 2) keeps the Phase-3 DMAC sink and allows the D0 page. | Reconciled in G6: FORBIDDEN = SIF page, SIF DMAC channel pages D5/D6/D7, any unrouted IOP-bus write; ALLOWED-and-RECORDED = the Phase-5–7 DMAC/GIF/VIF/FIFO/IPU/GS-priv pages. No DMAC file is written (§2.6 stands). |
| P4-TITLE-OBSERVABLE | plan (step 4) | "Reaches the title screen" (CONSTRUCTION §4.4 BUG-026 proof) has no visual form before Phase 6. | Defined as the boot-SM `6 -> 7` edge from the `bootsm` diag census (OLD `main.cpp:10465` grammar; the hook address `0x2b4738` and the state word are title data), for both the BUG-038 `frame` run and the BUG-026 `realtime` run; the visual check is Phase 6's. |
| P4-CEILING-INCLUSIVE | 4A | OLD `DrainQuantum` breaks only when the next due is PAST the ceiling (`> ceiling`, `iop_module_table.cpp:95`): a due exactly on the next boundary's tick is serviced one pump early — 61 heartbeat beats over 60 pumps from t=0 for the 60 Hz timer, not 60. | Transplanted verbatim; `IopClockFrame` pins it; §4.3's clock-7 phrase reads "over N pumps with the inclusive ceiling". |
| P4-NO-FIBER-LAYER | 4A | NEW had no fiber backend (Phase-1 stubs, `has_fibers=false`); §2.1's "the EE scheduler's fiber layer" did not exist. | `engine/platform/macos/fiber_macos` (ucontext) + Platform entries `fiber_current`/`fiber_destroy`. |
| P4-GUARD-HOOKS-DROPPED | 4A | OLD `IopKernel::SetDispatchGuard`/`SetContextSwitchHooks` exist only for the deleted `GuestMonitor` (`PS2_IOP_KERNEL=0` hosting, §1.5); §2.1 said "installed EMPTY". | Not written (fewer moving parts); `SetThreadRunHooks`/`SetWakeHook`/`SetTraceHook` kept. §2.1 row reworded (4A′). |
| P4-4A-SPLIT | 4A | STOP at ~38 min: `iop_host_thread`, `sif_rpc_bridge`, `iop_module_table`, `IopModuleTable`/`IopmSection` tests, the four §5.6 ledger rows + P4 = 46 regen, the `watch` line → 4A′ (as pre-registered). | `PHASE4_IMPL.md` 4A HANDOFF. |
| P4-HOOK-AFTER-HOST | 4A′ | `IopModuleTable` members die host-thread-first; `~IopKernel::Shutdown()` poked the destroyed `IopHostThread` (mutex EINVAL). OLD's `static` instance never destructs. | `Shutdown()` joins the host thread, then detaches the wake hooks. |
| P4-TIMER-NO-BURST | 4A′ | A sub-frame periodic timer under the boundary pump fires one beat per pass + forward resync at the frame jump (no burst): 134 beats over 8 pumps for a 1 ms timer, not `floor((N+1)·16667/1000)=150`; the clock-7 closed form holds only for periods ≥ one quantum. | Contract pinned in `IopModuleTable` (next due past the inclusive ceiling after each pump; 15..18 beats/pump) + the recorded 134; the paired `iopsched` census is the leg-side oracle. |
| P4-MAP-ANON | 4A′ | `_XOPEN_SOURCE 700` hides `MAP_ANON`/`MAP_ANONYMOUS` on macOS. | `_DARWIN_C_SOURCE` in `fiber_macos.cpp`; guest fiber stacks are mmap'd with a `PROT_NONE` guard page (`FiberGuardPage`). |
| P4-IOP-REGEN-FAST | 4B | The frozen `iop_recompiler_tool` regenerates STREAM.IRX in 5–6 s (§4.2 budgeted 600 s per run); byte-identical to the OLD `build-v2` oracle on every run (`c95059f165f8b68de442169ac932f871` / `b2102157c7dfa8285f21eadbb42c41a4`). | Bound kept at 600 s (harmless); the two-run + oracle proof is cheap enough to re-run on every `--iop` invocation; `--check --iop` (ctest `IopRecompManifestIntact`) re-hashes the installed pair without running the tool. |
| P4-SYSCLIB-BOUNDED | 4B | OLD's sysclib bodies (`iop_stubs.cpp:663-769`) ran `std::strlen/strcat/strcpy/strstr` on raw host pointers into the 2 MB buffer (unbounded at the RAM end). | NEW bounds every C string at the end of RAM (`CStringLength`; `strncmp` clamps both compare limits with `CompareLength`) and copies with `memmove`; same results for every in-range string (the module's strings are short path/name literals). Not an accepted-difference: no observable change for valid inputs; `IopSysclibMap` pins the RAM-end cases. |
| P4-4B-SCOPE | 4B | 4B's scope (regen + ABI root + stub surface + loader + 13 modules + 3 tests) did not fit one 45-min stage; the pre-registered split point (after the 10 kernel-facing dirs) was not reached — STOP came at the regen/census boundary with 4 modules (sysmem, intrman, stdio, sysclib) landed. | Split taken EARLY, recorded in the §7 4B row: **4B′** = `abi/` + `stub_surface` + `iop_loader` + ioman/sifcmd/sifman/thbase/thsemap/timrman/loadcore/thevent/thmsgbx + `ps2r_title_stream_irx` link + `StreamIrxStubSurface` (with OLD `test_iop_sysclib_map.cpp`'s artifact pin); **4B″** = the pre-registered device-facing remainder (8 dirs + `stream_irx_plugin` + `module_layout.h` + `lti_protocol` + `feed_contract.h` + `StreamIrxLoadsAndParks` + the IOPM re-park oracle). The import census is the 4B′ contract: 74 `iop_stubs::` names = 73 imports + the `hle_stream_cd_read_rpc` thunk. |
| P4-LINKGUARD-VACUOUS | 4B″ | 4B′'s "unlisted extern fails the link" was untrue: a static-archive member is linked only when referenced, and nothing referenced `iop_recompiled.o`. | Fixed (`TestLinkGuard` references the generated `iop_recompiled_functions`); negative proof recorded in `PHASE4_IMPL` 4B″. Lesson for every "the TU links" claim: reference a generated symbol. |
| P4-FIRST-BLOCK | 4B″ | STREAM.IRX's boot-path first block is tid 1 in `SleepThread`, not the RPC loop: the module's init waits for the EE handshake before SdInit / timer / RpcLoop. `[0x13328]` (= `$gp`) holds 1 after the entry. | `StreamIrxLoadsAndParks` pins the observed park; 4C's EE-side `sceSifBindRpc(0x12345)` + wakeup must drive the module to `RpcServiceLoop` + heartbeat armed (4C's oracle `StreamIrxServesFirstRpc`). |
| P4-IDLE-SIX | 4B″ | The six disc IRXs (sio2man, padman, mcman, mcserv, sdrdrv, cdvdstm) have no imported entries; six directories would hold six empty classes. | One `IdleModule` class + six title rows emitted by the plugin (`§2.3` dirs collapse to one). |
| P4-PARK-IS-SBRK | 4C | The coldboot park `park=sif_rpc_absent park_pc=0x2cdadc` was the libkernl `sbrk` row (`sdk_rows_hit_list=sbrk@0x2cf930:1`), not `sceSifInitRpc`: `KernlSifRpc` parks 8 non-SIF rows under the SIF reason. The EE reaches libcdvd (`sceCdReadClock`) before its first SIF call. | `engine/ee/hle/kernl_sbrk` (break word = `TitleProfile` `sbrk_break_word`); §1.1's "the SIF park" wording → doc delta in 4C′; the owner's reason string stays (D-SEQ-2) with this row as the record. |
| P4-DMAC-FILE | 4C | §2.6 said no DMAC file; the 4C brief asked for `engine/dmac/dmac.{h,cpp}` (D5-D7 semantics + idle reads). | Written as a register model only (SIF kicks complete synchronously; GIF/VIF/IPU/SPR kicks counted in `dmac_unmodelled_kicks=`); no transfer engine before Phase 5. §2.6 to be reworded in 4C′. |
| P4-SIFINIT-STATICS | 4C → **confirmed 4F′** | OLD `hle_sceSifInitRpc` seeded libkernl's RPC statics at title addresses (`sif_hle.cpp:64-120`); all consumers are HLE rows, so the boot does not need them, but the boundary-0 `mem.mhash` (G3) may. **4F′: it does — page `0x3ca000` (`0x3caa58..0x3caa90`, 9 words) is the G3 residual; retail's recompiled `sceSifInitRpc` fills them, so NEW's zero is NEW's deviation (P4-CKPT0-STATICS (1)).** | Not in `engine/`; a `games/<t>/hle` data pair applied by `SceSif::InitRpc` — **fix pending 4F″**. |
| P4-RUNTOKEN-FLAKE | 4C | `EeRunTokenAndEventQueue` (Phase-3) timed out once under the full `ctest` and once under `ctest -R`, passed directly and on the next `ctest -R` (0.07 s); nothing 4C touched is referenced by it. | Recorded, not tolerated: 4C′ opens with `--repeat until-fail:3` on it; a repeat → `docs/bugs/` root-cause entry. |
| P4-FIRST-RPC-NO-SDINIT | 4C″ | The 4C handoff predicted the first STREAM.IRX RPC drives tid 1 through `SdInit → StartHardTimer → sceSifRpcLoop`. Observed (`StreamIrxServesFirstRpc`): tid 1's first-block SleepThread IS the service loop's park (the bridge body completes on it, `main_handler` runs), and a zeroed request leaves libsd init 0 / hard timer idle — the init work belongs to the title's init command, not to "any first call". | The test pins the zeroed request as "not the init command"; 4C‴ sends the module's real init command (below). |
| P4-CDINIT-SOUND-SHAPE | 4C‴ | The module's init command CD_READ_INIT (cmd 12; request `u16 header, u16 count, {u16 cmd, u16 nparams, u16 params[]}` — OLD `sif_rpc_handlers.cpp:5271-5310, :5459-5479`) arms the hard-timer heartbeat (timrman hard timer 1 RUNNING) and sets the sound core up through libsd **SdSetSwitch ×2, SdGetSwitch ×2, SdGetAddr ×48** (+ ordinals 11, 15) — counts for the test's request `{config 1, p1 0, sectors 0x10, flags 0}` on a freshly booted module; other parameters may shape the switch/addr traffic differently — it never calls libsd ordinal 0 (`SdInit`), which the 4C recipe and `feed_contract.h` line 13 named as the init call; the armed heartbeat then polls the core every boundary (SdGetParam ×384 / SdGetAddr ×432 over 8 boundaries) with tid 1 parked. | `StreamIrxServesFirstRpc` second case pins the census; `feed_contract.h` item 1 wording is a 4E (sound) correction — the SPU2 core's "init" observable is the switch/addr traffic, not ordinal 0. |
| P4-RUNTOKEN-FLAKE (repeat) | 4C″/4C‴ | `--repeat until-fail:5` under leg load: 3 passes then a 120 s timeout (4C″); the reviewer's idle and concurrent repeats: 5/5 + 5/5 passed; 4C‴ idle `--repeat until-fail:10`: 10/10. Then two more reproductions under external load only (p4c4r, load avg ≈7, run 3; 4C‴ iter-2, load avg ≈5.5, run 5), each preceded by a wrong-order election `FAIL`; the second was attached live: the worker parked in `EeRunToken::AcquireSlow` (`ee_run_token.cpp:79`) with no holder — see `PHASE4_IMPL.md` §4C‴. | `docs/bugs/BUG-040-ee-run-token-test-hang.md` (renumbered from the duplicate id BUG-011; INDEX/TRIAGE rows added). Not a lost wakeup: a test-rig start race lets `MakeReady` re-validate an exited tid, `ElectLocked` elects a tid no host thread holds, and nothing re-elects past it (token wedge, BUG-035 family). Owner 4D: engine fix + rig fix, then the in-game reachability question. |
| P4-4C-SPLIT | 4C | Scope did not fit 45 min: STOP after `sif_state`, `dmac`, `rpc_router`, `Rebind`, `sce_sif` (18 rows), `kernl_sbrk`, root wiring, 3 tests, both legs released. | **4C′** = `ee_rpc_server`, `sceSifSetDma` memcpy into `IopMemory`, plugin boot at composition time (OLD `main.cpp:5381-5387`), bridge + module faces on the router, IOP-heap binding, `SceSifDma` + `StreamIrxServesFirstRpc`, `PS2_HB_TICKCLOCK`, doc deltas. `PHASE4_IMPL.md` 4C HANDOFF. |
| P4-MC-BOOT-CHECK | 4E | The boot's memory-card check as OLD serves it: the first `sceMcGetInfo(port 0, slot 0)` completes `type=2 free=0x1f00 format=1 result=-1` ("switched to a formatted card"), every later probe `result=0` (`OLD/logs/runtime_log.txt`: 1440 probes, one edge); the boot's first `sceMcSync` is the blocking idle call (a0=0 → -1). `PS2_MC_LATENCY` (4) → SETTING `cdvd.mc_latency_polls` (profile 0). | `engine/mc/hle/{mc_command_model, libmc_bindings}`; `LibMcSaveFlow` pins the three observables; the coldboot leg passes the row (`mc_calls=sync=1`). |
| P4-PRINTF-SINK | 4E | After the MC check the title's first `printf` runs libkernl's engine (`_sceSnprintf`) into `deci2Putchar` (0x2d0750: a 126-byte line buffer flushed on `\n` to `kputs` 0x2d6468 → syscall 124 `Deci2Call`, -1 on retail). OLD never bound these rows (its `write` sink served `write()`, not printf); the boot parked there once the libmc rows released. | `kputchar`/`deci2Putchar`/`kputs` registered `Recompiled` in `SdkBindingManifest::RegisterBuiltins` (pure bookkeeping + a served syscall; the text goes nowhere, as on a retail console); `SyscallTable` manifest pins 74→77 / 6→9. Coldboot then binds the two padman servers and parks on `libpad.a scePadGetModVersion` (4E′). |
| P4-PAD-LATCH-DELETED | 4E′ | OLD's R183 press LATCH (`pad_input.cpp:226-262` `ApplyPressLatch`, `PadInput_NoteRead`) kept a pressed button in the wire until the game had performed 2 button-payload reads after the host last saw it held, because OLD re-stamped the wire from instantaneous host key state at ~880 Hz while the title reads the pad at its frame rate (taps shorter than a game frame were stamped and un-stamped between reads: 4/4 taps lost in its measurement). NEW stamps once per boundary from a per-frame sample (`InputLog` replay, later the host sampler), so no press can be lost between reads: a script press over frames [F1,F2] is served on exactly those frames. NORM → the latch is not written; sub-frame host taps are the Phase-7 host sampler's edge-latching, not the protocol's. | `PadStampDuty` (one sample per boundary); `PadRoundTrip` pins record → replay → identical slot images every boundary. |
| P4-PAD-RELEASE | 4E′ | With the 19 libpad rows bound over `PadProtocol` and `PadmanModule` registered at `0x80000100/0x80000101`: the coldboot leg's `sceSifBindRpc ×3` all resolve (`sif_bound=MCSERV@0x80000400,PADMAN@0x80000100,PADMAN_DATA@0x80000101`), `scePadGetModVersion` returns 0x0300 and the title parks on **`libsdr.a sceSdRemoteInit` (0x2c4c30)** — the sound init runs BEFORE `scePadPortOpen` (`pad_calls=open=0`, `padman_calls=0/0/0/0`), so the STREAM.IRX command sequence (`stream_rpc_calls=0/0`) and the cmd-12 libsd shape stay behind the libsdr rows (4E″). The f46203 anchor's `scePadGetState` released: `scePadGetState:1, scePadRead:1` served from the RESTORED slot image (`served=1`, `pad_stamps=0` — the protocol's port table is not in the savestate yet, Phase 6), then a Phase-5 park `vu_micro_absent @0x19fd80`. | `engine/pad/hle/{libpad_bindings, padman_module, pad_stamp_duty}`, `games/<t>/hle/pad_readiness_hook`; run.meta `libpad_rows=19`, `pad_calls=`, `padman_calls=`, `pad_stamps=`, `pad_stage=`, `pad_input=`, `pad_readiness=`. |
| P4-PADMAN-READ-FANOUT | 4E″ (p4e2r) | OLD `HandlePadman` opcode 6 (PadRead, `sif_rpc_handlers.cpp:939-961`) writes the idle report and then the common fan-out (`:1008-1012`) clobbers reply bytes `+0x00..+0x03` with `01 00 00 00`; NEW `PadmanModule::kOpRead` serves the report intact (`00 70 FF FF 80 80 80 80`) and skips the fan-out. Accepted difference (OLD wrong; EQUIVALENCE_PLAN §7): unreached in both legs — `padman_calls=0/0/0/0` (libpad 3.0.1 reads through the slot-4 stamp). | `LibpadBindings` pins the intact report; `PHASE4_IMPL.md` §4E′ Findings. |
| P4-LIBSDR-NO-SIF-BIND | 4E″ it.2 | The two libsdr rows (`sceSdRemoteInit`, `sceSdRemote`; `sdk_bindings.tsv:159-160`) are HLE'd over the shared `SpuCore` exactly as OLD HLE'd them (`libsdr.cpp:271` → 0, `:311` → the command switch), so the EE never issues `sceSifBindRpc(0x80000701)`: **`sif_bound=` lists no `SDRDRV` on either tree** and OLD's `HandleLibSd` face was dead code. NEW registers `SdrRemote` as the `0x80000701` server anyway (a title that binds it itself is served the same model); `sdrdrv_calls=0` is the expected leg value for this title. Not a difference: an equivalence of the HLE seam. | `LibsdModule` test (the face's func 1/2/9/0xA); coldboot `libsdr_calls=init=1,remote=0`, `sdrdrv_calls=0`. |
| P4-SND-BOOT-SHAPE | 4E″ it.2 → **pinned 4E‴** | The coldboot leg's libsd census after the title's two STREAM RPCs (`stream_rpc_calls=2/0`): `libsd_calls=0:1,1:8,3:4,4:4,6:96,7:1,11:2,12:2,15:1` — the switch/addr/status rows are exactly **2×** P4-CDINIT-SOUND-SHAPE's single test request (SdSetSwitch ×2 / SdGetSwitch ×2 / SdGetAddr ×48 / ord 11 ×1 → ×4/×4/×96/×2, + BlockTransStatus ×2), plus an `SdInit` ×1 / SdSetParam ×8 / SdSetCoreAttr ×1 group the test request never produced. **The served command ids (4E‴, `stream_rpc_cmds=` decoded from the request records `{u16 header, u16 count, {u16 cmd, u16 nparams, u16 params[]}}`): `21,21,42,42,12,16,81,81,9,7,37,9`** — 12 commands over the 2 RPCs; **cmd 12 (CD_READ_INIT) appears ONCE**, so the 2× is NOT cmd 12 twice: the boot batch carries four commands issued in pairs (21, 42, 81, 9 — one per SPU2 core is the natural reading of a pair; the module's per-core setup walks the same switch/addr rows), and the `SdInit` + SetParam ×8 (the 4 voice params × 2 or the core volumes MVOL/EVOL/BVOL… of both cores) + SetCoreAttr group is the init command's (the first command, 21, or 16/7/37 — the per-command attribution needs a per-command libsd census, not in run.meta). Pinned as the TITLE's boot shape: ids + census together are the row; a change in either is a regression to explain. `frames=0` at the park, so no heartbeat polls (`SdGetParam` 0) are in the census. | coldboot leg `/tmp/p4e5/cb2/run.meta` (`stream_rpc_cmds=21,21,42,42,12,16,81,81,9,7,37,9`, `libsd_calls=` as quoted); the `StreamIrxServesFirstRpc` second case stays the single-request pin. |
| P4-SDR-BLOCK-CLOCK | 4E″ it.2 | OLD's virtual SPU2 block transfer (`libsdr.cpp:120-260`) ran on `std::chrono::steady_clock` (`PS2_SDR_BLOCK_MS` 8 ms per 0x4000 span) with an FMV-consumption "real mode"; the domain admits no wall clock, so `SdBlockTrans` paces the same ramp on the **mixer's frame count** (`audio.sdr_block_ms` × 48 frames; SETTING) — same address ramp shape, same completion/flip, deterministic per boundary. The real mode (`FmvPacerLive`, `PS2_SND_FMVPACE`, `PS2_SND_FMVQ_DIV`) needs `FmvStream` (Phase 7) and is not written; the SDRX section carries `{active, buf_index, elapsed_frames}` instead of `elapsed_ns`. An accepted difference in the pacing CLOCK only; unreached in both legs (`libsdr_calls=…remote=0`). | `LibsdModule` test (384 frames per block: 1 → 0x2000 → complete/flip; Export/Import re-base). |
| P4-SDR-REMOTE-SCOPE | 4E‴ | **Decision: match OLD.** OLD `sceSdRemote` (`libsdr.cpp:311-395`) forwarded ONLY `0x8010` rSdSetParam (→ `sound::SetParam(a2, a3)`), `0x80E0` rSdBlockTrans and `0x8100` rSdBlockTransStatus (the virtual block model) and answered 0 to every other code — including `0x8000` rSdInit (no `SdInit`: an EE-side init must not silence the voices the IOP module started) and the Get rows (a status query read 0 = "no progress"). 4E″ it.2's `SdrRemote::Dispatch` had forwarded the Get/Switch/Addr/CoreAttr rows and rSdInit as "the natural extension"; 4E‴ splits it: `Dispatch` (the EE row) = OLD's three-command scope, `Invoke` = the full rSd* model behind the SIF face `0x80000701` (OLD's `HandleLibSd` function table did forward func 0..8). Not an accepted difference — an equivalence restored before any leg reached the row (`libsdr_calls=…remote=0` on both trees). | `LibsdModule` test: `Dispatch(kInit)` queues no SilenceAll, `Dispatch(kGetParam)` → 0, `Dispatch(kSetCoreAttr)` leaves the shadow; `Invoke` rows read back; the bindings vector `0x8020` → `$v0 = 0`. |
| P4-SPU-LAT-REALTIME | 4E‴ | OLD's `SdVoiceTransStatus` under the REALTIME IOP policy reported the transfer BUSY for a `PS2_SPU_LAT_US` (300 µs, `iop_stubs.cpp:2362-2445` `ScheduleTransCompletion`/`TransStatusPoll`) wall-clock window after `SdVoiceTrans`, then delivered the completion; under OLD's tick-domain (FRAME) arm `due_us = 0`. NEW's `LibsdModule` schedules the completion `IopKernel::ScheduleEventUs(NowUs())` — due at once under BOTH policies, and the ord-11 poll retires it inline. **Accepted difference under `iop.dispatch=realtime` only**: a title polling ord 11 immediately after ord 9 sees 1 (done) where OLD saw 0 (busy) for ≤ 300 µs; the deterministic profile (FRAME) is unaffected and the coldboot leg has no voice transfer before the park (`libsd_trans=0/0/0`). Revisit when Phase 7's play profile drives the realtime policy (the window is the SPU2 DMA's real latency; a `Config.audio.spu_trans_latency_us` SETTING would restore it if a title depends on it). | `LibsdModule` test (the completion pending until the poll / the dispatch); the REALTIME leg is Phase 7's. |
| P4-SND-REALTIME-SAFE | 4E‴ | Under `iop.dispatch=realtime` the libsd bodies run on `IopHostThread` while the mixer pumps on the EE thread; 4E″'s `CommandRing` was a plain vector FIFO (drop-newest) and `SpuCore`'s readbacks plain fields — a data race under that policy. 4E‴ restores OLD's shape: `CommandRing` over `ps2r::CmdRing<SoundCommand, 4096>` (MPSC, producer-only mutex, lock-free consumer, **drop-OLDEST** as OLD `sound/ring.h`) and every published `SpuCore` field atomic (NAX/ENVX/VOLX/ENDX, the param/addr/core-attr shadows, the MVOL gains, the handler registry, the counters) with OLD's seq_cst eager-hold protocol (`sound_core.cpp:355-390`, `:842-848`: the hold flag before the eager stores; the pump re-asserts the eager values while a hold is up). The overflow policy change (drop-newest → drop-oldest) is OLD-equivalence, not a difference: the SPU2 register file keeps the latest write. A KON that drop-oldest discards leaves its eager hold UP until the next `SilenceAll` (`ClearEagerHolds()`), so that voice keeps publishing the eager NAX/ENVX rather than a stale model — OLD's behaviour too (`sound_core.cpp:842-848`); the hammer therefore asserts `holds == 0` only when `Dropped() == 0`. 4F (p4e4r): the pump SKIPS a held voice before its stores and rechecks after (OLD `:834-838`), the NAX/ENVX stores seq_cst. **4F″ (the intermittent `held_envx_zero` failure under `ctest -j8` load): the skip/recheck protocol left a WINDOW — the pump's hold check and its NAX/ENVX stores were separate operations, so a KON landing between them had its eager ENVX overwritten by the stale model (0) until the recheck; an EE-side poll in that window read ENVX 0 with the hold up (the SPU2 driver's watchdog-abort value). Engine bug, not a test race (the test re-reads the hold after ENVX and it is still up — the Kon is still in the ring). Fix: NAX, ENVX and the hold count are one 64-bit word (`readback_[vi]`); the key-on raises the hold and publishes the eager values in one CAS, the pump publishes the model with a CAS that fails if the hold field changed; no transient stale value can appear beside a raised hold. 30/30 hammer runs under `ctest -j8` load, `held_envx_zero=0` in every run.** | `SpuCoreHammer` (producer thread on the libsd surface vs the mixer thread, 20 000 iterations, 20 s deadline, every poll in range, KON count exact, ring drained); `MixerTickPump` ring vector updated to drop-oldest. |
| P4-LIBGRAPH-RELEASE | 4F | **Decision (orchestrator): Option A.** The title's manifest carries THREE libgraph rows only (`sdk_bindings.tsv:106-108`: `sceGsResetGraph`, `sceGsSyncV`, `sceGsSyncVCallback`); every other libgraph body (`sceGsResetPath`, `sceGsSetDefDispEnv`, `sceGsPutDispEnv`, `sceGsSetDefDrawEnv`, `sceGsPutDrawEnv`, `sceGsGetGParam`, `sceGsSyncPath`…) is RECOMPILED in both trees (OLD `sce_hle/libgraph.cpp` HLE'd the same three; its recompiled bodies write the GS privileged page through MMIO — NEW's Phase-3 sink page, counted). So the trap release is two rows: `engine/gs/hle/sce/libgraph_bindings` (`sceGsResetGraph` = CSR RESET + the GsGParam words `{interlace, mode, ffmode}` at the title's `0x2e7ff0` (`TitleProfile::gs.gparam_addr`) + `SetGsCrt` + the BIOS power-on display environment SMODE2/DISPLAY1/2/DISPFB1/2, as OLD `gs_hle.cpp:822-895`; `sceGsSyncVCallback` = the VBON INTC handler through the kernel table, GsGParam +0x08/+0x0C) over the Phase-3 GS-priv MmioSink page (the writes reach it and are counted) plus a **`GsPrivRegs` shadow (`engine/gs/state/gs_priv_regs`)** recording the written values. **Landed 4F, owner Phase 6** (the libdma precedent). Open for Phase 6: the SDK's `sceGsGParam` is a packed `{short inter, short omode, short ffmode, uchar version…}` layout, OLD wrote three u32 words — NEW matches OLD (the baseline's RAM image); the a0/a1/a2 → `SetGsCrt(interlace, mode, ffmode)` argument order is OLD's reading too. | `LibgraphBindings` test; run.meta `libgraph_rows=2`, `libgraph_calls=resetgraph=,syncvcb=,syncv=`, `gs_priv_writes=`. |
| P4-FRAMES-CAP | 4F | The EE composition root had no `--frames` stop and no present: a leg reaching boundaries would run until the bounded runner killed it, `frames=` stayed 0 and `frames.fhash` (the runner's progress file) never grew. | Slot 8 (`Step::Verify`) now presents the Phase-3 stub's black frame per boundary (`frames=` counts, `frames.fhash` grows; the `frames` lane stays unclaimed — Phase 6's GS owns slot 0) and ends the leg at `cfg.harness.frames` with `park=none park_detail=frames_cap` through the leg closer after the census (`write_census`, shared with the park sink). |
| P4-COLDBOOT-CKPT0-DIVERGENT | 4F → **attributed 4F′** (rows P4-CKPT0-METHOD … P4-CKPT0-STATICS, P4-LIBGRAPH-SDK-SHAPE below) | **The coldboot leg reaches boundary 0 and 60 more** (`snd_pumps=60`, `libgraph_calls=resetgraph=1,syncvcb=0,syncv=60`): `mem.mhash` line 0 = **`0,0x04749f8d3d458125` vs the baseline's `0,0xeec37d056857657b` — DIVERGENT at checkpoint 0**; audio windows 0–9 IDENTICAL to the baseline's silence (`0x0a6fb8af536edb25`, trivial); 3 reps identical. **Attribution: NOT YET MADE** — no `memdiff` exists (planned 4F, not landed) and no OLD RAM image at boundary 0 is available (the frozen anchors start at f36935). Pre-registered suspects, in order: P4-SIFINIT-STATICS (OLD `hle_sceSifInitRpc` seeded libkernl's RPC statics at title addresses, `sif_hle.cpp:64-120`; NEW does not), the HEAP section / `heap_limit_cap` image parity, the padman/libpad statics OLD's `HandlePadman` fan-out wrote (P4-PADMAN-READ-FANOUT), OLD's `sceGsSyncV` HLE's own RAM writes (`gs_hle.cpp:405-765`), the deci2 line buffer (P4-PRINTF-SINK — OLD never bound `kputchar`), the sbrk word. **Blocker per §6.1 (a row with N set and an empty attribution)**: the mem lane claim is `K=0`; 4F′ opens with the attribution (a NEW RAM dump at boundary 0 via `harness.state_every=1` + an OLD `--tree v2` savestate at f0, `memdiff` page list → sink (a)–(d)). | §6.1 coldboot row; `PHASE4_IMPL.md` §4F. |
| P4-LOADER-SEAM-PARK | 4F | Past boundary 60 the coldboot leg parks at **`park=iop_dependent park_pc=0x204020 park_tid=1 park_detail=HLE_FUN_002ad8f0 loader hook 3`** — one of the five Phase-3 loader file-IO seams of P3-SEAM-2/3 (`seams.cpp:92-96`, OLD `hle_heap.h:283-287`: windowed ISO read `0x2aac80`, `WaitForReadComplete` `0x2ad1a0`, `0x2ad8f0`, `0x2adb18`, Close Handle `0x2adb40`), reached from title code at `0x204020` — the EE-side loader path that plan §2.5's `hle/loader_model` (4D, carried as "the `LoaderModel` producer") owns; a Phase-4 row, not a Phase-5/6 device. 4F named all `iop_dependent` seams in their park record (the row-23 `sceCdSt*`/`sceCdDiskReady*` seams, the four `KernlFileIo` rows, the five loader hooks) so a park is attributable by `park_detail` alone. | 4F′: bind `HLE_FUN_002ad8f0` (+ its four siblings) over `LoaderModel`/`StreamReader`/`DeliveryMeter` per OLD `hle_heap.cpp` (the 4D row), re-run; the further stop is then expected on a Phase-5/6 page. **4F‴ BOUND:** the five bodies are `games/<t>/hle/loader_sync/loader_file_io.{h,cpp}` (`LoaderFileIo`: open/size/close/windowed read/wait over `ReadEngine` + `LoaderModel`; STREAM cmd 31/32 forwarded through the recompiled `FUN_001e2db0`/`FUN_001e74a8`/`FUN_001e2958` with the wire string in the frame below `$sp`, where retail's own frame stood), defined as title glue (`hle/glue/loader_glue.cpp`), the engine parks in `seams.cpp` removed. `LoaderSeams` test (26 checks, byte-exact vs the ReadEngine's sectors, delivery at slot 4, the blocking wait). **Leg: `frames=60`, the park MOVED to `park=vu_micro_absent park_pc=0x204020` (Phase 5)** — the loader hook no longer parks; `stream_rpc_cmds` now carries 31/32 (`…13,31,32,9,31,9,13,31,9,32,9,31,9`). Not yet ported: the DATA.BH archive-relative name resolver (`InitArchiveTable`/prefix cursor, OLD `hle_heap.cpp:391-790`): such opens return -1 and are counted (`archive_misses`); the DMA-chain save/restore of the sync path (`0x450180`, a workaround) and the `DEFERRED_READ_MAX` cap were not transplanted. `DeliveryBudget(0)` (unmetered) until a Config setting binds it. |
| P4-CKPT0-METHOD | 4F′ | **The attribution method (the reviewer's, now the tree's).** OLD has no boundary-aligned headless state drop (`PS2_VERIFY_STATE_EVERY` requests an async save that lands frames later: `state_b1.ps2st` vf=3), so the reviewer stopped BOTH binaries under `lldb` at the boundary-0 hash and dumped RAM/SPAD/arena (`/tmp/p4fr/{old,new}_{ram,spad,arena}.bin`). 4F′ made the NEW half a harness feature: `--state-every K` now WRITES `state_b<N>.ps2st` at boundary N (N % K == 0, N = the `mem.mhash` line index; synchronous atomic write in slot 8 after the hash) and `diff_legs.py --mask-ranges FILE` compares paired `state_b<N>.ps2st` images with the masked RAM ranges zeroed (FNV chain RAM→SPAD→META = `state_hash.py`'s). **Proofs:** NEW `state_b0.ps2st` hashes to exactly `mem.mhash` line 0 (`0x7b92ccfb3755a5b1` both) — the state file IS the image behind the line; the reviewer's OLD lldb dumps wrapped as a synthetic `state_b0.ps2st` hash to **`0xeec37d056857657b` = the frozen baseline's line 0** — the dump IS OLD's boundary-0 image, and OLD's mem lane covers RAM+SPAD+META in the same chain. Page-level attribution then is a 4 KiB page diff of the two images (`/tmp/p4f2/residual.py`). | `app/host/ee_bootstrap.cpp` (slot 8), `verify/diff_legs.py` (`--mask-ranges`, `IDENTICAL-UNDER-MASK` verdict word, selftest cases), `verify/mask/coldboot_elf_text.ranges`. |
| P4-CKPT0-ELF-TEXT-RESIDENCY | 4F′ | **487 → 474 of the divergent pages were the ELF `.text`** (`0x00100000-0x002d9a08`; the single PT_LOAD is `vaddr 0x100000 filesz 0x20a0b8 memsz 0x2dae80`): OLD left every text page ZERO (its first non-zero byte at `0x2d9a10` = `.vutext`, which both trees load), NEW's text is byte-equal to the file. **Retail rule:** the kernel's ELF loader copies every PT_LOAD byte; the title can read (and checksum) its own code. **Decision: NEW keeps PT_LOAD text; OLD's zero text is OLD's deviation — MASKED, not re-frozen** (baselines stay OLD's by contract; the claim compares under `verify/mask/coldboot_elf_text.ranges` = `0x00100000-0x002d9a10` and the §6.1 row says "under mask"). | mask file + this row; `diff_legs … --mask-ranges verify/mask/coldboot_elf_text.ranges`. |
| P4-CKPT0-MCSERV-VERSION | 4F′ | **NEW was wrong — fixed.** libmc 3.0.1's `sceMcInit` (`0x2c51d4-0x2c5280`) issues mcserv fno 0xFE with a 12-byte reply `{result, mcserv_version, mcman_version}` into `0x3c9b40` and REFUSES `mcserv_version < 0x20A` (`0x2c522c slti v0,v0,0x20a` → "libmc: too old release of mcserv.irx", v0 -91) and `mcman_version < 0x20E` (`0x2c5250 slti v0,v0,0x20e` → "… mcman.irx", v0 -121). NEW's `McservModule` answered the modules' RegisterLibraryEntries versions (0x201/0x207) — the wrong words: the reply carries the modules' PROTOCOL versions, which the disc's IRXs hold in `.data` right after their SDK stamps: MCSERV.IRX `"PsIImcserv  3010"` … **0x210**, MCMAN.IRX `"PsIImcman   3010"` … **0x226**. Both pass the checks (retail behaviour: the memory card works). Evidence in RAM: before the fix NEW's boundary-0 image held `"libmc: too old release of mcserv.irx\n"` in libmc's line buffer at `0x3ca900` (with 0x210 alone, `"… mcman.irx\n"`); after the fix the buffer is zero, `0x3c9b44/48 = 0x210/0x226`, and libmc's client record `0x3c8580/88/a4` is filled by the REAL init (`0x100000`, `-1`, the RPC handle word) — OLD's `0x3c85a4 = 0xdead0400` was its HLE'd `sceMcInit` planting a fake handle without running libmc (OLD never wrote the version words: `0x3c9b44/48 = 0`). **Residual vs OLD after the fix: `0x3c8580/88` and `0x3c9b44/48` = NEW retail, OLD deviation (HLE'd libmc) — accepted, mask candidates; `0x3ca900` gone.** | `engine/mc/hle/mcserv_module.h` (`kMcservVersion 0x210`, `kMcmanVersion 0x226`), `LibMcSaveFlow` asserts the constants; coldboot `mem.mhash` line 0 `0x04749f8d3d458125` → `0x7b92ccfb3755a5b1`. |
| P4-CKPT0-LIBDMA-TABLE | 4F′ | libdma's channel table at `0x323288..0x3232f4` (7 words): OLD `0x01cff000, +0x40, +0x80, +0xc0, +0x100, +0x200, +0x240` — an INVENTED RAM mirror (its page `0x01cff000` also holds OLD's `0x40` sentinels, 7 more words); NEW `0x10008000 (VIF0), 0x10009000 (VIF1), 0x1000a000 (GIF), 0x1000b000 (fromIPU), 0x1000b400 (toIPU), 0x1000d000 (fromSPR), 0x1000d400 (toSPR)` — the real DMAC channel register bases (4D″'s "DMAC global words made real"). **Retail rule:** `sceDmaReset` stores the channels' MMIO addresses; the title's `sceDmaGetChan` hands them back and the title writes them as MMIO. **Decision: NEW retail, OLD deviation — accepted; MASK both ranges** (`0x323288-0x3232f8`, `0x01cff000-0x01cff300`) once a second mask file is admitted; recorded here. | this row; the mask lines are listed in `verify/mask/coldboot_elf_text.ranges`'s header as candidates, NOT yet applied (one accepted range per stage review). |
| P4-CKPT0-LIBCDVD-RECORDS → **renamed P4-CKPT0-STREAM-REPLY-PATCH (I4-B)** | 4F′ | Two `sceCdlFILE`-shaped statics — `0x309b38` (.sdata: lsn `0x97c4ae`, size `0x7291f`, `0xc83800`, 1, 7, `0x88800`, `0x7c`, `0xa16e`, `0x2c`) and `0x3b30c8` (.bss, the same record: `0x88800, 0x701, 0xc83800, 0x7291f, 0x97c4ae …`) — FILLED in OLD, ZERO in NEW. These are the loader's file records (lsn/size of DATA.BD-class ISO files), written by the loader path OLD ran BEFORE boundary 0 through its HLE'd loader hooks (`hle_heap.cpp:855` `HLE_FUN_002ad8f0` → `SearchIso9660`/`ForwardStreamRegisterFile`); NEW parks on that very hook (P4-LOADER-SEAM-PARK) at frame 60 — i.e. the NEW boot reaches the loader LATER than OLD (an ordering difference: NEW's boundary 0 precedes the first loader open, OLD's follows it), not a missing body. **Retail rule:** filled by `sceCdSearchFile` when the loader opens the file — WHEN is the question; the retail order is the recompiled title's, so it is decided by binding the seam (4F″) and re-diffing: if NEW then fills them before boundary 0, resolved; if not, OLD's earlier fill is OLD's deviation (its instant HLE open). **4F″:** the values re-read from both boundary-0 images (`/tmp/p4f3/openclasses.py`): the two pages carry ONE table twice — `(lsn 0x7291f, size 0xc83800)`, `(lsn 0x701, size 0x88800)`, `0x97c4ae`, counts `0x7c/0xa16e/0x2c` — i.e. the loader's disc-extent table, not `sceCdlFILE` records with names (the name fields are empty in OLD too). NEW's zeros are the UNBOUND loader seam (P4-LOADER-SEAM-PARK, still parked at frame 60 this stage: `frames=60 park_detail=HLE_FUN_002ad8f0 loader hook 3` ×3 reps) — a missing body; whether retail fills them before boundary 0 remains the ordering half, decided by the OLD `--tree v2` leg with `PS2_VERIFY_STATE_EVERY=1` (not run this stage) once the seam binds. **OPEN → 4F‴.** | this row; re-check after P4-LOADER-SEAM-PARK. **4F‴ wording fix:** the two tables are NOT the same record — they share only the word `0x97c4ae`; `0x309b38` holds `{0x97c4ae, …, (0x7291f, 0xc83800), 1, 7, 0x88800, 0x7c, 0xa16e, 0x2c}` and `0x3b30c8` holds `{0x88800, 0x701, 0xc83800, 0x7291f, 0x97c4ae, …}` (the loader's file table, two layouts). **Re-diffed after the seams were bound (4F‴, `/tmp/p4f4x/openclasses.log`): still ZERO in NEW at boundary 0, 9 + 8 words.** With the seams live the table is written from the STREAM.IRX reply path (cmd 31 registration / the 6-word size reply retail's `FUN_002adb18` reads), which NEW serves through the real IRX at boundary slot 4 — so the remaining half is ORDERING (does the reply land before boundary 0 on retail?): the OLD `--tree v2` leg with `PS2_VERIFY_STATE_EVERY=1` decides. ~~OPEN.~~ **I4-B (2026-09-28): CLOSED-OLD-wrong → renamed P4-CKPT0-STREAM-REPLY-PATCH.** Not libcdvd records, not a loader table, not an ordering question: the 8 `.bss` words are constants OLD's HOST code plants into the STREAM.IRX RPC reply buffer on every reply (`sif_rpc_handlers.cpp:4632-4671`, words 9N+50/51/52/53/56/59/61/62 = `0x88800, 0x701, 0xC83800, 0x7291F, 0x97C4AE, 0x7C, 0xA16E, 0x2C` — "the values that PCSX2 shows at the same point in the game"), observed firing on both pre-boundary-0 replies while the module's own globals were zero (OLD `CT_FULL_LOG=1` leg: pre-patch `CD globals:` dump lists none of the sources, the next line shows the post-patch words); the 9 `.sdata` words are the title's `FUN_001e1f48` copies of them (8/8 + 9/9 matched). NEW's `StreamRpcServer::Dispatch` copies the module's real reply and is right. **Accepted difference — MASKED** (8 ranges appended to `verify/mask/coldboot_elf_text.ranges` by I4-S); the record row is `EQUIVALENCE_PLAN.md` §7.1 `P4-CKPT0-STREAM-REPLY-PATCH`, the evidence is `INTEGRATION_IMPL.md` §I4-B (finding INT-I4B-LIBCDVD-RECORDS). Under the repo mask RAM0 is identical (`differing words outside mask: 0`); checkpoint 0's `IDENTICAL-UNDER-MASK` verdict still needs the P4-CKPT0-STATICS (5) SPAD dead-stack extension (13 words `0x70003ef0-0x70003f40`, `diff_legs` masks RAM0 only). Watch INT-I4B-PLANTED-0x7C-GATE at the first checkpoint past the loader. |
| P4-CKPT0-LIBM-ULP | 4F′ | Four float-table regions differ by 1–50 ULP: `0x3320c8..0x3321a8` (33 words, a computed table), `0x3b4a64..0x3b4a98` and `0x3b52e4..0x3b5318` (sin/cos-shaped pairs `±0x3e785b43/±0xbf785b43/0xbdcbc89a…`, 6+8 words), `0xb9e744..0xb9e828` (14, the same pairs in a heap object). NEITHER tree HLEs libm (no `sinf/cosf/sqrtf` manifest rows in either; OLD's only `sqrtf` use is host-side in `ipu.cpp`), so both run the title's recompiled routines and the difference is in the RECOMPILED FPU arithmetic: neither build sets `-ffp-contract` (clang default `on` → mul+add in one expression fuses to FMADD on arm64) and the two generators emit different expression shapes. **Retail rule:** the EE FPU has no fused multiply-add (MADD.S rounds the product, then adds), no denormals (FTZ/DAZ), round-to-nearest; the game's own routines are ground truth and PCSX2's EE FPU model is the arbiter. **Decision: OPEN — NOT adjudicated this stage**; next: build the recomp shard with `-ffp-contract=off`, re-diff these pages, and settle the side against a PCSX2 memory dump at the same boot point. Until then these pages are UNMASKED divergences (no mask, no fix). **4F″:** the differing words are NAMED values — `±0x3e785b4x` = 1/√17 (0.2425356…), `±0xbf785b4x` = 4/√17, `0xbdcbc89x` = 1/√101, `0xbf7ebac2` = 10/√101: unit vectors of the directions (1,4) and (1,10), i.e. a 2-D normalize (`x·rsqrt(x²+y²)` / `x/sqrtf(…)`) in a recompiled title routine, NOT a libc HLE row (no libm row exists on either tree). ~~`0x3320c8..a8` no longer differs after 4F′'s fixes~~ **(4F⁗ correction: that claim came from a range typo in `/tmp/p4f3/openclasses.py` — `(0x3320c8, 0x3320ac)`, an EMPTY range — the 33-word table still differs, up to 50 ULP; fixed to `0x3321ac`.)** The correctly rounded float of 1/√17 is `0x3e785b42`; OLD holds `…43` (1 ULP high) in all four tables, NEW holds `…42` in three tables and `…44` (2 ULP high) in the fourth (`0x3b52e4..`) — NEW is not even self-consistent across the tables, the signature of `-ffp-contract=on` fusing differently per expression shape; the recomp shards set no `-ffp-contract` (grep of the CMake tree: none). The PS2 arbiter was unavailable this stage (`pcsx2-debug` DebugServer not reachable). **Next:** `-ffp-contract=off` on `ps2r_title_recomp`, re-diff, then the PCSX2 dump names the exact side. **OPEN → 4F‴.** | this row; the four ranges named. ~~**4F‴ rename: 1–2 ULP** (the table maxima are ±1 ULP in three regions, 2 ULP at `0x3b52f8`; the 50-ULP figure was the `0x3320c8` region, which no longer differs).~~ **4F⁗: REVERTED to "1–50 ULP, four regions (61 words)"** — the 4F‴ rename rested on the tool's empty range (above); `0x3320c8..0x3321a8` differs in all 33 words, max 50 ULP (`/tmp/p4f5/ulp_fixed.log`). **`-ffp-contract=off` landed on `ps2r_title_recomp` (P4-FPCONTRACT) and did NOT move a single word** (`0x3b4a64` NEW `…42`, `0x3b52e4` NEW `…44`, OLD `…43` both): contraction is not the cause; NEW's two tables still disagree with each other, so the two routines reach the value through different FPU paths — next: diff the EE FPU helper semantics (DIV/SQRT/RSQRT rounding, the PS2's non-IEEE results) between OLD `host_app` and NEW `ps2r_ee`, then PCSX2 (`read_memory 0x3b4a64 0x40`) for the side. ~~OPEN.~~ **4F⁗: CLOSED — NEW was wrong, fixed (P4-EE-ROUNDING):** the helpers were identical; NEW's guest threads ran at the host default `FE_TONEAREST` because nothing called `Ps2Float::SetGuestThreadRounding()`, while the EE FPU (and OLD) round toward zero. After the fix all 61 words (the 33-word table + the three unit-vector tables) are byte-identical to OLD at boundary 0 (`/tmp/p4f5/residual_cb4.log`). |
| P4-CKPT0-STATICS | 4F′ | The remaining words, one by one: **(1)** `0x3caa58..0x3caa90` (9 words: libkernl's SIF RPC packet ring head/tail `0x203ca980/0x203caa00`, ring `0x3caa80..0x3cac00`, step 0x20, the first packet's function words `0x2d1a18/0x2d19f8`) — filled in OLD by `hle_sceSifInitRpc` (`sif_hle.cpp:83-113`), zero in NEW = **P4-SIFINIT-STATICS confirmed as the G3 case**: retail's recompiled `sceSifInitRpc` fills them, so NEW's zero is NEW's deviation — **fix pending 4F″** (`games/<t>/hle` data pair applied by `SceSif::InitRpc`). **(2)** `0x30a138, 0x30a158, 0x30a178, 0x30a1a8, 0x30a268, 0x30a420, 0x30a448, 0x30a450, 0x30a620` (.sbss, 9 words): OLD `0x1fff`, NEW `0x2000` — writer NOT found this stage (the values look like a `size-1` mask vs a `size` — nine independent statics with the same shape suggests one SDK routine; grep OLD for `0x1fff` and NEW's recomp for the `.sbss` stores next). **OPEN.** **(3)** `0x2ea850` (.data): OLD 1, NEW 0 — **4F″: writer FOUND** — it is libkernl `sceSifInitRpc`'s `is_initialized` flag (`DAT_002ea850`; OLD `sif_hle.cpp:63-72` writes 1 in `hle_sceSifInitRpc`); retail's recompiled `sceSifInitRpc` sets it, NEW's `SceSif::InitRpc` row (`sceSifInitRpc@0x2d2180:5` hits) does not → **NEW deviation, the same class as (1)**: the fix is the same `games/<t>/hle` data pair (the flag + the 9 ring words + the two function words) applied by `SceSif::InitRpc` — **fix pending 4F‴**. (2) `0x1fff`/`0x2000` ×9: no literal in OLD's host sources or NEW's recomp shards (the stores are register-relative); OLD's `kMcFreeClusters = 0x2000` (`hle_heap.cpp:3898`) is the only `0x2000` static and is libmc-side — not these; **still OPEN** (a write-watch run on `0x30a138` is the next tool). **(4)** `0x3c8580/0x3c8588`, `0x3c9b44/0x3c9b48`: NEW retail (see P4-CKPT0-MCSERV-VERSION). **(5)** the 13 scratchpad words `0x70003ef0-0x70003f40`: stack residue at the top of the SPR stack (return addresses `0x204510`, `0x1815e0`, frame words `0xb8d5d8/0xba95d8`) — the call chains differ wherever one tree's HLE row replaced a recompiled frame; retail's residue is the recompiled chain's, which neither tree reproduces exactly. **Decision: a mask on the dead-stack window is the honest record** (proposed range `0x70003ef0-0x70003f44` in the SPAD section — the differ masks RAM0 only today; SPAD masking is a 4F″ extension). | this row; `/tmp/p4f2/residual2.out` (the post-fix page list). **4F‴: (1)/(3) CLOSED** — `SifInitStatics::ExampleTitleUS()` (`games/<t>/hle/sif_init_statics.{h,cpp}`: flag `0x2ea850`, the `0x180`-byte clear at `0x3caa80`, 8 ring words `0x3caa58..0x3caa74`, 4 handler words `0x3caa80/84/8c/90`) applied by `SceSif::InitRpc` on the first call (`SceSif::InitRpcStatics`, `SetInitRpcStatics`); `0x2ea850` now OLD 1 / NEW 1 and its neighbours identical (`/tmp/p4f4x/openclasses.log`). **(2)** `0x30a138…` ×9 still `0x1fff` OLD / `0x2000` NEW — ~~OPEN (write-watch next)~~ **4F⁗: CLOSED with P4-EE-ROUNDING** — a `cvt.w.s` of a product just below 8192 truncates to `0x1fff` under the EE's round-toward-zero (OLD, retail) and rounds to `0x2000` under NEW's missing mode; identical to OLD after the fix, no write-watch needed. |
| P4-LIBGRAPH-SDK-SHAPE | 4F′ | **NEW (and OLD) were wrong — NEW fixed to the SDK shape.** The title's `sceGsResetGraph` (`0x2c1bd8`) is `int sceGsResetGraph(short mode, short interlace, short omode, short ffmode)`: mode 0 → CSR `RESET` (0x200), mode 1 → CSR `FLUSH` (0x100); `GsGParam` = shorts `{interlace +0, omode +2, ffmode != 0 +4, GS revision +6 = CSR bits 48..55}` then the SyncV callback fn (+8) / handler id (+0xC), which a reset tears down (`_DisableIntc(2)`, `RemoveIntcHandler(2, id)`, both zeroed); `GsPutIMR(0xff00)`; tail call `SetGsCrt(interlace & 1, omode & 0xff, ffmode & 1)` whose v0 is the result. OLD (`gs_hle.cpp:822`) read a0/a1/a2 as `{interlace, mode, frame}` and wrote three u32 words — so OLD's GsGParam at `0x2e7ff0` was `{0, 1, 2}` (= `{mode, interlace, omode}` mislabeled) and its `SetGsCrt` got `(0, 1, 2)`; 4F's NEW matched OLD. Now NEW writes `0x00020001` at +0 (`interlace 1, omode 2`), 0 at +4/+6, and calls `SetGsCrt(1, 2, 0)` — the boundary-0 image differs from OLD at `0x2e7ff0/f4/f8` **by design: OLD deviation, accepted; mask candidate `0x2e7ff0-0x2e8000`**. Still OLD-parity, still open for Phase 6: the BIOS power-on display environment writes (SMODE2/DISPLAY1/2/DISPFB1/2, `gs_priv_writes=6`) are OLD's `[FIX]` block, not the SDK row (the kernel's `SetGsCrt` programs SMODE1/2 + SYNCH/SYNCV; the title programs its display environment through `sceGsPutDispEnv`), and the revision short reads the shadow's CSR (0 after RESET) where a real GS answers REV/ID (`0x1B`). | `engine/gs/hle/sce/libgraph_bindings.cpp`, `engine/gs/state/gs_priv_regs.h` (`kCsrFlush`), `LibgraphBindings` test (`ResetGraph(0,1,2,0)` → shorts `1,2,0`). |
| P4-FPCONTRACT | 4F‴ | **Cross-host determinism requirement:** the EE has no fused multiply-add, so every recompiled float expression must round after each operation on every host. `target_compile_options(ps2r_title_recomp PRIVATE -ffp-contract=off)` (clang's default `on` fuses `a*b+c` within an expression, and does so differently per expression shape and per target). Landed 4F‴; it changed no boot byte in this stage's images (the open ULP class is not contraction — P4-CKPT0-LIBM-ULP), but it is a standing requirement for the two trees and two hosts to agree bit-for-bit. |
| P4-SIFINIT-STATICS | closed 4F‴ | See P4-CKPT0-STATICS (1)/(3): the libkernl `sceSifInitRpc` statics are a `games/<t>/hle` data pair (`SifInitStatics`) consumed by `SceSif::InitRpc`; `0x2ea850` and the `0x3caa58..0x3caa90` words now match OLD at boundary 0. mem.mhash line 0 moved from `0x7b92ccfb3755a5b1` to `0x27927b50713c8bee` (this pair + the loader table writes of the bound seams). |
| P4-LOADER-WAIT-FORCED | 4F‴ | The loader's blocking wait (`FUN_002ad1a0`) completes an in-flight read INSIDE the call (OLD `CompleteDeferredReadForObj`, the blocking path): `LoaderModel::CompleteNow(fs)` delivers and consumes it now; the slot-4 closure already scheduled becomes a no-op through a per-read generation (a stale closure never delivers a later read on the same fs). The DeliveryMeter's virtual-drive head/busy time keep the original schedule (the drive's work was done); the DeliveryBudget is not charged for a forced read. `cdvd_deliveries=0/2` in the 60-frame leg: two reads were issued and forced by the wait before any boundary delivered them. |
| P4-EE-ROUNDING | 4F⁗ | **NEW was wrong — fixed.** The EE FPU rounds toward zero (PCSX2's default EE round mode Chop/Zero; no GameDB override for SLUS-12345); DIV/SQRT/RSQRT are the exception (nearest, single precision — the `ps2_div_s`/`ps2_sqrt_s`/`ps2_rsqrt_s` helpers, identical in both trees). OLD set `fesetround(FE_TOWARDZERO)` on every thread running recompiled code (`main.cpp:694`, `ps2_threading.cpp:466`); NEW declared `Ps2Float::SetGuestThreadRounding()` and never called it, so MUL/ADD/SUB/CVT rounded to nearest — the 1–50 ULP class (61 words) and the `0x1fff/0x2000` statics (9 words) of checkpoint 0. Fixed in `ThreadManager::RunGuestThread` and the composition root; `mem.mhash` line 0 `0x27927b50713c8bee` → `0x03018fa3110d34b5`; both classes identical to OLD. Standing requirement with P4-FPCONTRACT: recompiled EE/VU0-macro code runs under `FE_TOWARDZERO` on every host thread that executes it. | `thread_manager.cpp`, `ee_bootstrap.cpp`; no unit test yet (a `Ps2FloatRounding` test asserting `fegetround()==FE_TOWARDZERO` inside a guest thread is the Phase-5 opener nit). |
| P4-VU-PARK-OP | 4F⁗ | The coldboot park is the title's **`vcallms 0x1e` (VU0 micro byte address `0xf0`) in `FUN_0018b918`** — the angle→sin/cos micro subroutine (`cvt.s.w`, `× 2π/65536`, `lqc2 vf20`, `vcallms 0xf0`, `sqc2 vf31`); 54 sites in the shards. `park_pc` is stale (it is the last HLE row's `$ra`, `0x204020`); the site came from `lldb` on `Vu0Macro::Execute`. The boot's one unmodelled DMA kick (`dmac_unmodelled_kicks=1:0x1`, VIF0) is the microprogram's MPG upload. No interim INERT is legitimate (the result is consumed). | `seams.cpp` `VuParkDetail` → `park_detail=vcallms micro pc 0xf0 (VU micro memory + interpreter absent)`; Phase 5 order: DMAC ch0 + VIF0 MPG → VU0 micro memory, the VU0 micro interpreter, the M-bit interlock. |
| P4-LOADER-ARCHIVE-MISS | 4F⁗ | `LoaderFileIo::Open` answers -1 for an archive-relative name (the DATA.BH resolver, OLD `hle_heap.cpp:391-790`, is not ported); counted as `loader_archive_misses=` in `run.meta`. Coldboot to frame 60: `loader_opens=4 loader_misses=0 loader_archive_misses=0` — not reached. Accepted as an OPEN seam, not a difference, until a leg counts one. | `run.meta` keys `loader_opens/misses/archive_misses/forced/reads`. |
| (stage rows) | 4A–4F′ | one row per finding (a divergence attribution, a §7.1 candidate, a park that was not predicted, a split taken) | as Phase 3's §7.1 |

Stage rule reminders (STAGE_RULES): each stage records its handoff in `docs/phases/PHASE4_IMPL.md` (files, OLD sources
transplanted, flags consumed with outcome, tests added, gate status, next command); no stage edits `iop_recompiled.*` by hand;
every OLD binary run goes through `verify/run_bounded.sh` and OLD's `logs/` is never written; helpers under `/tmp/p4<stage>/`,
never `python3 -`; no commits.

## 8. Review notes

### 8.1 Step-1 self-review (§1 + §3, 2026-09-27)

Checked against the sources:
- Park PCs/tids and the relaunch count are quoted from the 3F′ handoff's expected `run_leg` output;
  the boundary-0 target, the ≥ 60-boundary anchor run length, the audio window-0 hashes and the sink
  list (a)–(d) are from CONSTRUCTION §4.4; the lane-move rationale is D-SEQ-1 verbatim in substance.
- §3.1 restates ARCHITECTURE §7.1 without adding mechanisms: the eight lines map to §7.1's five bullets
  (kernel/coroutines; custom IRX; RPC both directions; deferred events; dispatch policy + savestate).
- The BUG-026 signature is the one CONSTRUCTION §4.4 records; the bug file itself was not read in this
  step and is updated only when the root cause is found (§3.4 proof item iii).

Open items for later steps (not resolved here by design):
- **Slot index.** "Boundary slot 5 calls `dispatch()`" is taken from the brief; step 2 must confirm it
  against `BoundarySequencer::Order` (PHASE3_IMPL §3D2 slot table) when the module table names the
  step, and must fix the relative order of the IOP dispatch step and the `Mixer` pump step to OLD's
  boundary order (IOP writes SPU2 voice state that the mixer consumes; the audio window hashes are the
  arbiter).
- **Run length vs claim length.** ≥ 60 boundaries on anchors is the RUN length; the CLAIM length is the
  stopping rule (§1.3). §6 must state both per leg so a short claim is never mistaken for a short run.
- **BUG-038.** Named in scope as gate-blocking; its signature and closure plan belong to §6/§7 (step 4),
  next to the coldboot leg definition.
- **`PS2_HB_TICKCLOCK`.** Listed NORM in §4.4; its exact arm (heartbeat as a kernel timer on the kernel
  clock) is assigned in §5 at step 3, not assumed here.

### 8.2 Step-2 self-review (§2, 2026-09-27)

Checked against the sources:
- Every §4.4 group has rows: kernel 6 (= the 6 OLD `iop_kernel/` pairs, `iop_clock` gaining a `.cpp`),
  memory/loader 4, Sony libs 18, sif 5, cdvd 8, dmac 1 (assertion), sound 10, pad 2, mc 3, stream_irx 8,
  workarounds 6 — 71 rows. Class names are the OLD class names where OLD had a class (`IopKernel`,
  `IopCoroutine`, `IopHostThread`, `IopClock`, `SifRpcBridge`, `IopModuleTable`) and snake_case(type)
  file names everywhere (two §4.4 file names renamed: `loader` → `iop_loader`, `hle/loader` →
  `hle/loader_model`).
- Two §8.1 open items CLOSED here: (i) slot index — `BoundarySequencer::Step::IopDispatch` is enum
  index 4, the fifth slot, so §3.2's "slot 5" is the 1-based count (`Step::EeStreamRpc` = index 5
  services the marshals); (ii) the forced-advance bound — `IopClock::FrameCeilingUs()` caps intra-frame
  forced advances at one frame quantum past the current base (`iop_clock.h:63`), i.e. the next
  boundary's tick, as §3.2 proposed. The Mixer-vs-IopDispatch ORDER stays open (sound row → §7).

Findings (each is a doc delta or a step-3/4 item, none silent):
1. **Sony-lib set vs §4.4.** `iop_stubs.h:5` binds 12 libraries; §4.4 lists `thevent`/`thmsgbx`/
   `loadcore` (not imported by STREAM.IRX, not disc modules) and omits `sysmem`/`intrman` (imported).
   §2.3 follows the evidence (18 dirs). CONSTRUCTION §4.4 gets a doc delta — including its `:460` wording that
   labels `cdvdman` "disc-side": it is an IOPRP300 module (the 8th disc IRX is STREAM.IRX; §8.6 item 5); `IopKernel` keeps the
   thevent/thmsgbx APIs for the ported unit tests, no module dir is created for them.
2. **Generated-ABI include paths.** `build-v2/iop_recompiled.h:3-5` includes `../host_app/{iop_context,
   iop_memory,iop_stubs}.h` by relative path; since generated code is never edited, §2.10 adds the three
   include-compat headers under a directory literally named `host_app` plus the `stub_surface` row that
   keeps the `iop_stubs::` names as ABI. This is a NEW row not in §4.4 (analogue of Phase 3's
   `recomp_abi`); step 3 adds the grep oracle (every `iop_stubs::` name the `.cpp` references resolves).
3. **Flags outside §4.4's 42.** `PS2_CDVD_BUDGET_KB` (ledger `:307`, NORM, `ps2_vblank.h:163`) and the
   five `PS2_LOAD_BUDGET_LIFT*` rows (ledger `:491-495`, NORM, `budget_lift.h`) belong to the
   `delivery_meter`/`delivery_budget` rows but are absent from the §4.4 count; `PS2_MAINT_EEPACED` is
   SETTING in the ledger (`:510`) but NORM in §4.4; `PS2_SND_TICKCLOCK` is named in §4.4's sound row but
   has no ledger row in the grep. Step 3 (§5) reconciles all four against `FLAG_PHASE_COUNTS.txt` — the
   "42 rows" heading of §5 may move.
4. **OLD line-anchor drift in §4.4.** `sif_hle.cpp:710-732` → today `:745-778`; `main.cpp:4184-4200`
   → `:4315-4350` (RENDINIT); `:7989-8020` → `:8096-8140` (type registry); `:10063-10078` →
   `:6936-7234` (0x2ad110); `hle_heap.cpp:719-768` now reads `ForwardStreamRegisterFile` (the
   allocator-re-entry body is pinned at transplant from `:1816`/`:3039-3050`). OLD is frozen, so §4.4's
   anchors predate the freeze; §2 carries today's lines, CONSTRUCTION gets a doc delta.
5. **Reachability rule reused.** `HandleMtap`/`HandleEyeToy` (no IRX on disc) and `netcnfif` are written
   only if the Phase-4 boot BOUND census shows a binding — step 3 names the census oracle.
6. **`loader_sync` in the workarounds ledger.** Listed as row 6 of §2.11 because the brief asks for it,
   with its class stated as HARDWARE/TIMING HLE (HOOK §3.6) — its "retirement" is a shrink of title
   parameters, not a hook removal; the README ledger row must say so.

Handed to steps 3–4: the Mixer pump's slot (compose into `MaintDuties` vs a new tail slot; arbiter =
audio window hashes); `ChangeThreadPriority` no-op → routed (a re-freeze item, §7); the `PS2_HB_TICKCLOCK`
arm on the `timrman` row (§5); BUG-038 next to the coldboot leg (§6/§7); the `tests/*` group and
`archive_table` rows (§4).

### 8.3 Step-3 self-review (§4 + §5, 2026-09-27)

Checked against the sources:
- **§5 total = `FLAG_PHASE_COUNTS.txt` P4.** The 42 names are the file's `P4 NORM (16) / SETTING (4) /
  DIAG (11) / TEST (3) / DELETE (8)` lists verbatim, re-derived by running the tool's own attribution
  (`/tmp/s3/p4_touched.py` over `tools/flag_phase_counts.py`); 16+4+11+3+8 = 42. The `touched` table's 44
  = these 42 + `PS2_TICK_SYNC` (P3) + `PS2_MAINT_EEPACED` (P1), both via ledger prose cells that mention
  cdvd — §5.6 records them; §8.2 item 3 is closed with the heading UNCHANGED (the §2-named extras are P3
  rows or ledger gaps, never P4-attributed rows).
- **Every P4 row has an applied form of the STAGE_RULES kind** and its OLD site re-grepped today (54 vars
  read by the Phase-4 OLD files, `/tmp/s3/p4_getenv_census.py`, read-only); 4 SETTING fields already exist
  in `config.h`/`settings_table.tsv` with the deterministic values the frozen profile used
  (`iop.dispatch=frame`, `cdvd.latency_scale=0`, `cdvd.mc_latency_polls=0`, `audio.enabled` default).
- **OLD tests named by file** (§4.1): the Phase-4 set in OLD `tests/` is `test_iop_kernel.cpp`,
  `test_iop_sysmem_heap.cpp`, `test_iop_sysclib_map.cpp`, `test_libmc.cpp`, `test_vag_decoder.cpp`,
  `test_sound_voice.cpp`, `test_sound_state.cpp`, `test_sound_ring.cpp`, `test_snd_wire_governor.cpp`,
  `r140_budget/test_r140_budget.cpp`, the IOPM section of `test_savestate_format.cpp`, plus
  `host_app/input/test_pad_buffer.cpp` outside `tests/` (ctest `PadBuffer`) and `tools/loader_oracle_check.py`.
  There is NO OLD unit test for sif, for the ISO walker / read engine, or for the pad protocol proper —
  stated in the table with NEW tests in their place, so the gap is visible rather than a missing port.
- **Byte oracle re-verified**: tool / input / both output md5s match CONSTRUCTION §A.1 and §2.10 today
  (`build-v2` = `build-v2-nog`); the generated `.cpp` has exactly 5 `getenv` sites (all `PS2_STRMQ`) and
  references 74 distinct `iop_stubs::` names, none of the five misbound sysclib names.
- **Baseline shape read, not assumed**: 12 legs; coldboot 17 mem checkpoints / 162 audio windows / 1494
  frames; anchors 10 / 100 / 600. Coldboot audio windows 0–141 are the silence hash and window 142 is the
  first non-silent one — §4.5 makes its attribution (SPU voices vs FMV FIFO = sink d) a recorded step
  instead of letting a trivial silence claim stand in for the lane.

Closed here:
1. **Mixer-vs-IopDispatch order** (§8.1/§8.2): OLD `gs_hle.cpp:696-744` + `verify_boundary.cpp:59-84` —
   the pump is the LAST boundary step (inside `verify::OnEeBoundary`, after IopDispatch, EeStreamRpc and
   the savestate hook, before the mem checkpoint). NEW: compose into `Step::Verify`; §2.7's Mixer row
   updated to say so.
2. **`PS2_HB_TICKCLOCK` arm** (§8.1): the kernel periodic timer on `IopClock` (OLD's `KernelOn()` arm,
   `iop_stubs.cpp:1466`); the ticker-thread and boundary-fired variants are dead under the kernel arm.
3. **Census oracle for the reachability rule** (§8.2 item 5): `run.meta` keys `sif_bound=` and
   `iop_modules=` (§4.6).
4. **Grep oracle for the generated ABI** (§8.2 item 2): `StreamIrxStubSurface` (§4.1) + the 5-site lint
   exemption (§4.2).

Findings (doc deltas / step-4 items, none silent):
1. **Four ledger gaps in Phase-4 files**: `PS2_SND_TICKCLOCK`, `PS2_FIXC_TICKCLOCK`, `PS2_HB_BOUNDARY`,
   `PS2_IOPK_WATCH` have `getenv` sites but no `FLAG_LEDGER` row (two of them are pins of the frozen
   deterministic profile). Dispositions proposed in §5.6 (NORM / DELETE / DELETE / DIAG); adding the rows
   regenerates `FLAG_PHASE_COUNTS` (P4 → 46) — a §7 stage item, with the §5 heading updated in the same
   stage so plan and file never disagree.
2. **`iop.dispatch` default**: `config.h:75` says `Realtime` (the ledger's peak), §3.3 says `frame` until
   BUG-026 closes. §5.2 schedules the two one-line flips (land `IopModuleTable` → `Frame`; close BUG-026 →
   `Realtime`) with the `settings_table.tsv` regen each time — step 4 places them in §7.
3. **`PS2_SDR_BLOCK_MS` is a wall term in OLD** (`libsdr.cpp:244-258`, `steady_clock`), yet the frozen
   legs were 3-rep stable (8 ms < one frame, so the first poll one frame later always sees completion).
   NEW puts the 8,000 µs on the emu/kernel clock; if any leg's hashes move because of it, EQUIVALENCE §7,
   never a wall clock — step 4 should list it among the first-run suspects.
4. **Two small harness additions** are needed for the gate to be one command per leg: `diff_legs.py
   --claim mem=K,audio=W` and `run_leg.py --diag iopsched` (§4.4 step 5, §4.7). Both live under NEW
   `verify/` (allowed); neither touches `verify/baseline/`.
5. **`ReadEngine` diagnostics for attribution**: the per-page `memdiff` category (§4.4 step 4) is new
   diag mechanism (not in §4.4's `core/diag` list of three); it is what makes "attributed to sink (a)–(d)"
   checkable rather than asserted.
6. **Ledger line drift** in every Phase-4 site (§5.6 last bullet): the ledger's lines predate the freeze
   (like §4.4's, §8.2 item 4); §2's `iop_module_table.cpp:11/:19` were the ledger's lines — today `:21/:29`.
   `FLAG_LEDGER.md` is not edited in this step (planning only); the drift note travels with finding 1.
7. **One extra §2.7 row at transplant**: `CommandRing` (`sound/ring.h`, ported with `SoundRing`) — the
   §2.12 count becomes 72 in `PHASE4_IMPL`, noted in §4.1.
8. **Process slip, recorded**: one doc edit in this step used `python3 -` with a heredoc (three string
   replacements in `PHASE4_PLAN.md`) — STAGE_RULES says helpers go to files under `/tmp/<stage>/`; the two
   census helpers (`/tmp/s3/p4_touched.py`, `/tmp/s3/p4_getenv_census.py`) followed the rule. No helper
   process is left running.

Handed to step 4 (§6 gate + §7 stages): the per-leg claim table template (last claimed checkpoint / first
divergent `N` / attribution / audio window claim) for `PHASE4_IMPL`; BUG-038 next to the coldboot leg; the
ledger-gap rows + `FLAG_PHASE_COUNTS` regen stage; the `iop.dispatch` default flips; the harness additions
(finding 4); `ChangeThreadPriority` no-op → routed re-freeze item; the `battery.py` Phase-4 mode; the
3F′ carry-overs of §1.4 (P3-MANIFEST-12, P3-BOOT-WAITSEMA via §4.7, `KeyRedirect::NoteWrite`, HEAP section,
`hle/budget`/`loader_sync` data pairs, P3-RAMIMAGE-2, P4FIX-F1 probe).

### 8.4 Step-4 self-review (§6 + §7, 2026-09-27)

Checked against the sources:
- **Every brief item of the gate has a row**: coldboot boundary 0 with checkpoint 0 `== 0xeec37d056857657b` or a §7.1 record
  (G3); anchors ≥ 60 boundaries, `mem` IDENTICAL per checkpoint on 11/11 or adjudicated (G4); audio identical where claimable
  with the window-142 attribution made mandatory (G5); BUG-026 root-caused with the three §3.4 proofs (G7); workaround rows
  adjudicated with the retirement gates RUN (G8); `unhandled_mmio` rule (G6); ctest/lints green (G9). The DoD row restates
  CONSTRUCTION §4.4's DoD sentence item by item and adds the two facts §2 fixed (slot indices; the `Mixer` in slot 8).
- **Format = PHASE3_PLAN §6–§7**: G-rows with a Check column, an accepted-difference paragraph, a stage table with the four
  columns (scope / OLD sources read / green criterion), a findings ledger, the stage-rule reminders; pre-registered splits
  named like 3B′/3E′/3F′.
- **Slot numbers re-read from code**: `boundary_sequencer.h:46-48` — `MaintDuties`=3, `IopDispatch`=4, `EeStreamRpc`=5,
  `Savestate`=6, `Verify`=7 (0-based) = slots 4/5/6/7/8 (1-based, the header comment's numbering at `:18-23`). §7 uses the
  1-based numbers with the enum name beside each; the brief's "maintenance slot 4" is `Step::MaintDuties`.
- **Every §5 row is applied in exactly one stage**: 4A (`PS2_IOP_KERNEL`, `PS2_IOP_DISPATCH`, `PS2_IOPSCHED_DIAG`, `PS2_IOPK_WATCH`),
  4B (`PS2_HB_TICKCLOCK`, `PS2_SNDREG` registration arm, `PS2_SND_PROCBATCH`, `PS2_SPU_LAT_US`, `PS2_CD_LAT_US`, `PS2_STRMQ`
  exemption, `PS2_HB_BOUNDARY`, `PS2_FIXC_TICKCLOCK`, the four libsd probes), 4D (the four `PS2_CHUNK_APPLY_*`, `PS2_LOADER_HLE`,
  `PS2_LOADER_ORACLE(+_DIR)`, `PS2_LOADSPAN(+_DRAWMAX)`, `PS2_CHUNKQ`), 4E (`PS2_SOUND`, `PS2_SND_TICKCLOCK`, `PS2_SND_EAGER_HOLD`,
  `PS2_SND_KONLOG`, `PS2_MUSQ`, `PS2_SND_CENSUS`, `PS2_SND_SELFTEST`, `PS2_SND_FMVPACE`, `PS2_SND_FMVQ_DIV`), 4E′ (`PS2_SDR_BLOCK_MS`,
  `PS2_SND_PAUSE_DEFER`, `PS2_PAD_SCRIPT`, `PS2_STICKTRACE`, `PS2_INLAT_ANALOG`, the three watches, `PS2_U32POKE`, `PS2_MC_PERSIST`,
  `PS2_MC_LATENCY`) — 46 names incl. the four §5.6 gap rows; `PS2_SNDREG` is named on two stages because its ONE site owns two
  arms (registration 4B, CD-job engine 4D) and is counted once.
- **The four ledger-gap rows land in 4A** (one ledger edit, one `FLAG_PHASE_COUNTS` regen, the §5 heading → 46 in the same
  stage — §8.3 finding 1 closed by placement); the two `iop.dispatch` flips are placed (4A → `Frame`, 4F′ → `Realtime`; §8.3
  finding 2 closed).
- **Run vs claim** (§8.1 open item) is stated in §6's preamble and enforced by the length rule; `--claim` makes a shrunk claim
  visible in the command line (§8.3 finding 4's two harness additions are 4F items).

Findings (recorded in §7.1 as P4-SPU2-BUS, P4-DMAC-WORDING, P4-TITLE-OBSERVABLE), plus:
1. **Stage sizes.** 4B (18 dirs + regen + plugin), 4E (SPU2 + libsdr + pad + MC) and 4F (boot + 12 legs + two bugs + five
   retirement gates) each exceed one 45-min stage on any honest estimate; the brief's six names are kept and the continuations
   4A′/4B′/4D′/4E′/4F′ are pre-registered with their exact remainder (4A′/4D′ added per §8.6 item 4), so a STOP at ~35 min is a planned handoff, not a slip.
2. **Device-facing modules before their engines.** 4B creates `libsd/cdvdman/cdvdstm/padman/mcserv/sdrdrv` classes whose device
   calls return documented idle results until 4D/4E/4E′ — the alternative (landing the engines first) would leave the STREAM.IRX
   link unproven until late. Each idle body is recorded in the module's `PHASE4_IMPL` row and replaced in the named stage.
3. **BUG-038's producer does not exist in NEW** (its hypothesis names the `PS2_IOP_KERNEL=0` hosting, never written); the
   closure therefore states which CLASS survives (a holder spinning without a scheduling point) and proves the `frame` run
   through the edge; the `realtime` run is BUG-026's proof (ii) and doubles as BUG-038's play-path check.
4. **`ChangeThreadPriority` stays the no-op through the gate** (§2.3 (h)); routing it moves dispatch order under the frozen
   profile and is a post-gate re-freeze item, listed under MAY in §6 with that condition.
5. **No `PHASE4_IMPL.md` exists yet**; 4A creates it with the §6.1 table empty and the module rows of §2 as its checklist.

### 8.5 HANDOFF template (each stage appends one to `PHASE4_IMPL.md`; the 3F′ form)

```
### HANDOFF (4<X> → 4<Y>)
**Done:** <files landed (paths)>; <OLD sources transplanted (file:lines)>; <flags applied: NAME → outcome>; <tests added
(ctest names) + result line>; <legs run: command, park=/frames=/claims>; <ledger/doc deltas>.
**Not done (moved to 4<Y>):** <exact remainder from the §7 row>.
**Gate status:** G1..G9 each ✔ / ✘ / – (not yet) with one evidence phrase; §6.1 rows filled so far.
**Next command:** cmake --build --preset macos-dev -j4 && ctest --preset macos-dev, then <the first leg or test command of 4<Y>
with its expected output line>.
```

### 8.6 Independent review (rv, 2026-09-27) — verdict NEEDS_CHANGES (doc edits only; no re-plan) — **all five items applied by stage 4A (2026-09-27)**: (1) anchors corrected in §2.1/§2.3/§2.10/§7/§8.2; (2) `LintNoGetenv` → `CoreSingleGetenvSite` in §2.10/G1/G9; (3) the IOPM re-park oracle added to the 4B test list; (4) 4A′/4D′ pre-registered in §7; (5) `cdvdman` = IOPRP300 in §2.3/§2.12/§8.2

Verified against ground truth (read-only OLD greps/`md5 -q`/`stat`; NEW files; bounded `ctest`, 8/8 doc+lint tests green; no
commits; OLD mtimes predate this plan):
- OLD cites: ~30 anchors spot-checked. Exact: `iop_module_table.cpp:21/:29`, `iop_stubs.h:5`, `iop_stubs.cpp:560/:1187/:1286/
  :1440/:1466/:1542/:2010/:2066/:2543`, `sif_hle.cpp:747/:772`, `main.cpp:4210/:6987/:10465/:4315-4350/:8096-8140`,
  `gs_hle.cpp:696-744` order, `verify_boundary.cpp:59-84`, `sound_core.cpp:187/:196`, `libsdr.cpp:113/:244-258`,
  `sif_rpc_handlers.cpp:60/:441/:700/:761/:875/:1035/:1619/:1743/:1811/:2993`, `iop_loader.cpp:100/:299/:311-314`,
  `hle_heap.cpp:685/:2765`, `36864` at `iop_stubs.cpp:1453`. Drifted (see item 1).
- Sony lib set: `iop_stubs.h` sections = sysmem, intrman, ioman, sifcmd, sifman, stdio, sysclib, thbase, thsemap, timrman,
  libsd, cdvdman — the 12 of §2.3 exactly; union with the disc set = 18.
- STREAM.IRX oracle: tool `32a0c90c…` (1,022,288 B), input `d24082fd…` (116,156 B), `.h` `c95059f1…` (11,546 B), `.cpp`
  `b2102157…` (2,491,658 B), `build-v2` ≡ `build-v2-nog`; = CONSTRUCTION §A.1:960-976; 5 `getenv("PS2_STRMQ")` sites; 74
  distinct `iop_stubs::` names; `../host_app/` includes at `iop_recompiled.h:3-5`.
- §3: OpenGOAL-shaped (ARCHITECTURE §7.1 at `:950`), FRAME is the only hashed policy, BUG-026 signature verbatim from
  CONSTRUCTION `:484-485`.
- §5: `FLAG_PHASE_COUNTS.txt` P4 = 16/4/11/3/8 = 42 and all 42 names match set-for-set; the four §5.6 gap vars have 0 hits in
  `FLAG_LEDGER.md`; 46 arithmetic holds. D-SEQ-1/2/3 honoured (§1.4/§1.5/§4.6). `Step` enum indices, `mmio_sink.cpp` table (no
  SPU2 row), `config.h:75` `Realtime`, baseline values (`0,0xeec37d056857657b`; audio w0/w142; f46203 w0; 17/162/10/100 lines;
  `frames=1494`, last checkpoint 960) all as stated. Gate claims only `mem` + `audio` with §7.1 adjudication before the claim;
  workaround rows are title-side with retirement gates and G8 requires them RUN.

Changes required before 4A treats this plan as final:
1. **Six OLD anchors drift under the "re-verified today" claim** (§2 header, §2.1, §2.3, §7 4A row): `iop_kernel.h:31` →
   `:34` (`class IopKernel`); `iop_clock.h:66-70` → `:63` (`FrameCeilingUs`) and `:17-19` → `:15-16`; `iop_module_table.h:64-69`
   → `:61` (`PumpFromParkLoop`); `iop_stubs.h:29-35` → `:33-39` (`hle_stream_cd_read_rpc` note); `iop_stubs.h:78-90` →
   `:82-93` (`ChangeThreadPriority` note; `:53-73` for the sysclib block). Symbols exist; correct the numbers.
2. **`LintNoGetenv` is not a ctest** (named in G1, G9, §2.10). The getenv lint is `CoreSingleGetenvSite`
   (`tests/core/check_getenv_sites.py`, which owns `GENERATED_EXEMPT`). Name that test in G1/G9/§2.10 so the gate names only
   tests that exist (G1 currently lists both as if two tests).
3. **The IOPM boot-symmetric re-park oracle has no stage.** §4.1 `IopmSection` = codec + "boot-symmetric coroutine re-park after
   restore (`StreamIrxPlugin`)" + cross-policy; 4A scopes it to codec + cross-policy (correct — the plugin is 4B) but 4B's test
   list does not pick up the re-park half. Add it to 4B (`StreamIrxLoadsAndParks` or an `IopmSection` extension) explicitly.
4. **4A and 4D are unsplit at ≥ 4B's weight.** 4A: 9 pairs from 11 OLD files + 546 lines of ported tests + 3 NEW tests + a diag
   category + 2 regens + 4 ledger rows; 4D: 8 pairs + 7 tests + 2 workarounds + diag + the oracle port. §8.4 finding 1 names only
   4B/4E/4F. Pre-register 4A′ (`iop_host_thread`, `sif_rpc_bridge`, `IopmSection`, the ledger/regen items) and 4D′
   (`latency_model`, `delivery_budget`, `loadspan`, the two workarounds, `ArchiveTableLayout`) so a STOP is a planned handoff
   like 4B′/4E′/4F′.
5. **`cdvdman` is labelled "disc-side"** in §2.3's set sentence and §2.12 (inherited from CONSTRUCTION §4.4:460); it is an
   IOPRP300 module — the 8th disc IRX is `STREAM.IRX` (`runtime-environment.md`; §1.2 already says "7 stock + STREAM.IRX"). The
   18-dir union is unaffected; fix the wording and add it to §8.2 item 1's CONSTRUCTION doc delta.

## HANDOFF (planning step 4 → stage 4A)

- Done: §6 (gate G1–G9 + DoD, accepted-difference protocol, §6.1 claim-table template), §7 (stages 4A–4F with 4B′/4E′/4F′
  pre-registered, §7.1 findings ledger seeded with three plan-level rows, stage-rule reminders), §8.4 review, §8.5 handoff
  template; Status header → "planning complete"; `docs/phases/README.md` updated. Facts read this step: `boundary_sequencer.h`
  `Step` enum, `mmio_sink.cpp` idle table (no SPU2 row), `leg_closer.cpp` keys, `ctest -N` (74), OLD `main.cpp:10465/:4210`
  (`[BOOT-SM-EDGE]`), BUG-026/BUG-038 headers, EQUIVALENCE §7/§7.1, PHASE3_PLAN §6–§7, PHASE3_IMPL 3F′ gate table + HANDOFF.
- Not done: no code, no regen, no legs (plan step). Tree unchanged except this file and `docs/phases/README.md`; no commits;
  OLD read-only (greps only).
- Next command (stage 4A): the 3F′ pre-flight — `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`, then the two
  `run_leg.py` park legs of §1.1 (expect `park=sif_rpc_absent` at `0x2cdadc`/tid 1 and `0x1e7fa4`/tid 2); then read
  `OLD/host_app/iop_kernel/*` and write `engine/iop/{iop_kernel, iop_coroutine, iop_clock, iop_host_thread, sif_rpc_bridge,
  iop_module_table, iop_memory, iop_context, sysmem_heap}` per §2.1–§2.2 with the five 4A tests; create `PHASE4_IMPL.md`.

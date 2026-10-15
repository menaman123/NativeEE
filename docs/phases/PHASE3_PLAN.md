# PHASE3_PLAN — `engine/ee/` + the recompiled output: CONSTRUCTION_PLAN §4.3 made executable

Status: plan (stage p3p, 2026-09-26; revised by p3a per the p3pr review — §9 lists the corrections; 3A DONE, see `PHASE3_IMPL.md`; loop iteration 1 corrections per the p3ar review — §9.1). Binding rules: `docs/phases/STAGE_RULES.md` (bounded runs via
`verify/run_bounded.sh`, no commits, OLD read-only, one class per `.h/.cpp` pair, ONE `getenv` site,
flags applied at transplant time, ≤45 min per stage, tree green at every stop).
Inputs, cited not re-derived: `docs/design/CONSTRUCTION_PLAN.md` §0.2 D-SEQ-2, §1 rules 6–8, §4.3, §A.1;
`docs/design/HOOK_RECLASSIFICATION.md` §1–§3.9, §5; `docs/filemap/INDEX.md` ee/ rows (:223–:277, :474–:480,
:602–:607, :654, :663, :698) and `docs/filemap/FLAG_PHASE_COUNTS.txt` P3 block (73 rows); `docs/audit/A4_CPU_HLE.md`
§1–§2, §5, §6; `docs/phases/PHASE0_IMPL.md` §10–§12; `docs/design/ARCHITECTURE.md` §4.7, §4.11, §7.2, §13;
`docs/design/EQUIVALENCE_PLAN.md` §7. OLD sources are under `OLD/host_app/` unless prefixed
(`OLD = $PS2R_OLD_ROOT`, frozen, read-only).

---

## 1. Goal and D-SEQ-2 scope

Phase 3 builds the Emotion Engine side of the runtime — memory, CPU context, the generated recompiled code,
the BIOS-level kernel (threads/semaphores/event flags/alarms, syscall table), the emulated time domain and
the frame-boundary owner stub, INTC + EE timers — and the game-side pieces that the EE needs to exist
(title profile, the boot-critical workaround quarantine, the savestate section providers for RAM/SPAD/META/
THRD/EE). Nothing renders, no DMA moves, no IOP exists.

**What runs (D-SEQ-2, CONSTRUCTION §0.2 + §4.3 gate):**

1. `ElfLoader` maps `SLUS_123.45` (entry `0x00100008`, `$gp=0`; ONE `PT_LOAD`: vaddr `0x00100000`, file offset
   `0x1000`, `p_filesz=0x20A0B8` → file-backed bytes `0x00100000–0x0030A0B8`, `p_memsz=0x2DAE80` → `.bss` zero-filled to
   `0x003DAE80`; measured by `ElfLoaderTitle` in 3A — `runtime-environment.md`'s "`0x00100000–0x0020A0B8`" is the
   SIZE misread as an end address) into `MainMemory`, then `DataMapApplier` applies the title data map + post-load
   patches (§3.4.3); `BootEnv` writes the retail argv block
   (`argc=1`, `argv[0]="cdrom0:\SLUS_123.45;1"` — `ps2_argv.h:38`, pre-seed at `0x30a980`); `CpuContext`
   is initialised as OLD `main.cpp:529-600 init_cpu_context` does (`pc=0x00100008`, `$sp=0x01FFFF00`, zeroed; the exact
   line is re-cited by 3C2 when `BootEnv` lands — the p3p draft cited `:529` without checking the function's extent).
2. The generated `entry(ctx)` runs crt0 → ctors → `main` on PS2 thread 1 under the run token; kernel
   syscalls create/start threads and semaphores; the thbase 1.02 election picks runners.
3. Execution reaches the first IOP-dependent call — `sceSifInitRpc` (seam at `recompiled.cpp:26842`,
   HLE row) or any `sceSif*`/IOP-reboot wait — and **parks**: the `SifRpcAbsent` binding records
   `park=sif_rpc_absent` in `run.meta`, releases the run token, and the boot leg finalises with
   `frames=0`, `finalized=1`, all four lane files present (`mem.mhash` legitimately empty: OLD emits
   checkpoint 0 only at the first frame boundary, which lies after the IOP handshake).
4. **Anchor restore:** each of the 11 anchor `.ps2st` (verify/baseline/ps2state_slot0_*) is read through the
   Phase-1 PS2SAVE1 envelope reader; sections RAM0/SPAD/META are restored into NEW memory, THRD into
   `ThreadManager` + per-thread `CpuContext`, plus SEMA/EVFL/INTC/CNTR/MISC/HEAP. Immediately after the
   restore and before any guest instruction, `VerifyHost` writes one line `restore.mhash` = FNV-1a64 over
   RAM + SPAD + META in `MemoryCheckpointer` order. That value must equal `verify/state_hash.py <file>`,
   computed independently from the file's raw section payloads. The resumed anchor then runs from its
   SyncV return until its first SIF touch (`park=sif_rpc_absent`), deterministically across 3 reps.

**Not claimed (recorded so nobody adds it):** no `frames`/`audio`/`mem` lane identity vs OLD (D-SEQ-2);
no IOP, SIF DMA, cdvd, pad, mc, sound (Phase 4); no DMAC/GIF/VIF/VU execution (Phase 5) — their MMIO ranges
are served by `MmioSink` idle values (R17); no GS state/render (Phase 6); the loader HLE (`cdvd/hle/loader`,
FUN_0017d6b8 spin model) is Phase 4 — Phase 3 lands only its `TitleProfile` parameters.

**Stopping rule R17 (from §4.3):** the boot leg is allowed to *read* any MMIO register and get the documented
idle value and to *write* any MMIO register into the sink; it is not allowed to depend on a completion the
sink cannot produce. The first such dependency IS the park. `run.meta` records `unhandled_mmio=<n>`
(distinct sink ranges touched) so Phase 4/5/6 can see what displaced the sink.

## 2. Module table (one class per `.h/.cpp` pair; file = snake_case(type); flags per FLAG_LEDGER)

Column "OLD source" cites the frozen file (+ line ranges when the transplant is a slice). "Flags" lists the
P3 FLAG_PHASE_COUNTS vars consumed by that module with the outcome applied at write time (§5 has the full 73).

### 2.1 `engine/ee/cpu/`

| New pair | Class / content | OLD source | Shape | Flags |
|---|---|---|---|---|
| `cpu_state.h` (POD header, lint-exempt like OLD's; NO `.cpp`) | `EmotionEngineState` (typedef `CpuContext`): `CpuRegs` (32× 128-bit `GprReg` union UQ/UD/UL/US/UC + signed views, HI/LO + HI1/LO1, CP0[32], sa, pc), `FpuRegs` (32× `FprReg`, ACC, fprc[32]), `VUregs` as member **`vuRegs`** (VF[32] `VR_reg`, VI[16], ACC/Q/P/I/R, status/mac/clip, TPC/CMSAR0/FBRST/VPU_STAT/CMSAR1 — `cpu_state.h:143-281`; the generated code spells `ctx.vuRegs`, so the member and type names are kept verbatim). **Our own `u8/u16/u32/u64/u128/s*` typedefs in `core/types.h`** — the vendored `common/Pcsx2Types.h`/`Pcsx2Defs.h` are NOT transplanted (rule 8). `sizeof`/`offsetof` static_asserts pin the layout to OLD's values (`sizeof == 1600`: cpuRegs 720 + fpuRegs 264 → vuRegs at 992 (16-aligned) + 608; measured 3A against the THRD `ctx_size` field of **all 11 anchors** — the 8 `ps2state_slot0_*` + `artdet_f37152` + `slowspot_f37092_s1789428268` + `slowspot_f40298_s1789693059` legs of `verify/baseline/`, whose `.ps2st` files live in `OLD/save_states/` — all 1600 (2 THRD records each); `EeCpuStateLayoutAnchor` re-reads one at ctest time through the Phase-1 envelope reader — the THRD section serialises the raw struct, A4 §6) | `cpu_state.h` (layout), `common/Pcsx2Types.h` (typedef NAMES only, not text) | rewritten layout-identical | — |
| `ps2_float.h/.cpp` | `Ps2Float`: `Clamp` (exp==0xFF → ±MAX_FLOAT, no NaN/Inf), `DivS`/`SqrtS`/`RsqrtS` computed in single precision under FE_TONEAREST with the EE thread otherwise FE_TOWARDZERO (A4 §1.3; `cpu_state.h:26-38`). The generated code calls the free functions `ps2_float_clamp/ps2_div_s/ps2_sqrt_s/ps2_rsqrt_s` (53,750 / 463 / 307 / 7 sites in the shards) — declared in the recomp ABI header (§2.9) and defined here | `cpu_state.h` helpers; `RECOMP_CONVENTIONS` FPU rules | verbatim semantics | — |
| `cpu_context.h/.cpp` | `CpuContextInit`: `Reset(ctx, entry, sp)` (zero the 1600-byte state, `pc=entry`, `$sp=sp`, `VF0={0,0,0,1}`, `Q=1.0` per `VUregs::Reset`, `cpu_state.h:268-280`), `SetRoundingForGuestThread()` (FE_TOWARDZERO per guest thread — `ps2_threading.cpp:366`) | `main.cpp:529-600 init_cpu_context`, `cpu_state.h:268-280` | rewritten | `PS2_ARGV` NORM (argv writer lives in `BootEnv`; this only reserves the block) |
| `dispatch_table.h/.cpp` | `DispatchTable`: owns the dense-id tables the generated common TU expects (`g_recomp_fn_addrs/_cache/_hooked/_raw`, `recompiled_functions` map, `function_ranges`, sorted snapshots), `UpdateFunctionImpl` (R137 pointer-stability contract), `RecompFnIdFor(_Containing)`, `FindContainingFunction`, `MarkHooked` | `recompiled.cpp` runtime-table half (generated; the TABLE DEFINITIONS stay in the generated TU — this class is the OWNER of the install seam over them), `recomp_seams.cpp` | verbatim seam, class-wrapped | `PS2_EE_RAWDISP` DIAG (raw-dispatch toggle → `core/diag` only) , `PS2_SHADOW_DIAG` DIAG |
| `shadow_call_stack.h/.cpp` | `ShadowCallStack`: thread-local 128-deep (EE_CALL_STACK_MAX) func/ra chain; `ee_call_stack_push/pop`, `ee_get_call_stack_depth/top` C seams; park-site harvest hook for THRD | `recompiled.cpp` shadow-stack half, `savestate.cpp:78-95` contract | verbatim | — |
| `seams.cpp` (no header; the frozen A4 §1.3 surface) | the `extern "C"` glue the generated code links: `recomp_fcf_miss`, `crashring_*` (the ONLY definitions in the tree — real forwarders to `core/log` `RingLog`; `fatal_no_jump` = report then park, §3.4 row 26 / P3-SEAM-4), the park and interim-park seam bodies | `recomp_seams.cpp` (27 lines, frozen) | verbatim policy (report, then terminal — park instead of `_exit`) | — |

### 2.2 `engine/ee/mem/`

| New pair | Class / content | OLD source | Shape | Flags |
|---|---|---|---|---|
| `main_memory.h/.cpp` | `MainMemory`: 32 MB backing store + KUSEG/KSEG0/KSEG1 mirror rule (`& 0x1FFFFFFF`, `memory.cpp:470`) | `memory.h:13`, `memory.cpp:470-490` RAM+mirrors arm | rewritten | — |
| `scratchpad.h/.cpp` | `Scratchpad`: 16 KB at `0x70000000` (`(addr & 0x70000000)==0x70000000` test, offset `addr & 0x3FFF`, as OLD) | `memory.cpp:383-395` scratchpad arm | verbatim | — |
| *(no `meta_arena` pair here — p3a)* | The 2 MB arena at EE phys `0x02000000` is the quarantined workaround `games/<t>/hle/workarounds/meta_arena` (CONSTRUCTION §4.3; §3.4.2). `AddressMap::RegisterExtraRegion` is the generic primitive it uses | `memory.h:13-32` `meta_arena_memory` (title-side transplant, 3E) | — | — |
| `address_map.h/.cpp` | `AddressMap`: `Translate(addr,size)` in OLD's order (`memory.cpp:358-505`): scratchpad (`(addr & 0x70000000)==0x70000000`, `:383`) → HW window `0x10000000–0x1FFFFFFF` (VU0/VU1 mem, IOP RAM, IO, BIOS; `:400-457`) → RAM+mirrors (`addr & 0x1FFFFFFF` < 32 MB, `:470`) → **registered extra regions** (generic `RegisterExtraRegion(phys_base, bytes, backing)`; OLD's meta arena arm `:493-498` is the title's registration) → BIOS mirror (`:499-503`) → `nullptr`; `GetPointer` for DMA-grade access | `memory.cpp:358-505 translate_address` | verbatim order, generic region hook | — |
| `fastmem.h/.cpp` (+ `fastmem.inl` templates) | `FastMem`: `Read<T>/Write<T>/ReadQuad/WriteQuad` — inline bounds check `(addr & 0x1FFFFFFF)+size <= 32 MB` → memcpy, else slow path; quad ops apply `addr &= ~0xF` before the check (A4 §2.2). The slow path is the ROUTER call, not a gauntlet: the ~3,300-line `PS2_MEMWATCH` probe gauntlet is not transplanted (DIAG mechanism only) | `memory.h:55-260` [R130-FASTMEM], `memory.cpp *_slow` | split: fast path verbatim, gauntlet dropped | `PS2_FASTMEM` NORM (ON; no slow-only arm), `PS2_MEMWATCH` DIAG, `PS2_S15_WATCH`/`PS2_S8_MTXWATCH` DELETE |
| `mmio_router.h/.cpp` | `MmioRouter`: per-range handler registration (replaces the `read_io_register`/`write_io_register` if-chains), `Read/Write(addr,size)`, `UnhandledRanges()`; the `is_io_register` window `0x10000000–0x1000FFFF` + GS priv `0x12000000–0x12001FFF` (`memory.cpp:531-536`) | `memory.cpp:531-536 is_io_register`, `:539-981 read_io_register`, `:981+ write_io_register`, `:1145 read_slow` | rewritten | — |
| `mmio_sink.h/.cpp` | `MmioSink`: the DEFAULT handler over every unclaimed range with the idle READ values LIFTED in 3A from OLD: DMAC channel regs `0x10008000–0x1000D7FF` → `g_dmac.Read` idle = 0 (`memory.cpp:910-912`, `dmac.cpp:1045-1131`: CHCR.STR=0, all zero), DMAC ctrl `0x1000E000–0x1000E060` → 0 (`:933-935`), `D_ENABLER 0x1000F520` → `enabler` = **0** at idle (`dmac.h:75` initialiser; `dmac.cpp:1127`), `0x1000F590` → 0 (`dmac.cpp:1128`); GIF `0x10003000–0x100030AF` → `GIF_STAT` idle = 0 (`gif.cpp:221-246`: every bit is a live-state predicate; all false at idle), other GIF regs 0; VIF0/1 FBRST/ERR `0x10003810/20`, `0x10003C10/20` → `g_vif.Read` idle (0; `:584-618`), VIF1_STAT `0x10003C00` → fall-through 0 (`:539-580` comment: "the fall-through `return 0` = idle"); IPU `0x10002000–0x10002030` → 0 (`:619-627`); GS priv `0x12000000–0x12001FFF` → 0 except `GS_CSR 0x12001000` → **`0x8`** (VSINT set — `:921-929`, the first-match arm; the `g_gs_regs.GS_CSR` arm at `:945` is dead code behind it); SIF `0x1000F200–0x1000F260` → MSCOM/SMCOM/MSFLAG/SMFLAG/CTL = 0 at idle (`:962-971`; Phase 4 displaces); unknown IO → 0 (`:977`). Writes accepted and dropped; diag builds count per range; `run.meta unhandled_mmio=` | `memory.cpp:539-981` read-path idle constants (cited per row in `mmio_sink.cpp`) | new (values transplanted) | `PS2_VERBOSE_HW` already P1 DIAG |
| `elf_loader.h/.cpp` | `ElfLoader`: parses `Elf32_Ehdr`/`Elf32_Phdr` and copies every `PT_LOAD` (`p_filesz` bytes at `p_vaddr`, zero-fills to `p_memsz`) into `MainMemory`; reports entry / segment ranges; no hardcoded path or section table (§3.4.3) | `memory.cpp:166-221 LoadElfSections` (the 5-section list it REPLACES; the ELF walk is written from the ELF spec — there is no OLD `loader/` directory) | rewritten | `GAME_FILES_DIR` SETTING (`Config.paths.game_files`) |
| `data_map_applier.h/.cpp` | `DataMapApplier`: applies a title `addr:value` text map (OLD's `ram_data_map_v50.txt` grammar, `memory.cpp:258-300`: hex address, `0x`-prefixed big-endian hex value written little-endian byte-wise, `#` comments) and a post-load patch list onto `MainMemory`; returns entry/byte counts | `memory.cpp:240-312` | verbatim grammar | — |

### 2.3 `engine/ee/hle/` — kernel (BIOS scheduler semantics) + syscalls + boot env + libc

The prompt's "ee/kernel BiosScheduler" is realised as the CONSTRUCTION §4.3 pairs below (INDEX rows :238–:245);
the directory name stays `ee/hle` because it is the SDK/OS HLE class (HOOK §1).

| New pair | Class / content | OLD source | Shape | Flags |
|---|---|---|---|---|
| `thread_manager.h/.cpp` | `ThreadManager` (the BiosScheduler): thbase 1.02 semantics — 256 `ThreadInfo`, 128 priorities, ready queues, states RUN/READY/WAIT/SUSPEND/WAITSUSPEND/DORMANT, Create/Delete/Start/Exit/ExitDelete/Terminate/Sleep/Wakeup(+iWakeup, cancel)/Suspend/Resume/ChangePriority/Rotate/ReferStatus; one host thread per guest thread (park/wake via per-thread mutex+cv), stack size from `Config.engine.guest_stack_mb` (SETTING from the 1 GB accidental default — A4 §2.6; peak default kept for byte-identity of nothing: stack size is not guest-visible); `NotePark`/`RegisterThreadCtx` for THRD; maps out of the header | `ps2_threading.{h,cpp}` (threads half), `ps2_scheduler.cpp` (the fiber-era kernel — READ for the thbase rules only; not transplanted, INDEX :663 class) | split / rewrite | `PS2_EE_RUNAHEAD` SETTING (`engine.ee_runahead`, default 2) |
| `semaphores.h/.cpp` | `Semaphores`: 256 `SemaInfo` (count/max/attr/option/wait queue FIFO-or-priority per attr), Create/Delete/Signal/iSignal/Wait/Poll/ReferStatus, KE_* codes | `ps2_threading.cpp` sema half | split | — |
| `event_flags.h/.cpp` | `EventFlags`: Create/Delete/Set/iSet/Clear/Wait/Poll with AND/OR/CLEAR modes, KE_* | `ps2_threading.cpp` evflag half | split | — |
| `alarms.h/.cpp` | `Alarms`: SetAlarm/iSetAlarm/ReleaseAlarm/iReleaseAlarm — deadline in `DerivedClocks` counts, fired at the boundary tick step (Phase-3 form; the game arms none before the park — pinned by a test, present so Phase 4 never touches this file) | `syscalls.cpp` alarm cases (stubs in OLD → same stub behaviour + the count-domain hook) | rewritten | — |
| `syscall_table.h/.cpp` | `SyscallTable`: `runtime_syscall_dispatcher(v1, ctx)` seam → **119 live `case` labels + `default:`** (`syscalls.cpp:2315-2504`, measured; `// case 0x74: sceSifSetDma` at `:2496` is commented out and NOT counted — the p3a draft's 120 included it; the p3p draft's 271 was wrong) incl. negated/i-variants: threads, semas, evflags, InitMainThread/InitHeap, SIF (SetDma/DmaStat/BindRpc/CallRpc/SetReg/GetReg/SetDChain/StopDma → Phase-3 form returns OLD's idle results; Phase 4 registers the real `sif/` bodies through a small `SifSyscallHooks` interface), GS (PutIMR/GetIMR/SetGsCrt → `Intc`/GS-priv sink), INTC/DMAC handler add/remove/enable/disable, FlushCache, GetMemorySize, Deci2, RFU stubs; `handle_mtc0_write`/mfc0 with COP0 Count from `DerivedClocks` (free-run model, A4 §5.1 row 5 — the "known gap" closed as specified in ARCH §4.7) | `syscalls.{h,cpp}` dispatcher, `ps2_syscall_wrappers.{h,cpp}` (dissolved) | split | `PS2_HAVE_BACKTRACE` DIAG |
| `dyn_interp.h/.cpp` | `DynInterp`: the rabbitizer-driven last-resort EE interpreter (100k-instruction cap) used when `FindContainingFunction` fails; **Phase-3 form logs the address and parks** (`park=unrecompiled_pc`) — a boot leg that needs it is a construction bug, so it is a tripwire, not a tier | `syscalls.cpp:2177 dynamic_decode_and_execute` | split (mechanism; rabbitizer link deferred) | — |
| `rpc_callback_queue.h/.cpp` | `RpcCallbackQueue`: EE-side queue of SIF RPC completion callbacks drained at the event-queue step; empty in Phase 3 (Phase 4 producer) | `syscalls.cpp` callback queue | verbatim | — |
| `boot_env.h/.cpp` | `BootEnv`: writes argc/argv from `TitleProfile.boot` (retail: `argc=1`, `"cdrom0:\SLUS_123.45;1"`), the kernel-reserved block, `InitMainThread/InitHeap` answers (heap top from ELF end) | `ps2_argv.h`, `main.cpp` S8-ARGV writer, `sif_hle.cpp` boot-env half | split | `PS2_ARGV` NORM (retail only; legacy `host:` vector never written) |
| `libc.h/.cpp` | `Libc`: the 4 SdkWrap rows (memcpy/memset/strlen/sprintf family per `libc_hle.cpp`) in the guarded-fallback shape: wrapper receives the original v2 body and may call it | `libc_hle.{h,cpp}` | verbatim | `PS2_LIBC_HLE` NORM (ON) |
| `sdk_binding.h/.cpp` | `SdkBindingManifest` + the ONE installer (`Install(DispatchTable&)`): rows `{library, signature id, kind SdkBind/SdkWrap/GenProbe, fn}`; the address column is DATA from `games/<t>/gen/sce_hle_bindings.inc` (OLD-generated table copied with provenance until `tools/sce_lib_finder` in 9.x). Install order = OLD `main.cpp:5242+` composition: `initialize_recompiled_functions` → (manual dispatchers: NOT installed) → `register_sce_hle_overrides` last and wins | `sce_hle/{sce_hle.h,sce_hle_registry.cpp}` | rewritten (same seam) | — |
| `kernl_boot.h/.cpp`, `kernl_timer.h/.cpp`, `kernl_file_io.h/.cpp`, `kernl_sif_rpc.h/.cpp` | libkernl bindings by family. Phase-3 bodies: boot (`SetupThread/SetupHeap/ExecPS2` shapes), timer (`hle_TimerWaitMicroseconds` seam → `DerivedClocks`), file_io (sceOpen etc. → `park=iop_dependent` in Phase 3), **sif_rpc = the PARK OWNER**: `sceSifInitRpc/BindRpc/CallRpc/InitCmd/CheckStatRpc` bodies record `park=sif_rpc_absent` and hand off; Phase 4 replaces this pair's bodies through `sif/hle` registration, not by editing | `sce_hle/libkernl.cpp` (765 ln) | split by family | — |
| `libscf_bindings.cpp`, `libdev_bindings.cpp` (single-TU row contributors; header = `sdk_binding.h`) | libscf (`sceScfGetLanguage` etc. → `Config.game.language`), libdev stubs | `sce_hle/libscf.cpp`, `libdev.cpp` | move | — |

### 2.4 `engine/ee/sched/`

| New pair | Class / content | OLD source | Shape | Flags |
|---|---|---|---|---|
| `ee_run_token.h/.cpp` | `EeRunToken`: exactly one guest thread executes at a time; handoff only at kernel scheduling points under the thbase election; `BeginRelaunch/EndRelaunch` (BUG-036: a savestate load hands the loader's identity off and holds elections until every resumed thread is READY, so the first post-load runner is the thbase winner) | `ee_sched/ee_run_token.{h,cpp}` (215+404 ln) | verbatim (one class already) | `PS2_EE_RUNTOKEN` NORM ON (P1-attributed; no tokenless arm), `PS2_EESCHED_DIAG` DIAG |
| `ee_event_queue.h/.cpp` | `EeEventQueue`: foreign producers (later: graphics ack, IOP dispatcher, audio) enqueue guest-visible wakes; **BUG-035 invariant, BOTH halves: (a) holder-only drain — only the run-token holder drains, at every token acquisition and at boundary step 3, so an enqueue never mutates guest state on the producer's thread; (b) `ApplyingOnThisThread()` is honoured by every guest-state MUTATOR (`ThreadManager` wake/priority/state changes, `Semaphores::Signal`, `EventFlags::Set`, INTC raise) — **a mutator whose caller neither holds the run token nor is applying ENQUEUES** (a holder calling directly, or the holder's drain applying an entry, mutates in place; any other caller enqueues instead of mutating), and asserts (diag) if a foreign thread mutates directly** (both halves pinned by `test_ee_event_queue` porting OLD `test_ee_sched.cpp`) | `ee_sched/ee_event_queue.{h,cpp}` (134+148 ln) | verbatim | — |

### 2.5 `engine/ee/timing/` — TickSource / EmuDomain / DerivedClocks / boundary

| New pair | Class / content | OLD source | Shape | Flags |
|---|---|---|---|---|
| `tick_source.h/.cpp` | `TickSource`: the ONE generator; modes Realtime / Freerun / Turbo(K) / Warp(to_frame) as period + release policies; **under `time.tick_sync` (deterministic profile) it has no thread — ticks are demanded by the EE at boundary step 2** (ARCH §7.2) | `ps2_vblank.{h,cpp}` VBlankWorkerFunc (thread half), `turbo.h` K scalar, `warp.h` | split | `PS2_VBLANK_FREERUN` NORM (freerun = a mode, not a parallel mechanism), `PS2_TURBO` DELETE, `PS2_TURBO_DUCK` NORM, `PS2_LOAD_TURBO` SETTING (`cdvd.load_turbo`, default OFF since s71), `PS2_LOAD_TURBO_{ENGAGE,GAP,INPUT,RELEASE}_MS`/`_K` NORM (constants 1500/150/2000/250 ms), `PS2_WARP_TO` SETTING (`harness.warp_to_frame`) |
| `emu_domain.h/.cpp` | `EmuDomain`: `emu_count` — the only game-visible counter; `DomainTick()` sole read API; quantum carry (REGIME ticks per boundary), boundary settle top-up, lead governor (max_ahead 1), idle release (>100 ms no boundary → 1:1), `ReAnchor` (boot/restore) | `time_domain.h` [S94-TIMEDOM] | split (logic verbatim) | `PS2_TIME_EMU` NORM ON, `PS2_TIME_EMU_INTRA` NORM, `PS2_TIME_EMU_REGIME` NORM → pinned by profile (`time.regime_pin=1` in deterministic) |
| `regime_detector.h/.cpp` | `RegimeDetector`: hysteresis 15/120 windows, up>30 %, down>90 %, arrival-gap DOWN rule; `Pinned(n)` variant | `time_domain.h` detector | split | (same rows) |
| `derived_clocks.h/.cpp` | `DerivedClocks` — pure functions of `EmuDomain`; **the nine clocks, each with its domain (A4 §5.1):** (1) EE timers T0–T3 — emu counts, T0 9600/tick, subtick interpolation as presentation (clamped S40 contract), pumped at boundary and at first post-wrap COUNT read; (2) VBlank tick — **EE-synchronous** (TickSync), no wall generator in the profile; (3) frame boundary / sceGsSyncV — the settle point (`BoundarySequencer`); (4) frame-budget predicate anchor — emu counts (mechanism here, params `games/<t>/hle/budget`); (5) COP0 Count — emu counts, free-run model (counts/tick constant; new as ARCH §4.7 specifies); (6) RTC — constant 2005-06-15 12:00:00 (`Config.game.rtc_fixed`, the consumer is Phase 4's sceCdReadClock); (7) IOP heartbeat — tick domain (one beat per round(usec/16667) ticks; the consumer is Phase 4); (8) CDVD latency/refill epoch — tick-epoch metered (`cdvd.latency_model`, `PS2_CDVD_BUDGET_KB=256` constant; consumer Phase 4); (9) audio — device wall is the rate MASTER (never stretched; Phase 7 device) | `ee_timers.cpp` T0 constants, `time_domain.h`, `budget_math.h`, `syscalls.cpp` mtc0/mfc0, `sif_rpc_handlers.cpp:1128`, `turbo.h` HB, `ps2_vblank.h` delivery meter | split | `PS2_EE_TIM0` NORM, `PS2_T0_SUBTICK` NORM (ON; profile pins subtick bits off via `PS2_T0_TICKPACED`→`time.t0_tickpaced` SETTING), `PS2_RTC_FIXED` SETTING (`game.rtc_fixed`), `PS2_CDVD_BUDGET_KB` NORM, `PS2_CDVD_INSTANT` SETTING (`cdvd.latency_model`), `PS2_T0TRACE`/`PS2_T0_NOISR` DELETE, `PS2_TIMEDOM_DIAG` DIAG |
| `budget_clock.h/.cpp` + `phase_classifier.h/.cpp` | `BudgetClock` (FUN_00191bd0 predicate emulation: rate 576000 = BUSCLK/256, period 9600, 78-count phase guard, synthetic base per frame) and `PhaseClassifier` (≥2 consecutive low-draw frames → base_low). Mechanism only; base/base_low/step/lowdraw/phase from `TitleProfile.quirks.budget` | `budget_math.h`, `main.cpp:3226-3608`, `timer_hle.{h,cpp}` | split (mechanism) | `PS2_BUDGET_EMU` NORM ON (mode 1), `PS2_BUDGET_BASE/_BASE_LOW/_LOWDRAW/_PHASE/_STEP` NORM (constants → profile), `PS2_LOAD_BUDGET_LIFT` + `_ENGAGE/_GAP/_INPUT/_RELEASE_MS` NORM, `PS2_BUDGET_DIAG` DIAG |
| `tick_sync.h/.cpp` | `TickSync`: the tick BODY executed only on a guest EE thread at a fixed point (count increment, emu-domain carry, hold-to-edge delivery, sema 1/3 signals, INTC VBON/TIM2/VBOF, GS_CSR.VSINT via the sink in Phase 3); demands from non-guest threads deferred to the next boundary | `ee_timing/tick_sync.{h,cpp}` | verbatim | `PS2_TICK_SYNC` SETTING (`time.tick_sync`, deterministic=on), `PS2_TICK_DETERMINISTIC` SETTING, `PS2_FRAME_LOCKSTEP` TEST |
| `frame_rendezvous.h/.cpp` | `FrameRendezvous`: EE-issued ticket / GFX ack (mutex+cv); Phase-3 form acks immediately (no graphics thread) — the class exists so Phase 6 registers the acker without touching Phase-3 files | `ee_timing/frame_rendezvous.{h,cpp}` | verbatim | `PS2_PRESENT_EDGE` NORM |
| `boundary_sequencer.h/.cpp` | `BoundarySequencer`: THE fixed order (ARCH §7.2): 1 present rendezvous → 2 tick advance (+deferred ticks, EE_TIM0 pump) → 3 event-queue drain → 4 maintenance duties + pad stamp → 5 IOP FRAME dispatch → 6 EE stream RPC service → 7 savestate probe/save/load (a load stops the sequence) → 8 verification hooks. Phase 3 fills steps 1 (immediate ack), 2, 3, 7, 8; steps 4/5/6 are registered `Step` slots left empty with their positions FIXED NOW (Phase 4 fills 4–6, Phase 5 adds the VIF1 tail chase note into 2, Phase 6 the VSync push into 1) | `ee_timing/boundary_sequencer.{h,cpp}`, `gs_hle.cpp:405-745` order | verbatim | `PS2_WAITFIX` (P1 NORM) consumer |
| `boundary_protocol.h/.cpp` | `BoundaryProtocol`: NoteArrival → pace park → tick wait (quantum) → OnBoundary settle → timer pump → ISR dispatch, written once (the `timeline.h` WAITFIX arm is the shipped shape) | `quantum.h`, `ee_pace.h`, `timeline.h` WAITFIX arm | split | `PS2_QUANTUM` NORM ON, `PS2_EE_PACE` NORM (OFF arm dropped — the rendezvous is the bound in the profile; play keeps the depth park via `engine.ee_runahead`), `PS2_EEFEED` NORM, `PS2_MTVU`/`PS2_MTVU_DEFER` NORM (Phase 5 consumers; attributed here by site file), `PS2_RQ_MAXJOBS/_MAXMB/_SAFETY` NORM (render-queue bounds; constants recorded for Phase 6) |
| `turbo_governor.h/.cpp`, `warp.h/.cpp` | `TurboGovernor` (K scalar + load-turbo hysteresis), `Warp` (run-to-frame release policy for `harness.warp_to_frame`) | `turbo.h`, `warp.h` | split / verbatim | rows listed under `tick_source` |

### 2.6 `engine/ee/intc/`

| New pair | Class / content | OLD source | Shape | Flags |
|---|---|---|---|---|
| `intc.h/.cpp` | `Intc`: INTC_STAT/MASK at `0x1000F000/F010`, 15 causes, handler registry (flat table, savestate INTC section), `Raise`, `Pending`, `Dispatch(cause)` running guest handlers through `DispatchTable`; registered on `MmioRouter` (displaces the sink for `0x1000F000–0x1000F01F`) | `intc.{h,cpp}` | verbatim + class | — |
| `ee_timers.h/.cpp` | `EeTimers`: T0–T3 COUNT/MODE/COMP/HOLD MMIO `0x10000000–0x10001FFF` reading through `DerivedClocks`; `PumpAtBoundary`, `PumpAtCountRead`; INTC TIM0–3 raise | `ee_timers.{h,cpp}` [R195-TIM0] | split (MMIO+ISR here, clock math in timing) | `PS2_EE_TIM0` NORM |
| `timer_codec.h/.cpp` | `TimerCodec`: MODE bitfield pack/unpack, HOLD/COMP semantics (pure) | `ee_timers.h:1-177` | split | — |

### 2.7 `engine/gs/hle/sce/` — Phase-3 form of the boundary OWNER

| New pair | Class / content | OLD source | Shape | Flags |
|---|---|---|---|---|
| `gs_sync.h/.cpp` | `GsSync`: the two SdkBinding rows `sceGsSyncV` (libgraph; seam `recompiled.cpp:26198`) and `WaitForVblank` (sif_hle mirror) → `BoundarySequencer::RunBoundary()` and return; `g_sceGsSyncV_count` (referenced by the generated TU) is its counter; `ShutdownLadder` (S29 unwind) lives here. Phase 6 adds GS-side steps INTO the sequencer, never into this file. Also owns the seam body `hle_sceGsSyncPath` (`:26184`). NOT here: `hle_SetupDisplayRegisters` (`:12195`, hooked `0x0019aff0`) — a HOOK §2 workaround row whose pass-through body is title-side (§3.4.1, §2.8) | `gs_hle.cpp:405-745 hle_sceGsSyncV`, `sif_hle.cpp:194-431 hle_WaitForVblank` (boundary halves) | split | — |

### 2.8 `games/<title>/` — title-side pieces

| New file(s) | Content | OLD source | Shape | Flags |
|---|---|---|---|---|
| `title_profile.h/.cpp` | `kExampleTitleUS`: serial `SLUS_123.45`, crc `<elf-crc>`, elf path + PT_LOAD range `0x00100000–0x003DAE80` (file-backed to `0x0030A0B8`), entry `0x00100008`, sdk 3.0.1, `boot{argv0="cdrom0:\SLUS_123.45;1", argv_block=0x30a980}`, `quirks{xgkick_defer=true, round_sprite_mode=1, budget{base=8800, base_low, step, lowdraw, phase}}`, `save{slot layout}`, `recomp{shard_count=32}` | `runtime-environment.md`, A6 §4, `memory.cpp:140/166-170`, `ps2_argv.h`, `budget_math.h` constants | data | `PS2_RECOMP_SHARD_COUNT` SETTING (build option), `PS2_SHARDED_RECOMP` NORM ON, `PS2_RECOMP_DIRECT_CALLS` NORM ON |
| `hle/budget.h/.cpp`, `hle/loader_sync.h/.cpp` | `TitleBudgetModel` params; `LoaderSyncParams` (FUN_0017d6b8 spin model params; mechanism lands Phase 4) | `budget_math.h`, `hle/loader_sync_hle.h`, `hle/loader_oracle.h` | data | — |
| `hle/workarounds/meta_arena.h/.cpp` | **HOOK §3.1 row 0x181560 [META-ARENA]** — `MetaArena`: OWNS the 2 MB backing store, registers it on `AddressMap` as the extra region `[0x02000000, +2 MB)` (§3.4.2), and routes ≤0x20 metadata allocations from the pool-allocator call sites into it. REQUIRED for `mem.mhash`/`restore.mhash` parity (the META section exists because of it). Boot needs it: the allocator runs before the park | `hle_meta_arena.{h,cpp}`, `main.cpp:3095-3108` | quarantined verbatim | — |
| `hle/workarounds/key_redirect.h/.cpp` | **HOOK §3.8 [KEY-REDIRECT]** — the every-256-writes name-key repair (`obj+0x500`) — registered as a `FastMem` write observer over the one address range, NOT in `ee/mem`. Boot-critical? No — it acts at level load; transplanted in 3E anyway because it is a write-path mutator whose absence changes `mem.mhash` in Phase 4+ | `memory.cpp:3149-3221`, `memory.h:150-160` | quarantined verbatim | — |
| `hle/workarounds/README.md` ledger | rows for the 7 quarantined workarounds (HOOK §2: 0x181560, 0x1b36b8 [RENDINIT], 0x268440 [SESSION-14], 0x2ad110 dest-preservation, KEY-REDIRECT, 0x0019aff0 hle_SetupDisplayRegisters seam, 0x181490 piracy-check bypass). **Needed to BOOT to the park (Phase 3): 0x181560 and 0x0019aff0 (seam is in the generated TU; body = the title pass-through `hle/workarounds/setup_display_registers.cpp`, §3.4.1).** 0x1b36b8/0x268440 fire in the boot flow *after* `sceSifInitRpc` (module loads precede FUN_00178388's init paths) → Phase 4; 0x2ad110 + 0x181490 are loader/SIF → Phase 4. Each row: address, HOOK class, suspected defect, phase transplanted, retirement status (open) | HOOK §2/§3 | ledger | — |
| `recomp/` (TRACKED, lint-exempt) | `recompiled.h`, `recompiled.cpp`, `recompiled_shard_000..031.cpp`, `inputs/{tiny_stubs.txt,tiny_returners.txt}`, `MANIFEST.sha256`, `INVOCATION.md`, `README.md` | generated by the frozen tool (§3) | generated | `RECOMP_ENGINE` DELETE (v2 is the only engine), `PS2_RECOMP_CORPUS` TEST (the recorder arm compiled in by the tool; NEW never sets it) |
| `savestate/ee_sections.h/.cpp` (+ `thread_sections`, `kernel_sections` pairs) | `EeSectionProviders`: RAM0/SPAD/META/THRD/SEMA/EVFL/INTC/CNTR/MISC/HEAP providers for the Phase-1 `core/savestate` registry (tag → serialize/restore/validate); THRD = ThreadInfo kernel fields + raw `CpuContext` + shadow chain innermost-first with `ctx_size` guard; resume-address containment through `RecompFnIdFor(_Containing)` (the cross-engine gate, A4 §6). Sections whose owner is a later phase (VRAM, VU*, DMAC, GSPR/GSST, VIFS/GIFS, PADS, FILE, STRM, SIFS, VU1R, SDRX, MCHL, IOPM) are SKIPPED on read (optional) and not written | `savestate.cpp` (2,450 ln; row I), `savestate_format.{h,cpp}` (Phase 1), `docs/SAVESTATE_SCHEMA.md` | split (providers only) | `LOGO_MEMSNAP*` DELETE, `PS2_DETWATCH` DIAG |
| `verify/manual_body_ledger.tsv` | 243 rows {address, first leg exercising it, verdict=open} (HOOK §3.2 adjudication ledger; no bodies transplanted) | `manual_switch_dispatchers.cpp` install list (addresses only) | ledger | — |

### 2.9 `engine/ee/recomp_abi/` — the generated-code seam (NEW must provide; §3.4 has the contract)

| New file(s) | Content | Why |
|---|---|---|
| `recomp_abi/host_app/{cpu_state,memory,syscalls,sif_hle,gs_hle,hle_heap,ps2_threading,cdvd_stream,timer_hle,vu,sif_rpc_handlers}.h` + `recomp_abi/anchor/README.md` | Declaration-only forwarding headers with the OLD names the generated TUs `#include "../host_app/…"` (11 headers, `recompiled.cpp:1-20`). The build adds `-I engine/ee/recomp_abi/anchor` so `anchor/../host_app/x.h` resolves; no directory named `host_app` exists beside `recomp/`. Each header forwards to the engine class (e.g. `memory::read<T>` → `FastMem::Read<T>` on the singleton). Lint: declaration-only adapters, registered as the ONE exemption class "generated-code ABI" in `lint_one_class.py` (no types defined) | the frozen output is never hand-edited (CONSTRUCTION §5.3); its include paths and free-function names are the ABI |
| `recomp_abi/recomp_abi.cpp` | Definitions of the globals the generated TUs reference and no engine class owns (§3.4 rows 9, 12, 24, 25, 27): `g_logFile` (an unopened `std::ofstream` — failbit set, so the 42,306 `g_logFile <<` sites format nothing), `g_call_census_enabled=false` + empty `call_census_enter/exit`, `g_vu0` (`Vu0Macro`) / `g_vu1` (`Vu1Window`) Phase-3 park forms, `StreamPollCompleteMarker` inert, `g_alloc_trace_seq`, `g_pose_wb_count`, `g_alloc_live(_mutex)`, `g_ring_watch_addr`. NOT here: `g_func_trace_enabled` (TU-internal `static`, row 11), `g_ee_call_stack*` (defined by the common TU, row 8), `crashring_*` (`seams.cpp`, row 26 — and NOT `core/log/ring_log.cpp` either, P3-SEAM-4), `g_vsync_frame`/`g_sceGsSyncV_count` (`gs_sync`, row 15) | measured symbol census of `OLD/build-v2-nog/recompiled*.cpp` (§3.4) |

## 3. The RECOMPILER OUTPUT step (stage 3B)

### 3.1 Exact bounded invocation (CONSTRUCTION §A.1, verbatim; executable form `tools/regen_from_frozen.sh`)

```
OLD=$PS2R_OLD_ROOT
NEW=<repo>
TOOL=$OLD/build-v2-nog/recompiler_tool_v2        # md5 6235a2f9e08121b4b694a870dcffc95c (Sep 20 01:19)
ELF=$OLD/game_files/example_title.rep/SLUS_123.45   # md5 36583bd799a803cae7b8f2a7b2ca6592, 2,151,940 bytes
OUT=$NEW/games/<title>/recomp
WORK=$NEW/build/regen                              # NEW-side scratch: the tool's logs/ land HERE (R16), never in OLD

mkdir -p "$OUT/inputs" "$WORK" && cd "$WORK"       # cwd PINNED: main_v2.cpp:480-484 opens logs/… relative to cwd
cp "$OLD/tmp_analysis/tiny_stubs.txt" "$OLD/tmp_analysis/tiny_returners.txt" "$OUT/inputs/"
#   tiny_stubs.txt     216 lines  md5 045de1a51dadfd46ae58609bcd0ea2e8
#   tiny_returners.txt 535 lines  md5 b6537d0d014818ea4915a8538aaa9ddd   (recompiler.cpp:8003-8021 cwd lookup)
cp "$OUT/inputs/tiny_stubs.txt" "$OUT/inputs/tiny_returners.txt" "$WORK/"
"$NEW/verify/run_bounded.sh" 1800 "$WORK/recomp_gen.log" -- \
  "$TOOL" --input "$ELF" --output-header "$OUT/recompiled.h" --output-cpp "$OUT/recompiled.cpp" \
          --shards 32 --direct-calls
# NO --ghidra-input / --overrides / --manual-json: pure-discovery emission (main_v2.cpp:499). Exit via ${PIPESTATUS[0]}.
( cd "$OUT" && shasum -a 256 recompiled.h recompiled.cpp recompiled_shard_0*.cpp inputs/*.txt > MANIFEST.sha256 )
```

The script takes `--out DIR` so the determinism check runs it twice into `build/regen/a` and `build/regen/b`
(each ~10 s, §A.1 probe) and then `diff -r --exclude=logs a b` must be EMPTY before `a` is copied into `$OUT`.
`--check` re-hashes `$OUT` against `MANIFEST.sha256` (ctest `RecompManifestIntact`).

### 3.2 Expected report lines (quoted into `INVOCATION.md`; the stage fails if any differs)

```
[v2] no --ghidra-input: emitting the pure discovery set
[v2] discovered 8819 functions, 177 jump-table sites, 8848 unreached gap words
[v2] emission model: 8819 functions (0 aliases, 0 pinned), 177 jr table sites
Found stub file: tiny_stubs.txt        / Auto-registered 0 tiny 'jr ra; nop' stubs
Found stub file: tiny_returners.txt    / Auto-registered 36 tiny 'return N' stubs
[v2] emitted … (32 shard(s))
```
Shape checks: `recompiled.h` declares 8,818 `FUN_xxxxxxxx` + `entry` (= 8,819 — NOT the 9,327 of the frozen
`build-v2-nog/recompiled.h`, which is the Ghidra+overrides emission, §A.1 provenance paragraph); 34 files.
Size: §A.1 records ~151 MB for the pure-discovery output; the Ghidra emission in `build-v2-nog` measures
88 MB shards + 2.6 MB common + 0.5 MB header — 3B records the real number in `INVOCATION.md`.

`INVOCATION.md` provenance block: tool md5, ELF md5, both input md5s, cwd, the command, the six report lines,
the `build-v2-nog` provenance paragraph (so nobody re-derives the false "byte-compare against build-v2-nog"
premise), and the manifest.

### 3.3 Compiling the shards

- Library `ps2r_title_recomp` (STATIC): the 32 shards + `recompiled.cpp`; `app/host` links it through the
  title module. OLD compiles shards at `-O2 -fno-strict-aliasing -fwrapv` (`OLD/CMakeLists.txt:243/324/407`)
  and the common TU at `-O0`; NEW keeps exactly that per-TU split (`set_source_files_properties`), because
  `-fwrapv`/`-fno-strict-aliasing` are semantic (signed overflow and the `GprReg` union punning), not tuning.
- Include path: `-I engine/ee/recomp_abi/anchor` (§2.9); nothing under `games/<t>/recomp/` is edited.
- Warnings: the generated TUs get `-w` (they are not our code); the ABI headers are warning-clean.
- Compile-time budget: 3B measures, on this machine, `time cmake --build build --target ps2r_title_recomp -j4`
  with `-O2` shards and records wall/user/maxrss per shard (`/usr/bin/time -l`) in `PHASE3_IMPL.md`. Acceptance
  bound for the dev preset: a full recomp build ≤ 15 min wall at `-j4` and ≤ 3 GB RSS per shard; if exceeded,
  the dev preset drops the shards to `-O1` **and records it as a build-tuning setting** (`Config` is not involved —
  optimisation level is not guest-visible; `-fwrapv`/aliasing flags never change).
- The 6 `getenv` sites in `recompiled.cpp` (`PS2_EE_RAWDISP` ×1, `PS2_RECOMP_CORPUS` ×2, `PS2_SHADOW_DIAG` ×3;
  measured) are emitted by the frozen tool. Generated code is never edited: `LintNoGetenv` gets a dated exemption
  for exactly `games/<t>/recomp/recompiled.cpp` (the same rule as §A.1's `iop_recompiled.cpp`), the arms are
  grep-verified OFF-unless-set, and 9.1's in-tree recompiler removes them (golden diff = those arms).
  `recompiled_shard_*.cpp` contain 0 `getenv` (measured).

### 3.4 Generated-code seam contract — COMPLETE (p3a revision per the p3pr review; measured census over
`OLD/build-v2-nog/recompiled*.cpp`; the pure-discovery emission uses the same generator, so 3B re-runs
`tools/recomp_symbol_census.py` on the NEW output and diffs it against this table — any symbol not in it
is a plan defect, fixed in the ABI headers and recorded)

Provider kinds: **real** = an engine class body; **park** = a `SifRpcAbsent`-style body that records
`park=<reason>` in `run.meta`, releases the run token and never returns to the caller's flow;
**title pass-through** = a body in `games/<t>/hle/glue/` or `games/<t>/hle/workarounds/` that invokes the
hooked address's own generated body (decision §3.4.1); **inert** = a definition whose observable effect is
nothing (`false`, empty body, unopened stream); **defined by the generated TU** = NEW must NOT define it.

| # | Symbol the generated TUs use | Sites (build-v2-nog) | Phase-3 provider |
|---|---|---|---|
| 1 | `#include "../host_app/{cpu_state,memory,syscalls,sif_hle,gs_hle,hle_heap,ps2_threading,cdvd_stream,timer_hle,vu,sif_rpc_handlers}.h"` | `recompiled.cpp:1-20`, each shard | `engine/ee/recomp_abi/host_app/*.h` declaration-only forwarders (11) |
| 2 | `CpuContext` + `cpuRegs.GPR.r[n].UD/UL/US/UC/SD/SL`, `cpuRegs.HI/LO/HI1/LO1/CP0/sa/pc`, `fpuRegs.fpr[n].f/UL`, `fpuRegs.ACC`, **`ctx.vuRegs.*`** (the member is `vuRegs`, type `VUregs` — `cpu_state.h:290-294`; the p3p draft's `vu0Regs` was wrong) | every shard | **real**: `ee/cpu/cpu_state.h` (names AND layout identical; `sizeof == 1600` == anchor THRD `ctx_size`, measured 3A) |
| 3 | `QuadWord` (16-byte union `u8[16]/u16[8]/u32[4]/u64[2]/s*/f32[4]/m2[16]`, `memory.h:36-50`) | every `read_quad/write_quad` site | **real**: `ee/mem/quad_word.h` (own POD, same member names) |
| 4 | `memory::read<T>` 143,068 · `memory::write<T>` 71,192 · `memory::read_quad` 3,976 · `memory::write_quad` 4,240 | shards | **real**: `FastMem` through the `memory` namespace forwarder |
| 5 | `ps2_float_clamp` 53,750 · `ps2_div_s` 463 · `ps2_sqrt_s` 307 · `ps2_rsqrt_s` 7 | shards | **real**: `ee/cpu/ps2_float` (inline bodies in `ps2_float.inl`) |
| 6 | `runtime_syscall_dispatcher(v1, ctx)` 63 · `handle_mtc0_write` 3 | shards | **real**: `SyscallTable` (3C2) |
| 7 | `recomp_call` 21,740 · `recomp_indirect_call` 2,652 · `recomp_dispatch_fallback` · `recomp_mark_hooked` · `find_containing_function` (2,893 shard sites) · `update_function_impl` (177) · `initialize_recompiled_functions` · `recomp_fn_id_for(_containing)` · `g_recomp_fn_{count,addrs,cache,hooked,raw}` · `recomp_raw_dispatch` · `g_sorted_func_{impls,starts,ends}` · `g_sorted_tables_ready` · `recompiled_functions` · `function_ranges` (21 symbols) | header inline + common TU (+ shard call sites) | **defined by the generated TU**; `DispatchTable` (3B) installs THROUGH `update_function_impl` (R137) and defines nothing twice. **3B correction (P3-SEAM-1):** `recomp_dispatch_fallback` (`recompiled.cpp:1340`) and `recomp_mark_hooked` (`:4182`) are DEFINED by the common TU; the one real seam of OLD's `recomp_seams.cpp` (27 lines) is **`recomp_fcf_miss(pc)`** — declared `:4195`, called `:4270` from `find_containing_function`'s all-tiers-missed tail, defined by no generated TU → `seams.cpp` park body (`park=fatal_no_jump`), and row 7 now has 22 entries |
| 8 | `g_ee_call_stack` (thread_local, `recompiled.cpp:94`) · `g_ee_call_stack_depth` · `ee_call_stack_push/pop` · `ee_get_call_stack_depth/top` 2,619 | **defined by the common TU** (`:94-130`); shards call | **defined by the generated TU** — `ShadowCallStack` (3B) is a NO-STORAGE wrapper that reads the common TU's `extern` chain for the THRD harvest and the park-site dump; the p3p draft ("ShadowCallStack provides the storage") was wrong |
| 9 | `call_census_enter/exit` · `g_call_census_enabled` | header inline | **inert**: `recomp_abi.cpp` `false` + empty bodies (census is `core/diag`, PS2R_DIAGNOSTICS only) |
| 10 | `g_recomp_corpus_enabled` 2,619 · `recomp_corpus_record/flush_now` · `g_recomp_corpus_pairs` | shards + common TU | **defined by the generated TU**; NEW never enables (`PS2_RECOMP_CORPUS` TEST-ONLY) |
| 11 | `g_func_trace_enabled` (`static std::atomic<bool>`, `recompiled.cpp:79`) | common TU only | **TU-internal** — NOT a seam; NEW provides nothing (the p3p draft listed it under `recomp_abi.cpp` — wrong) |
| 12 | `g_logFile` 42,306 (shards) + 302 (common) | everywhere | **inert**: `recomp_abi.cpp` unopened `std::ofstream` (failbit ⇒ inserters no-op) — NOT `core/log` |
| 13 | `ps2_threading::exitThread(ctx)` 177 · `ps2_threading::lookupThreadInfo(1)` (`:25101`, reads `->gp`) | shards / common TU | **real**: `ThreadManager` (3C1) through the `ps2_threading` forwarder (`ThreadInfo` exposes `gp`) |
| 14 | `hle_sceSifInitRpc` (`:26842`, hooked at **`0x2d1a50`** — the "sceSifInitRpc" seam; the registry row for the same family is **`0x2d2180`**: `OLD/host_app/sce_hle/sce_hle_bindings.inc:228` `SCE_HLE_BINDING(0x002d2180, "sceSifInitRpc", "libkernl.a")` — `recompiled.cpp:1381` is only the `g_recomp_fn_addrs` dense table that lists the address) · `hle_sceSifBindRpc` `:26893` · `hle_sceSifCallRpc` `:26901` | common TU seams | **park** (`park=sif_rpc_absent`): `ee/hle/kernl_sif_rpc` (3C2) — the PARK OWNERS are the `0x2d1a50` seam AND the `0x2d2180` registry row; Phase 4 rebinds through `sif/hle` registration |
| 15 | `hle_sceGsSyncV` `:26198` · `hle_sceGsSyncPath` `:26184` · `g_sceGsSyncV_count` · `g_vsync_frame` (34 common-TU + 12 shard sites) | common TU (+ shards for `g_vsync_frame`) | **real** (Phase-3 form): `gs/hle/sce/gs_sync` (3D2) → `BoundarySequencer` |
| 16 | `hle_sceGsResetGraph` · `hle_sceGsResetPath` · `hle_sceGsSendImage` | common TU seams (1 each) | **title pass-through** (`games/<t>/hle/glue/sce_graph_glue.cpp`): the seam body invokes the hooked address's generated body; Phase 6 registers real GS-priv bodies |
| 17 | `hle_sceGifSyncDma` · `hle_sceDmaSync` | common TU seams (1 each) | **title pass-through** (`games/<t>/hle/glue/sce_dma_glue.cpp`); the underlying register polls read the `MmioSink` idle values (DMAC CHCR.STR=0 ⇒ the generated body returns immediately); Phase 5 rebinds |
| 18 | `hle_TimerWaitMicroseconds` `:11645` | common TU | **real**: `ee/hle/kernl_timer` (3C2) → `DerivedClocks` |
| 19 | `hle_SetupDisplayRegisters` `:12195` (hooked address `0x0019aff0`, HOOK §2 workaround row) | common TU | **title pass-through**: `games/<t>/hle/workarounds/setup_display_registers.cpp` — the body invokes the generated body of `0x0019aff0` (ledger row, §2.8) |
| 20 | `hle_InitTLB` (hooked `0x2d6618`) · `hle_ContextRestore` (hooked `0x2d6880`) | common TU seams | **real** (Phase-3 form): `ee/hle/kernl_boot` (3C2) — `InitTLB` = no-op returning as the BIOS does (no TLB in NEW, ARCH §4.11); `ContextRestore` = the kernel context-switch return (thbase 1.02 `RFU`-style restore of `$ra/$sp` from the thread's saved context via `ThreadManager`) |
| 21 | `hle_DoGlobalConstructors` (hooked `0x2d69a0`) | common TU seam | **title pass-through** (`games/<t>/hle/glue/crt0_glue.cpp`): invokes the generated body of `0x2d69a0` — this IS the §1 step-2 "ctors" step; no HLE reimplementation |
| 22 | `HLE_002c5080`, `HLE_002c5820`, `HLE_002c5940`, `HLE_002c6ce0`, `HLE_002c6d8c`, `HLE_002c6e88`, `HLE_002c7070`, `HLE_002c71bc`, `HLE_002c7418`, `HLE_002c75e8`, `HLE_002ccba0`, `HLE_002cf930` (12 free functions, `hle_heap.h:263-276`; `HLE_002c72d4`/`HLE_002c6f40` are declared there but unreferenced) — there is **no `hle_heap::` namespace**: the p3p draft's namespace row was wrong | common TU seams (1 each) | **title pass-through** (`games/<t>/hle/glue/libc_heap_glue.cpp`): each body invokes the hooked address's generated body (the allocator/libc HLE is quarantined→retired per INDEX :654; the meta-arena reroute, §2.8, is registered separately as a FastMem-independent allocator observer in 3E) |
| 23 | `hle_sceCdStInit/Start/Stop/Read/Seek/Pause/Resume/Stat` (8) · `hle_sceCdDiskReady` · `hle_sceCdDiskReady2` | common TU seams (1 each) | **park** (`park=iop_dependent`): `ee/hle/kernl_file_io` (3C2) — bodies never reached before the SIF park; recorded so the census is complete. Phase 4 rebinds through `cdvd/hle` |
| 24 | `StreamPollCompleteMarker` (`cdvd_stream.h`) | common TU (1) | **inert**: `recomp_abi.cpp` returns "no marker" (the Phase-4 stream HLE displaces it) |
| 25 | `g_alloc_trace_seq` · `g_alloc_live(_mutex)` · `g_pose_wb_count` · `g_ring_watch_addr` | common TU diag arms | **inert**: `recomp_abi.cpp` definitions (never read by NEW) |
| 26 | `crashring_enabled` · `crashring_record_indirect` · `crashring_fatal_no_jump(uint32_t pc)` declared **`extern "C" [[noreturn]]`** (`recompiled.cpp:4410`) | common TU (declarations + the `fatal_no_jump` call) **and every shard**: `crashring_enabled` / `crashring_record_indirect` are referenced 2,619× each from the shards' indirect-call sites (one per `recomp_indirect_call` guard; measured over `build-v2-nog`) | `seams.cpp` is the ONLY definer of the trio (**P3-SEAM-4**: Phase 1 had also defined it in `core/log/ring_log.cpp`; two strong definitions — removed there, ring_log keeps the behaviour): **real** — `crashring_enabled` is the plain bool the guards test, bound by `DispatchTable::Initialize` through `RingLog::BindGeneratedGate(&crashring_enabled)` and kept equal to "EE channel in the verbose tier" (latched at Bind and on every `RingLog::Configure`, either order); `record_indirect` → `RingLog::EmitV(CH_EE, TAG_INDIRECT_JUMP)`; **`fatal_no_jump` = `RingLog::FatalNoJumpReport(pc)` (ring_crash.rlog + crash_report.txt + stderr, RETURNS) then a park body that NEVER RETURNS** — it records `park=fatal_no_jump` + `park_pc` in `run.meta`, releases the run token and blocks the guest thread forever (`Park::Forever()`); `exit()`/`_exit()` is never called (Phase 1's `_exit(1)` is gone) and returning is UB under `[[noreturn]]` (the p3p draft's "no-op body" was wrong). `recomp_fcf_miss` (row 7) routes through it as OLD did |
| 27 | `g_vu0.Execute(...)` 76 · `g_vu0.ResumeIfWaiting()` 16 · `g_vu1.Execute(...)` 16 — `g_vu0` is referenced from **14 shards** (007, 008, 009, 011, 012, 014, 015, 016, 017, 023, 026, 027, 029, 030: `VCALLMS`/`VCALLMSR` sites), `g_vu1` only from **5** (009, 011, 012, 029, 030: the `vu1` window); the earlier "(shards 007/008)" was wrong | shards | **real** (Phase-3 form): `recomp_abi.cpp` defines `g_vu0` as `Vu0Macro` (register block = `CpuContext.vuRegs`; `Execute` records `park=vu_micro_absent` — a boot leg reaching a VCALLMS before the SIF park is a construction finding) and `g_vu1` as `Vu1Window` (`Execute` = same park); Phase 5 replaces both definitions |

**Row count: 29 rows / 108 roster entries (+ the 11 include paths of row 1)** — 3B added `recomp_fcf_miss` to row 7 (P3-SEAM-1) and rows 28–29 (P3-SEAM-2/3, below); the p3a figure was 27 rows / 101. The roster is the enumerated
list in §3.4.4 — every entry grep-verified present in `OLD/build-v2-nog/recompiled{.h,.cpp,_shard_*.cpp}` by
`tools/recomp_symbol_census.py --dir OLD/build-v2-nog --check` (landed in the p3a revision; 101/101 present, 11/11
include paths, 0 missing): the SCRIPT's `ROSTER` is the source of truth and this section quotes it. The p3a headline
"118 distinct symbols" (with a breakdown that summed to 106 and a row 7 that enumerated 21 under "15") was a
hand-sum error; 101 is the counted number and is the 3B census criterion.

#### 3.4.4 Symbol roster (101 entries; row → symbols)

- Row 2 (1): `CpuContext`
- Row 3 (1): `QuadWord`
- Row 4 (4): `memory::read<T>`, `memory::write<T>`, `memory::read_quad`, `memory::write_quad`
- Row 5 (4): `ps2_float_clamp`, `ps2_div_s`, `ps2_sqrt_s`, `ps2_rsqrt_s`
- Row 6 (2): `runtime_syscall_dispatcher`, `handle_mtc0_write`
- Row 7 (21): `recomp_call`, `recomp_indirect_call`, `recomp_dispatch_fallback`, `recomp_mark_hooked`,
  `find_containing_function`, `update_function_impl`, `initialize_recompiled_functions`, `recomp_fn_id_for`,
  `recomp_fn_id_for_containing`, `g_recomp_fn_count`, `g_recomp_fn_addrs`, `g_recomp_fn_cache`,
  `g_recomp_fn_hooked`, `g_recomp_fn_raw`, `recomp_raw_dispatch`, `g_sorted_func_impls`, `g_sorted_func_starts`,
  `g_sorted_func_ends`, `g_sorted_tables_ready`, `recompiled_functions`, `function_ranges`
- Row 8 (6): `g_ee_call_stack`, `g_ee_call_stack_depth`, `ee_call_stack_push`, `ee_call_stack_pop`,
  `ee_get_call_stack_depth`, `ee_get_call_stack_top`
- Row 9 (3): `call_census_enter`, `call_census_exit`, `g_call_census_enabled`
- Row 10 (4): `g_recomp_corpus_enabled`, `recomp_corpus_record`, `recomp_corpus_flush_now`, `g_recomp_corpus_pairs`
- Row 11 (1): `g_func_trace_enabled` (TU-internal; listed so the census knows it is NOT a seam)
- Row 12 (1): `g_logFile`
- Row 13 (2): `ps2_threading::exitThread`, `ps2_threading::lookupThreadInfo`
- Row 14 (3): `hle_sceSifInitRpc`, `hle_sceSifBindRpc`, `hle_sceSifCallRpc`
- Row 15 (4): `hle_sceGsSyncV`, `hle_sceGsSyncPath`, `g_sceGsSyncV_count`, `g_vsync_frame`
- Row 16 (3): `hle_sceGsResetGraph`, `hle_sceGsResetPath`, `hle_sceGsSendImage`
- Row 17 (2): `hle_sceGifSyncDma`, `hle_sceDmaSync`
- Row 18 (1): `hle_TimerWaitMicroseconds`
- Row 19 (1): `hle_SetupDisplayRegisters`
- Row 20 (2): `hle_InitTLB`, `hle_ContextRestore`
- Row 21 (1): `hle_DoGlobalConstructors`
- Row 22 (12): `HLE_002c5080`, `HLE_002c5820`, `HLE_002c5940`, `HLE_002c6ce0`, `HLE_002c6d8c`, `HLE_002c6e88`,
  `HLE_002c7070`, `HLE_002c71bc`, `HLE_002c7418`, `HLE_002c75e8`, `HLE_002ccba0`, `HLE_002cf930`
- Row 23 (10): `hle_sceCdStInit`, `hle_sceCdStStart`, `hle_sceCdStStop`, `hle_sceCdStRead`, `hle_sceCdStSeek`,
  `hle_sceCdStPause`, `hle_sceCdStResume`, `hle_sceCdStStat`, `hle_sceCdDiskReady`, `hle_sceCdDiskReady2`
- Row 24 (1): `StreamPollCompleteMarker`
- Row 25 (5): `g_alloc_trace_seq`, `g_alloc_live`, `g_alloc_live_mutex`, `g_pose_wb_count`, `g_ring_watch_addr`
- Row 26 (3): `crashring_enabled`, `crashring_record_indirect`, `crashring_fatal_no_jump`
- Row 27 (3): `g_vu0.Execute`, `g_vu0.ResumeIfWaiting`, `g_vu1.Execute` (two OBJECTS `g_vu0`/`g_vu1` defined by NEW)
- Row 28 (5, **added 3B — P3-SEAM-2**): `HLE_FUN_002aac80`, `HLE_FUN_002ad1a0`, `HLE_FUN_002ad8f0`, `HLE_FUN_002adb18`,
  `HLE_FUN_002adb40` — the loader file-IO hooks the common TU installs (windowed ISO read, WaitForReadComplete, close
  handle; OLD `hle_heap.h:283-287`, spelled `HLE_FUN_` so the p3a census regex `HLE_[0-9a-f]{8}` missed them).
  **park** (`park=iop_dependent`), `seams.cpp`; Phase 4 rebinds through `cdvd/hle`
- Row 29 (1, **added 3B — P3-SEAM-3**): `GetPtcRecord(const std::string&)` (OLD `hle_heap.h:495`, the PTC-stub record
  lookup inside the common TU's stream-open arm). **inert**: `recomp_abi.cpp` returns `nullptr` ("no known .ptc path")

Sum: 1+1+4+4+2+22+6+3+4+1+1+2+3+4+3+2+1+1+2+1+12+10+1+5+3+3+5+1 = **108** (`tools/recomp_symbol_census.py`
`ROSTER_TOTAL`). By Phase-3 provider kind (each entry counted once; the 3B measured split — see `PHASE3_IMPL.md` §3B):
**defined by the generated TU** 31 (row 7 minus `recomp_fcf_miss` = 21, row 8 = 6, row 10 = 4), **not a seam** 1 (row 11),
**real (types + forwarders)** 11 (rows 2, 3, 4, 5 — `CpuContext`, `QuadWord`, `memory::*` → `FastMem`, `ps2_*` →
`Ps2Float`; row 26 `crashring_enabled` (RingLog-bound gate) + `crashring_record_indirect` → `RingLog::EmitV` — moved
from inert in the p3br loop, P3-SEAM-4), **park** 24 (rows 14, 23, 26 `fatal_no_jump` (report first), 27 `Execute`×2,
`recomp_fcf_miss`, row 28, `exitThread`), **interim park until the named stage** 9 (rows 6, 18, 20 → 3C2; row 15
`SyncV/SyncPath` → 3D2), **inert** 13 (rows 9, 12, 24, 25, 29, row 27 `ResumeIfWaiting`, row 15 counters ×2,
`lookupThreadInfo`), **title pass-through** 19 (rows 16, 17, 19, 21, 22): 31+1+11+24+9+13+19 = 108.

#### 3.4.1 Decision — where pass-through seam bodies live (review item 2)

**Chosen: title-side glue TUs.** Every "pass-through" body above is a file under
`games/<t>/hle/glue/` (`sce_graph_glue.cpp`, `sce_dma_glue.cpp`, `crt0_glue.cpp`, `libc_heap_glue.cpp`) or, for the
HOOK §2 workaround row `0x0019aff0`, under `games/<t>/hle/workarounds/setup_display_registers.cpp`. Each body is
one line: `RecompGenerated::Invoke(kHookedAddress, ctx)` where `kHookedAddress` is the title constant and
`RecompGenerated::Invoke` is the generated `FUN_xxxxxxxx(ctx, addr)` symbol called directly (the glue TU
includes `recompiled.h`, which is title-side and lint-exempt). Rejected alternative: an address-free
`DispatchTable::InvokeGeneratedBody(addr)` primitive — it would put a "call the ORIGINAL body of a hooked
address" concept into `engine/`, whose only consumers are title workarounds; ARCH §11 purity prefers the
concept to live with its consumers. Consequence: `engine/` contains zero game addresses (LintEnginePurity);
`games/<t>/hle/glue/README.md` lists the rows with their hooked addresses and retirement status (Phase 4/5/6
rebinds each through a subsystem registration and deletes the glue line).

#### 3.4.2 Meta arena placement (review item 3)

Per CONSTRUCTION §4.3 the meta arena is `games/<t>/hle/workarounds/meta_arena.{h,cpp}` (the arena AND the
reroute policy are one quarantined workaround; the p3p draft's `engine/ee/mem/meta_arena` pair is DROPPED).
`AddressMap` gains a GENERIC `RegisterExtraRegion(phys_base, bytes, backing)` primitive (3A); the title
registers `[0x02000000, +2 MB)` at composition time (3E). `restore.mhash` coverage: `MemoryCheckpointer`
order is RAM 32 MB → SPAD 16 KB → META 2 MB; the META bytes come from the registered extra region, so the
hash covers it only when the title has registered it — the coldboot leg registers it before `entry` (the
allocator runs before the park), and the anchor restore writes the META section into the registered region
(a missing registration makes the META restore fail loudly, never silently skip).

#### 3.4.3 Initial RAM image contract (review item 4) — decided

OLD's image (`memory.cpp:166-221` + `:236-310`) = zero-fill → 5 hardcoded ELF SECTIONS (`.vutext` `0x2D9A10`
0xD140, `.data` `0x2E6B80` 0x5428, `.vudata` `0x2EBFA8` 0x8A, `.rodata` `0x2EC080` 0x1D458, `.sdata`
`0x309500` 0xBB8 — **no `.text`**: the code is the recompiled output) → the Ghidra data map
`game_files/ghidra_functions/ram_data_map_v50.txt` (19,281 `addr:value` entries covering `[0x2E6B80, 0x30A0BB)` — the last entry starts at `0x30A0B7` and is 4 bytes wide, §7.1 P3-RAMIMAGE-1;
md5 `b064c68028af08ad282b30e22b1fa5a3`) → three post-map patches (`:310-312`: `0x2e6f40=0x10008000`,
`0x2e6f44=0x10009000`, `0x2e804c=0`). **NEW loads the ELF's PT_LOAD segments retail-faithfully** (`ElfLoader`,
every `PT_LOAD` `p_filesz` bytes at `p_vaddr`, `p_memsz-p_filesz` zero-filled — so `.text` is ALSO present, which
OLD never had; the recompiled code never reads it, but the image is now a superset and the loader HLE / reference
interpreter can) **and then applies the same data map as a TITLE INPUT** (`games/<t>/data/ram_data_map_v50.txt`,
copied with md5 in `games/<t>/data/README.md`; the three patches become `games/<t>/data/post_load_patches.txt`
applied by the same `DataMapApplier`), so byte-for-byte OLD ⊆ NEW over every address OLD initialised.
**3A probe** (`tests/ee/test_elf_loader <ELF> <data map> <patches>` — the probe runs whenever the map path is given; there is no separate mode flag): for every map entry compare the ELF's own
bytes at that address against the map value; report `entries`, `covered_by_elf`, `mismatches`; any mismatch is
recorded in §7.1 as P3-RAMIMAGE with the addresses (a mismatch means OLD's boot image differs from the retail
image at that address — a finding to carry into the Phase-4/5 gates, never silently tolerated).
The p3p draft's `loader/*` cite is DROPPED (no such OLD directory; the ELF walk is written fresh from the ELF
spec: `Elf32_Ehdr`/`Elf32_Phdr`, little-endian, `EM_MIPS`).

### 3.5 The two v2 defects as Phase-3 work items — P4FIX-F1 (`ps2state_slot0_f57292`, `ps2state_slot0_f59991`)

Symptom on OLD `build-v2/host_app/host_app` (Ghidra+overrides emission): deterministic `[IND-JUMP-BAD] @PC=0x1f4940`
→ `[FATAL-NOJUMP] pc=0` — FUN_001f4730's callback slot reads 0 (RECOMP_P4_FIX §3; CONSTRUCTION §4.3). Family:
"v2 loses a race" — a timing/OS-layer ordering defect, not an emission defect (the legacy binary runs both legs).

Phase-3 work (root cause starts here; CLOSURE is Phase 4/5 when a gate runs the legs through the crash frame):
- **W1 repro leg (deterministic, NEW self-consistent):** `run_leg.py --tree new --load-state f57292 --frames 0`
  restores and runs to `park=sif_rpc_absent` 3×; `restore.mhash` == `state_hash.py` (the state itself is sound).
- **W2 slot census:** at restore, `MemoryCheckpointer`-style FNV over the callback table FUN_001f4730 indexes
  (address range from the THRD chain + the OLD log's slot index) is recorded in `run.meta` as `p4fix_slot=`;
  the same census on the OLD binary's own restore (`--tree v2`, bounded, 60 frames — the same probe CONSTRUCTION
  §2 used) tells whether the slot is 0 *in the state file* (⇒ the race happened before the save, on OLD, and the
  defect is a Phase-4 IOP/SIF ordering item) or is zeroed *after* restore (⇒ an EE-side relaunch/election
  ordering item — BUG-036 territory, fixable in `EeRunToken::EndRelaunch` here).
- **W3 invariant test:** `tests/ee/test_relaunch_order` — after `EndRelaunch`, the first runner is the thbase
  winner and no `EeEventQueue` entry was drained on a non-holder thread (BUG-035/036 as unit tests).
- Recorded in `docs/bugs/` under the P4FIX-F1 id with the W2 verdict; no pins, no hand bodies (rule 6 exception).

## 4. Oracle and test strategy

### 4.1 Unit tests (`tests/ee/`, ctest names in CamelCase)

| Test | Pins | Oracle |
|---|---|---|
| `EeCpuStateLayout` | `sizeof(CpuContext)` and `offsetof` of every serialized member == OLD's values. OLD is never built (rule 7): the expected `sizeof` is the THRD section's `ctx_size` field in the 11 anchor `.ps2st` files (8 `ps2state_slot0_*` + `artdet_f37152` + 2 `slowspot_*`; it IS OLD's `sizeof(CpuContext)`; all 11 measured 1600 in 3A), and the member offsets are read from `cpu_state.h` by hand and cited by line | anchor THRD `ctx_size` + `cpu_state.h` |
| `EeCpuStateLayoutAnchor` | the same binary in anchor mode: parses `OLD/save_states/ps2state_slot0_f36935.ps2st` with `ps2r::savestate::ReadStateFile`, walks the THRD records (`ThreadRecHdr`, 80 bytes packed, `savestate.cpp:547-559`) and requires every `ctx_size == sizeof(CpuContext)`; SKIP 77 when the frozen anchor is absent | the anchor file itself (read-only) |
| `Ps2FloatSemantics` | clamp table, div/sqrt/rsqrt single-precision + rounding-mode cases | OLD `tests/recomp_semantics/cases.h` FPU rows |
| `FastMemDiff` | fast vs slow path equivalence over the mirror/scratchpad/extra-region/HW-window/quad-mask grid (**all 27 pools** × 400 addresses × 5 widths — the p3a first port carried 24 and silently dropped `iop_ram_end`, `kseg1_bios_mirror`, `high_seg_ram_wrap`; restored in the p3a revision with direct spot checks, `static_assert`ed at 27) | port of OLD `tests/fastmem_diff/test_fastmem_diff.cpp` grid (`:96-122`); no stubs needed — NEW's slow path is `MmioRouter`/`AddressMap` |
| `AddressMapAndMmioSinkIdleValues` | translation order + straddle rule + generic window/region registration; every sink range's read value == the lifted table | table-driven (`mmio_sink.cpp` cites OLD lines per row) |
| `ElfLoaderTitle` | entry `0x00100008`, one PT_LOAD `0x00100000` filesz `0x20A0B8` memsz `0x2DAE80`, `.bss` zero-filled, data map 19,281 entries applied as a superset, post-load patches last; **the RAM-image probe** (§3.4.3) | ELF file (OLD `game_files/`, read-only) + `games/<t>/data/*` |
| `ThreadManagerThbase` | election, priorities, rotate, sleep/wakeup counts, KE_* codes | port of OLD `tests/r139_threads/test_r139_threads.cpp` (+ `r139_shims.cpp`) |
| `EeRunTokenAndEventQueue` | BUG-035 drain invariant, BUG-036 relaunch handoff | port of OLD `tests/test_ee_sched.cpp` |
| `EmuDomainAndRegime` | quantum carry, settle, lead governor, regime hysteresis, ReAnchor | port of OLD `tests/test_time_domain.cpp` |
| `TickSyncBoundaryOrder` | the 8-step order; deferred demands; one tick per boundary | port of OLD `tests/test_ee_timing.cpp` |
| `BoundaryProtocolPace` | park/valve behaviour under `engine.ee_runahead` | port of OLD `tests/test_ee_pace.cpp` (`EEPACE_SOAK_SEED` TEST-ONLY seed) |
| `EeTimersTimr` | T0 9600/tick, subtick clamp, PumpAtCountRead | port of OLD `tests/test_ee_timers_timr.cpp` (+ `ee_timers_stub.cpp`) |
| `BudgetClock` | closed-form fold vs the shard_009 emission constants | port of OLD `tests/r140_budget/test_r140_budget.cpp` |
| `BootEnvRetailArgv` | argc=1 vector at `0x30a980`; the legacy vector is never produced | port of OLD `tests/test_ps2_argv.cpp` (retail rows only) |
| `LibcHleDiff` | the 4 SdkWrap rows vs the v2 bodies | port of OLD `tests/libc_hle_diff/test_libc_hle_diff.cpp` |
| `SyscallTableCoverage` | all 119 live case labels reachable (+ `default:`; `0x74` is commented out at `:2496` and must NOT be dispatched); unknown numbers return OLD's RFU result | table-driven from `syscalls.cpp:2315-2504` labels |

### 4.2 OLD recompiler oracle suites, ported to run against `games/<t>/recomp/` (`tests/recompiler/`)

From `OLD/tests/`: `recomp_semantics/` (cases.h, gen_words.cpp, mem_shim.cpp, prelude — semantics of emitted
instruction forms), `recompiler_verify/` (interpreter.{h,cpp}, mem_image, state_delta, savestate.{h,cpp},
sut_runner + `sut_link_shims.cpp`: the reference-interpreter-vs-SUT differential runner; anchor tests,
controlflow tests, savestate tests), `recompiler_v2/` (test_convention_enforcers.cpp — the RECOMP_CONVENTIONS
enforcers DS1–DS7/R1–R6; test_oracle_diff_v2.cpp + oracle_executor + fixtures_{delayslot,structure,switch}.h;
test_recomp_gates.cpp; test_discovery_real_elf.cpp — the 8819/177 discovery counts — NOTE: discovery tests need the
tool SOURCES, which are Phase 9.1: only the emission-shape assertions port now, the discovery tests are listed as
9.1 ports), the corpus audit over `OLD/recompiler_tool/corpus` pair files (`corpus_observed_p4.txt`: GAPS=0
against the NEW output's static tables — the audit binary is OLD's `build-v2/…` tool run bounded as an oracle, or
the pair check re-implemented in `tools/corpus_audit.py` — 3B decides by what the frozen binary accepts as input).
Legacy-engine arms (`test_oracle_diff_legacy.cpp`) are not ported (D Phase 2 per INDEX :620).

### 4.3 The restore.mhash gate (OLD-derived memory oracle)

`verify/state_hash.py <anchor.ps2st>`: parse PS2SAVE1 (magic, version 1–2, section table, CRC32 per section,
trailer CRC), locate RAM0/SPAD/META, and print FNV-1a64 over their raw payloads in `MemoryCheckpointer` order
(RAM 32 MB → SPAD 16 KB → META 2 MB). NEW's `VerifyHost` writes `restore.mhash` with the same definition right
after the restore, before the first guest instruction. 11/11 anchors must match (`verify/battery.py --restore-only`).
A mismatch is ALWAYS a construction bug (address map, section restore, meta-arena layout, envelope reader).

### 4.4 The boot leg (headless, deterministic profile)

`nativeee --profile deterministic --headless --harness-out D --frames 0` (coldboot) and `--load-state <anchor>`
(11 anchors). `run.meta` keys added in Phase 3: `park=<none|sif_rpc_absent|iop_dependent|unrecompiled_pc|fatal_no_jump>`,
`park_pc=0x…`, `park_tid=`, `unhandled_mmio=<n>`, `restore_mhash_ok=<0|1>`. Pass = `park=sif_rpc_absent`,
`finalized=1`, `frames=0`, four lane files present, identical `run.meta` (minus `wall_ms`) across 3 reps.

## 5. Flags consumed (73 rows, FLAG_PHASE_COUNTS P3 block; outcome applied at transplant time)

| Outcome | Vars | Applied as |
|---|---|---|
| NORM (38) | `PS2_ARGV`, `PS2_BUDGET_BASE`, `PS2_BUDGET_BASE_LOW`, `PS2_BUDGET_EMU`, `PS2_BUDGET_LOWDRAW`, `PS2_BUDGET_PHASE`, `PS2_BUDGET_STEP`, `PS2_CDVD_BUDGET_KB`, `PS2_EEFEED`, `PS2_EE_PACE`, `PS2_EE_TIM0`, `PS2_FASTMEM`, `PS2_LIBC_HLE`, `PS2_LOAD_BUDGET_LIFT`, `PS2_LOAD_BUDGET_LIFT_ENGAGE_MS`, `PS2_LOAD_BUDGET_LIFT_GAP_MS`, `PS2_LOAD_BUDGET_LIFT_INPUT_MS`, `PS2_LOAD_BUDGET_LIFT_RELEASE_MS`, `PS2_LOAD_TURBO_ENGAGE_MS`, `PS2_LOAD_TURBO_GAP_MS`, `PS2_LOAD_TURBO_INPUT_MS`, `PS2_LOAD_TURBO_K`, `PS2_LOAD_TURBO_RELEASE_MS`, `PS2_MTVU`, `PS2_MTVU_DEFER`, `PS2_PRESENT_EDGE`, `PS2_QUANTUM`, `PS2_RECOMP_DIRECT_CALLS`, `PS2_RQ_MAXJOBS`, `PS2_RQ_MAXMB`, `PS2_RQ_SAFETY`, `PS2_SHARDED_RECOMP`, `PS2_T0_SUBTICK`, `PS2_TIME_EMU`, `PS2_TIME_EMU_INTRA`, `PS2_TIME_EMU_REGIME`, `PS2_TURBO_DUCK`, `PS2_VBLANK_FREERUN` | winning arm only (peak config); constants become `TitleProfile.quirks.budget` / named constexprs in the owning class; `PS2_MTVU*`/`PS2_RQ_*` are Phase-5/6 consumers whose SITE file is Phase 3 — the constants are recorded in `PHASE3_IMPL.md` for those phases, no code |
| SETTING (11) | `GAME_FILES_DIR`→`paths.game_files`, `PS2_CDVD_INSTANT`→`cdvd.latency_model`, `PS2_EE_RUNAHEAD`→`engine.ee_runahead`(2), `PS2_LOAD_TURBO`→`cdvd.load_turbo`(off), `PS2_PERF`→`diag.perf`, `PS2_RECOMP_SHARD_COUNT`→CMake `PS2R_RECOMP_SHARDS`(32; must equal `--shards`), `PS2_RTC_FIXED`→`game.rtc_fixed`, `PS2_T0_TICKPACED`→`time.t0_tickpaced`, `PS2_TICK_DETERMINISTIC`→`time.tick_deterministic`, `PS2_TICK_SYNC`→`time.tick_sync`, `PS2_WARP_TO`→`harness.warp_to_frame` | `Config` fields already declared in Phase 1 with peak defaults; Phase 3 adds the READERS |
| DIAG (11) | `PS2_BOOT_WATCH`, `PS2_BUDGET_DIAG`, `PS2_DETWATCH`, `PS2_EESCHED_DIAG`, `PS2_EE_RAWDISP`, `PS2_HAVE_BACKTRACE`, `PS2_INJ_PROBE`, `PS2_LVL_DIAG`, `PS2_MEMWATCH`, `PS2_SHADOW_DIAG`, `PS2_TIMEDOM_DIAG` | `core/diag` categories under `PS2R_DIAGNOSTICS`; probe SITES keyed on game addresses are not transplanted; `PS2_EE_RAWDISP`/`PS2_SHADOW_DIAG` remain as the frozen tool's emitted `getenv` arms (§3.3 exemption) until 9.1 |
| TEST (3) | `EEPACE_SOAK_SEED`, `PS2_FRAME_LOCKSTEP`, `PS2_RECOMP_CORPUS` | `tests/` only (seed param; lockstep assertion; the corpus recorder is compiled by the tool, never enabled) |
| DELETE (10) | `LOGO_MEMSNAP`, `LOGO_MEMSNAP_FRAMES`, `PS2_FMV_NATIVE`, `PS2_S15_WATCH`, `PS2_S8_MTXWATCH`, `PS2_STRMQ`, `PS2_T0TRACE`, `PS2_T0_NOISR`, `PS2_TURBO`, `RECOMP_ENGINE` | var, gate and subject code not transplanted (`PS2_STRMQ` is the Phase-4 IOP output's emitted arm — listed here by site attribution, handled by §A.1's exemption there) |

`tools/flag_phase_counts.py` is not modified; `PHASE3_IMPL.md` quotes each row with its applied outcome and
`FlagPhaseCountsReproducible` stays green.

## 6. Proof gate and Definition of Done (CONSTRUCTION §4.3, restated as checkable items)

| # | Gate item | Check |
|---|---|---|
| G1 | Regen determinism | two bounded runs of the frozen tool → `diff -r --exclude=logs` empty; report lines == §3.2; 8,818 `FUN_` + `entry`; 34 files; `MANIFEST.sha256` + `INVOCATION.md` written; `RecompManifestIntact` ctest |
| G2 | Recomp oracle suites | ported `recomp_semantics`, `recompiler_verify`, convention enforcers green against `games/<t>/recomp/`; corpus audit GAPS=0 |
| G3 | Restore oracle | `restore.mhash == verify/state_hash.py` on 11/11 anchors (`battery.py --restore-only`) |
| G4 | Boot reachability | `coldboot` → `park=sif_rpc_absent`, `frames=0`, `finalized=1`, four lane files, `mem.mhash` empty; each anchor → `park=sif_rpc_absent`; 3 reps identical `run.meta` (NEW self-consistency, recorded not claimed) |
| G5 | Unit | §4.1 table 100 % green; `ElfLoaderTitle` (+ RAM-image probe recorded); `MmioSinkIdleValues`; `ThreadManagerThbase`; `SyscallTableCoverage` (119 labels + default) |
| G6 | Lints | `LintOneClass`, `LintEnginePurity`=0, `LintNoGetenv` (with the ONE dated generated-file exemption), `FlagPhaseCountsReproducible` |
| G7 | Ledgers | `hle/workarounds/README.md` rows (7, two transplanted in Phase 3), `verify/manual_body_ledger.tsv` 243 rows, `INVOCATION.md` provenance block, P4FIX-F1 W2 verdict recorded in `docs/bugs/` |
| DoD | all of G1–G7; the boundary owner stub + MMIO sinks + empty sequencer slots exist so Phase 4 adds only its own files to reach boundary 0; `PHASE3_IMPL.md` complete with per-stage handoffs | — |

Accepted-difference protocol: not applicable (no lanes claimed). A `restore.mhash` mismatch is a construction bug.

## 7. Stages (≤45 min each; every stage ends with the tree building and `ctest --preset macos-dev` green)

| Stage | Scope | OLD sources read | Green criterion (own) |
|---|---|---|---|
| **3A** memory + cpu + tests | `ee/cpu/{cpu_state.h, ps2_float(.inl), cpu_context}`, `ee/mem/{quad_word.h, main_memory, scratchpad, address_map, fastmem(.inl), mmio_router, mmio_sink, elf_loader, data_map_applier}`, `core/types.h`; `games/<t>/data/{ram_data_map_v50.txt, post_load_patches.txt, README.md}`; tests `EeCpuStateLayout`, `Ps2FloatSemantics`, `FastMemDiff`, `AddressMap`, `MmioSinkIdleValues`, `ElfLoaderTitle` (+ the RAM-image probe, §3.4.3); READMEs | `cpu_state.h`, `common/Pcsx2Types.h` (names only), `memory.{h,cpp}` (fast path `memory.h:60-260`, translate order `:358-505`, image `:166-312`, read-path idle constants `:539-981` — cited by line in `mmio_sink.cpp`), `main.cpp:529-600`, `tests/fastmem_diff/*`, `tests/recomp_semantics/cases.h` | tests green; `sizeof(CpuContext)` == anchor THRD `ctx_size` (1600); ELF loads with the §1 facts; probe result recorded (§7.1); lints 0 |
| **3B** recompiler output + shard build + seam contract | `tools/regen_from_frozen.sh` (+`--check`), two bounded regen runs, `games/<t>/recomp/` populated (+`inputs/`, `MANIFEST.sha256`, `INVOCATION.md`, README), `tools/recomp_symbol_census.py --dir games/<t>/recomp --check` (script landed in 3A; 3B adds the UNLISTED-symbol scan over the NEW output), `engine/ee/recomp_abi/{host_app/*.h, anchor/, recomp_abi.cpp}`, `ee/cpu/{dispatch_table, shadow_call_stack (no storage — §3.4 row 8), seams.cpp (row 26 park body)}`, CMake `ps2r_title_recomp` with the per-TU `-O2/-O0` + `-fwrapv -fno-strict-aliasing`; `LintNoGetenv` exemption; compile-time measurement | `build-v2-nog/recompiled{.h,.cpp}` shape, `recomp_seams.cpp`, `main.cpp:464 install_dual_hook` seam, `CMakeLists.txt:243/324-419`, `recompiler_tool/main_v2.cpp:480-499`, `recompiler.cpp:8003-8021` | G1 items; `tools/recomp_symbol_census.py --dir games/<t>/recomp --check` reports 101/101 present, 0 unlisted (§3.4.4; or the roster updated and the diff recorded); `ps2r_title_recomp` links into `nativeee` (unused yet); build time + RSS recorded; ctest green. If the shard build exceeds 35 min of the stage, STOP after the regen + manifest + ABI headers and hand the library build to 3B′. **3B outcome:** all items landed (library builds in 101 s at -j4); 3B′ inherits only the per-TU RSS follow-up (common TU 4.83 GB at `-O0`) and the link into `nativeee` (3F) — see `PHASE3_IMPL.md` §3B |
| **3C1** kernel objects + run token + event queue | `ee/hle/{thread_manager, semaphores, event_flags, alarms, rpc_callback_queue}`, `ee/sched/{ee_run_token, ee_event_queue}`; tests `ThreadManagerThbase`, `EeRunTokenAndEventQueue` (BUG-035 both halves), W3 `RelaunchOrder` | `ps2_threading.{h,cpp}`, `ps2_scheduler.cpp` (rules only), `ee_sched/*`, `tests/{r139_threads,test_ee_sched.cpp}` | 3 tests green; thbase election + KE_* codes pinned; holder-only drain AND `ApplyingOnThisThread` mutator rule pinned; lints 0 |
| **3C2** syscalls + boot env + libc + SDK binding + libkernl families | `ee/hle/{syscall_table, dyn_interp, boot_env, libc, sdk_binding, kernl_boot, kernl_timer, kernl_file_io, kernl_sif_rpc, libscf_bindings.cpp, libdev_bindings.cpp}`, `games/<t>/gen/sce_hle_bindings.inc` (copied DATA + provenance), `games/<t>/hle/glue/*` pass-through TUs (§3.4.1); tests `BootEnvRetailArgv`, `LibcHleDiff`, `SyscallTableCoverage` (119 labels + default; `0x74` commented out) | `syscalls.{h,cpp}:2315-2504`, `ps2_syscall_wrappers.*`, `ps2_argv.h`, `libc_hle.*`, `sce_hle/*`, `sif_hle.cpp` boot-env half, `main.cpp:5242-5300` composition order, `tests/{test_ps2_argv.cpp,libc_hle_diff}` | 3 tests green; `SdkBindingManifest::Install` runs over the generated tables in the OLD order; `runtime_syscall_dispatcher` linked to the shards; every §3.4 row has its provider linked (census script reports 0 unresolved); lints 0 |
| **3D1** tick + emu domain + derived clocks + budget | `ee/timing/{tick_source, emu_domain, regime_detector, derived_clocks, budget_clock, phase_classifier, turbo_governor, warp}`; tests `EmuDomainAndRegime`, `BudgetClock` | `time_domain.h`, `ps2_vblank.{h,cpp}` (generator half), `turbo.h`, `warp.h`, `budget_math.h`, `main.cpp:3226-3608`, `timer_hle.*`, `tests/test_time_domain.cpp`, `tests/r140_budget` | 2 tests green; the nine clocks each have a named accessor with its domain in the header comment; `Pinned` regime under the deterministic profile; lints 0 |
| **3D2** boundary + intc + timers + gs_sync | `ee/timing/{tick_sync, frame_rendezvous, boundary_sequencer, boundary_protocol}`, `ee/intc/{intc, ee_timers, timer_codec}` (registered on `MmioRouter`, displacing the sink), `gs/hle/sce/gs_sync` Phase-3 form; tests `TickSyncBoundaryOrder`, `BoundaryProtocolPace`, `EeTimersTimr` | `ee_timing/*`, `quantum.h`, `ee_pace.h`, `timeline.h` WAITFIX arm, `ee_timers.{h,cpp}`, `intc.{h,cpp}`, `gs_hle.cpp:405-745`, `sif_hle.cpp:194-431`, `tests/{test_ee_timing,test_ee_pace,test_ee_timers_timr}.cpp` | 3 tests green; `UnhandledRanges()` no longer lists INTC/timers; sequencer slots 4/5/6 exist and are empty; one tick per boundary pinned; lints 0 |
| **3E** workarounds quarantine + title profile + savestate restore + restore.mhash | `games/<t>/{title_profile, hle/budget, hle/loader_sync, hle/workarounds/{meta_arena_reroute, key_redirect, README}, savestate/{ee_sections, thread_sections, kernel_sections}, verify/manual_body_ledger.tsv}`, `verify/state_hash.py`, `core/verify/verify_host` `restore.mhash` line + `run.meta park=` keys, `battery.py --restore-only` | `hle_meta_arena.{h,cpp}`, `main.cpp:3095-3108`, `memory.cpp:3149-3221`, `savestate.cpp` providers, `savestate_format.*`, `docs/SAVESTATE_SCHEMA.md`, `verify/memory_checkpointer.cpp` order, `runtime-environment.md`, `manual_switch_dispatchers.cpp` install list | G3: 11/11 `restore.mhash == state_hash.py`; ledger files exist (7 + 243 rows); lints 0 |
| **3F** boot leg + gate | `app/host` wiring: composition order (ElfLoader → BootEnv → CpuContextInit → `initialize_recompiled_functions` → `SdkBindingManifest::Install` → workarounds → ThreadManager main thread → `entry`), the `SifRpcAbsent` park, `run.meta` keys, `battery.py` Phase-3 mode (coldboot + 11 anchors × 3 reps), P4FIX-F1 W1/W2 legs, `PHASE3_IMPL.md` gate table | `main.cpp:5225-5300` (order), `recompiled.cpp:26842-26901` seams, OLD `--tree v2` bounded run for W2 (oracle use only) | G4 + G7; all §6 rows ticked or handed off with the exact failing command |

### 7.1 Findings ledger (recorded, never silently tolerated)

| Id | Stage | Finding | Disposition |
|---|---|---|---|
| P3-RAMIMAGE-1 | 3A | RAM-image probe over the 19,281 data-map entries: `covered_by_elf=19278`, `mismatched_entries=0`, `mismatched_bytes=0` — every map value equals the retail ELF's own bytes. The 3 uncovered entries (`0x30A0B5/B6/B7`, 4-byte zeros) straddle the `.sdata` file end into `.bss`, which the retail load zero-fills anyway (map end is therefore `0x0030A0BB`, not the review's `0x30A0B7` = last entry's START). | The data map is redundant with the retail PT_LOAD image; kept as a title input for byte-identity with OLD's boot image and as the probe's oracle. Candidate for retirement in Phase 4 (deleting it changes nothing measurable). |
| P3-RAMIMAGE-2 | 3A | Post-load patches (OLD `memory.cpp:310-312`): `0x2e6f40=0x10008000` and `0x2e6f44=0x10009000` EQUAL the ELF bytes (no-ops); **`0x2e804c=0` DIFFERS — the retail image holds `0x00000001`** (OLD's comment: "Initialize this so it can move past in 2d18f8 which checks for"). | OLD's boot image deviates from retail at ONE word. Kept as `games/<t>/data/post_load_patches.txt` (applied last) so Phase-3/4 legs reproduce OLD's image; adjudicated when the boot leg runs (3F): if `FUN_002d18f8`'s check passes with the retail value under NEW's kernel HLE, the patch retires as a HOOK §2 workaround row; else it stays a documented title patch. |
| P3-SEAM-1 | 3B | The census's UNLISTED scan over the NEW output found `recomp_fcf_miss` declared (`recompiled.cpp:4195`) and called (`:4270`) but defined by no generated TU; the plan's row 7 had named `recomp_dispatch_fallback`/`recomp_mark_hooked` as the `seams.cpp` bodies — both are DEFINED by the common TU (`:1340`, `:4182`). | Row 7 corrected (22 entries); `seams.cpp` provides `recomp_fcf_miss` as a `fatal_no_jump` park (OLD `recomp_seams.cpp:25` policy: terminal). |
| P3-SEAM-2 | 3B | Compiling the common TU against the ABI headers failed on five `HLE_FUN_002a*` hooks (`:23742`, `:23908`, `:23942`, `:23988`, `:23995`) — the census regex `HLE_[0-9a-f]{8}` did not match the `HLE_FUN_` spelling. | Row 28 (5 entries, park `iop_dependent`); `FAMILY_RE` gained `HLE_FUN_\w+`. |
| P3-SEAM-3 | 3B | Same compile: `GetPtcRecord` (`:26501`) belongs to no census family. | Row 29 (inert `nullptr`); `FAMILY_RE` lists it by name. |
| P3-SEAM-4 | 3B (p3br loop 1) | Row 26 had TWO strong `extern "C"` providers: `ee/cpu/seams.cpp` (3B) and Phase 1's `core/log/ring_log.cpp` (`_exit(1)` on fatal). `test_dispatch_table` linked green only because no object pulled `ring_log.o`; a TU referencing `RingLog::Configure` (the composition root does) fails with 3 duplicate symbols, and the two bodies disagreed (`_exit` vs park). | ONE provider: `seams.cpp` (the ee layer owns the frozen C names — a park lives in `ee/`, and `core` knows nothing above it). `ring_log.{h,cpp}` lose the trio; `RingLog::FatalNoJump` → `FatalNoJumpReport` (dumps + report, RETURNS, no `_exit`); `RingLog::BindGeneratedGate(bool*)` replaces the direct write to `crashring_enabled` (bound by `DispatchTable::Initialize`, re-latched by every `Configure`). Link guard: `RecompDispatchTableLink` references `RingLog` AND the trio, so `ring_log.o` and `seams.o` are always pulled together — a second definition can never hide again. `test_ring_log` tests the bind; CONSTRUCTION_PLAN row `core/log/ring_log` annotated. |
| P3-VROPS | 3B | Shards 009/011/012/029/030 (VU0 macro `VSUB`/`VMUL` sites, 58 `-` + 21 `*`) use `VR_reg op VR_reg`; OLD declared the operators as `VR_reg` members (`cpu_state.h:82-109`), which the frozen register POD in `engine/ee/cpu/cpu_state.h` does not carry. | Free operators `+ - * /` in the ABI forwarder `recomp_abi/host_app/cpu_state.h` (component-wise, operands and result through `ps2_float_clamp` — OLD's rule); the POD's layout is untouched. |
| P3-ELFRANGE | 3A | The plan (and `runtime-environment.md`) stated the load range as `0x00100000–0x0020A0B8`; the ELF has one `PT_LOAD` with `p_filesz=0x20A0B8` (end `0x0030A0B8`) and `p_memsz=0x2DAE80` (end `0x003DAE80`). OLD's hand-typed section table (`.vutext … .sdata` up to `0x30A0B8`) agrees with the file-backed end. | Plan §1/§2.8/§4.1 corrected in 3A; the steering note is a doc-only fix outside this tree. **3B (p3ar item 1):** `tests/lint/lint_engine_purity.py` `EE_TEXT_HI` widened from `0x0020FFFF` (a value derived from the wrong size-as-end) to the PT_LOAD memsz end `0x003DAE80` (exclusive), so game DATA/BSS addresses in `engine/`/`verify/` are caught too; selftest gained the `.data`/`.bss`/`memsz-end` cases; the scan stays at 0 findings (no size-constant collision present — `purity_allowlist.txt` documents that only 1/2/3 MiB size constants may ever be listed). |
| P3-THPRI-SELF | 3C1 (p3c1r nit d) | thbase R8 says ChangeThreadPriority moves the target to the TAIL of the new priority's ready queue and reschedules. For the CALLER (tid 0 / self) the kernel's observable rule is unverified against hardware: does a self-priority change to a LOWER level yield at once to an equal-priority READY thread (tail placement ⇒ yes) or keep running (RUN state is not in a ready queue)? `ThreadManager::ChangeThreadPriority` currently re-enqueues self at the tail and runs the election (yields). OLD (`ps2_threading.cpp`) did the same. | Recorded, not tolerated silently: pinned by `EeRunTokenAndEventQueue` (CTP tail) as NEW's behaviour; adjudicate against the thbase 1.02 manual / a PCSX2 trace when a boot-leg divergence names it. |
| P3-TIMPUMP-SUPERSET | 3D2 (p3d2r note 5) | OLD's ISR-at-read pump ([R195-TIM0], `ee_timers.cpp`) fired only T0's pending compare/overflow ISRs when T0_COUNT was read; NEW's `EeTimers::PumpAtCountRead` runs at a COUNT read of ANY of T0..T3 and pumps every dispatchable timer (`HasDispatchable()`), a SUPERSET of OLD's site. The boundary pump (slot 2) is identical in both. Observable only when the game reads T1/T2/T3 COUNT with a pending, enabled ISR between two boundaries — the title programs T0 (the budget/`sceGsSyncV` clock) and reads T0_COUNT; T1..T3 reads are not seen in the OLD traces. | Recorded, not tolerated silently: the superset is the hardware rule (an ISR is pending regardless of which register is read). If a Phase-4+ leg diverges on a T1..T3 ISR count, gate the read-pump to T0 behind the title profile, never in `engine/`. |
| P3-MANIFEST-12 | 3F | 12 of the 295 `sdk_bindings.tsv` rows name addresses that are NOT function starts of the pure-discovery emission (`_sceMpegWaitIpuIdle`, `_sceMpegFlushBuf`, `_copyAddRefImage`, `_copyRefImage`, `adddate`, `subdate`, `AdjustTime`, `iWakeupThread`, `SetT2`, `iSetTimerAlarm`, `sceSifInitRebootNotify`, `InitTLB`); `DispatchTable::Install` rejects them, so they run inside their enclosing generated function. `SyscallTableCoverage` installed through an always-true recorder and did not see it. | Recorded in run.meta (`sdk_rows_installed=283`, `sdk_rows_not_fn_start=12` + list); 3F′/Phase 4 decides: regen with the 12 starts, or `no-hook` TSV rows with the reason. The two syscall wrappers (`SetT2`, `InitTLB`) still reach the kernel table through their `syscall`. |
| P3-BOOT-WAITSEMA | 3F | The cold boot dispatches `WaitSema` before any `CreateSema` (trace: InitMainThread, InitHeap, FlushCache, GetThreadId, WaitSema, GetThreadId, then the SIF park at caller `0x2cdadc`). | Recorded; compare with the OLD boot trace in 3F′ before calling it a defect (the kernel returns the thbase error and the boot proceeds). |

Stage rule reminders (STAGE_RULES): each stage records its handoff in `docs/phases/PHASE3_IMPL.md` (files, OLD
sources transplanted, flags consumed with outcome, tests added, gate status, next command); no stage edits
`games/<t>/recomp/` by hand; every OLD binary run goes through `verify/run_bounded.sh`.

## 8. Review notes (p3p self-review, 2026-09-26) and pre-registered decisions

1. **Directory naming vs the brief.** The brief says "ee/kernel BiosScheduler"; CONSTRUCTION §4.3 and INDEX :238–:245
   place the thread/sema/evflag/syscall classes under `ee/hle` (SDK/OS HLE class, HOOK §1) with the run token
   under `ee/sched`. This plan follows CONSTRUCTION/INDEX (the skeleton has `engine/ee/{cpu,hle,intc,mem,sched,timing}`
   and no `kernel/`). `ThreadManager` is the BiosScheduler.
2. **`recomp_abi` is engine-side, not title-side.** The frozen tool's include paths and free-function names are the
   tool's ABI, identical for any title it emits; the adapter therefore lives in `engine/ee/recomp_abi/` (no game
   constants → `LintEnginePurity` unaffected). It is the only place OLD's namespace names (`memory::`,
   `ps2_threading::`) appear in NEW (there is no `hle_heap::` namespace — `hle_heap.h` declares free `HLE_002c*` functions, §3.4 row 22), and 9.1's in-tree recompiler retires it (golden diff: the
   generated includes become `engine/…` headers and the free functions become class calls).
3. **`g_logFile` as an unopened `std::ofstream`** keeps 42k generated `<<` sites inert without editing them; the
   3B census verifies the failbit trick costs nothing observable (the sentry short-circuits) — if the per-call cost
   shows in the compile-time/RSS numbers it is irrelevant to behaviour, and runtime cost is a 9.1 concern.
4. **Phase-3 SIF syscalls return OLD's idle results** (SetDma → OLD's transfer-id shape, DmaStat → done) so crt0/
   init code that touches SIF *registers* proceeds; the PARK fires only at the RPC bindings (`sceSifInitRpc` and
   friends). If the boot leg shows the game spinning on a SIF register instead of reaching the binding, the spin
   site becomes a second park reason (`park=sif_reg_spin`) — recorded, not worked around.
5. **Workarounds needed for the Phase-3 boot:** only 0x181560 [META-ARENA] (the allocator runs before the park and
   the META section must exist for `restore.mhash`) and the 0x0019aff0 seam body (pass-through). KEY-REDIRECT is
   transplanted in 3E for completeness of the write path but does not fire before the park (level-load time).
   The remaining four are Phase 4 (verified against the boot flow in 3F: if any fires before the park, 3F transplants
   it and updates the ledger row).
6. **Manual bodies:** none transplanted (rule 6 exception). The 243-row ledger is created empty-verdict in 3E.
7. **What could push a stage over 45 min:** the shard build (3B — pre-registered split into 3B′), the 119-label
   syscall table (3C2 — the table is mechanical; if the port exceeds the budget the remaining labels return the RFU
   result and are listed in the handoff), the savestate providers (3E — THRD chain validation is the risky part;
   restore.mhash needs only RAM0/SPAD/META, so the memory oracle can go green before THRD is complete).

## 9. p3pr review — the eight corrections as applied by p3a (2026-09-26)

| # | Review item | Applied where |
|---|---|---|
| 1 | §3.4 seam contract COMPLETE with a provider per symbol; wrong rows fixed (no `hle_heap::` namespace; `g_func_trace_enabled` TU-internal; `g_ee_call_stack` + the four `ee_*call_stack*` seams DEFINED by the common TU — `ShadowCallStack` wraps, no storage; `ctx.vuRegs` not `vu0Regs`; `crashring_fatal_no_jump` `[[noreturn]]` → park body never returns; park owners = `0x2d1a50` seam AND `0x2d2180` registry row) | §3.4 (27 rows / 101 roster entries — §3.4.4, corrected from the hand-summed 118 in the p3ar iteration), §2.1 cpu_state row, §2.9 `recomp_abi.cpp` row, §7 3B row, §8.2 |
| 2 | Pass-through seam bodies live title-side (`games/<t>/hle/glue/*`, `hle/workarounds/setup_display_registers.cpp`); no address-free DispatchTable primitive | §3.4.1 (decision + rejected alternative), §2.7, §2.8 |
| 3 | Meta arena per CONSTRUCTION §4.3 (`games/<t>/hle/workarounds/meta_arena`), region registered generically via `AddressMap::RegisterExtraRegion`; `restore.mhash` coverage documented | §3.4.2, §2.2 (engine pair dropped), §2.8 |
| 4 | Initial RAM image contract: retail PT_LOAD load + the data map as a title input (`games/<t>/data/`, md5) + post-load patches; 3A probe; `loader/*` cite dropped | §3.4.3, §2.2 `elf_loader`/`data_map_applier` rows, §7.1 P3-RAMIMAGE-1/2 |
| 5 | Syscall count 271 → 120 → **119 live labels + `default:`** (`syscalls.cpp:2315-2504`; 120 had counted the commented-out `// case 0x74: sceSifSetDma` at `:2496`) | §2.3, §4.1, §8.7, 3C2 row, G5 |
| 6 | BUG-035 both halves (holder-only drain AND `ApplyingOnThisThread` honoured by mutators) | §2.4 `ee_event_queue` row, 3C1 green criterion |
| 7 | 3C → 3C1 + 3C2; 3D → 3D1 + 3D2, each with its own green criterion | §7 stage table |
| 8 | Citation fixes (9): (a) `main.cpp:529` → `:529-600` + extent caveat; (b) `memory.cpp translate_address` → `:358-505` with per-arm lines; (c) `memory.cpp:140/166-170` → `:166-221` LoadElfSections + `:240-312` map/patches; (d) `loader/*` → dropped (nonexistent); (e) `read_slow/write_slow` dispatch → `is_io_register :531-536`, `read_io_register :539-981`; (f) D_ENABLER "`memory.cpp:941-943`, lifted in 3A" → `dmac.h:75` initialiser + `dmac.cpp:1127` (= 0); (g) GS_CSR idle → `:921-929` first-match arm (0x8), `:945` dead arm noted; (h) `cpu_state.h` layout rows → `:143-281` (VUregs), `:268-280` (Reset), `:290-294` (`vuRegs` member); (i) the ELF range `0x00100000–0x0020A0B8` → PT_LOAD facts (P3-ELFRANGE) | §1, §2.1, §2.2, §4.1, §7.1 |

### 9.1 p3ar review of the 3A submission (loop iteration 1) — the five items as applied

| # | Review item | Applied where / evidence |
|---|---|---|
| 1 | Syscall count is **119** live `case` labels + `default:`, not 120 (`// case 0x74: sceSifSetDma` at `syscalls.cpp:2496` is commented out) | §2.3 `syscall_table` row, §4.1 `SyscallTableCoverage`, §6 G5, §7 3C2 row, §8.7, §9 row 5; `PHASE3_IMPL.md` Part 1 row 5. Re-measured: `sed -n 2315,2504p | grep -cE '^\s*case '` = 119, `default:` = 1 |
| 2 | §3.4 count inconsistent (118 / 106 / 21-under-15) | §3.4.4 enumerated roster, **101 entries** (+11 include paths); `tools/recomp_symbol_census.py` landed with the roster embedded (`--check` → 101/101 present, 0 missing over `OLD/build-v2-nog`); row 7 now says 21; §7 3B criterion and `PHASE3_IMPL.md` HANDOFF quote 101 |
| 3 | `test_fastmem.cpp` ported 24 of the original's 27 pools without a record | the three pools (`iop_ram_end` 0x1C1FFFF0, `kseg1_bios_mirror` 0xBFC00000, `high_seg_ram_wrap` 0xE0000000) restored, `static_assert(== 27)`, each pinned by a direct spot check; result: 27 pools × 400 × 5 = 54,000 grid reads agree (§4.1 row updated; `PHASE3_IMPL.md`) |
| 4 | Citation nits: row 14 `:1381` is the dense table, the registry row is `sce_hle_bindings.inc:228`; row 27 "(shards 007/008)" wrong | §3.4 row 14 and row 27 rewritten with the measured shard lists (g_vu0: 14 shards; g_vu1: 009/011/012/029/030) |
| 5 | `EeCpuStateLayout` pins 1600 as a literal; "11 anchors" vs 8 measured | new ctest entry `EeCpuStateLayoutAnchor` (same binary, anchor mode: `ReadStateFile` → THRD walk → every `ctx_size == sizeof(CpuContext)`; SKIP 77 when absent) — PASS on `ps2state_slot0_f36935.ps2st` (2 records, 1600); all **11** anchors re-measured 1600 (`/tmp/p3a/thrd_ctx_size_all.py`: 8 `ps2state_slot0_*` + `artdet_f37152` + 2 `slowspot_*` — the `verify/baseline/` legs minus `coldboot`); §2.1 and §4.1 now say which 11 |

## HANDOFF (stage p3p)

- Done: `docs/phases/PHASE3_PLAN.md` written (§1 scope, §2 module table, §3 recompiler output + seam contract +
  P4FIX-F1 work items, §4 oracle/tests, §5 73 flags, §6 gate/DoD, §7 stages 3A–3F, §8 review notes). Facts measured
  this stage: symbol census of `OLD/build-v2-nog/recompiled*.cpp` (§3.4 table), 6 `getenv` sites in the common TU
  and 0 in shards, OLD shard flags `-O2 -fno-strict-aliasing -fwrapv` (`OLD/CMakeLists.txt:243/324/407`), seam line
  numbers in `recompiled.cpp`, `init_cpu_context` at `main.cpp:529` and the composition order at `:5225-5300`.
- Not done: no code, no regen run, no tests (plan stage). The tree is unchanged apart from this file (ctest state
  as left by p2d/2R).
- Next command (stage 3A): read `OLD/host_app/cpu_state.h` + `memory.{h,cpp}` and write `engine/ee/cpu/cpu_state.h`,
  `core/types.h`, `engine/ee/mem/*` per §2.1–§2.2 with the five 3A tests; then
  `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`.

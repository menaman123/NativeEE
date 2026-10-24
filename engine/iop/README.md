# `engine/iop/`

**Phase:** 4  
**Purpose:** IOP execution environment, OpenGOAL shape (ARCH §7.1): native code under a small cooperative kernel; NEVER CPU emulation.

## Belongs here

- IopKernel, IopCoroutine, IopHostThread, IopClock, SifRpcBridge, IopModuleTable, IopLoader, IopMemory, SysmemHeap, hle/ (stock Sony modules)
- `iop_relocator.{h,inl}` (header-only): the IRX relocation (R_MIPS_32/26/HI16+LO16) both `tools/iop_recompiler --base`
  and `IopLoader::LoadSections(irx, size, base)` apply -- the same code, so the text the tool disassembled is the image
  the kernel hosts; also the `.iopmod` reader and the relocation-table pointer walk the tool's discovery uses
- `iop_library_table.{h,cpp}`: the library registry (name/version -> indexed entries: an HLE body or a guest address);
  `abi/` (`IopAbiSurface` + `abi/host_app/*.h` + `abi/anchor/`): the generic generated-code ABI of a module recompiled
  with `--namespace --import-call` (memory, `import_call`, `iop_resolve`; an unresolved import is handed to the
  composition's hook -- a park -- never a silent 0)
- Several modules per process: `IopModuleTable::OnModuleLoaded` runs each booted module's threads to their first block;
  `SifRpcBridge` keeps one queue per RPC-dispatch thread (`RunOnRpcThread(body, tid)`); `ThbaseModule` takes thread stacks
  from the heap (`SetStackAllocator`) and the creating thread's `$gp`; `SysmemHeap::Layout` sets floor/band/ceiling

## Does not belong here

- STREAM.IRX recomp + lti_ protocol (games/<title>/iop/stream_irx/), the PS2_IOP_KERNEL=0 per-guest-thread std::thread hosting (not transplanted)

## Transplanted from (OLD, read-only)

- `iop_kernel/*, iop_loader.{h,cpp}, iop_memory.h, iop_context.h, iop_sysmem_heap.h`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
- `IopClock` (FRAME policy) keeps TWO counters: the frame base (`frames_`, one per EE boundary) and the microsecond
  time (`virtual_us_`, forward-only). A savestate restore rebases BOTH through `RebaseToFrame(restored_vf)`
  (`savestate/iopm_section.cpp` `ReconcileKernel`): the restored image's vsync frame count is the clock's frame count,
  so the next boundary advances the clock and a hard timer re-armed at the restored now beats (BUG-066). Never rebase
  the microseconds alone.

## Restored hard-timer phase (Config `iop.restored_timer_due`, DP-20 (c); I8.13-A)

Neither savestate format records a hard timer's remaining time (the IOPM section carries the module's descriptor words only),
so the first due of a timer the image records RUNNING is a modelled rule (`TimrmanModule::SetRestoredTimerDue`, bound by the
root from Config; `IopKernel::StartPeriodicTimerAtUs(first_due, period, beat)` carries it):

- `one_period` (default = today's behaviour): `due = now + period` — the latest admissible phase; the first beat lands in
  the first boundary pump (its inclusive ceiling reaches a due 60 µs past the quantum), AFTER the EE's RPC-driven passes of
  boundary 0 (the RPC-woken sweep clears the restored ENDX latch before the first release pass — the W0 class of
  f36935/f39807/slowspot_f40298).
- `at_restore`: `due = now` — a timer mid-period at the save has at most one period left, so the restore is the earliest
  admissible phase; the handler runs in the FIRST `Dispatch` after the restore, before any RPC-woken thread of that pass
  (OLD's observable order: beat → release pass → sweep → KOFF with the full restored ENDX mask).
  Pins: `IopKernelFacingModules` (`TestTimrmanRestoredDue` both rules), `RootAnchorHeartbeatAtRestore` (slowspot_f40298:
  `iop_hb_beats >= frames`, `state_b1` v29 Off). Census: `run.meta iop_restored_timer_due=<rule>`.

## In-frame IOP progress at the EE's poll (`IopModuleTable::PumpWithinFrame`; BUG-070, DP-17 (a))

Under FRAME dispatch the IOP ran only at the boundary (slot 5 advances the frame base and drains to the ceiling; slot 6
after an EE service) and inside the loader's blocking-wait seam. The console's IOP runs on its own clock while the EE
spins in a poll loop, so the title's load-wait lap (`LoaderSpinLift`, `games/…/hle/loader_sync/`) asks for the IOP's work
due inside the CURRENT frame at the poll under `cdvd.delivery_visibility=poll`: `PumpWithinFrame` is the slot-6 drain
(`DrainQuantum` to the inclusive ceiling, NO frame-base advance — `IopModuleTable` pins base/ceiling untouched,
a due inside the frame fires and is reported, one past the ceiling waits for the boundary) and answers whether anything
ran; the root also services a worker's bridged call and delivers the queued RPC end-callbacks there. Nothing due = the
lap yields and the frame ends. One drive: `CdvdmanModule::SetCompletionDue` lets the root schedule a module `sceCdRead`'s
completion at the `DeliveryMeter` drive's end (`engine/cdvd/README.md`) instead of `kCompletionUs`; unbound (boundary
mode) the fixed latency applies. Census: `run.meta iop_pending=events=<queued completions>,rpc=<end-callbacks>,
due_in_frame=<0|1>,next_due_us=,now_us=,ceiling_us=`.

## The round trip's IOP time (`SifRpcBridge::SetServiceUs`, `IopModuleTable::PumpToInstant`, `IopClock::OffsetInFrameUs`; DP-17 attempt 4, HW-1)

A SIF RPC is not free on the console: the send DMA, the module's handler and the reply DMA take the IOP's time (a
title's module round trip measured on the reference console is the composition root's data, see the title's loader_sync). Under FRAME
drive the bridge charges every completed EE→IOP round trip `SetServiceUs(S)` microseconds on the IOP's virtual clock AFTER
the body ran (the reply's instant; a forward move only); `S = 0` (default, boundary mode) charges nothing — the value is
the title's data (`LoaderSpinLift::Layout::stream_rpc_service_us`), bound by the root under `cdvd.delivery_visibility=poll`
only. The IOP's in-frame clock is thereby the EE's instant inside the frame (the one sub-frame quantity the engine
models): `OffsetInFrameUs` reads it, `PumpToInstant` drains the kernel to that instant (what became due while the EE
waited for the replies fires; a due later in the frame waits for the instant to reach it or for the frame's pumps; no
base advance — pinned beside `PumpWithinFrame`), and the lap's lift continues on a reply while the instant lies inside
the slice, lifting when the slice is consumed; a boundary moves the base past the overshoot (never backwards). Census:
`run.meta stream_rpc_service=<S>us,<charged µs>`, `iop_inframe_us=<max>,<mean>` (the offset sampled at every boundary
before slot 5's pump), `stream_rpc_per_frame=<max>,<mean>` (every module RPC of every frame, whichever thread issued it),
`stream_rpc_per_lift_frame=<max>,<mean>,<frames>` and `stream_reply_laps_per_frame=<max>,<mean>` (the composition root's
census over the frames in which the title's load-wait lift decided on a reply: the rule's own measure).

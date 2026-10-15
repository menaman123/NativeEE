# BUG-026 — IOP guest worker nondeterminism (free-running host threads)

**Status**: OPEN (play path) / FIXED under the deterministic profile.
The cooperative IOP kernel landed in the OLD tree 2026-09-24 and is what
the deterministic profile runs (`PS2_IOP_KERNEL=1 PS2_IOP_DISPATCH=frame`:
12/12 legacy legs ADMISSIBLE-STABLE, PHASE0_IMPL §11.5). **The shipped
PLAY default is `PS2_IOP_KERNEL` OFF** (reconciled 2026-09-25, round 1B —
header, `iop_module_table.h` comment and the flag-ledger row now agree with
`Enabled()`): the REALTIME dispatch policy has a boot-phase mutual wait that
stalls cold boots at present ~f60-f91 (coldboot_gate FAIL 2/2 in round 1A,
0/4 completed since; 5/17 completed on 2026-09-24 — pre-existing, not a
round-1A regression, see the fix-item bullet; signature below). NORM to
default ON when that stall is closed.

## Fix item (round 1B, 2026-09-25) — the realtime boot stall signature

One bounded run (`PS2_IOP_KERNEL=1 PS2_IOPSCHED_DIAG=1 PS2_EESCHED_DIAG=1`
+ user env, cold, `--bench-frames 300`, 90 s bound → timeout; last progress
print `[FRAME 60]`, last scanout frame 91; evidence
`/tmp/s126/iopk/{head5400.txt,tail200.txt}`, PHASE0_IMPL §12.2):

* **IOP side — who waits on what:** tid 1 = the STREAM.IRX **RPC-dispatcher
  thread (entry 0x64F4, prio 30)** enters at vf 91 a `DelayThread(20 µs)`
  poll loop at **ra=0x8c94**, re-reading the module words
  `[0xe50]=4 [0xe54]=1 [0xf34]=273 [0xf1c]=0 [0xf5c]=0x1`, which never change:
  64,768,556 identical `delay tid=1 ra=0x8c94 us=20` iterations in ~88 s
  (≈736 k/s — the realtime host thread honours the 20 µs delay as ~1.4 µs, a
  hot spin that also emits ~60 MB/s of diag). tid 2 (CD worker, entry
  0x30D4) made its last `signal_sema 0`/`wait_sema 0` handoffs and
  `sleep 1`/`sleep_woke 1` pairs just before; no CD completion follows.
* **EE side — which loop spins:** last EE scheduling events at vb=116:
  `ready tid=1 prio=10` → `block tid=2 prio=9` → **`run tid=1 prio=10
  own=1`** → `ready tid=-2 prio=-1` (host RPC-callback context) — then NO
  ready/run/block/drain line for the rest of the run while the vblank ticker
  enqueues 5,275 ticks (`vb` 117→5391) that nobody drains. EE tid 1 (main
  thread) holds the run token and spins in the vf~61 boot loader wait (the
  0x2b4738 loader-SM poll, "exposed (b)" below) without reaching a kernel
  scheduling point.
* **Class:** mutual wait across the two hosts — IOP dispatcher poll ⇄ EE
  loader spin. Under the FRAME policy the EE thread dispatches the IOP
  itself at boundaries/park sites, so the poll is serviced in EE order and
  the race cannot form.
* **Regression question — settled 2026-09-26 (s127), NOT a round-1A
  regression:** PHASE0_IMPL §9.7's 2026-09-24 "kernel ON" cold-boot row is
  NOT a kernel-ON measurement (its ON arm ran the legacy hosting:
  `/tmp/s120/neut_coldON.log` has `StartHardTimer … (tick-clocked)` and no
  `[IOPK] kernel up`), so it is no evidence that the realtime boot completed
  before round 1A. The 2026-09-24 binary itself no longer exists (all three
  build dirs hold the 2026-09-25 21:25-21:26 builds), but the IOPK session's
  own realtime boots do: `/tmp/s119/pb*.log` (`policy=realtime`, 900f) —
  5 of 17 reached `[FRAME 900]`, 12 stalled at `[FRAME 60]` after the
  boot-SM edge 0→1 at vf 61, i.e. this signature pre-dates the run token.
  Discriminator on the current binary: kernel ON, realtime, user env,
  **`PS2_EE_RUNTOKEN=0`**, cold 300f → same stall (last `[FRAME 60]`, scanout
  frame 86, edge 0→1 vf 61, killed at the 100 s bound; `/tmp/s127/kern_rt/
  tokoff.log`). Odds may have shifted (09-24: 5/17 completed; since round
  1A: 0/4 incl. this run) — record the completion rate with the fix.
* **Fix direction (Phase 1):** make the realtime dispatcher's delay-poll
  yield to the EE completion path (or give the EE loader wait a kernel
  scheduling point that drains the queued events), then gate with
  `python3 tools/coldboot_gate.py build/host_app/host_app --iop-kernel on`
  (flag added in round 1B; `PS2_IOP_KERNEL=1` as an ENV=VAL override is
  equivalent). Related Phase-1 item: `ChangeThreadPriority` is a documented
  no-op in both hosting modes (`iop_stubs.h`); routing it to
  `IopKernel::ChangePriority` changes dispatch order under the profile and
  must land with a battery re-freeze.

## Symptom

Phase-0 admissibility self-test: **0/24 legs ADMISSIBLE** — every paired
profiled leg (same binary, same anchor, same inputs) diverged within 1–3
presents; audio window 0 and the first memory checkpoint diverged alongside
(PHASE0_IMPL.md §4/§5.4). Cold boots carried a persistent 1-frame phase
shift from vf≈69.

## Root cause

STREAM.IRX's guest threads (RPC dispatcher entry 0x64f4, CD worker entry
0x30d4) were hosted as free-running per-thread `std::thread`s synchronized
by condvars under a "guest monitor" mutex. Guest-visible progress (worker
passes, sema handoffs, heartbeat beats, deferred CD/SPU completions) was
host-scheduler-paced: the same anchor replayed twice interleaved the worker
against the EE differently. `PS2_IOPSCHED_DIAG=1` paired-run evidence
(f46203, 300 vf ×2, legacy hosting): the CD-worker sema pass
(`signal_sema 0` → `wait_sema 0` by tid 2) lands at vf 46203 in rep 1 vs
vf 46204 in rep 2; frames fork at vf 46205 with audio window 0.

## Fix (the OpenGOAL shape — REFERENCE_ARCHITECTURES §2.5)

The IOP is native code under a small cooperative kernel; never emulated:

- `host_app/iop_kernel/iop_kernel.*` — cooperative scheduler (thread table,
  priorities, Dormant/Ready/Run/Wait, wait kinds Sleep/Delay/Sema/EventFlag/
  Mbx/Rpc, banked wakeups, `Dispatch()` to idle + next-deadline report,
  injection queue for cross-host wakeups, deferred events + periodic timers
  with the counted-queue "never the issuing pass" contract).
- `iop_coroutine.*` — the platform fiber backend (ucontext/Win32) shared
  with the EE scheduler.
- `iop_host_thread.*` — ONE host thread (realtime policy): dispatch, then
  condvar-sleep until an EE wakeup or the next deadline.
- `iop_clock.h` — the only kernel time source (realtime = steady_clock;
  frame = EE-boundary-derived virtual time + discrete-event advances).
- `sif_rpc_bridge.*` — RPC = memcpy + wakeup both directions; EE→IOP
  commands run on the module's own RPC coroutine (replacing the S43
  tid-impersonation bracket at the root); IOP→EE marshals park Wait::Rpc.
- `iop_module_table.*` — boot sequencing (module init to first block before
  the EE starts), FRAME pumps at both frame-boundary sites + syscall edge +
  loader poll-spin.

## Verification

- 22 IopKernel unit checks green (`tests/test_iop_kernel.cpp`), incl. the
  determinism property (two FRAME-clock scripted-workload runs → identical
  effect sequences).
- Paired profiled legs (f46203, 300 vf ×2): the `[IOPSCHED]` stream is
  bit-identical including virtual-clock stamps; with the two follow-on pins
  this exposure forced (below), **frame and audio lanes 300/300 + 50/50
  identical** — previously divergent at the first content present.

## Exposed downstream (peeled by this fix; separate families)

1. `PS2_RTC_FIXED` — sceScfGetLocalTimefromRTC wrote the host wall RTC into
   EE RAM (a per-run seed). Pinned constant under the profile.
2. `PS2_TICK_DETERMINISTIC` — the freerun tick generator's 16.67 ms
   timeout-floor tick source fired a wall-varying number of ticks per EE
   frame (~6.4 ±1 on the debug host), feeding the game's T0 master clock
   (BUG-014 family). Pinned under the profile: ticks fire only on EE
   progress (consumption / vsync retire / cdvd delivery).
3. RESIDUAL (root-caused in round 2, 2026-09-24 later session): the memory
   lane's first-checkpoint float — THRD @0x9E8 = tid 1's saved
   `fpuRegs.fpr[f11]` (main thread parked in sceGsSyncV, pc 0x1A018C) —
   is the game's per-frame delta-seconds: `FUN_001b74a0` (from the main
   loop `FUN_00100d78`) computes it from the T0-reconstructed master
   clock, stores it at `0x3098C4`, and `FUN_001b6b40` accumulates it into
   per-object timers (`0x3723AC` family; LSB echoes ride transforms into
   VU0D/SPAD/VRAM). Found with the `[DETW]` write watch (PS2_DETWATCH,
   memory.cpp) naming the writer FUN via the EE dispatch call stack.
   Wall inputs found and pinned:
   - `PS2_T0_TICKPACED` — the LIVE legacy T0 read path (memory.cpp
     0x10000000) interpolated COUNT from RAW WALL ns since the last SyncV;
     `PS2_T0_SUBTICK=0` never covered it (that gate exists only in the
     default-OFF PS2_EE_TIM0 path — a nine-clock inventory coverage gap).
   - `PS2_MAINT_EEPACED` — the CPU maintenance loop ran three
     guest-visible duties at ~1 kHz WALL: SIF RPC end-callbacks,
     deferred-ISO delivery (per-pass readiness decrement + cdvd-fired
     ticks: the tick-binning noise AND the boot loader-spin exit race — the
     mechanism behind (b): the 0x2b4738 loader-SM hook pumps on
     spin-iteration cadences whose count depends on when the wall-timed
     delivery lands), and suspended-DMA resume. Under the pin they run at
     EE boundaries + the park-loop pump sites; the pad slot stamp
     (`WriteSlotPhaseBytes` ← `PadInput_Update` on the SDL main thread —
     slot seq 386 vs 397 by checkpoint 0) moves to the boundary too.
   Still open after both pins (named, Phase 1 runtime fix items — see
   PROFILE.md "Round 2"): the EE↔gfx drain-depth race (MAX_FRAME_LAG=1
   park on the WALL-paced graphics thread; `PS2_FRAME_LOCKSTEP` hatch
   sketched but wedges on unmatched present-skip pops), the free-running
   guest EE threads (no run token — the EE dual of this kernel), and the
   ticker-thread tick-body asynchrony. Battery verdicts stay NONDET with
   deepened first divergences until those land.

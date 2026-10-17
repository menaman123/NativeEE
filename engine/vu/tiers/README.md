# engine/vu/tiers — the VU execution tiers

**Purpose:** the ways a VU microprogram is executed over a `VuUnit`: the interpreter tier (the instrumented
reference, one bundle per VU cycle) and, when they land, the cached tier (decoded/superblock programs), the
tier dispatch (`VuTierDispatch`, mode + HLE-library consult) and the MTVU worker.

## Belongs here
- `vu_interpreter_tier.h/.cpp` + `vu_interpreter_tier_upper.cpp` + `vu_interpreter_tier_lower.cpp` — `VuInterpreterTier`
  (ONE class; the upper/lower instruction bodies are TU-private halves). Rules and kept divergences are in the header
  comment and in `engine/vu/README.md` "Interpreter notes".
- `vu_cached_tier.h/.cpp` + `vu_cached_tier_exec.cpp` — `VuCachedTier` (ONE class; the fast instruction bodies are a
  TU-private half: `ExecUpperFast` dispatching to `ExecUpperFmac` / `ExecUpperAcc` over the `VuFloat` lane kernels,
  `ExecLowerFast`; NOP words are short-circuited in `ExecBundle`) with the `VuCachedProgram` POD (the table entry)
  declared in its header.
- `vu_block_tier.h/.cpp` — `VuBlockTier` (ONE class; the instruction bodies are the cached tier's resolved
  kind -> body tables `VuCachedTier::UpperFastFn / LowerFastFn`) with the `VuBlockOp` / `VuBlock` / `VuBlockProgram`
  PODs declared in its header: per image (hash key, epoch fast path, 64-entry table) a lazily built table of
  straight-line blocks keyed by entry address -- ops pre-resolved (upper / lower body pointer, stall-model entry per
  static class, stall kind None / Push / Model, hazard flags), the delay slot merged into its branch / E-bit block,
  successors chained (`fall` / `jump` with static branch targets), and the STATIC STALL SCHEDULE (`Schedule` /
  `StaticStall`: the FMAC read-after-write recurrence over the three previous writers) applied by addition once the
  model has agreed on two consecutive ops at or past the fourth (`kWarmupOps 3`, `kMatchStreak 2`, `kMinStaticOps 3`),
  the FMAC ring rebuilt from recorded issue cycles (`StallModel::MaterializeFmacIssued`) on leaving static mode; a
  post-issue clock advance (second XGKICK, WAITQ/WAITP) re-arms a 3-op model guard. VU1 only (VU0 and a misaligned
  entry run elsewhere). Flag bookkeeping is never skipped (STATUS sticky bits are observable across programs).
- `vu_tier_dispatch.h/.cpp` — `VuTierDispatch` (the Execute ladder) with `VuSettings` / `VuTierMode {Cached, Interp,
  Block, Aot}` and the two hook slots; `Block` / `Aot` run VU1 on that tier and VU0 on the cached tier. `Aot` is the
  generic run-time recompiler of `engine/vu/vu1_aot/` (owned here as the `aot_` member: a verified image on its
  (kind, dest)-specialised block table, every other image on the AOT tier's fallback).
- `vu_block_tier` also carries the AOT tier's hook: `VuBlockProgram::resolve` (a `VuBlockOpResolver`, null = `ResolveOp`),
  `SetResolver(prog, fn)` (re-resolves the built blocks, recomputes the schedule) and `ExecuteAcquired(unit, pc, prog)`.
- `vu_cached_tier_exec.cpp`'s body templates take `D` (the dest mask, -1 = dynamic): `UpperFastFnD / LowerFastFnD` are
  the (kind, dest) tables the AOT resolver installs; the dynamic tables are the `D = -1` instantiations (byte-identical).
- `mtvu_settings.h` — `MtvuSettings {enabled=true, depth_frames=2}` (types-only; mirrors the Config keys `engine.mtvu` /
  `engine.mtvu_depth`, copied by the composition root; deterministic profile `{false, 2}`).
- `mtvu_clock.h/.cpp` — `MtvuClock` (virtual `NowNs()`, default = the process steady clock via `ps2r::Park::NowNs`;
  `Steady()` shared instance; tests derive a fake).
- `runahead_guard.h/.cpp` — `RunaheadGuard(int depth_frames, std::function<bool()> low_band, const MtvuClock&)`:
  `EffMaxFrames` (setting; exactly 2 lifts to 3 while `low_band()`), `DepthFrames(declared, presented)` clamp,
  `Over()`, `FramePark(declared, presented, engaged_at_ns)` (park while over; the 100 ms `kFrameValveNs` valve
  releases a frozen depth), nested `Tripwire {kPersistPresents=30, kMaxReports=5}` (`over = depth > cap+1`, one
  report per episode, recovery re-arms) + `RunaheadFramePark` POD.
- `mtvu_pipeline.h` — `MtvuPipeline` abstract: what the backpressure reads/waits on (`Running / MayPark /
  PipelineBytes / WorkerIdle / SealCount / EagerDrain / WaitIdleOrSeal / WaitFrameProgress / Enter-LeaveSyncWait`);
  the worker implements it.
- `mtvu_backpressure.h/.cpp` — `MtvuBackpressure(RunaheadGuard&, const MtvuClock&)`: `kPipelineHighBytes = 256 MiB`,
  `kLowWaterBytes = high - high/8`, `kPollNs = 500 µs`, the 2-stall idle-worker valve, frame arm through the guard,
  `Engage()` (producer thread; eager drain per iteration; shutdown flag releases), `SetFrameCounters(declared,
  presented)`, `SetShutdownFlag`, spin-tier constants `kSpinBudgetNs = 40000` (clamp [1000, 1000000]), `kRecentNs =
  200000`, `SpinArmed`, `CpuRelax` (= `std::this_thread::yield`, identical to the macOS platform relax).
- `mtvu_vif_feed.h` — `MtvuVifFeed` abstract: what the worker drives on VIF1 (`ProcessVif1 / SetVif1TteFeed /
  ResetVif1Latch / BindVu1`); `Vif` implements it, so `engine/vu` never includes `engine/vif`.
- `mtvu_worker.h/.cpp` — `MtvuWorker(MtvuVifFeed&, VuUnit& worker_vu1, GifArbiter&, MtvuBackpressure&, MtvuSettings)`
  : `GifMarkerOwner` + `MtvuPipeline`. `Start` (32 MiB pthread stack, `on_start(user)` for the host's pin/name,
  `BindVu1(worker)`, `SetMarkerOwner`, `backpressure.Bind`) / `Stop` (drains, joins, unbinds); producer edge
  `SubmitVif1 / SubmitVif1Staged / FlushVif1Staged / SubmitResetLatch` (+ the `Staged*Thunk` trio for
  `ChainWalker::SetVif1StagedFeed`), Batch records `[u32 len][u8 tte][pad3]`, `kBatchFlushBytes = 32 KiB`,
  `kBatchFlushRecords = 32`, one arbiter marker per segment (opened at submit, sealed after replay), `inflight_`
  acquire/release publication, `seal_count_` seq_cst wake hint, batched pop + the spin tier from the backpressure
  constants, `WaitIdle / WaitIdleOrSeal / TryDrainGuarded` (the one-drain-at-a-time exchange + thread-local guard).
- `mtvu_sync.h/.cpp` — `SyncSite` enum (OLD id order) + `MtvuSync(MtvuWorker&, GifArbiter&)`: `Sync(site)` (idle
  fast path; else the eager loop — guarded drain, `WaitIdleOrSeal(500 µs, seen_seal)` — then `DrainFence` when the
  drain thread runs), `NoteSiteSkipped`, `TryVif1StatBusyView` (`0x03 | 1<<2 | 16<<24` = VPS 3 | VEW | FQC 16 while in
  flight), `TryEagerDrain / TryParkDrain` on `hasSealedReady`, `DeferOn ≡ worker.IsRunning()`, per-site counters,
  and the dispatcher step-2 hook `Redirect` + `InstallInto(VuTierDispatch&)` / `UninstallFrom` (an EE kick on the
  INLINE unit syncs `Vu1EeKick` and runs on the worker's unit; a kick on the worker unit or a stopped worker falls
  through to the tier).

## Does not belong here
- The decode, the flag pipeline, the stall model, XGKICK, the float rules (`engine/vu/` core — the tiers only consume them).
- The VU1 HLE library (title data under `games/<title>/vu1_library/`) and the HLE mechanism (`engine/vu/hle/`).
- Trace / probe sinks (`PS2_VUTRACE` family is DIAG; no sink is wired this phase).

## Transplanted from (OLD, read-only)
`host_app/vu.cpp:5203-5210, :5300-5301, :5737-5760, :5990-7910, :8095-8103, :8104-11314` (interpreter `Execute`,
`RunUpper`, `RunSpecial2Upper`, `RunLower`); the probe forest `:4334-4999, :5027-5195` and the in-loop taps were not written.
Cached tier: `host_app/vu.h:218-252`, `host_app/vu.cpp:2228-2695, :2696-3928`, `host_app/vu_fast_exec.h:111-117, :663-1806`
(RUNFLAT / SPANFOLD2 / AOT arms and the census counters not written). Dispatch: `host_app/vu.cpp:364-407, :4318-4332,
:4808-4816, :5000-5008, :8095-8099`.
MTVU (5G part 1): `host_app/runahead_guard.h:60-75, :104-131, :134-177`; `host_app/vu1_async.cpp:186-194, :247-258,
:608-650, :861-989` (`BackpressureIfNeeded`, spin constants; exec-thread arm, `perf::`/`ringlog::` census, W16 hooks
not written).

## Flags applied (5G part 1)
`PS2_MTVU` → `MtvuSettings::enabled` · `PS2_MTVU_MAX_FRAMES` → `MtvuSettings::depth_frames` (SETTING mirror; the
`RunaheadGuard` takes the int) · `PS2_MTVU_DYNDEPTH` NORM (lift 2→3 always considered) · `PS2_RUNAHEAD_ALARM` NORM
(`Tripwire` mechanism; its consumer is the Phase-6 graphics thread) · `PS2_MTVU_MAX_MB` → `kPipelineHighBytes` ·
`PS2_MTVU_SPIN` NORM (always armed) · `PS2_MTVU_SPIN_NS` → `kSpinBudgetNs` · `PS2_VU1_EXEC_*`, `PS2_MTVU_PATHSYNC_RELAX`,
`PS2_PERF` announce lines — not written.

# `tests/vu/`

**Phase:** 5  
**Purpose:** ctest suites for `engine/vu` (`ps2r_vu`): decode / float / flag-pipeline / stall-model /
XGKICK / unit-surface property tests, the ported OLD VU0 and VU1 ground-truth tests over
`fixtures/`, the tier differential suites, the VU1 HLE mechanism tests, and (5G) the MTVU
runahead / backpressure tests. Every row is described next to its `ps2r_vu_test(...)` line in
`CMakeLists.txt`; corpus rows read the frozen OLD tree (`PS2R_OG_ROOT`) and SKIP 77 without it.

## Belongs here

- One `test_<subject>.cpp` per ctest row; fixtures under `fixtures/` (raw binaries lifted from the
  OLD `.inc` arrays); `check_vu1_library_purity.py` (the title-data purity gate).
- `test_runahead_guard.cpp` → ctest `VuRunaheadGuard` — OLD `tests/test_runahead_guard.cpp`'s asserts
  (depth clamp, `over = depth > cap+1`, 30-present window, one report per episode, recovery re-arm,
  5-report cap, the cap-0 ratchet) + `EffMaxFrames` 2→3 under the injected low-band predicate only,
  `Over()`, and the 100 ms frame valve on a fake `MtvuClock`.
- `test_mtvu_backpressure.cpp` → ctest `VuMtvuBackpressure` — the producer park loop over a fake
  `MtvuPipeline` and fake clock: fast path, byte high water + 7/8 hysteresis, the idle-worker stall
  valve, frame park released by present progress, the 100 ms valve on frozen depth, depth 0 never
  parks, lifted cap, both arms together, shutdown flag, eager drain per iteration, sync-waiter
  balance, spin clamp / recency constants. The deterministic twin of the wall-clock `vif_replay
  --runahead` arm.
- `test_mtvu_worker.cpp` → ctest `VuMtvuWorker` (RUN_SERIAL; links `ps2r_vif`) — the worker over the real
  `Vif` + two `VuUnit(1)` + `GifArbiter`: one VIF1 chain (MPG / UNPACK / MSCAL+XGKICK / DIRECT) fed inline,
  per span, staged (one Batch per walk) and through the `Staged*Thunk`s gives identical VU1 data + micro
  memory, registers and `RegisterOpStream` hash; the TTE bracket; the reset-latch segment; the Batch
  record layout + 32-record / 32 KiB flush thresholds; drops on a stopped worker; Stop drains and joins.
- `test_mtvu_sync.cpp` → ctest `VuMtvuSync` (RUN_SERIAL; links `ps2r_vif`) — a gated fake `MtvuVifFeed`
  holds the worker mid-span so `Sync` takes the loop path deterministically (its eager drain on the EE
  thread releases the gate); the VIF1_STAT busy view only while in flight; Sync + `TryEagerDrain` gives
  the inline `RegisterOpStream`; `InstallInto` redirects `VuUnit::Execute` on the inline unit to
  `Sync(Vu1EeKick)` + the worker unit (identical to inline, the inline unit untouched); site counters;
  `DeferOn`. Fake `MtvuClock`, predicate waits only, no wall-clock assert.
- ctest `Vu1CollectSweep` (5H, gate G5; no test TU — runs `tools/vu1_replay/sweep_vs_old.sh`) — OLD vs
  NEW `--compare` over the whole 3,293-case `vutrace_collect` corpus in 200-case bounded chunks under
  `build/tests/vu/sweep_vs_old/`, then `aggregate_sweep.py --expect 3293`. LABEL `long`, TIMEOUT
  18000 s, RUN_SERIAL, SKIP 77 without the OLD tool / corpus. It is registered **DISABLED** by default
  (about two hours; `CMakePresets.json` carries no label filter and is outside this directory's remit),
  so the default battery reports it as "Not Run (Disabled)" and stays green. Run it by hand with
  `cmake --preset macos-dev -DPS2R_VU1_SWEEP=ON && ctest --preset macos-dev -L long` (then reconfigure
  with `-DPS2R_VU1_SWEEP=OFF`), or directly:
  `tools/vu1_replay/sweep_vs_old.sh /tmp/sweep 200 --expect 3293` (resumable — re-run the same command
  to finish an interrupted sweep; `aggregate_sweep.py --logs /tmp/sweep/logs --expect 3293` re-prints the
  `SWEEP` line).

## Does not belong here

- harness legs (no lane is claimed in Phase 5); the `vu1_replay` / `vif_replay` tool arms (their ctest
  rows live in `tests/vif/CMakeLists.txt` and the tools' own directories); wall-clock assertions.

## OLD sources (read-only)

- `tests/test_runahead_guard.cpp`, `tests/test_vu_superblock.cpp`, `tests/test_lazyflags_algebra.cpp`,
  `tests/test_vu0_shadow_replay.cpp`, `tests/test_vu0_vis_micro.cpp`, `tests/test_vu0_decal_eval.cpp`,
  `tests/test_vu1_transform_gt.cpp`, `tests/test_gs_packet.cpp`.

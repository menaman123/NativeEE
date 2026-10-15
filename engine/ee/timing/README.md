# `engine/ee/timing/`

**Phase:** 3  
**Purpose:** The TimeDomain family (ARCH §4.7/§7.2): TickSource → EmuDomain → DerivedClocks; tick sync, frame rendezvous, the ONE boundary sequencer; turbo/warp governors; budget clock mechanism.

## Belongs here

- EmuDomain, RegimeDetector, TickSource, TickSync, FrameRendezvous, BoundarySequencer, BoundaryProtocol, TurboGovernor, Warp, BudgetClock, PhaseClassifier

## Landed (3D1)

- `tick_source`, `emu_domain`, `regime_detector`, `derived_clocks` (the nine clocks; table in the header and in
  `docs/phases/PHASE3_IMPL.md` §3D1), `budget_clock`, `phase_classifier`, `turbo_governor`, `warp`.
- Invariant: no wall-clock value enters this directory's state. `TickSource` publishes a COUNT and a PERIOD policy;
  the play pacer decides WHEN to call `Tick()`; `EmuDomain` is a pure function of (raw ticks, arrivals, boundaries,
  re-anchors); every `DerivedClocks` accessor is a pure function of `EmuDomain` (pinned by `DerivedClocksPurity`).
- 3D2 adds `tick_sync`, `frame_rendezvous`, `boundary_sequencer`, `boundary_protocol`.

## The Play policy's wall lock (DP-13 (a)+(b), play mode only)

- The host pacer (`app/host/tick_thread`) sleeps to an absolute deadline every field period and only NOTES a tick
  (`TickSync::NotePending`) and wakes the waiters (`BoundaryProtocol::NotifyTick`); it never runs a tick body.
- The composition root binds the pacer's protocol to the sequencer iff it started a pacer (`BoundarySequencer::
  BindPacer`). Bound, under `TickSource::Policy::Play` and no bypass, the TickAdvance slot fires NO Boundary tick of
  its own -- THE CLOCK CONTRACT (DT-14, play mode only):
  * rule 1 -- the boundary fires at the pacer's 60 Hz wall edge, never before it: a boundary with nothing noted
    parks (`BoundaryProtocol::ParkForPacerTick`, counted `pace_parks`, its sleep summed as `pace_slept_ms`) until
    the pacer's next noted tick; the park is bounded by `kBoundaryParkPeriods` (2) field periods -- a stopped pacer
    is counted (`pace_timeouts`) and the slot falls back to one Boundary tick so the field still happens. Nothing
    is ever burst-repaid: after a late frame the next boundary parks for the next edge again.
  * rule 3 -- every noted tick is one elapsed wall field and is fired 1:1 (`TickSync::FirePending`, cause Deferred,
    `EmuDomain::OnDeferredTick`): the first is the boundary's field; the further ticks a LATE boundary finds noted
    (frame work > period: no park, counted `pace_late`; the extra fields counted `pace_late_fields`) are the fields
    that elapsed during the work, delivered to the title's T0 so its per-frame delta grows (TR
    `src/game/clock.cpp` GameTimeSetStamp / `src/platform/ps2/time.cpp` Platform::Time::Ticks: the title's frame
    time is the T0 difference between two boundaries) -- the simulation keeps wall speed with a larger step, never
    slow motion. `EmuDomain::OnBoundary` does NO quantum top-up under the pacer release (no regime estimate may
    add a field the wall did not produce).
- With the pacer bound, `EmuDomain::SetPacerRelease(true)`: a Deferred tick rides 1:1 (`OnDeferredTick`), never
  absorbed by the quantum -- a pace park of N ticks costs the game N fields, a late frame of N fields costs N fields.
  `OnRawTick` (Boundary / Demand / CdvdDelivery causes) is unchanged.
- Deterministic / headless Freerun: nothing is bound; `OnDeferredTick` is exactly `OnRawTick`; the slot is the
  3D2 order byte for byte (pins: `PacerReleaseDomain`, `PacerReleaseSequencer`, root `RootPlayPolicyInert`).
- Census (run.meta): `pace_parks=`, `pace_late=`, `pace_timeouts=`, `pace_late_fields=`, `pace_slept_ms=`, `absorbed=`,
  `game_hz=` / `field_hz=` (per 1 s wall window series); the root play pins `RootPlayWallLock`, `RootClockContract`.

## Does not belong here

- budget bases/thresholds (TitleProfile.quirks.budget), present pacing (gs/hle/present)

## Transplanted from (OLD, read-only)

- `time_domain.h, ps2_vblank.{h,cpp}, ee_timing/{tick_sync,frame_rendezvous,boundary_sequencer}.*, quantum.h, ee_pace.h, turbo.h, warp.h, budget_math.h + main.cpp:3226-3608 + timer_hle.{h,cpp}, timeline.h WAITFIX arm`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

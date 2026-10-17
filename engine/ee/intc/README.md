# `engine/ee/intc/`

**Phase:** 3  
**Purpose:** INTC (15 causes) + EE timers T0–T3 with clock reads through ee/timing DerivedClocks.

## Belongs here

- Intc (globals folded into the class), EeTimers, TimerCodec

## Does not belong here

- vblank tick bodies (ee/timing)

## Transplanted from (OLD, read-only)

- `intc.{h,cpp}, ee_timers.{h,cpp}`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

## Dispatch points and the refusal census

The EE kernel takes an interrupt at the exception vector and runs the registered handler chain
with interrupts disabled (EXL): INTC handlers never nest. A cause raised while a handler runs sets
its STAT bit and is serviced after the handler returns. Here the same contract holds through
`Intc::InDispatch()`: `Dispatch` / `DispatchPending` refuse while a handler runs, the STAT bit
stays set, and the cause is taken at the NEXT dispatch point the running thread reaches. The
dispatch points are the `Intc::Site`s: `boundary` (BoundarySequencer's TickAdvance),
`yield` (EeYieldPoint's back-edge trip), `syscall` (a composition's syscall-entry delivery),
`timer` (EeTimers' pump), `other`.

`NestedRefusals()` counts refused ATTEMPTS, not lost interrupts. The cause in dispatch keeps its
own STAT bit until the ack at its exit, so a kernel call made from inside a handler (the common
`iSignalSema`) reaches the syscall-entry dispatch point with that cause still "pending" and is
refused -- one refusal per handler run whose body makes one kernel call. For a title whose GS
FINISH handler signals a semaphore, `intc_nested_refusals == gs_irq raises` exactly.

Two census strings separate the cases:

- `RefusalCensus()` -- `site/<cause in dispatch>><cause refused>=n`, descending: only OTHER
  causes that landed during a handler (a cause's own still-set bit is the handler in progress,
  not a refused edge). These are the console's "pending until the handler returns".
- `EdgeCensus()` -- `<cause>:raised/dispatched/guest-acked/still-pending` per cause with any
  edge (a re-raise of a set bit is not an edge). Nothing else clears a STAT bit, so a LOST edge
  would show as `raised > dispatched + acked + pending`. Pinned by `tests/ee/test_intc_census.cpp`.

Measured on Sly 2 (SCUS_973.16, 1200 headless frames): `intc_nested_refusals=3301`,
`intc_refused=syscall/GS>VBON=36,syscall/GS>TIM2=36`,
`intc_edges=GS:3301/3301/0/0,VBON:1822/1822/0/0,VBOF:1/0/0/1,TIM2:1822/1822/0/0` -- 3229 of the
3301 are the GS handler's own `iSignalSema` entry seeing GS still set; 72 are VBON / TIM2 landing
during the GS handler, all serviced at the next point; VBOF is one masked (never enabled) edge
left set in STAT, as on the console. No lost edge.

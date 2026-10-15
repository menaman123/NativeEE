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

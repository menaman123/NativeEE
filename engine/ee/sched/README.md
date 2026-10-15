# `engine/ee/sched/`

**Phase:** 3  
**Purpose:** EE run token (thbase election, relaunch handoff) and the holder-only-drain event queue (BUG-035).

## Belongs here

- EeRunToken, EeEventQueue

## Does not belong here

- guest thread tables (ee/hle/ThreadManager)

## Transplanted from (OLD, read-only)

- `ee_sched/{ee_run_token,ee_event_queue}.{h,cpp}`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

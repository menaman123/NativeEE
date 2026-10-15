# `engine/tools/diagnostics/`

**Phase:** 9  
**Purpose:** Offline dump/trace/log analysis scripts (GS-dump family, log/trace tools, ring_decode, flicker/).

## Belongs here

- 21 kept scripts

## Does not belong here

- concluded one-off sessions (35 — never)

## Transplanted from (OLD, read-only)

- `tools/ group 4.3`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

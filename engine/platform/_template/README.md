# `engine/platform/_template/`

**Phase:** 1 (README), 9.4 (stub)  
**Purpose:** The copy-me skeleton for a new target: every Platform entry point stubbed with unimplemented() + a checklist README + CI stub-build wiring.

## Belongs here

- stubbed Platform impl + CHECKLIST

## Does not belong here

- real code

## Transplanted from (OLD, read-only)

- `— (new)`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

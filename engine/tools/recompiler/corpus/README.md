# `engine/tools/recompiler/corpus/`

**Phase:** 9.1  
**Purpose:** Corpus-as-audit: observed jr/jalr pairs vs static tables; recorder generator (TEST channel PS2_RECOMP_CORPUS).

## Belongs here

- Audit, RecorderGen

## Does not belong here

- —

## Transplanted from (OLD, read-only)

- `recompiler_tool/corpus/*`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

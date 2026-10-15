# `engine/tools/recompiler/config/`

**Phase:** 9.1  
**Purpose:** Per-game metadata that remains data-not-code: corpus pair files.

## Belongs here

- corpus pair fixtures (copied from OLD recompiler_tool/corpus)

## Does not belong here

- overrides/pins

## Transplanted from (OLD, read-only)

- `recompiler_tool/corpus pair files`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

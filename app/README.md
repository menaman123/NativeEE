# `app/`

**Phase:** 1 (host), 8 (gui)  
**Purpose:** Composition roots. The ONLY place a title module is linked to the engine.

## Belongs here

- host/, gui/

## Does not belong here

- engine logic

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

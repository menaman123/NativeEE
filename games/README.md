# `games/`

**Phase:** 3+  
**Purpose:** Title modules. One directory per title; engine/ never references this tree.

## Belongs here

- games/<serial>_<slug>/

## Does not belong here

- engine code

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

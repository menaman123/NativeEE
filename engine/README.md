# `engine/`

**Phase:** 1-9  
**Purpose:** Title-neutral PS2 semantics + host services. Links with ZERO references into games/ (tests/lint/lint_engine_purity.py).

## Belongs here

- generic PS2 subsystems (each with its own state model and hle/), core services, render contract + backends, platform folders, build-time tools

## Does not belong here

- game addresses, VRAM base constants, VU1 program stamps, game strings, anything keyed on SLUS-12345 (that is games/)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

# `engine/core/`

**Phase:** 1  
**Purpose:** Platform-independent primitives: no SDL, no GL, no getenv (config loader excepted).

## Belongs here

- config schema/loader, compile-time-category logger + ring, diagnostics registry (PS2R_DIAGNOSTICS), PS2SAVE1 savestate envelope, thread primitives, hashes, harness emitters, triage capture

## Does not belong here

- PS2 device semantics, rendering, OS surface emulation

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

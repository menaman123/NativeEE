# `engine/tools/triage/`

**Phase:** 9.2  
**Purpose:** Bundle-consumer CLI: triage list | repro | lift | perf (TRIAGE_CAPTURE §10). Bundle paths are arguments; zero game tokens.

## Belongs here

- triage CLI

## Does not belong here

- —

## Transplanted from (OLD, read-only)

- `— (new; absorbs bug_report reader idioms)`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

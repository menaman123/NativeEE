# `docs/`

**Phase:** 0+  
**Purpose:** Design (ARCHITECTURE, CONSTRUCTION_PLAN, EQUIVALENCE_PLAN, HOOK_RECLASSIFICATION, TRIAGE_CAPTURE, superseded MIGRATION_PLAN), audits, file map + ledgers, phase plans/records, bug knowledge base, the freeze contract.

## Belongs here

- design/, audit/, filemap/, phases/, bugs/, REFERENCE_FREEZE.md

## Does not belong here

- code

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

# `engine/tools/`

**Phase:** 9.1 (recompiler), 4/9 (iop_recompiler), 9 (rest)  
**Purpose:** Build-time programs; NEVER linked into the engine runtime libraries. Until Phase 9.1 the FROZEN OLD tools generate the title outputs (CONSTRUCTION_PLAN §A.1).

## Belongs here

- recompiler/, iop_recompiler/, sce_lib_finder/, ee_analyze/, diagnostics/, triage/

## Does not belong here

- runtime code, game inputs (TitleProfile.recomp data)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

# `engine/tools/recompiler/ir/`

**Phase:** 9.1  
**Purpose:** IR node defs, MIPS→IR lifter, IR verifier (ARCH §5).

## Belongs here

- IrNode, MipsLifter, IrVerifier

## Does not belong here

- —

## Transplanted from (OLD, read-only)

- `recompiler_tool/recompiler.cpp translate_instruction switch → lifter tables`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

# `engine/tools/ee_analyze/`

**Phase:** 9.1  
**Purpose:** MIPS static-analysis library (rabbitizer-backed) + standalone disasm driver — the 6 survivors.

## Belongs here

- analyze.{h,cpp}, function.h, register_state.h, data_flow_engine.h (type dep), main.cpp

## Does not belong here

- the 4 dead TUs (never)

## Transplanted from (OLD, read-only)

- `EEAnalyze/{analyze.h,analyze.cpp,Function.h,register_state.h,DataFlowEngine.h,main.cpp}`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

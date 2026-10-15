# `engine/tools/recompiler/`

**Phase:** 9.1  
**Purpose:** THE recompiler engine (v2 static discovery): loader, discovery, IR, emitter, corpus audit. Gate 1 = byte-identical regeneration of games/<title>/recomp/ before any IR change.

## Belongs here

- config/, loader/, discovery/, ir/, emit/, corpus/; MipsLifter, CppEmitter, ShardWriter, DispatchTables, SnippetEmitter

## Does not belong here

- legacy CLI, Ghidra JSON input, overrides/pins, manual bodies, per-address emission cases (D1 — never)

## Transplanted from (OLD, read-only)

- `recompiler_tool/{main_v2.cpp, discovery/*, elf/*, emit/*, translate/control_flow_v2.cpp, corpus/*, recompiler.cpp general path}`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

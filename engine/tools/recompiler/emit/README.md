# `engine/tools/recompiler/emit/`

**Phase:** 9.1  
**Purpose:** IR→C++ emitter, shard writer, dispatch/entry tables, snippet emitter, GenProbe manifest channel (diagnostics builds).

## Belongs here

- CppEmitter, ShardWriter, DispatchTables, SnippetEmitter, EmissionModelBuilder, emission_config.h, IndirectDispatch

## Does not belong here

- env-gated generator injections (PS2_SHADOW_DIAG/PS2_EE_RAWDISP)

## Transplanted from (OLD, read-only)

- `recompiler_tool/recompiler.cpp write_cpp_tail, emit/driver_v2.*, emit/v2_emission.h, translate/control_flow_v2.cpp`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

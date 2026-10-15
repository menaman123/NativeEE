# `engine/sound/hle/`

**Phase:** 4 (stage 4E″ iteration 2: `SdBlockTrans`, `SdrRemote`, `LibsdrBindings`; the FMV real mode is Phase 7)  
**Purpose:** libsdr surface: SdrRemote, SdBlockTrans, libsdr bindings.

## Belongs here

- SdrRemote, SdBlockTrans, libsdr_bindings

## Does not belong here

- —

## Transplanted from (OLD, read-only)

- `sce_hle/libsdr.cpp`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

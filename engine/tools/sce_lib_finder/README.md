# `engine/tools/sce_lib_finder/`

**Phase:** 9.1  
**Purpose:** SDK 3.0.1 signature matching → SdkBindingManifest addresses (the ONLY way an HLE row acquires an address).

## Belongs here

- sce_lib_finder/*.py (8), detect_sdk_version.py, generate_bindings.py

## Does not belong here

- hand-authored addresses

## Transplanted from (OLD, read-only)

- `tools/sce_lib_finder/*, sce_hle/generate_bindings.py`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

# `engine/cdvd/hle/`

**Phase:** 4  
**Purpose:** libcdvd SdkBinding rows + the loader completion-visibility model (HARDWARE/TIMING HLE, HOOK §3.6).

## Belongs here

- Loader (hold/decision core), libcdvd_bindings

## Does not belong here

- FUN_0017d6b8 spin-model params (games/<title>/hle/loader_sync)

## Transplanted from (OLD, read-only)

- `sce_hle/libcdvd.cpp, hle/loader_sync_hle.h mechanism half, cdvd_stream.cpp bindings`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

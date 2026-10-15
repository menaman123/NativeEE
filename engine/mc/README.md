# `engine/mc/`

**Phase:** 4 (stage 4E: `McHostStore`, `McCommandModel`, `LibmcBindings`, `McservModule`); integration I4-A: `savestate/MchlSection` (the MCHL section over `McCommandModel`, optional; registered after SNDC in the composition root)  
**Purpose:** Memory card; hle/ = libmc over a host directory; savestate/ = the MCHL codec.

## Belongs here

- hle/
- savestate/ (`MchlSection`: 12-byte MCHL payload -- i32 pending cmd, i32 pending result, u8 seen[2][2])

## Does not belong here

- —

## Transplanted from (OLD, read-only)

- `sce_hle/libmc.cpp, hle_heap.cpp MC half`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

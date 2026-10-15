# `engine/sif/hle/`

**Phase:** 4  
**Purpose:** sceSif RPC handler bodies (invoked through the sce registry rows) + the IOP→EE marshal.

## Belongs here

- RpcRouter, SceSif, netcnfif_bindings

## Does not belong here

- —

## Transplanted from (OLD, read-only)

- `sif_hle.cpp, sif_rpc_handlers.cpp`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

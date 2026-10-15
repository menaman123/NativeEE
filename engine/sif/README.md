# `engine/sif/`

**Phase:** 4  
**Purpose:** SIF mailbox registers + RPC router (bind/call, IOP→EE marshal Wait::Rpc, CompletePending); hle/ = sceSif handler bodies.

## Belongs here

- SifState, hle/RpcRouter, hle/SceSif, hle/netcnfif_bindings

## Does not belong here

- the 0x181490 piracy bypass (games/<title>/hle/workarounds/)

## Transplanted from (OLD, read-only)

- `sif.{h,cpp}, sif_hle.{h,cpp}, sif_rpc_handlers.{h,cpp} router half, syscalls.cpp sceSifSetDma decode, sce_hle/netcnfif.cpp`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

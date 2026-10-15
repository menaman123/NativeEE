# `engine/iop/hle/`

**Phase:** 4  
**Purpose:** Stock Sony module stubs, ONE directory per module (IOPRP300 set + disc IRX): thbase, thsemap, thevent, thmsgbx, timrman, sifman, sifcmd, sysclib, stdio, ioman, loadcore, sio2man, padman, mcman, mcserv, libsd, sdrdrv, cdvdstm, cdvdman.

## Belongs here

- per-module stub classes delegating to IopKernel; deferred_events

## Does not belong here

- custom modules (title plugins)

## Transplanted from (OLD, read-only)

- `iop_stubs.{h,cpp} split per module, sif_rpc_handlers.cpp per-module RPC halves`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

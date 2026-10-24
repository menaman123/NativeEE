# `engine/iop/hle/`

**Phase:** 4  
**Purpose:** Stock Sony module stubs, ONE directory per module (IOPRP300 set + disc IRX): thbase, thsemap, thevent, thmsgbx, timrman, sifman, sifcmd, sysclib, stdio, ioman, loadcore, sio2man, padman, mcman, mcserv, libsd, sdrdrv, cdvdstm, cdvdman.

## Belongs here

- per-module stub classes delegating to IopKernel; deferred_events
- `stock_library_binder.{h,cpp}`: the one table of export ORDINALS (sysmem, intrman, ioman, loadcore, stdio, sysclib,
  thbase, thsemap, thevent, thmsgbx, timrman, sifman, sifcmd, libsd, cdvdman, modload, usbd) that registers each bound
  module's bodies in an `IopLibraryTable` -- what a recompiled module's `import_call("lib", idx, ctx)` resolves against
- `loadcore/` (RegisterLibraryEntries -> the library table; ReleaseLibraryEntries; QueryBootMode), `thevent/`, `thmsgbx/`,
  `modload/` (ReferModuleStatus / GetModuleIdListByName over the module table's rows), `usbd/` (the idle USB bus: a
  driver registers, no device ever attaches)
- `sifcmd/`: the server registry (sceSifRegisterRpc records + queue tids -> the composition's registrar puts an
  `IopRpcServer` on the EE's RpcRouter), sceSifGetOtherData (EeRamSource), sceSifSendCmd (-> the EE's SIF command
  buffer + sceSifAddCmdHandler handler, `SceSif::QueueCommand`)

## Does not belong here

- custom modules (title plugins)

## Transplanted from (OLD, read-only)

- `iop_stubs.{h,cpp} split per module, sif_rpc_handlers.cpp per-module RPC halves`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

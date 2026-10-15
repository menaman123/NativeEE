# `engine/gs/hle/sce/`

**Phase:** 6  
**Purpose:** libgraph surface as SdkBinding rows: sceGsSyncV (delegates to BoundarySequencer), SyncPath, ResetGraph/Path, SendImage; packet builders.

## Files

| File | Type | What |
|---|---|---|
| `gs_sync.{h,cpp}` | `GsSync` | the frame-boundary owner: `sceGsSyncV` / `WaitForVblank` run `BoundarySequencer::RunBoundary`; `sceGsSyncPath` returns at once |
| `libgraph_bindings.{h,cpp}` | `ps2r::gs::LibgraphBindings` | the two other libgraph rows: `sceGsResetGraph(mode, interlace, omode, ffmode)` (CSR RESET / FLUSH, the GsGParam shorts, the SyncV-callback teardown, `GsPutIMR(0xff00)`, the BIOS power-on display environment, the `SetGsCrt` tail call) and `sceGsSyncVCallback(fn)` (the cause-2 INTC handler in the kernel table, GsGParam +0x08/+0x0C). Each register store goes through the guest page (the MMIO route) AND the `ps2r::gs::GsPrivRegs` shadow the composition root binds; the GsGParam address is title data (`TitleProfile::gs.gparam_addr`). Compiled in `ps2r_gs_sync`, which links `ps2r_gs_state` for the shadow (`state/gs_priv_regs.cpp` is compiled only there). Test: `tests/gs/test_libgraph_bindings.cpp` (ctest `LibgraphBindings`). |

## Belongs here

- GsSync, GsPacket, PacketDiff, libgraph_bindings, GsCallbacks

## Does not belong here

- hle_InitGraphics/hle_RenderLoop (dead — never), 0x0019aff0 display-regs seam (games/ quarantine)

## Transplanted from (OLD, read-only)

- `gs_hle.{h,cpp}, hle/gs_packet.h, sce_hle/libgraph.cpp`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

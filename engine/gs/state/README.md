# `engine/gs/state/`

**Phase:** 6  
**Purpose:** GsRegisters (dual context, env, xfer latch, clut state, privileged regs), VertexKick, the ONE flush → DrawIR, ir_apply.

## Contents (stage 6B)

- `gs_registers.h/.cpp` — `GsRegisters`: PRIM + PRMODE/PRMODECONT, ctx[2] (FRAME ZBUF XYOFFSET SCISSOR TEST TEX0 TEX1 MIPTBP CLAMP ALPHA FBA), env (TEXA FOGCOL DTHE DIMX COLCLAMP PABE SCANMSK TEXFLUSH), the attribute registers (RGBAQ ST UV FOG, register + PACKED forms), CLUT load state; `Write(addr, u64)` with every field decode; the pending-draw flush hooks; dirty bits + `TestDrawChanged`/`SnapshotState`.
- `gs_priv_regs.h/.cpp` — `GsPrivRegs`: the 0x12000000.. registers, CSR reset/clear rules, DISPFB/DISPLAY/BGCOLOR → `SetFrontBuffer`/`SetWindow`/`SetClearColor` jobs, SIGNAL/FINISH/LABEL.
- `vertex_kick.h/.cpp` — `VertexKick` (+ `VertexKickQuirks`): vertex assembly from the attribute registers, the 3-slot queue, strip parity / degenerate cull / restart, fan, sprite corner normalisation, ADC consume, the first-kick TME latch, deferred flush at the kick.
- `draw_flush.h/.cpp` — `DrawFlush`: THE flush → `DrawIr` (`FillTexture` converts the register mirror into the draw's `TexReadView`; TEX1.K is sign-extended there — the register keeps the raw 12-bit field, the draw carries the s7.4 value, GS User's Manual 3.4.12) (`FlushKind::StateChange` resets parity + snapshot; `GifEop` keeps them), `RunKey`/`RunKeyEquals` (the producer merge key incl. DTHE/DIMX).
- `gs_state.h/.cpp` — `GsState` façade: `WriteRegister` / `WritePacked` / `WriteImage` / `KickBoundary` / `GifTagRead` / `KickVertex` / `WritePrivileged` / `ReadPrivileged` / `SetSink` / `SetIdentity`; owns `VramImage`, `GsRegisters`, `VertexKick`, `DrawFlush`, `TransferEngine`, `GsPrivRegs`.
- `gs_mmio_handler.h/.cpp` — `GsMmioHandler : MmioHandler` (integration item 6.2): the EE's view of the privileged page `[0x12000000, 0x12002000)` over a `GsState` — 64-bit → `WritePrivileged`/`ReadPrivileged`, 32-bit halves → `GsPrivRegs::WriteLower/WriteUpper` (and half reads), 16/8-bit lanes widened to the enclosing half; read/write access census. Tests: `tests/gs/state/test_gs_mmio_handler.cpp`.
- Library `ps2r_gs_state` (links `ps2r_gs_xfer`, `ps2r_gs_vram`, `ps2r_gs_ir`, `ps2r_core`). Tests: `tests/gs/state/test_gs_state.cpp`.

## Belongs here

- GsRegisters, GsPrivRegs, VertexKick, Flush, IrApply

## Does not belong here

- scene latches (games/<title>/hle/scene_hooks), roundSprite MODE value (TitleProfile.quirks)

## Transplanted from (OLD, read-only)

- `gs_state.{h,cpp}, gs.{h,cpp}, gif.cpp FlushBatch`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

# `engine/gif/savestate/`

**Phase:** integration I5.7-A (plan item 5.7)  
**Purpose:** `GifsSection` -- the GIFS `.ps2st` section over `GifRegs`: `u32 ctrl, u32 mode, u32 stat, u8 path3_masked`, 13 bytes, REQUIRED. The `stat` word is the reference's never-written STAT mirror (its GIF_STAT reads were composed, as `GifRegs::ComposeStat` is): written as 0, ignored on restore. Restore = `GifRegs::RestoreRegisters(ctrl, mode, path3_masked)` (CTRL, MODE, the VIF mask; FIFO count and in-flight transfer cleared) + an optional root-bound callback for the parser's buffered tail. Registered in `app/host/ee_bootstrap.cpp` after VIFS and before PADS.

## Belongs here

- GifsSection

## Does not belong here

- the register file (`engine/gif/gif_regs`); the parser / arbiter; the envelope / registry (`engine/core/savestate`)

## Transplanted from (OLD, read-only)

- `host_app/savestate.cpp:1239-1245` (capture), `:1866-1874` (restore: ctrl, mode, stat, path3_masked; `dma_buffer.clear()`, `fifo_count = 0`, `in_transfer = false`)
- `host_app/gif.cpp:185-196` (`GIF::Reset`: the only writer of the `stat` field), `:221-245` (GIF_STAT composed on read)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

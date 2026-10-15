# `engine/vif/savestate/`

**Phase:** integration I5.7-A (plan item 5.7)  
**Purpose:** `VifsSection` -- the VIFS `.ps2st` section over `Vif`: `VifRegs` for VIF0 then VIF1, each the frozen 92-byte 23-word record (`vif_regs.h`), 184 bytes, REQUIRED. Restore copies both register files and calls `Vif::ResetTransient(unit)` on each (parser state, latch, FIFO, DIRECT buffer, pending command back to idle; registers untouched), the reference's post-restore idling. Registered in `app/host/ee_bootstrap.cpp` after DMAC / TIMR and before GIFS.

## Belongs here

- VifsSection

## Does not belong here

- the unit (`engine/vif/vif`); the MMIO gate; the envelope / registry (`engine/core/savestate`)

## Transplanted from (OLD, read-only)

- `host_app/savestate.cpp:1233-1238` (capture: `regs[0]`, `regs[1]` raw), `:1855-1864` (restore: registers, then `state = IDLE`, `latch_fill_count = 0`, `fifo.clear()`, `direct_buffer.clear()` per unit)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

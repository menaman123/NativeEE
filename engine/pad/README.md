# `engine/pad/`

**Phase:** 7  
**Purpose:** scePad protocol; hle/ = libpad bindings; savestate/ = the PADS `.ps2st` section (`PadsSection` over `PadProtocol`). Device polling lives in platform/sdl/input.

## Belongs here

- hle/
- savestate/

## Does not belong here

- SDL input (platform/)

## Transplanted from (OLD, read-only)

- `pad_input.{h,cpp}`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

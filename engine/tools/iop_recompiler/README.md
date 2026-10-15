# `engine/tools/iop_recompiler/`

**Phase:** 9.1 (4 uses the frozen tool's output)  
**Purpose:** R3000A → C++ mechanism (rabbitizer-backed). Per-title IRX inputs are title data.

**State (2026-10-14):** the transplant lives in `tools/iop_recompiler/` (byte-identical OLD sources,
`PROVENANCE.md`; target `ps2r_iop_recompiler`), run by the build over the STREAM.IRX from the user's
disc (cmake/TitleInputs.cmake). This directory stays the planned home of a NEW-authored rewrite.

## Belongs here

- IopRecompiler, main

## Does not belong here

- STREAM.IRX seams (games/<title>/iop/stream_irx/recomp_seams.toml), PS2_STRMQ probe injections (deleted)

## Transplanted from (OLD, read-only)

- `iop_recompiler/{main.cpp, iop_recompiler.{h,cpp}, ExportIopFunctions.java}`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

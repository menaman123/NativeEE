# `engine/gs/hle/`

**Phase:** 6  
**Purpose:** The NATIVE RENDERER — the GS's HLE (ARCH D3): DrawIR, render queue, targets, texture sources, class renderers, interpolation, present, libgraph surface.

## Belongs here

- ir/, queue/, targets/, texsrc/, classrdr/, interp/, present/, sce/

## Does not belong here

- graphics-API types (render/backends), per-game trigger data (games/<title>/renderer_profile.cpp)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

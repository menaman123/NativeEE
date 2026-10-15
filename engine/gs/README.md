# `engine/gs/`

**Phase:** 2 (format), 6 (rest)  
**Purpose:** Graphics Synthesizer: format layer, register state + vertex kick + ONE flush, VRAM image, transfers, and hle/ = the native renderer.

## Belongs here

- format/, state/, vram/, xfer/, hle/

## Does not belong here

- VRAM base constants / class taxonomy (RendererProfile), GL calls (render/backends/)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

# `engine/gs/hle/interp/`

**Phase:** 6  
**Purpose:** Optional frame interpolation (video.interp), identity-stamp matched.

- **Status (Phase 6 closure, 2026-09-28): deferred to integration / Phase 8 — nothing landed here in 6A–6F″** (P6-INTERP-DEFERRED, PHASE6_IMPL.md `6F″`; `video.interp` is OFF in the deterministic profile and in every corpus baseline; invariants 25/26 are registered as SKIP stubs from `tests/gs/invariants/`).

## Belongs here

- FrameInterpolator

## Does not belong here

- —

## Transplanted from (OLD, read-only)

- `gs_interp.h, opengl_backend.cpp interp region`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

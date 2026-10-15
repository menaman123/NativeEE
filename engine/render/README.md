# `engine/render/`

**Phase:** 6  
**Purpose:** The backend CONTRACT (ARCH §6): IRenderBackend, Caps, surface handles. Backends beneath.

## Contents (stage 6C)

- `backend.h` -- `render::Backend` (the interface; named `Backend`, not `IRenderBackend`, so the file name = snake_case(type)), `Caps`, `SurfaceId`, `FenceId`, `Rect`, `SurfaceDesc`, `ClearFlags`, `CdCopyState`, `StencilState`, `StencilProgram`, `InheritedUniform` (the register-program uniforms a class-native generic-arm draw leaves as the previous full-set draw wrote them), `DrawParams`, `DrawResult`, `ScanoutDesc`; the stencil-composite primitives `ClearStencil` / `DrawConstQuad` (a constant colour under the stencil test over a scissor rect -- the one operation a DrawIr cannot express).
- `surface_format.h` -- `SurfaceFormat {Rgba8, Depth32f, R8}`.
- `CMakeLists.txt` -- INTERFACE lib `ps2r_render`; `add_subdirectory(backends/gl)`.

## Belongs here

- backend.h, Caps, SurfaceId

## Does not belong here

- coherence decisions (gs/hle)

## Transplanted from (OLD, read-only)

- `graphics/graphics_backend.{h,cpp} (rewritten as the interface)`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

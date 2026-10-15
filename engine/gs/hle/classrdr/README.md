# `engine/gs/hle/classrdr/`

**Phase:** 6 (stages 6E, 6E′, 6E″)  
**Purpose:** the ONE per-draw pipeline above the backend interface (`DrawPipeline`: render size →
target → same-buffer pre-draw flush → alias refresh → target params + composite-consumption mark →
texture resolve + column-shuffle snap → `Backend::Draw` → coherence tail; plus the per-job entry points
InvalidateTextures / UploadVRAMTexture / SetWindow / SetFrontBuffer / SyncAll / frame end), the draw-class
taxonomy + census names (`ClassClassifier`), the renderer registry with the caller-runs-fallback dispatch and
the `[GS-CLASSRDR]` counter grammar (`ClassRegistry`), the state-shape vocabulary (`DrawShapeSpec`), the
replica class renderers (world-tristrip / skinned (+ raw skin) / sprites-HUD / particles / FMV blit -- each
`Accept(spec) + Draw = DrawPipeline::DrawGeneric` under the shape's `ClassRenderer::kInherit*Arm` mask -- the
register-program uniforms the reference's native generic arm never wrote stay as the last full-set draw left them
(`render::InheritedUniform`; PHASE6_IMPL 6F' P6-NATIVE-STALE-UNIFORMS) -- so their pixels are the reference's),
the stencil-composite sequence FSM + byte arithmetic (`SequenceRecipe`), the seam through which the
pipeline's hooks reach the object owning the stencil surface (`SequenceMaterializer`), the stencil-composite
renderer itself (`StencilCompositeRenderer`: CLEAR prime, the world-strip steer, the three-pass stencil ladder per
strip, MATERIALIZE as 32 stencil-masked constant quads, the native byte-NOT invert behind its backend predicate,
the `[GS-S9RDR]` lines; it owns no backend -- its destructor makes no backend call, the stencil attachment is
the device's to release at `Shutdown` or the owner's through `Release(backend)`), and the column-shuffle snap
(`TextureShuffleSnap`), and the per-draw lowering record of the last completed frame (`DrawRouteTrace`: texture owner
decoded / surface / none, blend arm ff / ff-rt / sw, destination-copy state -- written by the harness as
`<leg>/draw_routes.txt`; diagnostics only, consulted by nothing).

## Belongs here

- `DrawPipeline`, `ClassRenderer` (interface), `ClassRegistry`, `ClassClassifier` (the enum + the ten census
  names ARE the engine taxonomy -- the corpus tools' vocabulary; the stamp substrings and raw thresholds it
  reads are `ClassClassifier::Rules` from the profile), `DrawShapeSpec`, `SequenceRecipe`,
  `WorldTristripRenderer` (its strip family steered through the stencil composite when one is attached),
  `SkinnedRenderer`, `SpriteHudRenderer`, `ParticlesRenderer`, `FmvBlitRenderer`, `StencilCompositeRenderer`,
  `TextureShuffleSnap`.

## Does not belong here

- Stamp substrings, Accept tuples, sequence constants, VRAM pins (`games/<title>/renderer_profile`).
- Any GL type (`engine/render/backends` is never included here; `tests/gs/hle_include_depth.py`).

## Transplanted from (OLD, read-only)

- `host_app/gs_class_renderer.h` (taxonomy, ClassifyFields, Dispatch, EmitCounters, WarmUpAll),
  `graphics/opengl/{world_tristrip,skinned,sprite_hud,particles,fmv_blit}_native.inc` (Accept tuples + the
  emit lines), `graphics/opengl/s9_composite.h` (FSM, constants, predicates), `graphics/opengl/
  s9_composite_native.inc` (`S9CompositeRenderer::Accept/Draw`, `GsS9InvertBackendOk`, `DrawS9ClearNative`,
  `DrawS9StripOrWorld`, `DrawS9StripStencil`, `GsS9Materialize`, `GsS9RdrEmit`; the `[GS-S9DEM]` demotion census
  and every `PS2_S9_DEMOTE` / `PS2_GS_S9RDR_DBGPIX` / `PS2_SH6_SWEEP` / `DebugBothOn` probe are DIAG, not written),
  `graphics/opengl/world_tristrip_native.inc:200-216` (the strip steer), `host_app/shuf_snap.h`,
  `opengl_backend.cpp:9008-9034` (same-buffer pre-draw flush), `:9717-9731` + `:10153-10165` (snap), `:9148-9155`
  (composite consumption), `:8323-8330` (sequence hook site).

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4; `docs/phases/PHASE6_PLAN.md` §2.6.

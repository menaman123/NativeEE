// gs/hle/classrdr/stencil_composite_renderer.h -- StencilCompositeRenderer: the
// shadow-composite class rendered as a stencil winding counter.
//
// The title accumulates a shadow-volume winding count in a CT16S buffer (CLEAR to
// a base byte, +8 / -8 flat DATE strips wrap-added under COLCLAMP off, a byte-NOT
// invert pass); the generic lowering reproduces it per strip through the shader
// blend and a destination copy. This renderer keeps the SAME bytes while the
// sequence is live by counting in a stencil attachment instead: the CLEAR runs the
// generic path and PRIMES the sequence FSM (SequenceRecipe) when every vertex
// carries the base colour on a native-scale target; the first conforming strip
// ARMS it (stencil cleared to base >> 3); every strip then runs the three-pass
// ladder -- pass 1 draws the geometry into the stencil alone (DATE + colour-authored
// Z discards in the shader) toggling bit 7 where a fragment landed, pass 2 a
// bbox-quad INCR_WRAP / DECR_WRAP of the 5-bit counter where bit 7 is set, pass 3
// the same quad clearing bit 7 (one count per strip per pixel however many
// fragments) -- and records the same coherence tail one generic strip would (the
// per-strip CT16S ledger record is load-bearing for the mirror). MATERIALIZE (any
// draw into or out of the accumulator, a texture serve or writeback of it, a
// non-conforming strip, a surface change) rebuilds the bytes with 32 stencil-masked
// constant quads (byte = 8 * counter) over the union of the strip rects and stamps
// the target. The INVERT pass runs the folded byte-NOT program over the target's
// destination copy when the backend predicate holds (a live 32-bit target drawn
// this frame, its CLUT the identity-invert table, no deferred writeback under the
// CLUT). Everything the sequence compares against is title data (the recipe
// constants and the three Accept shapes); the counters print in the reference's
// grammar.
//
// Lifetime rule: the renderer does not own the backend, the pipeline or the
// registry it caches from WarmUp / Draw, so its destructor touches none of them
// (the device releases every surface at Shutdown). The owner that needs the
// stencil attachment freed earlier -- before the backend is shut down, or when the
// renderer is replaced against a live backend -- calls Release(backend) with the
// backend it knows to be alive. Symmetrically, the registry slot and the
// pipeline's materializer pointer that name this renderer are their owners' to
// clear before the renderer goes while they live.
#pragma once

#include <cstdint>
#include <cstdio>

#include "engine/gs/hle/classrdr/class_registry.h"
#include "engine/gs/hle/classrdr/class_renderer.h"
#include "engine/gs/hle/classrdr/draw_pipeline.h"
#include "engine/gs/hle/classrdr/draw_shape_spec.h"
#include "engine/gs/hle/classrdr/sequence_materializer.h"
#include "engine/gs/hle/classrdr/sequence_recipe.h"

namespace ps2r {
namespace gs {

class StencilCompositeRenderer final : public ClassRenderer, public SequenceMaterializer {
public:
    struct Shapes {
        const DrawShapeSpec* clear = nullptr;    // pass A: the accumulator CLEAR
        const DrawShapeSpec* strip = nullptr;    // the world DATE strip family (tristrip)
        const DrawShapeSpec* fan = nullptr;      // the same family as a triangle fan
        const DrawShapeSpec* invert = nullptr;   // pass D: the byte-NOT self-read
    };
    struct Counters {
        uint64_t clears = 0, primes = 0, arms = 0, strips_stencil = 0, strips_color = 0, inverts = 0,
                 materializes = 0, mat_draws = 0, demotes = 0, scale_declines = 0, dedupe_draws = 0;
        uint64_t inv_no_target = 0, inv_no_tex = 0, inv_no_content = 0, inv_stale = 0, inv_psm = 0,
                 inv_pending = 0, inv_clut = 0;
    };

    // `registry` supplies the world-slot check for raw strips (nullable = declined).
    StencilCompositeRenderer(const Shapes& shapes, SequenceRecipe& seq, const ClassRegistry* registry);
    ~StencilCompositeRenderer() override = default;   // no backend call (see the lifetime rule)

    // Free the stencil attachment through a backend the caller knows to be alive
    // and forget the cached pipeline (the next Draw re-caches it).
    void Release(render::Backend& be);

    const char* Name() const override { return "S9-shadow"; }
    bool Accept(const DrawIr& ir) const override;
    void Draw(DrawPipeline& pipe, const DrawIr& ir) override;
    int WarmUp(DrawPipeline& pipe) override;
    void Materialize(DrawPipeline& pipe) override;

    // The world renderer's strip steer: the stencil ladder when the sequence is
    // live on the draw's buffer, else the colour path (no pre-draw hook).
    void DrawStripOrWorld(DrawPipeline& pipe, const DrawIr& ir);
    // Registered + enabled in the S9 slot (the steer short-circuits otherwise).
    bool Active() const;
    // The invert pass's backend predicate (counts its decline cause).
    bool InvertBackendOk(const DrawIr& ir) const;

    // The renderer's own counters (steer demotes only); the emitted demote total
    // adds the sequence's pre-draw-hook demotes (one counter in the reference).
    const Counters& Count() const { return cnt_; }
    uint64_t Demotes() const { return cnt_.demotes + seq_.Count().demotes; }
    void Emit(std::FILE* f) const;
    render::SurfaceId StencilSurface() const { return stencil_; }

private:
    void DrawClear(DrawPipeline& pipe, const DrawIr& ir);
    void DrawInvert(DrawPipeline& pipe, const DrawIr& ir);
    void DrawStripStencil(DrawPipeline& pipe, const DrawIr& ir, SequenceRecipe::StripDelta delta);
    void DrawStripColor(DrawPipeline& pipe, const DrawIr& ir);
    void MaterializeIfArmedOn(DrawPipeline& pipe, uint32_t fbp);
    bool EnsureStencil(render::Backend& be, int w, int h);

    Shapes shapes_;
    SequenceRecipe& seq_;
    const ClassRegistry* registry_;
    DrawPipeline* pipe_ = nullptr;
    render::SurfaceId stencil_;
    render::SurfaceId res_color_;   // the routed view the stencil was armed against
    int sten_w_ = 0, sten_h_ = 0;
    mutable Counters cnt_;
};

}  // namespace gs
}  // namespace ps2r

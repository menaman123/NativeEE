// gs/hle/classrdr/sequence_materializer.h -- SequenceMaterializer: the one object
// that owns the stencil surface of the live stencil-composite sequence and can
// rebuild the accumulator's bytes from it.
//
// The sequence FSM (SequenceRecipe) decides WHEN the armed accumulator must
// materialise -- a draw into or out of it, a texture serve or writeback of it, a
// falling-edge capture -- from hooks that sit in the draw pipeline, the target cache
// and the texture resolver. Those hooks call Materialize on the installed object
// (the stencil-composite renderer); with none installed the action is dropped,
// which is only correct while nothing arms (no renderer registered).
#pragma once

namespace ps2r {
namespace gs {

class DrawPipeline;

class SequenceMaterializer {
public:
    virtual ~SequenceMaterializer() = default;
    // The 32 stencil-masked constant quads over the sequence extent + the
    // extent's coherence stamps; the FSM is already IDLE when this runs.
    virtual void Materialize(DrawPipeline& pipe) = 0;
};

}  // namespace gs
}  // namespace ps2r

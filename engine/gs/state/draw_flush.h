// gs/state/draw_flush.h -- DrawFlush: THE flush that turns the queued draw
// buffer into one DrawIr and hands it to the sink.
//
// Snapshot order (the record's field order): the vertex array moves out of the
// kick's draw buffer; PRIM type and context; the texture-read view when the
// draw's latched TME is set (TEX0 of the context, TEX1 filters/mip fields, CLAMP,
// TEXA, MIPTBP1/2 decoded to per-level base/width) with the UV-vs-STQ flag from
// the attribute registers; the environment (COLCLAMP PABE DTHE DIMX FBA ALPHA
// FOGCOL); FRAME, ZBUF, SCISSOR, TEST of the context; IIP/FGE/FST/SCANMSK; the
// producer identity stamp; seq / src_frame / vram_epoch; the four-lane vertex
// hash and the bbox. Then the draw buffer is cleared.
//
// The two flush kinds differ only in what they reset afterwards:
//   StateChange (a register write / deferred flush before a kick): the strip
//               parity restarts and the dirty-register snapshot is taken;
//   GifEop      (end of a GIF packet): strip parity and dirty bits are kept, so
//               a strip continuing across packets keeps its winding.
// Neither clears the vertex queue.
//
// RunKey / RunKeyEquals: the producer-side merge key -- every state field two
// consecutive draws must share to be appended into one run, INCLUDING the dither
// state (DTHE, DIMX) and the alpha / test / texture / frame fields. An empty
// vertex array keys to 0; a non-empty draw never does.
#pragma once

#include <cstdint>
#include <functional>

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/ir/draw_ir_sink.h"
#include "engine/gs/state/gs_registers.h"
#include "engine/gs/state/vertex_kick.h"

namespace ps2r {
namespace gs {

class DrawFlush {
public:
    enum class FlushKind : uint8_t { StateChange, GifEop };

    DrawFlush(GsRegisters* regs, VertexKick* kick) : regs_(regs), kick_(kick) {}

    void SetSink(DrawIrSink* sink) { sink_ = sink; }
    // Identity of the producer whose kicks fill the next draws (0 = raw).
    void SetIdentity(const DrawIr::Identity& id) { identity_ = id; }
    void SetJobContext(uint32_t* seq_counter, const uint32_t* src_frame, const uint64_t* vram_epoch);
    // Called before the draw is built (the observation boundary settles VRAM writes).
    void SetBeforeFlushHook(std::function<void()> hook) { before_ = std::move(hook); }

    // No-op when nothing is queued. Returns true when a draw was emitted.
    bool Flush(FlushKind kind);

    static uint64_t RunKey(const DrawIr& d);
    static bool RunKeyEquals(const DrawIr& a, const DrawIr& b) { return RunKey(a) == RunKey(b); }

    uint32_t Emitted() const { return emitted_; }

private:
    void FillTexture(DrawIr& d, const GsRegisters::Context& c) const;
    void FillRaster(DrawIr& d, const GsRegisters::Context& c) const;

    GsRegisters* regs_;
    VertexKick* kick_;
    DrawIrSink* sink_ = nullptr;
    DrawIr::Identity identity_;
    uint32_t* seq_ = nullptr;
    const uint32_t* src_frame_ = nullptr;
    const uint64_t* vram_epoch_ = nullptr;
    std::function<void()> before_;
    uint32_t emitted_ = 0;
};

}  // namespace gs
}  // namespace ps2r

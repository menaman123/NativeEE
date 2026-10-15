// gs/hle/classrdr/draw_route_trace.h -- DrawRouteTrace: the per-draw record of HOW
// the pipeline lowered each draw of the most recently completed frame.
//
// A GS draw's visible result depends on three lowering choices the DrawIR itself
// does not carry: where the texture came from (a decoded VRAM window, a live
// render target, nothing), which blend arm ran (GL fixed-function per ALPHA
// (A-B)*C+D, the shader-side blend over a destination copy, or the direct
// target-read arm), and whether the destination copy was refreshed for the draw.
// The trace keeps one line per draw of the last frame so a harness leg can write
// them next to the frame image it hashed; it decides nothing and is not consulted
// by the pipeline.
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/texsrc/texture_source.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class DrawRouteTrace {
public:
    struct Line {
        uint32_t seq;            // pipeline draw ordinal
        uint32_t src_frame;      // the draw's vsync frame
        uint8_t prim, abe, tex;  // PRIM type, ABE, TME
        uint8_t a, b, c, d;      // ALPHA selectors
        uint8_t fix;             // ALPHA.FIX
        uint8_t tfx, tcc;        // TEX0 texture function / colour component
        uint8_t owner_kind;      // TextureSource::OwnerKind
        uint8_t fmt_case;        // target-read format case
        uint8_t ate, atst, aref, afail, zte, ztst, zmsk, date, colclamp, pabe, fba;
        uint32_t fbp, fbmsk, tbp, tpsm, cbp;
        uint32_t verts;
        uint32_t rgba0;          // first vertex colour
        bool drew, sw_blend, route_rt_blend, needs_rt_copy, cd_copied, wrap_route, fbmsk16_exact;
    };

    // Records the draw just executed (the generic arm's inputs and result).
    void Note(const DrawIr& ir, const TextureSource& src, const render::DrawResult& res, uint32_t seq);
    // Frame boundary: the current frame's lines become the last completed frame's.
    void OnFrameEnd();
    const std::vector<Line>& LastFrame() const { return last_; }
    // One text line per draw of the last completed frame.
    void Write(std::FILE* f) const;

private:
    std::vector<Line> cur_;
    std::vector<Line> last_;
};

}  // namespace gs
}  // namespace ps2r

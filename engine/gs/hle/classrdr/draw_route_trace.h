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
// by the pipeline. Each line also carries the draw's COST as the pipeline measured
// it (wall ns over Prepare + Draw + Finish, and the backend call census deltas:
// readbacks, copies, uploads, draw calls) and the frame's end-of-frame cost
// (present, grab, writeback) lands in a summary line, so a per-route histogram of
// where the consumer's frame goes is readable from one file.
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/present/scanout_composer.h"
#include "engine/gs/hle/targets/pending_rt_sync_ledger.h"
#include "engine/gs/hle/texsrc/texture_source.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class DrawRouteTrace {
public:
    // The measured cost of one draw (or of the frame end): wall nanoseconds and
    // the backend call census deltas over it.
    struct Cost {
        uint64_t ns = 0;
        uint32_t readbacks = 0, readback_bytes = 0;
        uint32_t copies = 0, copy_bytes = 0;
        uint32_t uploads = 0, upload_bytes = 0;
        uint32_t draw_calls = 0, texture_binds = 0, program_binds = 0, framebuffer_binds = 0;
        // Census b - a (the deltas over a bracket; a null census reads zero).
        static Cost Delta(const render::CallCensus* a, const render::CallCensus* b, uint64_t ns);
        void Add(const Cost& o);
    };

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
        Cost cost;
    };
    // The trace is OFF until armed (gs_replay's --dump-ppm, the title's
    // NATIVEEE_GS_ROUTES): the per-draw record and the two clock reads + census
    // snapshot that cost it were ~0.3 ms of a Paris frame on the consumer thread.
    // Arming mid-run starts recording at the next draw; the frame written is the
    // first completed after the arm.
    void Arm(bool on) { armed_ = on; }
    bool Armed() const { return armed_; }
    // Records the draw just executed (the generic arm's inputs and result).
    void Note(const DrawIr& ir, const TextureSource& src, const render::DrawResult& res, uint32_t seq,
              const Cost& cost);
    void Note(const DrawIr& ir, const TextureSource& src, const render::DrawResult& res, uint32_t seq) {
        Note(ir, src, res, seq, Cost{});
    }
    // Frame boundary: the current frame's lines become the last completed frame's;
    // `frame_end` is the cost of the boundary's own work (grab, writeback, present).
    // `frame_total` is the whole frame's census from the previous frame end
    // (draws + frame end + the jobs between: uploads, invalidates).
    void OnFrameEnd(const Cost& frame_end, const Cost& frame_total);
    void OnFrameEnd() { OnFrameEnd(Cost{}, Cost{}); }
    // The frame's present as the composer reported it (written as one row) and
    // the pending-writeback ledger's cumulative flush counts per reason.
    void NotePresent(const ScanoutComposer::DrawResult& r, const PendingRtSyncLedger& ledger);
    const std::vector<Line>& LastFrame() const { return last_; }
    // One text line per draw of the last completed frame.
    void Write(std::FILE* f) const;

private:
    bool armed_ = false;
    std::vector<Line> cur_;
    std::vector<Line> last_;
    Cost last_frame_end_;
    Cost last_frame_total_;
    ScanoutComposer::DrawResult present_;
    uint64_t ledger_flushes_[static_cast<size_t>(PendingRtSyncLedger::Reason::kCount)] = {};
    size_t ledger_entries_ = 0;
};

}  // namespace gs
}  // namespace ps2r

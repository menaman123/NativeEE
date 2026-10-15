// gs/hle/ir/draw_ir_sink.h -- DrawIrSink: the consumer boundary of the GS state
// machine. GsState hands every flushed draw, every non-draw job and every frame
// boundary (the VSync job) to one sink; the render queue, the recorder and the
// replay driver are its implementations.
#pragma once

#include <utility>

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/ir/job_rec.h"

namespace ps2r {
namespace gs {

class DrawIrSink {
public:
    virtual ~DrawIrSink() = default;
    virtual void OnDraw(const DrawIr& draw) = 0;
    // The same draw, OWNED: a sink that keeps the draw (the cross-thread queue's
    // record) takes it by move instead of copying its vertex vector; the default
    // delivers it as OnDraw. The state machine hands its flushed draws this way.
    virtual void OnDrawOwned(DrawIr&& draw) { OnDraw(draw); }
    virtual void OnJob(const JobRec& job) = 0;
    // The VSync job that closes a frame (JobKind::VSync; arg1 == 1 marks an
    // EE-pushed game-frame boundary).
    virtual void OnFrame(const JobRec& vsync) = 0;
};

}  // namespace gs
}  // namespace ps2r

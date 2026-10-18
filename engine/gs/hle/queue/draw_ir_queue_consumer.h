// gs/hle/queue/draw_ir_queue_consumer.h -- DrawIrQueueConsumer: the DrawIrSink
// that lowers the GS state machine's stream into DrawPipeline in stream order.
//
// Mapping (the same the corpus replay driver applies to a recording):
//   OnDraw(ir)                 -> SetSrcFrame / SetGfxEpoch from the record,
//                                 DrawPipeline::Draw(ir, handler name)
//   OnJob SetWindow            -> OnSetWindow(window w/h, content w/h); a window
//                                 size change reaches the host through the
//                                 window_resized hook (the GL viewport)
//   OnJob SetFrontBuffer       -> OnSetFrontBuffer(fbp, fbw px, psm)
//   OnJob InvalidateTextures   -> OnInvalidate(addr, size)
//   OnJob UploadVRAMTexture    -> OnUpload(addr, w, h, pixels, footprint)
//   OnJob SyncAllRTsToVRAM     -> OnSyncAll
//   OnJob SetClearColor / SetScissor / SetBlendMode / SetDepthTest -> no-op
//   OnFrame(vsync)             -> OnFrameEnd(arg1 == 1, crtc snapshot, &frame):
//                                 the EE frame mark, the reader-band present
//                                 flush, the display grab, the display quad and
//                                 the backend present; the grab is kept for the
//                                 frames lane (LastFrame).
//
// The upload payload is not part of JobRec: the transfer engine publishes the
// completed upload's RGBA8 copy (LastUploadPixels) only after its sink call
// returns, so an UploadVRAMTexture job is held as pending and applied at the
// start of the NEXT sink event (draw, job or frame) -- the same point in stream
// order, since nothing else reaches the pipeline in between.
//
// Deterministic profile: the consumer runs on the EE thread at the boundary
// (no cross-thread queue yet); the `enter` hook runs before any backend work
// (the host makes its GL context current on the calling thread there).
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include "engine/gs/hle/classrdr/draw_pipeline.h"
#include "engine/gs/hle/ir/draw_ir_sink.h"
#include "engine/gs/hle/present/crtc_classifier.h"

namespace ps2r {
namespace gs {

class DrawIrQueueConsumer : public DrawIrSink {
public:
    // The existing backend owner enters and drains the previous upload before
    // a complete owned group. This is a host task, not a synthetic JobRec.
    bool RunOrdered(const std::function<bool()>& work);
    struct Hooks {
        std::function<void()> enter;                                       // before any backend work
        std::function<const std::vector<uint8_t>*()> upload_pixels;        // the engine's completed upload copy
        std::function<CrtcClassifier::Snapshot()> crtc;                    // the CRTC snapshot at frame end
        std::function<void(int w, int h)> window_resized;                  // SetWindow changed the window size
        std::function<const char*(uint32_t hle_handler)> handler_name;     // DrawPipeline::Draw name override
        // Optional application frame owner. False means unselected only.
        // A selected failure must throw; never replay the retained presenter.
        std::function<bool(bool ee_frame_end, DrawPipeline::Frame*)> owned_frame = {};
        // Optional: whether anyone will read the boundary's display grab (the
        // frames lane, a frame dump, a screenshot). Unset = always grab (the
        // harness contract). When set and false, OnFrame presents without the
        // grab -- the full-target readback (a GPU pipeline drain per frame) and
        // its hash are the lane's cost, not a player's -- and LastFrame reads
        // grabbed = false for that boundary.
        std::function<bool()> grab_wanted = {};
    };
    struct Counters {
        uint64_t draws = 0, jobs = 0, frames = 0;
        uint64_t uploads = 0;           // uploads applied
        uint64_t uploads_dropped = 0;   // pending upload with no pixels available
        uint64_t resizes = 0;
        uint64_t presents = 0;          // frames whose present ran (a scanout base was known)
        uint64_t grab_empty = 0;        // frames with no grabbable target
        uint64_t forgotten_frames = 0;  // ForgetLastFrame calls (the live restore)
        uint64_t grabs_skipped = 0;     // boundaries presented without a grab (grab_wanted false)
    };

    DrawIrQueueConsumer(DrawPipeline& pipe, Hooks hooks);

    void OnDraw(const DrawIr& draw) override;
    void OnJob(const JobRec& job) override;
    void OnFrame(const JobRec& vsync) override;

    // The last frame boundary's grab (empty before the first OnFrame). The
    // reference form is for a caller synchronised with the consumer (the
    // deterministic boundary after WaitFrame, or the consumer thread itself).
    const DrawPipeline::Frame& LastFrame() const { return frame_; }
    // A copy for a caller running CONCURRENTLY with the consumer (the wall-locked
    // play boundary, which never waits for the consumer): the grab is published
    // under a lock at the end of each OnFrame.
    DrawPipeline::Frame LastFrameCopy() const;
    // A live restore (F8 in the running session): the published grab is the
    // PREVIOUS session's last completed picture; forget it so the first
    // restored boundary presents nothing (as a fresh process before its first
    // image) rather than a stale frame. Consumer thread (RunOrdered).
    void ForgetLastFrame();
    // The same frame WITHOUT its pixels (w, h, grabbed, source, hash): the play
    // boundary's read -- the frames lane takes the hash, the present policy the
    // flag; no pixel copy crosses the threads.
    DrawPipeline::Frame LastFrameHeaderCopy() const;
    // Hash every grab at this native scale on the consumer (0 = never; the
    // deterministic boundary hashes the reference frame itself). The hash equals
    // FrameHasher::HashNativeScale over the grab's bytes.
    void SetFrameHashScale(int scale) { hash_scale_ = scale; }
    int FrameHashScale() const { return hash_scale_; }
    const DrawPipeline& Pipeline() const { return pipe_; }
    // Consumer thread (inside RunOrdered): the pipeline the ordered work mutates.
    DrawPipeline& Pipeline() { return pipe_; }
    const Counters& Count() const { return n_; }
    bool HasPendingUpload() const { return pending_.valid; }

private:
    struct PendingUpload {
        bool valid = false;
        uint32_t addr = 0, footprint = 0, payload_bytes = 0;
        int w = 0, h = 0;
    };
    void Enter();
    void DrainPendingUpload();
    void Stamp(uint32_t src_frame, uint64_t vram_epoch);

    DrawPipeline& pipe_;
    Hooks hooks_;
    Counters n_;
    PendingUpload pending_;
    DrawPipeline::Frame frame_;            // the published grab (LastFrame)
    DrawPipeline::Frame scratch_;          // the grab in progress (consumer-owned)
    mutable std::mutex frame_mu_;          // guards the publish / LastFrameCopy
    int hash_scale_ = 0;
};

}  // namespace gs
}  // namespace ps2r

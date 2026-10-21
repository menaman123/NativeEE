// gs/hle/queue/draw_ir_queue_consumer.cpp -- the stream-order lowering (see .h).
#include "engine/gs/hle/queue/draw_ir_queue_consumer.h"

#include <utility>

#include "engine/core/verify/frame_hasher.h"

namespace ps2r {
namespace gs {

DrawIrQueueConsumer::DrawIrQueueConsumer(DrawPipeline& pipe, Hooks hooks) : pipe_(pipe), hooks_(std::move(hooks)) {}

bool DrawIrQueueConsumer::RunOrdered(const std::function<bool()>& work) {
    if (!work) return false;
    Enter();
    return work();
}

void DrawIrQueueConsumer::Enter() {
    if (hooks_.enter) hooks_.enter();
    DrainPendingUpload();
}

void DrawIrQueueConsumer::Stamp(uint32_t src_frame, uint64_t vram_epoch) {
    pipe_.SetSrcFrame(src_frame);
    pipe_.SetGfxEpoch(vram_epoch);
}

// The payload the engine published after its UploadVRAMTexture push. Dropped
// (and counted) when no provider is bound or the copy is not the job's size.
void DrawIrQueueConsumer::DrainPendingUpload() {
    if (!pending_.valid) return;
    pending_.valid = false;
    const std::vector<uint8_t>* px = hooks_.upload_pixels ? hooks_.upload_pixels() : nullptr;
    if (!px || px->empty() || px->size() != pending_.payload_bytes) {
        n_.uploads_dropped++;
        return;
    }
    pipe_.OnUpload(pending_.addr, pending_.w, pending_.h, px->data(), pending_.footprint);
    n_.uploads++;
}

void DrawIrQueueConsumer::OnDraw(const DrawIr& ir) {
    Enter();
    Stamp(ir.src_frame, ir.vram_epoch);
    const char* name = nullptr;
    if (ir.identity.hle_handler != 0 && hooks_.handler_name) name = hooks_.handler_name(ir.identity.hle_handler);
    pipe_.Draw(ir, name);
    n_.draws++;
}

void DrawIrQueueConsumer::OnJob(const JobRec& j) {
    Enter();
    Stamp(j.src_frame, j.vram_epoch);
    n_.jobs++;
    switch (static_cast<JobKind>(j.type)) {
        case JobKind::SetWindow: {
            // arg2 = window_w << 16 | content_w, arg3 = window_h << 16 | content_h.
            const int new_w = static_cast<int>(j.arg2 >> 16), new_h = static_cast<int>(j.arg3 >> 16);
            const int cw = static_cast<int>(j.arg2 & 0xFFFF), ch = static_cast<int>(j.arg3 & 0xFFFF);
            const bool resized = new_w > 0 && new_h > 0 && (new_w != pipe_.WindowW() || new_h != pipe_.WindowH());
            pipe_.OnSetWindow(new_w, new_h, cw, ch);
            if (resized) {
                n_.resizes++;
                if (hooks_.window_resized) hooks_.window_resized(new_w, new_h);
            }
            break;
        }
        case JobKind::SetFrontBuffer:
            pipe_.OnSetFrontBuffer(j.arg1, j.arg2, j.arg3);
            break;
        case JobKind::InvalidateTextures:
            pipe_.OnInvalidate(j.arg1, j.arg2);
            break;
        case JobKind::UploadVRAMTexture:
            // The pixels are published after this call returns: applied at the next event.
            pending_.valid = j.payload_bytes > 0;
            pending_.addr = j.arg1;
            pending_.w = static_cast<int>(j.arg2);
            pending_.h = static_cast<int>(j.arg3);
            pending_.footprint = j.arg4;
            pending_.payload_bytes = j.payload_bytes;
            break;
        case JobKind::SyncAllRTsToVRAM:
            pipe_.OnSyncAll();
            break;
        default:
            break;   // SetClearColor / SetScissor / SetBlendMode / SetDepthTest: display-only or no-ops
    }
}

void DrawIrQueueConsumer::HashAndPublish(DrawPipeline::Frame* grab) {
    // The lane hash on the grab's thread: the same function the boundary
    // applies to the reference frame (HashNativeScale; 0 for no grab).
    grab->hashed = hash_scale_ > 0;
    grab->hash = grab->hashed && grab->grabbed
                     ? verify::FrameHasher::HashNativeScale(grab->rgba.data(), grab->w, grab->h, hash_scale_)
                     : 0;
    std::lock_guard<std::mutex> lk(frame_mu_);
    std::swap(frame_, *grab);
}

bool DrawIrQueueConsumer::FlushPendingGrab() {
    if (!pipe_.GrabPending()) return false;
    Enter();
    if (!pipe_.CompletePendingGrab(&scratch_)) return false;
    HashAndPublish(&scratch_);
    n_.grabs_completed++;
    return true;
}

void DrawIrQueueConsumer::OnFrame(const JobRec& vsync) {
    Enter();
    Stamp(vsync.src_frame + 1, vsync.vram_epoch);
    bool presentable = pipe_.Scanout().ScanoutFbp() != DrawPipeline::kNoFbp;
    // arg1: 1 = an EE-pushed game-frame boundary (the EE frame mark + the grab
    // that feeds the frames lane and the present policy); 2 = a play-mode
    // DISPLAY TICK (the FMV pace park / kernel wait): present only -- no grab,
    // the last boundary's frame stays LastFrame().
    // The grab lands in the consumer-owned scratch and is published under the
    // lock (a concurrent LastFrameCopy reader never sees a vector mid-write).
    const bool wanted = !hooks_.grab_wanted || hooks_.grab_wanted();
    auto* grab = (vsync.arg1 == 2 || !wanted) ? nullptr : &scratch_;
    if (!wanted) n_.grabs_skipped++;
    // Deferred grabs: the previous boundary's queued read lands and is published
    // first (one frame late; see DrawPipeline::SetDeferredGrab).
    if (grab && pipe_.GrabPending()) {
        if (pipe_.CompletePendingGrab(&scratch_)) {
            HashAndPublish(&scratch_);
            n_.grabs_completed++;
        }
    }
    if (hooks_.owned_frame && hooks_.owned_frame(vsync.arg1 == 1, grab)) {
        presentable = true;
    } else {
        const CrtcClassifier::Snapshot crtc = hooks_.crtc ? hooks_.crtc() : CrtcClassifier::Snapshot{};
        pipe_.OnFrameEnd(vsync.arg1 == 1, crtc, grab);
    }
    if (grab && pipe_.GrabPending()) {
        // Queued: nothing to publish for this boundary -- the lane reads the
        // previous completed picture until the next boundary lands this one.
        n_.grabs_deferred++;
    } else if (grab) {
        HashAndPublish(grab);
    } else if (!wanted) {
        // No grab this boundary: the published frame says so (a reader that
        // wants pixels asked for none).
        std::lock_guard<std::mutex> lk(frame_mu_);
        frame_ = DrawPipeline::Frame{};
    }
    n_.frames++;
    if (presentable) n_.presents++;
    if (wanted && !frame_.grabbed && !pipe_.GrabPending()) n_.grab_empty++;
}

DrawPipeline::Frame DrawIrQueueConsumer::LastFrameCopy() const {
    std::lock_guard<std::mutex> lk(frame_mu_);
    return frame_;
}

void DrawIrQueueConsumer::ForgetLastFrame() {
    pipe_.DropPendingGrab();   // a queued read of the previous session's picture
    std::lock_guard<std::mutex> lk(frame_mu_);
    frame_ = DrawPipeline::Frame{};
    scratch_ = DrawPipeline::Frame{};
    n_.forgotten_frames++;
}

DrawPipeline::Frame DrawIrQueueConsumer::LastFrameHeaderCopy() const {
    std::lock_guard<std::mutex> lk(frame_mu_);
    DrawPipeline::Frame f;
    f.w = frame_.w;
    f.h = frame_.h;
    f.grabbed = frame_.grabbed;
    f.source = frame_.source;
    f.source_fbp = frame_.source_fbp;
    f.source_fallback = frame_.source_fallback;
    f.hash = frame_.hash;
    f.hashed = frame_.hashed;
    return f;
}

}  // namespace gs
}  // namespace ps2r

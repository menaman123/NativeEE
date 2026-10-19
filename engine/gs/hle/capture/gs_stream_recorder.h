// gs/hle/capture/gs_stream_recorder.h -- GsStreamRecorder: a DrawIrSink decorator
// that writes the consumer's complete input stream as a `.gsstream` capture
// (gs_stream_format.h) and forwards every call to the next sink. It is also the
// EE image's VramMirror: every EE-side local-memory call is logged (VramOpLog) and
// written as a kVramOps record right before the next sink event -- the point the
// consumer observes those writes -- then forwarded to the mirror the host had
// installed (the pipelined queue's producer mirror; null under a shared image).
//
// The upload payload is published by the transfer engine only after its
// UploadVRAMTexture push returns, so that job record is held and written WITH
// its pixels at the next event (from the `upload_pixels` hook), as the thread
// queue attaches it. A frame record carries the CRTC snapshot from the `crtc`
// hook at the push. Open() writes the header and the image as it stands
// (kVramInit); Close() flushes the held job. Diagnostics / harness plumbing:
// never a draw, scanout or mirror input.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "engine/gs/hle/ir/draw_ir_sink.h"
#include "engine/gs/hle/present/crtc_classifier.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/gs/vram/vram_mirror.h"
#include "engine/gs/vram/vram_op_log.h"

namespace ps2r {
namespace gs {

class GsStreamRecorder : public DrawIrSink, public VramMirror {
public:
    struct Hooks {
        // Producer side, at the event after an UploadVRAMTexture push: the engine's
        // completed upload copy (nullptr / empty = none).
        std::function<const std::vector<uint8_t>*()> upload_pixels;
        // Producer side, at a frame push: the CRTC snapshot.
        std::function<CrtcClassifier::Snapshot()> crtc;
    };
    struct Counters {
        uint64_t draws = 0, jobs = 0, frames = 0;
        uint64_t vram_ops = 0;        // kVramOps records written
        uint64_t vram_op_bytes = 0;   // their payload bytes
        uint64_t payload_bytes = 0;   // upload pixels written
        uint64_t bytes = 0;           // file bytes written
    };

    // `next` = the sink the stream continues to (may be null: record only);
    // `next_mirror` = the mirror the EE image had (may be null).
    GsStreamRecorder(DrawIrSink* next, VramMirror* next_mirror, Hooks hooks);
    ~GsStreamRecorder() override;
    GsStreamRecorder(const GsStreamRecorder&) = delete;
    GsStreamRecorder& operator=(const GsStreamRecorder&) = delete;

    // Creates `path`, writes the header and `initial`'s bytes as kVramInit. False
    // (nothing recorded, calls still forwarded) when the file cannot be created.
    bool Open(const std::string& path, const VramImage& initial);
    void Close();
    bool IsOpen() const { return file_ != nullptr; }
    const std::string& Path() const { return path_; }
    // The windowed form: open at the `first` frame boundary seen (0 = at once) with
    // `image`'s bytes then, close after `count` complete frames (0 = never). Counted
    // and applied in OnFrame on the pushing thread, so the kVramInit is the EE image
    // at that boundary and the stream after it is consistent with it; the opening
    // boundary's own frame record is not written (its frame was not captured).
    void Arm(const std::string& path, const VramImage* image, uint64_t first, uint64_t count);
    uint64_t FramesSeen() const { return frames_seen_; }

    // DrawIrSink.
    void OnDraw(const DrawIr& draw) override;
    void OnDrawOwned(DrawIr&& draw) override;
    void OnJob(const JobRec& job) override;
    void OnFrame(const JobRec& vsync) override;

    // VramMirror (the EE image's calls).
    void OnWriteStream(uint32_t byte_off, const uint8_t* src, size_t len) override;
    void OnNoteWriteNoBytes(uint32_t byte_off, size_t len) override;
    void OnNoteStreamWrite(uint32_t start, uint32_t size) override;
    void OnSettleWrites() override;
    void OnNoteCpuWrite(uint32_t start, uint32_t size) override;

    const Counters& Count() const { return n_; }
    // "path=<p> draws=<n> jobs=<n> frames=<n> vram_ops=<n> bytes=<n>" (the census report).
    std::string Report() const;

private:
    void WriteRecord(uint32_t kind, const void* payload, size_t size, const void* tail = nullptr,
                     size_t tail_size = 0);
    void WriteDraw(const DrawIr& draw);
    // Everything owed before the next event: the op log, then the held upload job.
    void BeforeEvent();

    DrawIrSink* next_;
    VramMirror* next_mirror_;
    Hooks hooks_;
    std::FILE* file_ = nullptr;
    std::string path_;
    std::vector<char> iobuf_;
    VramOpLog ops_;
    bool pending_valid_ = false;
    JobRec pending_{};
    Counters n_;
    // The display state a windowed open must restate (the jobs that set it came
    // before the window): the last SetWindow and SetFrontBuffer jobs seen.
    bool have_window_ = false, have_front_ = false;
    JobRec last_window_{}, last_front_{};
    // Arm(): the deferred open / close window.
    std::string armed_path_;
    const VramImage* armed_image_ = nullptr;
    uint64_t armed_first_ = 0, armed_count_ = 0;
    bool armed_ = false;
    uint64_t frames_seen_ = 0;   // frame boundaries forwarded (armed or not)
};

}  // namespace gs
}  // namespace ps2r

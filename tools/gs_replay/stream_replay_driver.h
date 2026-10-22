// tools/gs_replay/stream_replay_driver.h -- StreamReplayDriver: the GS consumer ALONE
// over a `.gsstream` capture (engine/gs/hle/capture) -- the renderer throughput
// harness. The capture is everything the live consumer thread received (draws
// with vertices, jobs with upload pixels, frames with the CRTC snapshot, the EE
// image's local-memory writes between records, the image at open); the driver
// feeds it back-to-back into the SAME objects the play composition runs on the
// consumer thread -- DrawIrQueueConsumer over the generic DrawPipeline (no class
// registry, an empty VramLayout, live snapshot on, the scanout base unset until
// the first DISPFB job) over GlDevice with the present capture armed and read
// after every present (the session window's image) and the frames-lane grab +
// hash at the consumer's scale -- with no producer, no pacing and no frame skip.
//
// The file is read by a prefetch thread into a bounded record queue so the
// measured thread does only the consumer's work (the live consumer never reads
// a file). Per frame the driver records the consumer's wall time from the previous
// frame boundary's return to this one's (every record in between included), the
// draw count and the frames-lane hash (DrawIrQueueConsumer::LastFrame().hash). Output:
// the per-frame CSV, the summary JSON (frames, warmup excluded, mean / p50 fps,
// p50 / p99 / max ms, frames over the 12 ms bar, the chained STREAM_HASH), the
// optional PPM dumps of the grab. Exit 0 ok, 2 load failure, 77 no GL.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engine/gs/hle/capture/gs_stream_reader.h"
#include "engine/gs/hle/classrdr/draw_pipeline.h"
#include "engine/gs/hle/present/presenter.h"
#include "engine/gs/hle/present/scanout_composer.h"
#include "engine/gs/hle/queue/draw_ir_queue_consumer.h"
#include "engine/gs/hle/targets/render_target_cache.h"
#include "engine/gs/hle/targets/scale_policy.h"
#include "engine/gs/hle/targets/target_refresher.h"
#include "engine/gs/hle/targets/vram_layout.h"
#include "engine/gs/hle/texsrc/texture_resolver.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/platform/sdl/gl_context.h"
#include "engine/platform/sdl/window.h"
#include "engine/render/backend.h"
#include "engine/render/backends/gl/gl_device.h"
#if defined(PS2R_HAS_METAL)
#include "engine/render/backends/metal/metal_device.h"
#endif
#include "tools/gs_replay/frame_range.h"

namespace ps2r {
namespace tools {

class StreamReplayDriver {
public:
    struct Options {
        const char* in_path = nullptr;
        const char* csv_path = nullptr;    // per-frame rows
        const char* json_path = nullptr;   // the summary
        const char* dump_dir = nullptr;    // --dump-ppm: g<frame>.ppm of the grab for frames in dump_range
        FrameRange dump_range;
        int scale = 1;                     // the internal (target) scale, 1..4
        uint32_t warmup = 30;              // frames excluded from the statistics
        uint32_t max_frames = 0;           // 0 = the whole capture
        bool quiet = false;
        bool deferred_capture = true;      // the play composition's present-capture mode (--sync-capture = off)
        bool phash = false;                // --phash: hash the presented window image per frame (CSV column phash; the
                                           // oracle of a --no-grab run -- ~3 ms a frame of harness time, so opt-in)
        bool gate_trace = false;           // --gate-trace: the image's first gated deferred-deposit completions on stderr
        bool deferred_deposit = true;      // --sync-deposit: the display deposit's GPU read at once (the A/B of the deferral)
        bool grab = true;                  // --no-grab: the play composition with no frames-lane reader (grab_wanted false;
                                           // the per-frame hash column reads 0 -- a timing run, not an identity run)
        bool deferred_grab = false;        // --deferred-grab: the play composition's queued grab (DrawIrQueueConsumer::
                                           // SetGrabDeferred): the row's hash is filled when its grab lands (the next frame
                                           // boundary, or the stream's end) -- row-for-row comparable to the sync run
        bool swap_hidden_window = false;   // --swap: SDL_GL_SwapWindow on the hidden window instead of a flush
        bool cd_barrier = false;           // --cd-barrier: the NV_texture_barrier destination read (not tile-coherent on Apple)
        bool coalesce = true;              // --no-coalesce: every record delivered as it arrives (DrawIrQueueConsumer::SetCoalesce)
        bool metal = false;                // --backend metal: the Metal device (macOS builds; exit 77 when absent)
        bool metal_hw_depth = false;       // --metal-hw-depth: the rasterizer's depth instead of the shader-written one
        uint32_t metal_commit_draws = 256; // --metal-commit-draws N: draws per command buffer (0 = one per frame)
        uint32_t trace_draw = 0xFFFFFFFFu; // --trace-draw N@F: print draw N (0-based) of frame F (registers + vertices) on stderr
        uint32_t trace_draw_frame = 0;
        bool force_shader_blend = false;   // --force-shader-blend: GL's exact shader blend on every blended draw
        int unified_blend = -1;            // --unified-blend 1 / --no-unified-blend 0: GlDevice::SetUnifiedBlend (-1 = the default)
        uint32_t draw_limit = 0;           // --draw-limit N[@F]: only the first N draws of frame F (default: every dumped frame) reach the consumer (0 = all; a bisection aid)
        uint32_t draw_limit_frame = kDrawLimitDumpRange;
    };
    static constexpr uint32_t kDrawLimitDumpRange = 0xFFFFFFFFu;
    struct FrameStat {
        double ms = 0;
        uint32_t draws = 0;
        uint32_t jobs = 0;
        uint64_t hash = 0;
        uint64_t phash = 0;      // the presented window image read at this frame's PresentWindow (0 = none read)
        bool grabbed = false;
        int w = 0, h = 0;        // the grab's size (the STREAM_HASH chain folds it in)
        uint64_t readbacks = 0, copies = 0, draw_calls = 0;   // backend census deltas
        uint64_t submits = 0, gpu_ns = 0;                     // (the GPU time lands when its command buffers complete)
    };
    static constexpr uint64_t kHashSeed = 1469598103934665603ull;
    static constexpr uint64_t kHashPrime = 1099511628211ull;
    static constexpr double kFrameBarMs = 12.0;

    explicit StreamReplayDriver(const Options& opt);
    ~StreamReplayDriver();
    StreamReplayDriver(const StreamReplayDriver&) = delete;
    StreamReplayDriver& operator=(const StreamReplayDriver&) = delete;

    int Run();
    const std::vector<FrameStat>& Frames() const { return frames_; }
    uint64_t StreamHash() const { return stream_hash_; }

private:
    bool CreateContext();
    bool DrawLimitApplies(uint32_t frame_index) const;
    void BuildScene();
    void Teardown();
    // The prefetch thread: records from the reader into the bounded queue.
    void ReaderMain(gs::GsStreamReader* reader);
    void TraceDraw(const gs::DrawIr& d);
    bool PopRecord(gs::GsStreamReader::Record* out);
    void PresentWindow();   // the device's swap callback: the window image read (as play) + swap
    void OnFrameDone(const gs::JobRec& vsync);
    void TakeGrab(FrameStat* st, uint32_t row);   // the consumer's published grab into the row (+ its --dump-ppm)
    void ChainHash(const FrameStat& st);
    void WriteOutputs() const;
    static void WritePpm(const std::string& path, const std::vector<uint8_t>& rgba, int w, int h);
    static double NowMs();

    Options opt_;
    platform::Window win_;
    platform::GlContext ctx_;
    render::GlDevice gl_;
#if defined(PS2R_HAS_METAL)
    std::unique_ptr<render::MetalDevice> metal_;
#endif
    render::Backend* be_ = nullptr;   // gl_ or *metal_
    std::string renderer_name_, api_name_;
    gs::VramLayout layout_{};
    std::unique_ptr<gs::VramImage> vram_;
    std::unique_ptr<gs::ScalePolicy> scale_;
    std::unique_ptr<gs::RenderTargetCache> cache_;
    std::unique_ptr<gs::TargetRefresher> refresher_;
    std::unique_ptr<gs::TextureResolver> resolver_;
    std::unique_ptr<gs::ScanoutComposer> scanout_;
    std::unique_ptr<gs::Presenter> presenter_;
    std::unique_ptr<gs::DrawPipeline> pipe_;
    std::unique_ptr<gs::DrawIrQueueConsumer> consumer_;
    // The hooks' sources: the last upload record's pixels, the frame record's CRTC.
    std::vector<uint8_t> last_payload_;
    bool last_payload_valid_ = false;
    gs::CrtcClassifier::Snapshot crtc_{};
    std::vector<uint8_t> presented_;
    uint64_t presented_hash_ = 0;
    uint64_t presents_read_ = 0;
    // Per-frame bookkeeping.
    std::vector<FrameStat> frames_;
    double frame_t0_ = 0;
    uint32_t frame_draws_ = 0, frame_jobs_ = 0;
    render::CallCensus census_at_frame_{};
    uint64_t stream_hash_ = kHashSeed;
    // --deferred-grab: the row whose grab is still queued (-1 = none) and the
    // consumer's completion count at the last boundary.
    long pending_row_ = -1;
    uint64_t grabs_completed_seen_ = 0, grabs_deferred_seen_ = 0;
    uint64_t vram_ops_ = 0, uploads_ = 0;
    double total_ms_ = 0;
    // The prefetch queue (reader thread -> the consumer loop).
    static constexpr size_t kPrefetchDepth = 2048;
    static constexpr size_t kBatch = 64;   // records per reader hand-over
    std::mutex q_mu_;
    std::condition_variable q_cv_;
    std::deque<gs::GsStreamReader::Record> q_;
    std::deque<gs::GsStreamReader::Record> local_;   // the consumer loop's taken run
    bool reader_done_ = false;
    bool stop_reader_ = false;
    std::thread reader_thread_;
    std::string reader_error_;
};

}  // namespace tools
}  // namespace ps2r

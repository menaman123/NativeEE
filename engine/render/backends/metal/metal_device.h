// render/backends/metal/metal_device.h -- MetalDevice: the Metal Backend (Apple GPUs).
//
// One MTLDevice and one command queue; one command buffer open at a time (committed by
// EndFrame, Present, FlushCommands, a synchronous readback or a vertex-ring wrap). A
// surface is an MTLTexture in SHARED storage: Rgba8 (renderable = colour attachment +
// shader read), R8, Depth32f (sampleable depth attachment), DepthStencil24_8 (a
// Depth32Float_Stencil8 attachment). Draws run inside a render command encoder kept open
// across consecutive draws to the same (colour, depth) pair; a target change, a clear, a
// blit (copy / busy-texture upload / readback) or a present ends it. The register program
// reads its destination through the framebuffer fetch, so the shader-side blend, DATE and
// the FBMSK merges need no destination copy (MetalDrawExecutor); the copy texture is kept
// for the UV-addressed self reads only. Uploads go straight into the texture when no
// in-flight command buffer touched it, else through the staging ring + a blit (GPU order).
// Readbacks drain the queue (shared storage: getBytes after completion); the staging pair
// completes immediately as the GL arm's does. Present draws the scanout quad into an
// owned window-sized capture texture whose rows are stored bottom-up (the GL readback
// order) and reads it back (synchronously, or one present late behind the command
// buffer's completion when deferred) for ReadPresented; a CAMetalLayer, when handed over,
// receives the same image. Memory layout matches the GL arm (PS2 row 0 = texture row 0;
// the vertex program flips NDC y), so every Readback / Upload / scissor / position agrees
// byte for byte with the GL device. Metal objects are opaque pointers here (retained;
// manual reference counting in the .mm) so this header compiles as plain C++.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/render/backend.h"
#include "engine/render/backends/metal/metal_pipeline_cache.h"
#include "engine/render/texture_filter_selection.h"

namespace ps2r {
namespace render {

class MetalDrawExecutor;

struct MetalDeviceSettings {
    uint8_t scale = 1;          // default surface scale
    bool shader_depth = true;   // the register program writes the window depth itself (else the rasterizer's)
    uint32_t commit_draws = 256; // draws per command buffer before a mid-frame commit (0 = commit at frame end only)
    // Per-pass GPU timestamps (MTLCounterSampleBuffer at the stage boundaries of every render /
    // blit pass) folded per (surface, kind) and per frame: the gpu_pass= / gpu_frame= rows.
    // Diagnostics: off by default (the sampling costs a little per pass).
    bool gpu_timing = false;
    // The exact fast arms of the register program (kFstQ1 / kTexPow2*: divides folded where the IEEE result is
    // the same). Off = the plain program (the A/B oracle; output identical by construction).
    bool exact_arms = true;
    // Attachments (renderable colour, Depth32f) in MTLStorageModePrivate: the GPU's lossless framebuffer
    // compression applies and the tile loads / stores shrink; uploads into them go through the staging ring,
    // readbacks through the blit (both already the GPU-busy paths). Off = every surface Shared (the A/B).
    bool private_attachments = false;
    // DIAGNOSTIC (measurement only; changes every pixel): 1 = the register program returns the vertex colour
    // right away (the fill + per-draw floor); 2 = ... after the texture block only. 0 = the real program.
    int diag_fragment = 0;
};

// One frame's (Present to Present) slice of the backend's work: the call census deltas and
// the GPU busy time of the command buffers committed inside it (summed when they complete).
struct MetalFrameStats {
    uint32_t draw_calls = 0;
    uint32_t program_binds = 0;      // pipeline-state switches
    uint32_t framebuffer_binds = 0;  // render passes opened (target switches + clears + re-opens)
    uint32_t texture_binds = 0;
    uint32_t uploads = 0;
    uint32_t upload_bytes = 0;
    uint32_t readbacks = 0;
    uint32_t readback_bytes = 0;
    uint32_t copies = 0;
    uint32_t submits = 0;
    uint32_t clears = 0;
    uint64_t gpu_ns = 0;             // GPU busy of the frame's command buffers (completion handlers; overlapping buffers over-count)
    uint64_t gpu_span_ns = 0;        // the frame's GPU wall: last GPUEndTime - first GPUStartTime over its command buffers
    double gpu_start_s = 0, gpu_end_s = 0;   // (internal) the span's bounds
    uint64_t frag_invocations = 0;   // gpu_timing + statistics: fragment shader invocations of the frame's render passes
    uint64_t gpu_wait_ns = 0;        // CPU time spent waiting for the GPU inside the frame
    uint64_t pass_frag_ns = 0;       // gpu_timing: sum of the frame's fragment-stage spans
    uint64_t pass_vtx_ns = 0;        // gpu_timing: sum of the frame's vertex-stage spans
    uint64_t pass_blit_ns = 0;       // gpu_timing: sum of the frame's blit-encoder spans
};

// gpu_timing: the per-(surface, kind) fold over the run.
struct MetalPassStats {
    uint32_t surface = 0;            // SurfaceId.id of the colour attachment (0 = present / blit)
    uint32_t width = 0, height = 0;  // physical texels
    uint8_t kind = 0;                // 0 register-program pass, 1 fullscreen (pack5551 / alpha lane), 2 present, 3 blit
    uint64_t passes = 0;
    uint64_t draws = 0;
    uint64_t vtx_ns = 0;
    uint64_t frag_ns = 0;            // blit passes: the encoder span
    uint64_t frag_invocations = 0;   // fragment shader invocations (statistics counters; 0 when unsupported)
    uint64_t frags_passed = 0;       // fragments that passed every test (statistics counters)
};

// One surface's Metal objects.
struct MetalSurface {
    SurfaceDesc desc;
    void* tex = nullptr;          // id<MTLTexture> (retained)
    void* view = nullptr;         // id<MTLTexture> view over levels [0, levels + synth_levels] (the sampled chain)
    uint64_t last_gpu_use = 0;    // serial of the last command buffer that touched it
    uint8_t levels = 0;           // authored mip levels
    uint8_t synth_levels = 0;     // synthesized tail levels
    uint8_t mip_levels_alloc = 0; // storage levels (0 = level 0 only)
    bool priv = false;            // MTLStorageModePrivate (attachments): no replaceRegion / getBytes -- staging + blits
};

class MetalDevice : public Backend {
public:
    explicit MetalDevice(const MetalDeviceSettings& settings = MetalDeviceSettings{});
    ~MetalDevice() override;

    // Creates the system default device + queue; `swap` is called at the end of Present.
    bool Init(std::function<void()> swap);
    void Shutdown();
    bool IsInitialized() const { return initialized_; }
    const char* DeviceName() const { return device_name_.c_str(); }
    // video.backend=auto's probe: the system default device exists and is MTLGPUFamilyApple7 or
    // newer (Apple silicon: framebuffer fetch, the arm this backend is measured on). Creates no
    // queue; `name` (optional) receives the device name.
    static bool Recommended(std::string* name = nullptr);

    Caps QueryCaps() const override;
    SurfaceId CreateSurface(const SurfaceDesc& desc) override;
    void DestroySurface(SurfaceId id) override;
    SurfaceDesc DescribeSurface(SurfaceId id) const override;
    void Clear(SurfaceId color, SurfaceId depth, uint32_t rgba, float z, uint32_t flags) override;
    void Upload(SurfaceId id, const Rect& rect, const void* bytes, size_t stride) override;
    void UploadLevel(SurfaceId id, int level, int w, int h, const void* rgba) override;
    void FinishMipChain(SurfaceId id, int authored_levels, bool synthesize_tail) override;
    DrawResult Draw(const gs::DrawIr& ir, const gs::TextureSource& tex, const DrawParams& params) override;
    void ClearStencil(SurfaceId stencil, uint8_t value) override;
    void DrawConstQuad(SurfaceId color, SurfaceId stencil, const Rect& scissor, uint32_t rgba, bool write_alpha,
                       const StencilState& st) override;
    void CopyRect(SurfaceId src, const Rect& src_rect, SurfaceId dst, const Rect& dst_rect, uint32_t transform) override;
    void WriteAlphaLane(SurfaceId dst, const Rect& dst_rect, SurfaceId lane, const Rect& exclude) override;
    void Readback(SurfaceId id, const Rect& rect, void* out, size_t stride) override;
    FenceId BeginReadback(SurfaceId id, const Rect& rect) override;
    // The point-sampled reads (backend.h): a NearestColor / NearestDepth pass into a pooled logical-size
    // texture (Private), then the blit into the readback buffer. Rgba8 and Depth32f sources; a rect
    // not a multiple of `step` or another format returns false / a null fence (the caller's physical read).
    bool ReadbackNearest(SurfaceId id, const Rect& rect, int step, void* out, size_t stride, int sub_x = 0, int sub_y = 0) override;
    FenceId BeginReadbackNearest(SurfaceId id, const Rect& rect, int step, int sub_x = 0, int sub_y = 0) override;
    bool ReadbackReady(FenceId fence) override;
    bool PollReadback(FenceId fence, void* out, size_t stride) override;
    void Present(const ScanoutDesc& desc) override;
    void ArmPresentCapture(bool armed) override;
    bool ReadPresented(std::vector<uint8_t>* rgba, int* w, int* h) override;
    const CallCensus* Census() const override;
    void BeginFrame() override;
    void EndFrame() override;
    void SetWindowSize(int w, int h) override;
    void SetPresentCaptureDeferred(bool deferred) override;
    bool PresentCaptureDeferred() const { return present_capture_deferred_; }
    uint64_t PresentCaptureWaits() const { return present_capture_waits_; }
    void SetPresentCaptureWaitNs(uint64_t ns) override { present_capture_wait_ns_ = ns; }
    void SetPresentCaptureGate(std::function<bool()> gate) override { present_capture_gate_ = std::move(gate); }
    int PresentedLag() const override { return presented_lag_; }
    void FlushCommands() override;
    void SetTextureFilter(std::shared_ptr<const TextureFilterSelection> selection) override {
        texture_filter_ = std::move(selection);
    }
    const TextureFilterSelection* TextureFilter() const { return texture_filter_.get(); }
    int MaxAnisotropy() const { return max_anisotropy_; }
    // A CAMetalLayer (SDL_Metal_GetLayer) that Present also draws the scanout into; null = offscreen.
    void SetMetalLayer(void* layer);

    // ---- the per-frame / per-pass statistics (the gpu= rows; any thread after the run, or the device's thread) ----
    // Every frame closed so far (a frame = Present to Present), in order. The GPU busy of a frame's
    // command buffers lands when they complete: read after Shutdown / a Commit(wait) for final numbers.
    std::vector<MetalFrameStats> FrameStats() const;
    // gpu_timing: the per-(surface, kind) fold over the run, unsorted.
    std::vector<MetalPassStats> PassStats() const;
    bool GpuTimingActive() const { return timing_on_; }
    uint64_t PassesUnsampled() const { return passes_unsampled_; }

    // ---- the executor's surface (every call below runs on the device's thread) ----
    const MetalSurface* Surface(SurfaceId id) const;
    MetalSurface* SurfaceMut(SurfaceId id);
    CallCensus& CensusMut() { return census_; }
    // A rasterising call issued: census_.draw_calls++ and the open pass's draw count (gpu_timing).
    void NoteDraw();
    MetalPipelineCache& Pipelines() { return pipelines_; }
    const MetalDeviceSettings& Settings() const { return settings_; }
    struct TextureFilterCensus {
        uint64_t owned_draws = 0;
        uint64_t enhanced_draws = 0;
        uint64_t hw_lod_draws = 0;
    };
    TextureFilterCensus& FilterCensus() { return filter_census_; }
    const TextureFilterCensus& FilterCensus() const { return filter_census_; }
    // id<MTLRenderCommandEncoder> over the colour surface with `depth` attached (the device's
    // scratch depth of the colour's size when `depth` is none, so the pipeline layout is
    // constant). Reuses the open encoder for the same pair; `clear_*` end it and start a pass
    // whose load action clears. Null when the colour surface is not renderable.
    void* BeginTargetPass(SurfaceId color, SurfaceId depth, bool clear_color, bool clear_depth, uint32_t clear_rgba,
                          float clear_z);
    // The executor's split from INSIDE a pass (after a draw) once the open command buffer holds
    // settings_.commit_draws draws: ends the encoder (a tile store, ~2 MB at scale 1) and commits; the
    // next draw re-opens the pass with load actions. The GPU works on the committed chunk while
    // the CPU encodes the next.
    void SplitPassIfDue();
    // The open encoder (null when none) and its attachments.
    void* CurrentEncoder() const { return encoder_; }
    void EndEncoder();
    // A blit command encoder (ends the render encoder); valid until the next render pass.
    void* BlitEncoder();
    // Marks the surface as touched by the current command buffer.
    void Touch(MetalSurface& s);
    // Vertex ring: copies n vertices (32 bytes each) and returns the first-vertex index;
    // the ring buffer (id<MTLBuffer>) is bound at offset 0 on vertex buffer slot 0.
    int UploadVertices(const void* verts, size_t n);
    void* VertexRing() const { return vertex_ring_; }
    void* DummyTexture() const { return dummy_tex_; }
    // The texture a draw samples: the level-limited view when a mip chain was finished.
    static void* SampleTexture(const MetalSurface& s) { return s.view ? s.view : s.tex; }
    void* DummyDepthTexture() const { return dummy_depth_; }
    // The attachment -> copy-texture copy (physical texels, same rect) through a blit.
    void CopyTexture(MetalSurface& src, MetalSurface& dst, int x, int y, int w, int h);
    // Rows of `tex` (id<MTLTexture>) into `out` after the queue drained.
    void ReadTexture(MetalSurface& s, const Rect& rect, void* out, size_t stride);

private:
    // One BeginReadback: the blit's destination buffer (shared storage, pooled) and the
    // serial of the command buffer that carries the blit; ready once that serial completed.
    struct PendingReadback {
        void* buf = nullptr;        // id<MTLBuffer> (retained; returned to free_readback_bufs_ on Poll)
        size_t stride = 0;
        int rows = 0;
        uint64_t serial = 0;
    };
    struct CaptureSlot {
        void* tex = nullptr;        // id<MTLTexture>
        void* buf = nullptr;        // id<MTLBuffer>, shared: the slot's rows, blitted by the present's command buffer
        int w = 0, h = 0;
        uint64_t serial = 0;        // the command buffer that drew it
        bool pending = false;
    };
    void* CommandBuffer();          // the open command buffer (creates one)
    void Commit(bool wait);
    // Commits the open command buffer once it holds settings_.commit_draws draws (called between
    // encoders): the GPU runs the frame's early passes while the CPU encodes the rest.
    void MaybeCommit();
    bool GpuIdle(uint64_t serial) const { return serial <= completed_serial_.load(std::memory_order_acquire); }
    void WaitSerial(uint64_t serial);
    void* ScratchDepth(uint32_t w, uint32_t h);
    bool EnsureCapture(CaptureSlot& slot, int w, int h);
    void CompleteCapture(CaptureSlot& slot, bool wait, int lag = 1);
    bool WaitCaptureBounded(const CaptureSlot& slot);
    void StagingUpload(MetalSurface& s, int level, const Rect& rect, const void* bytes, size_t src_stride, size_t bpp);
    // A shared buffer of at least `bytes` for the colour readbacks (the blit encoder linearises
    // the texture on the GPU; the CPU copies rows out after completion).
    void* ReadbackBuffer(size_t bytes);
    // A pooled Private texture of (w, h, format) whose last use the GPU has finished (or a new one), for
    // the nearest-pick pass; `serial` on return = the serial it is handed to.
    struct PickTexture {
        void* tex = nullptr;     // id<MTLTexture> (retained)
        uint32_t w = 0, h = 0;
        bool depth_source = false;   // R32Float (a depth source) vs RGBA8
        uint64_t serial = 0;
    };
    PickTexture* AcquirePickTexture(uint32_t w, uint32_t h, bool depth_source);
    // Encodes the pick pass of `src` over `rect` at `step` into a pick texture; null when refused.
    PickTexture* EncodeNearestPick(MetalSurface& src, const Rect& rect, int step, int sub_x, int sub_y);
    std::vector<PickTexture> pick_pool_;
    void ReleaseSurface(MetalSurface& s);
    void DrawFullscreen(MetalProgram program, void* color_tex, const Rect& scissor, const void* uniforms, size_t uniform_bytes,
                        void* tex0, uint32_t w, uint32_t h);

    MetalDeviceSettings settings_;
    bool initialized_ = false;
    std::function<void()> swap_;
    std::string device_name_;
    void* device_ = nullptr;        // id<MTLDevice>
    void* queue_ = nullptr;         // id<MTLCommandQueue>
    void* cb_ = nullptr;            // id<MTLCommandBuffer> open
    void* encoder_ = nullptr;       // id<MTLRenderCommandEncoder> open
    void* blit_ = nullptr;          // id<MTLBlitCommandEncoder> open
    void* enc_color_ = nullptr;     // the open encoder's attachments
    void* enc_depth_ = nullptr;
    uint64_t serial_ = 1;           // the open / next command buffer's serial
    std::atomic<uint64_t> completed_serial_{0};
    std::atomic<uint64_t> gpu_ns_{0};   // summed GPU busy time of the completed command buffers
    uint64_t draws_at_commit_ = 0;
    MetalPipelineCache pipelines_;
    std::unique_ptr<MetalDrawExecutor> executor_;
    void* dummy_tex_ = nullptr;
    void* dummy_depth_ = nullptr;
    std::unordered_map<uint64_t, void*> scratch_depth_;   // (w << 32 | h) -> id<MTLTexture>
    // The vertex ring.
    void* vertex_ring_ = nullptr;   // id<MTLBuffer>, shared
    size_t ring_offset_ = 0;
    uint64_t ring_wrap_serial_ = 0;
    // The staging ring for GPU-busy uploads.
    void* staging_ = nullptr;       // id<MTLBuffer>, shared
    size_t staging_offset_ = 0;
    uint64_t staging_wrap_serial_ = 0;
    void* readback_buf_ = nullptr;  // id<MTLBuffer>, shared (grown on demand)
    size_t readback_buf_bytes_ = 0;
    void* present_sampler_nearest_ = nullptr;
    void* present_sampler_linear_ = nullptr;
    void* layer_ = nullptr;         // CAMetalLayer*
    int window_w_ = 0, window_h_ = 0;
    bool present_capture_ = false;
    bool present_capture_deferred_ = false;
    uint64_t present_capture_waits_ = 0;
    CaptureSlot capture_[2];
    int capture_cur_ = 0;
    std::vector<uint8_t> presented_;
    int presented_w_ = 0, presented_h_ = 0;
    uint64_t present_capture_wait_ns_ = 0;   // the bounded wait for THIS present's capture (0 = deferred only)
    std::function<bool()> present_capture_gate_;   // false = no bounded wait at this present (backend.h)
    int presented_lag_ = 0;                  // the lag of presented_ (0 = this present, 1 = the previous)
    bool presented_fresh_ = false;           // presented_ not yet handed over by ReadPresented
    std::unordered_map<uint32_t, MetalSurface> surfaces_;
    uint32_t next_surface_ = 1;
    std::unordered_map<uint64_t, PendingReadback> readbacks_;
    std::vector<std::pair<void*, size_t>> free_readback_bufs_;   // (id<MTLBuffer>, capacity) returned by Poll
    uint64_t next_fence_ = 1;
    int max_texture_size_ = 0;
    int max_anisotropy_ = 16;
    std::shared_ptr<const TextureFilterSelection> texture_filter_;
    TextureFilterCensus filter_census_;
    mutable CallCensus census_;

    // ---- per-frame statistics ----
    // The frame ordinal the open / next command buffer belongs to (advanced by Present).
    uint32_t frame_ordinal_ = 0;
    CallCensus frame_census_base_;           // the census at the frame's start
    mutable std::mutex stats_mu_;            // guards frames_ / pass_stats_ (completion handlers write them)
    std::vector<MetalFrameStats> frames_;    // frames_[i] = frame i (grown by Present and by completion handlers)
    void CloseFrameStats();                  // Present: the census delta of the frame that ends
    void AddFrameGpu(uint32_t ordinal, uint64_t gpu_ns, double start_s, double end_s, uint64_t frag_ns, uint64_t vtx_ns,
                     uint64_t blit_ns, uint64_t frag_invocations);
    // ---- gpu_timing (MTLCounterSampleBuffer) ----
    struct PassMeta {
        uint32_t surface = 0, width = 0, height = 0;
        uint8_t kind = 0;
        uint32_t draws = 0;
        uint32_t sample_index = 0;           // first of 4 (render: vtx start/end, frag start/end) or 2 (blit)
    };
    struct CbTiming {                        // the open command buffer's samples
        void* sample_buf = nullptr;          // id<MTLCounterSampleBuffer> (retained; pooled)
        void* stat_buf = nullptr;            // id<MTLCounterSampleBuffer> of the statistics set, same indices (null = unsupported)
        uint32_t used = 0;                   // sample indices consumed
        std::vector<PassMeta> passes;
        int32_t open_pass = -1;              // index into passes of the pass whose draw count is still growing
    };
    bool timing_on_ = false;
    void* timestamp_counter_set_ = nullptr;  // id<MTLCounterSet> (retained)
    void* statistic_counter_set_ = nullptr;  // id<MTLCounterSet> (retained; null when the device has none)
    std::vector<void*> free_sample_bufs_;    // id<MTLCounterSampleBuffer> pool (under stats_mu_: handlers return them)
    std::vector<void*> free_stat_bufs_;      // the statistics buffers' pool
    bool TakeSampleBuffers();                // cb_timing_ gets a timestamp (+ statistics) buffer from the pools / new
    std::atomic<uint32_t> handlers_pending_{0};   // completion handlers not yet run (Shutdown waits for 0)
    CbTiming cb_timing_;
    uint64_t passes_unsampled_ = 0;
    double gpu_ticks_to_ns_ = 1.0;
    std::unordered_map<uint64_t, MetalPassStats> pass_stats_;   // key = surface << 8 | kind
    // Attaches the sample-buffer slots to a render pass (null-safe when timing is off); the
    // returned PassMeta* is the pass's record (draw count grows until the encoder ends).
    void AttachRenderTiming(void* render_pass_desc, uint32_t surface, uint32_t w, uint32_t h, uint8_t kind);
    void* NewBlitEncoder();                  // a blit encoder with its timing attached (timing off: plain)

};

}  // namespace render
}  // namespace ps2r

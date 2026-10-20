// render/review/frame_review.h -- FrameReview: the display-rate re-view of a
// completed game frame (video.render_rate=display, Charter 2 A3).
//
// A Backend proxy under the scene's allocation layer. Pass-through when idle.
// While recording (the presenter runs at the display's rate), every backend
// command of the frame being drawn -- Clear, Draw (the DrawIr, its texture source
// and draw parameters copied), ClearStencil, DrawConstQuad, CopyRect and the
// final Present -- is kept in order; the frame's Present closes the list. The
// game thread publishes, in stream order after each frame's present, the camera
// that frame was drawn with (NoteCamera: the title's world->camera matrix, its
// camera->screen matrix and the boundary's wall instant). The TWO latest frames
// with a camera are retained: T (previous) and T+1 (latest).
//
// Replay(now) re-issues T's command list with the camera interpolated between
// T and T+1 at the wall phase of `now` (rotation slerp, position lerp -- the one
// tick of presentation latency OpenGOAL-style decoupling takes; a cut (a
// position jump or a rotation beyond the thresholds) replays T at its own
// camera). The game tick is never touched: the re-view is a per-vertex
// re-projection of the RECORDED screen-space vertices --
//     h = (x_gs, y_gs, z, 1) . K,  K = P^-1 . V_T^-1 . C(phase) . P
// (P the frame's camera->screen matrix, V_T its world->camera matrix, C the
// interpolated world->camera; row-vector convention as the title's) -- applied
// to the draws the classifier marks Space::Screen (the camera-projected
// families); Space::Fixed draws (HUD, text, sprites, screen-space passes, the
// model/raw route) are re-issued as recorded, so they draw at T. The STQ of a
// re-viewed vertex is rescaled by the depth ratio (perspective-correct).
//
// The replay never touches the live frame: every surface T WROTE (its targets,
// their Cd copies, the display image) is redirected to a SHADOW surface of the
// same description, allocated here; reads of surfaces T did not write go to the
// originals (textures, CLUTs). Surfaces destroyed since T are skipped (counted
// stale). Uploads are not replayed (the originals hold the texels). The replay's
// Present goes to the window exactly as the frame's did; the host decides
// whether the tick's own Present reaches the window (TickPresentSuppressed).
//
// Threads: every mutation on the backend owner (the GS consumer); Armed() /
// Replaying() are atomics for the presenter. Census (Count): frames recorded,
// commands, replays, reviewed / fixed draws, stale skips, cuts, camera samples,
// replay microseconds (last / max), tick presents passed / suppressed.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/texsrc/texture_source.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace render {

class FrameReview final : public Backend {
public:
    using Clock = std::chrono::steady_clock;
    struct Camera {
        bool valid = false;
        Clock::time_point at{};          // the frame boundary's wall instant (the pacer's edge)
        double to_camera[16] = {};       // world -> camera (rows = basis, row 3 = translation)
        double camera_to_screen[16] = {};// camera -> screen (the GS 12.4 coordinate space)
    };
    enum class Space : uint8_t { Fixed = 0, Screen = 1 };
    using Classifier = std::function<Space(const gs::DrawIr&)>;
    struct Replayed {
        bool presented = false;          // a re-viewed picture reached the window
        bool cut = false;                // T replayed at its own camera (a camera cut)
        double phase = 0;
        uint64_t tick = 0;               // T's camera ordinal
        double position[3] = {0, 0, 0};  // the camera's world position shown
        uint32_t reviewed_draws = 0, fixed_draws = 0, stale = 0;
        uint64_t micros = 0;
    };
    struct Census {
        uint64_t frames = 0;             // frames recorded (closed by a Present)
        uint64_t frames_uncamera = 0;    // closed frames replaced before a camera arrived
        uint64_t empty_presents = 0;     // presents with no draw since the last (display ticks)
        uint64_t cameras = 0;            // NoteCamera calls
        uint64_t cameras_unframed = 0;   // cameras that found no closed frame (a dropped frame)
        uint64_t replays = 0;            // Replay calls that presented
        uint64_t replays_refused = 0;    // Replay calls with nothing to show
        uint64_t cuts = 0;
        uint64_t reviewed_draws = 0, fixed_draws = 0, stale_commands = 0, unprojectable_draws = 0;
        uint64_t tick_presents = 0;      // the tick's own Present reached the window
        uint64_t tick_suppressed = 0;    // ... or was held because a replay can show the frame
        uint64_t stale_cameras = 0;      // Armed() reads with the latest camera older than kFreshness (a lagging consumer)
        uint64_t commands_last = 0;
        uint64_t replay_us_last = 0, replay_us_max = 0, replay_us_sum = 0;
        uint64_t shadows = 0;            // shadow surfaces alive
        uint64_t faults = 0;             // replays a backend command refused (no picture that edge)
        // The live frame's own backend draw time on this thread (microseconds; the
        // budget a re-view competes with): last closed frame, max, sum over frames.
        uint64_t live_draw_us_last = 0, live_draw_us_max = 0, live_draw_us_sum = 0;
        // The Present calls' own time (the display quad + the host's window readback):
        // the live frames' and the replays' -- the GPU sync each present pays.
        uint64_t live_present_us_sum = 0, replay_present_us_sum = 0, live_presents = 0;
        // The consumer's wall time from a frame's first recorded command to its
        // Present (the pipeline's work between backend calls and any re-view run
        // in between included): what the live frame costs the consumer.
        uint64_t frame_span_us_sum = 0, frame_span_us_max = 0;
    };
    // Thresholds a camera step beyond which is a CUT (T replays at its own camera).
    static constexpr double kCutTravel = 2.0;        // world units per tick
    static constexpr double kCutAngleDegrees = 30.0; // rotation per tick

    explicit FrameReview(Backend& next);
    ~FrameReview() override;
    FrameReview(const FrameReview&) = delete;
    FrameReview& operator=(const FrameReview&) = delete;

    // Backend owner. Off: pass-through; the retained frames and shadows are released.
    void SetRecording(bool on);
    bool Recording() const { return recording_.load(std::memory_order_relaxed); }
    void SetClassifier(Classifier classifier) { classify_ = std::move(classifier); }
    // Backend owner, in stream order after the frame's Present: that frame's camera.
    void NoteCamera(const Camera& camera);
    // Presenter: a replay can show a re-viewed picture now -- two camera'd frames
    // are retained AND the latest camera is FRESH (its boundary within kFreshness
    // of now): a consumer that fell behind the stream holds stale cameras, and a
    // re-view of them would clamp at the tick's camera; the tick's own frames (one
    // tick newer) go to the window instead until the consumer catches up.
    bool Armed() const { return ArmedAt(Clock::now()); }
    bool ArmedAt(Clock::time_point now) const;
    static constexpr std::chrono::nanoseconds kFreshness{30000000};   // 1.8 ticks at 60 Hz
    bool Replaying() const { return replaying_.load(std::memory_order_relaxed); }
    // The host's Present policy: while armed, the tick's own Present is held
    // (the window shows re-viewed frames only). Counted either way.
    bool TickPresentSuppressed();
    // Backend owner: re-issue T at the camera of `now` (+ the host's lookahead).
    Replayed Replay(Clock::time_point now);
    Census Count() const;

    // The interpolated world->camera between two samples (exposed for tests):
    // rotation slerp of the 3x3 (rows = basis), position lerp of the inverse's
    // translation. False when a matrix is singular. `cut` reports the thresholds.
    static bool Interpolate(const double* a16, const double* b16, double phase, double out16[16], bool* cut);
    // h = (x_gs, y_gs, z, 1) . K on one recorded vertex; false when w' <= 0.
    static bool ReviewVertex(const double K[16], float off_x, float off_y, double z_max, bool rescale_stq,
                             gs::VertexRec& v);
    static bool Invert4(const double m[16], double out[16]);
    static void Multiply4(const double a[16], const double b[16], double out[16]);

    // Backend.
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
    void Readback(SurfaceId id, const Rect& rect, void* out, size_t stride) override;
    FenceId BeginReadback(SurfaceId id, const Rect& rect) override;
    bool ReadbackReady(FenceId fence) override;
    bool PollReadback(FenceId fence, void* out, size_t stride) override;
    void Present(const ScanoutDesc& desc) override;
    void ArmPresentCapture(bool armed) override;
    bool ReadPresented(std::vector<uint8_t>* rgba, int* w, int* h) override;
    void BeginFrame() override;
    void EndFrame() override;

private:
    enum class Kind : uint8_t { Clear, Draw, ClearStencil, ConstQuad, Copy, Present };
    struct Command {
        Kind kind = Kind::Clear;
        SurfaceId a, b;                  // Clear: color/depth; ClearStencil: a; ConstQuad: color/stencil; Copy: src/dst
        Rect rect, rect2;
        uint32_t rgba = 0, flags = 0, transform = 0;
        float z = 0;
        bool write_alpha = false;
        StencilState stencil;
        ScanoutDesc scanout;
        Space space = Space::Fixed;
        std::unique_ptr<gs::DrawIr> ir;
        gs::TextureSource tex{};
        DrawParams params{};
    };
    struct Frame {
        bool valid = false;
        std::vector<Command> commands;
        std::unordered_set<uint32_t> written;   // surfaces the frame rendered into
        uint32_t draws = 0;
        uint64_t draw_us = 0;                    // backend time inside the live draws
        Clock::time_point first_at{};            // the first recorded command's instant
        Camera camera;                           // valid once NoteCamera attached it
        uint64_t ordinal = 0;
    };
    struct Shadow { SurfaceId id; SurfaceDesc desc; };

    void Record(Command&& command);
    void CloseFrame(const ScanoutDesc& desc);
    void ReleaseRetained();
    void ReleaseShadows();
    void UpdateArmed();
    SurfaceId Redirect(const Frame& frame, SurfaceId id, bool* stale);
    SurfaceId ShadowOf(SurfaceId id, bool* stale);
    void Issue(const Frame& frame, const Command& command, const double* K, bool review, Replayed& out,
               std::unordered_map<const CdCopyState*, CdCopyState>& trackers);

    Backend& next_;
    Classifier classify_;
    std::atomic<bool> recording_{false}, armed_{false}, replaying_{false};
    std::atomic<int64_t> latest_at_ns_{0};       // the latest camera's boundary instant (steady ns)
    mutable std::atomic<uint64_t> stale_cameras_{0};   // ArmedAt reads refused for staleness (main and consumer)
    bool in_replay_ = false;
    Frame current_, closed_, previous_, latest_;
    uint64_t camera_ordinal_ = 0;
    std::unordered_set<uint32_t> alive_;         // surfaces created through this proxy and not destroyed
    std::unordered_map<uint32_t, Shadow> shadows_;
    Census n_;
};

}  // namespace render
}  // namespace ps2r

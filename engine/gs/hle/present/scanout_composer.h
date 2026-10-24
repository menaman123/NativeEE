// gs/hle/present/scanout_composer.h -- ScanoutComposer: the CRTC-side state the
// render path needs (scanout base, DISPFB width / format, content dimensions),
// the display grab and the display quad.
//
// GrabDisplay reads the colour attachment of the target at the scanout base. When
// no target exists there, the grab policy decides: LocalMemory (the live root's)
// decodes the DISPFB tuple (base, FBW, PSM) over the content dims out of local
// memory -- what the CRTC scans out when no draw has yet produced the display
// buffer, e.g. the first vblank after a restore (GS User's Manual 6 / 8.1: the
// CRTC reads the buffer DISPFB names out of local memory) -- and only when that
// tuple is unknown (no DISPFB job yet) or undisplayable falls back to the most
// recently drawn target; TargetFallback (the default; the corpus replay's) falls
// back to the most recently drawn target at once (a frame whose DISPFB has not
// yet been written still shows what was drawn last). The grab is the full
// physical rect, RGBA8, rows bottom-up as the attachment stores them -- the exact
// bytes the corpus hash and the frames lane consume; the local-memory decode is
// flipped into that order, so both sources hash alike.
//
// DrawScanout is the user-facing present of the same pick (display-only; no hash
// reads it). The source is the target at the scanout base with valid content, else
// the most recently drawn target with valid content (the fallback), else nothing
// is presented; a display target not drawn for over kStaleDisplayFrames frames
// while the title's main frame buffer (VramLayout) was drawn within
// kFreshWorkFrames frames and more recently is stood in for by that buffer (the
// stale-display pick; when its render class differs from DISPFB's the deposit is
// encoded in the DISPFB format and the quad decodes the mirror). Before the quad
// the display footprint is a VRAM READ: the deferred 16-bit writebacks under it
// are flushed (Scanout), then the picked target's own mirror maintenance runs --
// NATIVE (the CRTC snapshot is the dual-identical one-line class, the queue-order
// DISPFB tuple is the title's native scanout tuple, no fallback engaged, and the
// page ledger resolves the whole display footprint to THIS target: head page
// GPU-authored by it, no page inside authored by another target) defers the
// deposit (ledger record + owed rows), anything else deposits the drawn rows now.
// The quad then samples the target's colour attachment over the content rect;
// the stale-display format round trip and a target without a colour view fall
// to the mirror decode (DecodeScanoutBuffer at the DISPFB tuple over the content
// dims, uploaded to a scratch surface).
#pragma once

#include <cstdint>
#include <vector>

#include "engine/gs/hle/present/crtc_classifier.h"
#include "engine/gs/hle/targets/render_target_cache.h"
#include "engine/gs/hle/targets/vram_layout.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class ScanoutComposer {
public:
    enum class Source : uint8_t { None = 0, Target = 1, Mirror = 2 };
    // The display grab's source when no target sits at the scanout base (header).
    enum class GrabPolicy : uint8_t { TargetFallback = 0, LocalMemory = 1 };
    struct DrawResult {
        Source source = Source::None;
        uint32_t fbp = 0xFFFFFFFFu;   // the presented target's base (None: unchanged)
        bool used_fallback = false;   // another target stood in for the scanout base
        bool stale_display = false;   // the stale-display pick presented the main frame buffer
        bool native = false;          // the deposit was deferred (native scanout)
    };
    static constexpr uint32_t kStaleDisplayFrames = 30;   // display target older than this ...
    static constexpr uint32_t kFreshWorkFrames = 16;      // ... while the main buffer is this fresh

    ScanoutComposer(render::Backend& be, RenderTargetCache& targets);
    ~ScanoutComposer();

    void SetScanoutFbp(uint32_t fbp) { scanout_fbp_ = fbp; }
    void SetDispfb(uint32_t fbw_px, uint32_t psm);
    // Ignored when either dimension is <= 0.
    void SetContentDimensions(int w, int h);
    // Quiescent shared-host restore; zero content dimensions preserve the
    // explicit unconfigured state. Does not read registers, pixels or memory.
    void RestoreView(uint32_t color_key, uint32_t buffer_width, uint32_t format,
                     int content_width, int content_height);

    uint32_t ScanoutFbp() const { return scanout_fbp_; }
    uint32_t DispfbFbw() const { return dispfb_fbw_; }
    uint32_t DispfbPsm() const { return dispfb_psm_; }
    int ContentW() const { return content_w_; }
    int ContentH() const { return content_h_; }

    void SetGrabPolicy(GrabPolicy p) { grab_policy_ = p; }
    GrabPolicy GetGrabPolicy() const { return grab_policy_; }
    // The target the grab reads (scanout base, else most recent drawn); null when none.
    const RenderTarget* GrabSource() const;
    // Fills `out` with w*h*4 bytes (rows bottom-up); false (w = h = 0) when nothing
    // is readable. `vram` enables the LocalMemory arm (null = targets only).
    bool GrabDisplay(std::vector<uint8_t>* out, int* w, int* h, const VramImage* vram = nullptr);
    // The DEFERRED form of GrabDisplay (perf: the sync read is a GPU pipeline drain
    // every frame): the same source pick, but a Target's read is QUEUED
    // (Backend::BeginReadback; the GPU copies the attachment as it gets there) and
    // `*fence` names it -- the caller completes it later with Backend::PollReadback
    // into w*h*4 bytes (rows bottom-up, stride w*4). The Mirror arm is a CPU read and
    // lands in `out` at once (`*fence` = none). Returns the same bool as GrabDisplay;
    // LastGrab* describe the pick either way.
    bool BeginGrabDisplay(render::FenceId* fence, std::vector<uint8_t>* out, int* w, int* h,
                          const VramImage* vram = nullptr);
    // What the last GrabDisplay read: Target, Mirror (local memory), None; the
    // target's base and whether it was the most-recently-drawn fallback (not the
    // target at the scanout base).
    Source LastGrabSource() const { return last_grab_; }
    uint32_t LastGrabFbp() const { return last_grab_fbp_; }
    bool LastGrabFallback() const { return last_grab_fallback_; }

    // The display quad into `window` (see the header comment); `crtc` is the EE
    // register snapshot, `frame_count` the renderer's present counter (target
    // ages), `gfx_epoch` the current processing epoch.
    DrawResult DrawScanout(VramImage& vram, const VramLayout& layout, const CrtcClassifier::Snapshot& crtc,
                           uint32_t frame_count, uint64_t gfx_epoch, const render::Rect& window);
    // The swap WITHOUT the display quad or any mirror maintenance: the frame end
    // of the corpus replay, whose only output is the grab (the reference replay
    // tool never ran the scanout composite; its present was the swap alone).
    void PresentBlank(const render::Rect& window);
    // The page-ledger resolve: is the target at `fbp` the authoritative content for
    // the display footprint [fbp, fbp + size)?
    static bool ResolvesToTarget(const VramImage& vram, uint32_t fbp, uint32_t size);
    // The scanout footprint rule (2 bytes per pixel for a 16-bit render class, else 4).
    static uint32_t ScanoutFootprintBytes(const RenderTarget& t);

private:
    RenderTarget* PickSource(const VramLayout& layout, uint32_t frame_count, bool* used_fallback,
                             bool* stale_display);
    bool GrabLocalMemory(const VramImage& vram, std::vector<uint8_t>* out, int* w, int* h);
    bool GrabDisplayImpl(render::FenceId* fence, std::vector<uint8_t>* out, int* w, int* h, const VramImage* vram);
    void PresentMirror(const VramImage& vram, uint32_t fbp, uint32_t fbw, uint32_t psm, const render::Rect& window);

    render::Backend& be_;
    RenderTargetCache& targets_;
    uint32_t scanout_fbp_ = 0;
    uint32_t dispfb_fbw_ = 0;
    uint32_t dispfb_psm_ = 0;
    int content_w_ = 0;
    int content_h_ = 0;
    GrabPolicy grab_policy_ = GrabPolicy::TargetFallback;
    Source last_grab_ = Source::None;
    uint32_t last_grab_fbp_ = RenderTargetCache::kNoFbp;
    bool last_grab_fallback_ = false;
    render::SurfaceId mirror_tex_;   // the mirror-decode scratch surface
    int mirror_w_ = 0, mirror_h_ = 0;
    std::vector<uint8_t> view_;
    std::vector<uint32_t> rgba_;
};

}  // namespace gs
}  // namespace ps2r

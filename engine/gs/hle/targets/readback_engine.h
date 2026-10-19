// gs/hle/targets/readback_engine.h -- ReadbackEngine: GPU -> GS local memory
// writeback of a render target's colour and Z attachments, staged as deposits
// into the VramImage shadow (never committed here).
//
// Rules:
//   * a writeback requires valid content (valid_rect non-empty); rows are clamped
//     to drawn_since_sync (colour) / z_drawn_since_sync (Z) -- a target that has
//     not drawn since its last sync writes back nothing unless a rescue rect asks
//     for its footprint (rescue ∩ valid_rect, an optional narrower stride);
//   * colour encodes per the target's render PSM (CT32 with GPU->PS2 alpha, CT24
//     three bytes, CT16 5551 with the MASKED-SYNC keep bits preserved from
//     current VRAM) at the TARGET WIDTH stride (a rescue may pass its own);
//   * per-page veto: a page whose EE upload epoch is newer than the deposit's
//     batch epoch keeps its bytes (every colour format); a rescue additionally
//     keeps every page this target is not the recorded GPU author of; an
//     uploaded texture overlapping the footprint whose pages' upload epoch is
//     >= the batch epoch protects its byte range (CT16: only for a full-write
//     mask) -- the uploaded spans are fed in by the refresher;
//   * touched pages: the pages of the LINEAR span [fbp + y0*fbw*bpp, fbp +
//     y1*fbw*bpp) are staged; a swizzled pixel that lands on another page of the
//     footprint reaches the shadow only when that page is already Pending;
//   * tail marks: the linear rows a colour writeback encoded are marked CPU-dirty
//     (the mirror now differs from what a 16-bit view of the attachment holds) and
//     aliased (a later frame over them imports the mirror first);
//   * the fog-lane companion deposit (DepositAlt16Lane): the companion's rect is
//     5551-packed straight into VRAM at the draw's FBW stride, merging the keep
//     bits from the current halfword (the index-lane form writes the high byte
//     only), then marked like a writeback;
//   * Z encodes through the Z-buffer layouts (GS User's Manual 8.3.2: PSMZ modes
//     have their own block order) in the native Z format 0x30 | (ZBUF.PSM & 0xF),
//     Z16 saturating to 0xFFFF and Z24 to 0xFFFFFF.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "engine/gs/hle/targets/alias_page_set.h"
#include "engine/gs/hle/targets/render_target.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class ReadbackEngine {
public:
    struct UploadedSpan {
        uint32_t start = 0;   // VRAM byte address of the uploaded texture
        uint32_t size = 0;    // its footprint in bytes
    };

    ReadbackEngine();

    // Colour writeback. `rescue` (logical, half-open, may be null) replaces the
    // drawn_since_sync clamp with a footprint deposit; `rescue_fbw` (0 = none)
    // narrows the row width and stride; `enc_psm` (kNoPsmOverride = the target's
    // render PSM) is the PSM the bytes are ENCODED in -- a reader-driven deposit
    // encodes in the READ's format so the decode that follows reads them back
    // (only CT32 / CT24 / CT16 encode; the keep mask applies to a 16-bit encode);
    // `dest_fbp` (kSameFbp = the target's own base) is the VRAM base the rows are
    // deposited AT -- the display path deposits a stand-in target at the scanout
    // base so the mirror decode at DISPFB reads it.
    // Clears drawn_since_sync when it was the bound. Returns the number of texels staged.
    static constexpr uint32_t kNoPsmOverride = 0xFFFFFFFFu;
    static constexpr uint32_t kSameFbp = 0xFFFFFFFFu;
    uint32_t SyncRtToVram(render::Backend& be, RenderTarget& t, VramImage& vram,
                          uint64_t batch_epoch, const LogicalRect* rescue, uint32_t rescue_fbw,
                          uint32_t enc_psm = kNoPsmOverride, uint32_t dest_fbp = kSameFbp);
    // The companion (alt16) rows of `rect` deposited as 5551 at `draw_fbw` (0 or
    // > 1024 falls back to the target's FBW / width). Returns texels staged.
    uint32_t DepositAlt16Lane(render::Backend& be, RenderTarget& t, VramImage& vram, uint16_t keep16,
                              const LogicalRect& rect, uint32_t draw_fbw, uint64_t batch_epoch);
    // Z writeback over z_drawn_since_sync; clears it. Returns texels staged.
    uint32_t SyncZToVram(render::Backend& be, RenderTarget& t, VramImage& vram,
                         uint64_t batch_epoch);

    // The live uploaded-texture spans (protection sources); the refresher keeps
    // this current after every upload / eviction.
    void SetUploadedSpans(std::vector<UploadedSpan> spans) { uploaded_ = std::move(spans); }
    const std::vector<UploadedSpan>& UploadedSpans() const { return uploaded_; }

    // The colour PSMs the writeback encodes (others stage nothing).
    static bool EncodesColorPsm(uint32_t psm);
    // A colour attachment just read in full by the caller (the frame end's display
    // grab, physical rows bottom-up, w*4 stride): until Revoke, a colour writeback
    // of that attachment takes its rows from `rgba` instead of a second GPU read.
    // The caller guarantees nothing draws into the attachment in between (the
    // frame-end sequence: grab -> deferred deposits of other targets -> the scanout
    // deposit) and that `rgba` outlives the offer. Counted (OfferedReads).
    void OfferPixels(render::SurfaceId color, int pw, int ph, const std::vector<uint8_t>* rgba);
    void RevokeOffer() { offer_ = Offer{}; }
    // Off = every colour writeback reads the GPU (the A/B of the sharing).
    void SetOfferEnabled(bool on) { offer_enabled_ = on; }
    uint64_t OfferedReads() const { return offered_reads_; }
    // The shared page sets the tail marks (may be null: no marks).
    void SetAliasPages(AliasPageSet* alias) { alias_ = alias; }

private:
    uint32_t EncodeColorRows(RenderTarget& t, VramImage& vram, uint64_t batch_epoch,
                             const LogicalRect& rows, uint32_t fbw, bool rescue, uint32_t enc_psm,
                             uint32_t dest_fbp);
    uint32_t EncodeZRows(RenderTarget& t, VramImage& vram, uint64_t batch_epoch,
                         const LogicalRect& rows, uint32_t zpsm_field);
    // Stage [lo, hi) of the scratch image: pages inside the linear span
    // [span_lo, span_hi) unconditionally, other pages only when already Pending.
    void StageTouched(VramImage& vram, uint32_t lo, uint32_t hi, uint32_t span_lo,
                      uint32_t span_hi, uint64_t batch_epoch);
    // Byte ranges of uploaded textures overlapping [fbo_start, fbo_end) whose
    // pages carry an upload epoch >= batch_epoch (and != 0), sorted.
    std::vector<std::pair<uint32_t, uint32_t>> ProtectedRanges(const VramImage& vram,
                                                               uint32_t fbo_start,
                                                               uint32_t fbo_end,
                                                               uint64_t batch_epoch) const;

    // The pixels_ fill for `pr` of `color`: the offer when it covers the rect,
    // else the backend's readback.
    void ReadColorRows(render::Backend& be, render::SurfaceId color, const render::Rect& pr);

    struct Offer {
        render::SurfaceId color;
        int pw = 0, ph = 0;
        const std::vector<uint8_t>* rgba = nullptr;
    };
    Offer offer_;
    bool offer_enabled_ = true;
    uint64_t offered_reads_ = 0;
    std::vector<uint8_t> pixels_;    // readback scratch (physical rows)
    std::vector<float> depth_;
    std::vector<uint8_t> linear_;    // encoded linear rows
    std::vector<uint8_t> scratch_;   // 4 MiB VRAM-shaped work image
    std::vector<uint8_t> before_;    // the page rows as read, for the vetoes
    std::vector<UploadedSpan> uploaded_;
    AliasPageSet* alias_ = nullptr;
};

}  // namespace gs
}  // namespace ps2r

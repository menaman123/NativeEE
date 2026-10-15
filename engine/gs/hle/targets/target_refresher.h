// gs/hle/targets/target_refresher.h -- TargetRefresher: the EE -> GPU direction.
//
//   * Uploaded(addr) textures: an UploadVRAMTexture job carries the linear RGBA8
//     copy of a host->local transfer; it becomes a backend texture keyed by its
//     VRAM byte address (exact key first, else the entry whose byte span contains
//     the read base). A same-address re-upload of the same size replaces the
//     texels in place (footprint unchanged); a different size re-creates. The
//     map is bounded before an insert: 512 entries, 64 MiB of w*h*4 texels, LRU.
//     The live spans are mirrored into the ReadbackEngine as protection sources.
//   * EvictUnderDraw: after a draw the rendered rows ARE the VRAM content, so an
//     upload record over those rows (page-rounded) is stale and is evicted --
//     except under a partial-bit-masked 16-bit draw, which leaves the uploaded
//     bytes ~intact and keeps its record (and its writeback protection).
//   * MarkPagesCpuDirty: the InvalidateTextures job marks the pages an EE write
//     touched (the indexed-serve "pages not CPU-dirty" predicate reads them); the
//     set itself is the cache's AliasPageSet, shared with the writeback tail.
//   * AliasPreDraw: before a colour frame draw with any write enabled, the frame
//     region (FBW x render height x bpp) is checked against the aliased pages (a
//     writeback deposited under them) and, for a 16-bit frame, the CPU-dirty
//     pages; a hit flushes the deferred writebacks under the region and imports
//     the mirror over the draw's bbox ∩ scissor row band (whole target when no
//     band forms; a composite into the scanout target imports whole).
//   * UploadRowBand: the page-tile fold of an EE upload onto a target's rows (the
//     rule OLD's upload-deposit refresh used; that refresh's winning arm is OFF, so
//     nothing in this class refreshes a live target from an upload).
//   * RefreshFromVram: import a row band of the target's buffer from the COMMITTED
//     mirror, per render PSM. CT32 / CT24: an ALPHA-ONLY merge -- RGB stays what
//     the attachment holds, the alpha byte comes from the mirror scaled to the GPU
//     domain (x2 capped 255), addressed at the target width (the PSMT8H index lane
//     lives in that byte). CT16: the full 5551 image decoded from the mirror at the
//     render FBW, except rows whose pages carry an EE upload newer than the job's
//     epoch, which keep the attachment bytes. CT16S: no 16S writeback exists, so
//     the mirror is never authoritative -- bookkeeping only. The tail authors the
//     main image, stamps the indexed-serve voucher (32 for CT32/CT24, 16 for a
//     whole-image CT16 absorb; never for a band or CT16S), clears the aliased
//     pages the import fully covered and stales the companion.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "engine/gs/format/psm.h"
#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/targets/render_target.h"
#include "engine/gs/hle/targets/render_target_cache.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class TargetRefresher {
public:
    struct Uploaded {
        render::SurfaceId tex;
        int w = 0, h = 0;
        uint32_t vram_addr = 0;
        uint32_t size_bytes = 0;     // VRAM footprint (protection span)
        uint64_t last_use = 0;
    };
    static constexpr size_t kUploadedMaxEntries = 512;
    static constexpr uint64_t kUploadedMaxBytes = 64ull * 1024u * 1024u;   // of w*h*4

    TargetRefresher(render::Backend& be, VramImage& vram, RenderTargetCache& targets);
    ~TargetRefresher();

    // UploadVRAMTexture: `rgba` = w*h*4 linear pixels; footprint = VRAM bytes covered.
    void UploadDirect(uint32_t vram_addr, int w, int h, const uint8_t* rgba, uint32_t footprint);
    // The uploaded texture serving a read at `tbp` (exact, else containing span).
    const Uploaded* FindUploaded(uint32_t tbp);
    void EvictUploadedInRange(uint32_t start, uint32_t size);
    // The post-draw eviction rule over the draw's row range; `render_height` is
    // the draw's logical render height.
    void EvictUnderDraw(const DrawIr& ir, const render::DrawResult& res, int render_height);
    // The page-rounded byte range that rule evicts (false: nothing -- no draw,
    // no bbox, no width, or a partial-bit-masked 16-bit draw).
    static bool EvictRangeUnderDraw(const DrawIr& ir, const render::DrawResult& res, int render_height,
                                    uint32_t* start, uint32_t* size);
    size_t UploadedCount() const { return uploaded_.size(); }
    uint64_t UploadedTexelBytes() const;

    void MarkPagesCpuDirty(uint32_t addr, uint32_t size);
    bool ArePagesCpuDirty(uint32_t addr, uint32_t size) const;
    void ClearPagesCpuDirty(uint32_t addr, uint32_t size);

    // Rows [*y0, *y1) of `t` the upload [addr, addr+size) lands on (page-tile
    // fold, clamped to the target); false when disjoint.
    static bool UploadRowBand(uint32_t addr, uint32_t size, const RenderTarget& t, int* y0, int* y1);

    // Rows [y0, y1) of the target imported from the committed mirror; `job_epoch`
    // is the current gfx processing epoch (the CT16 keep-run reference).
    void RefreshFromVram(RenderTarget& t, int y0, int y1, uint64_t job_epoch);
    // The pre-draw alias demand for `ir` into `t`; `render_h` is the draw's
    // logical render height, `scanout_fbp` the DISPFB base. The deferred
    // writebacks under the frame land first (`hold_fbp` keeps that buffer's own
    // entry deferred -- the stencil-composite ladder). Returns true when the
    // demand exists; the import runs only when `import` (a caller that aborts on
    // the demand re-runs the generic ladder, which imports).
    bool AliasPreDraw(RenderTarget& t, const DrawIr& ir, int render_h, uint32_t scanout_fbp,
                      uint64_t job_epoch, uint32_t hold_fbp = RenderTargetCache::kNoFbp,
                      bool import = true);
    // The alias-refresh region of a frame draw (0 = none: not a colour frame, no
    // write enabled, or no width).
    static uint32_t AliasRegionBytes(const DrawIr& ir, int render_h);

private:
    void EnforceUploadedBudget();
    void PublishSpans();
    // The stride (64-pixel units) the de-swizzle walk for `mode` addresses the
    // mirror at: the target width for CT32/CT24, the render FBW for CT16.
    static uint32_t WalkBufferWidth(const RenderTarget& t, Psm mode);
    // The committed mirror's pages the walk of rows [y0, y1) of a `w`-wide target
    // at stride `bw` addresses, read into `mirror_` at their own offsets.
    void ReadMirrorBand(uint32_t fbp, Psm mode, uint32_t bw, uint32_t w, int y0, int y1);
    void RefreshAlphaMerge(RenderTarget& t, int y0, int y1, uint32_t bw);
    void RefreshCt16(RenderTarget& t, int y0, int y1, uint32_t bw, uint64_t job_epoch, bool* absorbed);
    void UploadRows(RenderTarget& t, int y0, int y1, const uint8_t* rgba);

    render::Backend& be_;
    VramImage& vram_;
    RenderTargetCache& targets_;
    std::unordered_map<uint32_t, Uploaded> uploaded_;
    uint64_t tick_ = 0;
    std::vector<uint8_t> mirror_;       // 4 MiB committed-mirror scratch
    std::vector<uint8_t> pixels_;       // attachment rows (RGBA8)
    std::vector<uint8_t> scaled_;
    std::vector<uint8_t> linear_;       // the band's mirror rows, de-swizzled (CT16 halfwords / CT32 words)
};

}  // namespace gs
}  // namespace ps2r

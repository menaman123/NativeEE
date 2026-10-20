// gs/hle/targets/render_target_cache.h -- RenderTargetCache: the live render targets
// keyed by FRAME.FBP.
//
// Rules: a target grows but never shrinks (a smaller FRAME over an existing
// buffer keeps the larger allocation); a size or scale change re-creates the
// surfaces after a rescue writeback of everything drawn since the last sync
// (stride narrowed to the target's FBW only when that is a page multiple), and
// carries the content tags (32-bit history, PSM / FBW) so a retag never loses
// what the buffer held; a PSM-only change is a retag, never a clear; a fresh
// target has no PSM / FBW tag until its first main-route draw.
// SyncAll is the SyncAllRTsToVRAM job: colour only, batch epoch 0, targets with
// valid + drawn content. most_recent_drawn_fbp is the grab fallback when no
// target exists at the scanout base.
// The cache also owns the two shared page sets (AliasPageSet) and the deferred
// 16-bit writeback ledger: AfterDrawTail records a 16-bit-class draw in the ledger
// (deposits the fog-lane companion rows eagerly) and clears the CPU-dirty pages the
// draw made GPU-authoritative; FlushPending writes the overlapping deferred targets
// back at their recorded epochs; Destroy is the OLD rescue form (full extent at the
// target's last-draw epoch, page-aligned stride) followed by the ledger flush.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

#include "engine/gs/hle/ir/draw_ir.h"
#include "engine/gs/hle/targets/alias_page_set.h"
#include "engine/gs/hle/targets/pending_rt_sync_ledger.h"
#include "engine/gs/hle/targets/readback_engine.h"
#include "engine/gs/hle/targets/render_target.h"
#include "engine/gs/hle/targets/scale_policy.h"
#include "engine/gs/vram/vram_image.h"
#include "engine/render/backend.h"

namespace ps2r {
namespace gs {

class RenderTargetCache {
public:
    RenderTargetCache(render::Backend& be, VramImage& vram, const ScalePolicy& scale);
    ~RenderTargetCache();

    // The target for (fbp, logical w x h, psm); creates or grows as needed.
    RenderTarget& GetOrCreate(uint32_t fbp, int width, int height, uint32_t psm,
                              uint64_t gfx_epoch);
    RenderTarget* Find(uint32_t fbp);
    const RenderTarget* Find(uint32_t fbp) const;
    bool Has(uint32_t fbp) const { return targets_.count(fbp) != 0; }
    // Rescue writeback (full extent, last-draw epoch, page-aligned stride) +
    // ledger flush, then the surfaces go.
    void Destroy(uint32_t fbp);
    void DestroyAll();
    // Transactional application restore: the caller staged complete host
    // attachments after a fence. Discard the abandoned timeline without a
    // rescue/writeback into the incoming image. No clears or draws are issued.
    bool ReplaceAfterQuiescence(std::vector<std::unique_ptr<RenderTarget>>& targets,
                                uint32_t recent);
    // The post-draw remainder after RenderTarget::AfterDraw: the fog-lane
    // companion deposit, the 16-bit ledger record, the GPU-valid page clears.
    void AfterDrawTail(RenderTarget& t, const DrawIr& ir, const render::DrawResult& res,
                       int render_h, uint64_t gfx_epoch);
    // Write back the deferred targets overlapping [start, start+size) (0, 0 = all)
    // at their recorded epochs; `reader_pages_only` keeps entries no texture read
    // observes deferred (the present rule). Returns entries flushed.
    // `hold_fbp` (kNoFbp = none) keeps that buffer's own entry deferred: the
    // stencil-composite ladder's pre-draw flush must not deposit the armed
    // accumulator mid-sequence (its writeback hook would materialise + idle it).
    uint32_t FlushPending(PendingRtSyncLedger::Reason reason, uint32_t start, uint32_t size,
                          bool reader_pages_only = false, uint32_t hold_fbp = kNoFbp);
    // Called with the target's fbp before EVERY colour writeback of a target (the
    // OLD SyncRTToVRAM entry hook): the stencil sequence materialises the armed
    // accumulator so the encode reads bytes, never the mid-sequence stencil fiction.
    void SetTouchHook(std::function<void(uint32_t)> hook) { touch_ = std::move(hook); }
    uint64_t HeldEntries() const { return held_; }
    // The plain drawn-rows colour writeback of `t` at `batch_epoch` (encoded in
    // `enc_psm`, default the render PSM; deposited at `dest_fbp`, default its own
    // base) WITH the sync tail: once the drawn rect is deposited and reset, the
    // deferred entry for the deposit base is moot and goes. Every direct
    // (non-rescue) sync goes through here; a rescue never does. Returns texels staged.
    uint32_t SyncDrawn(RenderTarget& t, uint64_t batch_epoch,
                       uint32_t enc_psm = ReadbackEngine::kNoPsmOverride,
                       uint32_t dest_fbp = ReadbackEngine::kSameFbp);
    // The DEFERRED scanout deposit (perf: the display deposit's GPU read was a
    // pipeline drain every frame). SyncDrawnDeferred is SyncDrawn with the GPU read
    // queued (Backend::BeginReadback) and the encode run later -- at the image's
    // gate (any reader / writer of the owed span, see VramImage) or at FlushDeferred
    // (the next frame end), whichever comes first; the image evolves exactly as if
    // the deposit had landed at once. Falls back to SyncDrawn when deferral is off,
    // the backend's readbacks are synchronous, or the grab's offer covers the rows.
    // Only for a cache whose image is read on the backend's thread alone (the
    // pipelined split image, or inline): the gate completes the read on the caller.
    void SetDeferredDeposit(bool on) { deferred_on_ = on; }
    bool DeferredDeposit() const { return deferred_on_; }
    uint32_t SyncDrawnDeferred(RenderTarget& t, uint64_t batch_epoch,
                               uint32_t enc_psm = ReadbackEngine::kNoPsmOverride,
                               uint32_t dest_fbp = ReadbackEngine::kSameFbp);
    // Completes every deferred deposit (waits for the GPU only if it has not
    // finished the read). Returns the number completed.
    uint32_t FlushDeferred();
    struct DeferredCensus {
        uint64_t begun = 0;        // deposits queued
        uint64_t gated = 0;        // completed by the image's gate (a reader / writer of the span)
        uint64_t frame_end = 0;    // completed at FlushDeferred (the frame end)
        uint64_t offered = 0;      // not deferred: the grab's offer covered the rows
        uint64_t sync = 0;         // not deferred: off / no staging / nothing to queue
    };
    const DeferredCensus& Deferred() const { return deferred_n_; }
    // The SyncAllRTsToVRAM job (save-time quiescence): colour writeback of every
    // target with valid + drawn content at batch epoch 0; leaves the ledger empty.
    void SyncAll();
    void NoteDrawn(uint32_t fbp) { most_recent_drawn_fbp_ = fbp; }
    uint32_t MostRecentDrawnFbp() const { return most_recent_drawn_fbp_; }
    static constexpr uint32_t kNoFbp = 0xFFFFFFFFu;

    // The rescue encode stride for a target's FBW (0 = keep the width stride).
    static uint32_t RescueStride(uint32_t render_fbw);

    ReadbackEngine& Readback() { return readback_; }
    AliasPageSet& AliasPages() { return alias_; }
    const AliasPageSet& AliasPages() const { return alias_; }
    PendingRtSyncLedger& Ledger() { return ledger_; }
    const PendingRtSyncLedger& Ledger() const { return ledger_; }
    // The whole-target footprint in the render format (32 bpp when untagged).
    static uint32_t FootprintBytes(const RenderTarget& t);
    template <typename Fn> void ForEach(Fn fn) { for (auto& kv : targets_) fn(*kv.second); }
    size_t Count() const { return targets_.size(); }
    int TargetScale(uint32_t key) const { return scale_.TargetScale(key); }

private:
    void RescueAndDrop(std::unordered_map<uint32_t, std::unique_ptr<RenderTarget>>::iterator it);
    void Touch(uint32_t fbp) { if (touch_) touch_(fbp); }

    std::function<void(uint32_t)> touch_;
    uint64_t held_ = 0;
    render::Backend& be_;
    VramImage& vram_;
    const ScalePolicy& scale_;
    ReadbackEngine readback_;
    AliasPageSet alias_;
    PendingRtSyncLedger ledger_;
    std::unordered_map<uint32_t, std::unique_ptr<RenderTarget>> targets_;
    uint32_t most_recent_drawn_fbp_ = kNoFbp;
    bool deferred_on_ = false;
    bool deferred_flushing_ = false;
    std::vector<ReadbackEngine::Deferred> deferred_;
    DeferredCensus deferred_n_;
    uint32_t CompleteDeferred(bool gated);
};

}  // namespace gs
}  // namespace ps2r

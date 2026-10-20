// gs/hle/targets/render_target_cache.cpp -- grow-only target lookup with tag carry and
// the rescue writeback before a re-create.
#include "engine/gs/hle/targets/render_target_cache.h"

#include <algorithm>
#include <cmath>

namespace ps2r {
namespace gs {

RenderTargetCache::RenderTargetCache(render::Backend& be, VramImage& vram, const ScalePolicy& scale)
    : be_(be), vram_(vram), scale_(scale) {
    readback_.SetAliasPages(&alias_);
    vram_.SetDeferredGate([this] { CompleteDeferred(/*gated=*/true); });
}

uint32_t RenderTargetCache::FootprintBytes(const RenderTarget& t) {
    uint32_t bpp = RenderTarget::FrameBitsPerPixel(t.identity.render_psm);
    if (bpp == 0) bpp = 32;
    return uint32_t(t.identity.width) * uint32_t(t.identity.height) * (bpp / 8);
}

RenderTargetCache::~RenderTargetCache() {
    DestroyAll();
    vram_.SetDeferredGate(nullptr);
    vram_.ClearDeferred();
}

uint32_t RenderTargetCache::SyncDrawnDeferred(RenderTarget& t, uint64_t batch_epoch, uint32_t enc_psm,
                                              uint32_t dest_fbp) {
    if (!deferred_on_) { deferred_n_.sync++; return SyncDrawn(t, batch_epoch, enc_psm, dest_fbp); }
    Touch(t.identity.fbp);
    if (!t.coherence.HasValidContent() || !t.coherence.HasDrawnSinceSync()) return 0;
    ReadbackEngine::Deferred d;
    if (!readback_.BeginDeferredColorSync(be_, t, vram_, batch_epoch, enc_psm, dest_fbp, &d)) {
        // Offered rows or no staging: the synchronous deposit (no GPU drain in the
        // offered case, the backend's own read otherwise).
        const int s = t.identity.scale;
        LogicalRect rows;
        rows.x0 = std::max(0, t.coherence.drawn_since_sync.x0);
        rows.y0 = std::max(0, t.coherence.drawn_since_sync.y0);
        rows.x1 = std::min(t.identity.width, t.coherence.drawn_since_sync.x1);
        rows.y1 = std::min(t.identity.height, t.coherence.drawn_since_sync.y1);
        const render::Rect pr{rows.x0 * s, rows.y0 * s, (rows.x1 - rows.x0) * s, (rows.y1 - rows.y0) * s};
        if (readback_.OfferCovers(t.views.color, pr)) deferred_n_.offered++;
        else deferred_n_.sync++;
        return SyncDrawn(t, batch_epoch, enc_psm, dest_fbp);
    }
    deferred_.push_back(d);
    deferred_n_.begun++;
    // The tail SyncDrawn runs: the drawn rect is reset (Begin did it), the deferred
    // ledger entry for the deposit base is moot.
    ledger_.Remove(d.dest_fbp);
    return 0;
}

uint32_t RenderTargetCache::CompleteDeferred(bool gated) {
    if (deferred_.empty() || deferred_flushing_) return 0;
    deferred_flushing_ = true;
    const bool commit = vram_.DeferredCommitOwed();
    std::vector<ReadbackEngine::Deferred> taken;
    taken.swap(deferred_);
    vram_.ClearDeferred();   // the staging below passes the gate
    uint32_t n = 0;
    for (const ReadbackEngine::Deferred& d : taken) {
        readback_.CompleteDeferred(be_, vram_, d, commit);
        ++n;
    }
    if (gated) deferred_n_.gated += n;
    else deferred_n_.frame_end += n;
    deferred_flushing_ = false;
    return n;
}

uint32_t RenderTargetCache::FlushDeferred() { return CompleteDeferred(/*gated=*/false); }

RenderTarget* RenderTargetCache::Find(uint32_t fbp) {
    auto it = targets_.find(fbp);
    return it == targets_.end() ? nullptr : it->second.get();
}

const RenderTarget* RenderTargetCache::Find(uint32_t fbp) const {
    auto it = targets_.find(fbp);
    return it == targets_.end() ? nullptr : it->second.get();
}

uint32_t RenderTargetCache::RescueStride(uint32_t render_fbw) {
    // Only a page-aligned FBW may narrow the rescue stride; anything else keeps
    // the target-width encode.
    return (render_fbw >= 64 && (render_fbw & 63) == 0) ? render_fbw : 0;
}

RenderTarget& RenderTargetCache::GetOrCreate(uint32_t fbp, int width, int height, uint32_t psm,
                                             uint64_t gfx_epoch) {
    const int want_scale = scale_.TargetScale(fbp);
    TargetIdentity carry;
    bool have_carry = false;
    auto it = targets_.find(fbp);
    if (it != targets_.end()) {
        RenderTarget& t = *it->second;
        const bool grow = width > t.identity.width || height > t.identity.height ||
                          t.identity.scale != want_scale;
        if (!grow) return t;   // a retag keeps content: no clear, no re-create
        width = std::max(width, t.identity.width);
        height = std::max(height, t.identity.height);
        carry = t.identity;
        have_carry = true;
        RescueAndDrop(it);
    }
    // A fresh target carries no PSM / FBW tag: the first main-route draw sets them.
    TargetIdentity id;
    id.fbp = fbp;
    id.width = width;
    id.height = height;
    id.scale = static_cast<uint8_t>(want_scale);
    if (have_carry) {
        id.has_32bit_content = carry.has_32bit_content;
        id.was_32bit = carry.was_32bit;
        id.was_16bit = carry.was_16bit;
        id.render_psm = carry.render_psm;
        id.render_fbw = carry.render_fbw;
    }
    auto t = std::make_unique<RenderTarget>();
    t->Create(be_, id);
    RenderTarget& ref = *t;
    targets_[fbp] = std::move(t);
    (void)psm;
    (void)gfx_epoch;
    return ref;
}

void RenderTargetCache::RescueAndDrop(
    std::unordered_map<uint32_t, std::unique_ptr<RenderTarget>>::iterator it) {
    FlushDeferred();   // a deposit of this target still in flight lands before its rescue
    RenderTarget& t = *it->second;
    // Rescue deposit: everything the surfaces hold would be lost with them, so
    // the full extent is written back at the target's own last-draw epoch
    // (stride narrowed to its FBW when that is a page multiple), then the
    // deferred entry for this base is flushed.
    LogicalRect all{0, 0, t.identity.width, t.identity.height};
    Touch(t.identity.fbp);
    readback_.SyncRtToVram(be_, t, vram_, t.coherence.last_draw_gfx_epoch, &all,
                           RescueStride(t.identity.render_fbw));
    FlushPending(PendingRtSyncLedger::Reason::Destroy, t.identity.fbp, 1);
    t.Destroy(be_);
    targets_.erase(it);
}

void RenderTargetCache::Destroy(uint32_t fbp) {
    auto it = targets_.find(fbp);
    if (it == targets_.end()) return;
    RescueAndDrop(it);
}

bool RenderTargetCache::ReplaceAfterQuiescence(
    std::vector<std::unique_ptr<RenderTarget>>& targets, uint32_t recent) {
    if (!ledger_.Empty()) return false;
    FlushDeferred();
    std::unordered_map<uint32_t, std::unique_ptr<RenderTarget>> next;
    // Allocate the complete map before moving any staged owner.
    for (const auto& target : targets) {
        if (!target || !next.emplace(target->identity.fbp, nullptr).second) return false;
    }
    if (recent != kNoFbp && !next.count(recent)) return false;
    for (auto& target : targets) next.at(target->identity.fbp) = std::move(target);
    targets_.swap(next);
    most_recent_drawn_fbp_ = recent;
    for (auto& old : next) old.second->Destroy(be_);
    for (const auto& item : targets_) {
        const auto& target = *item.second;
        alias_.ClearAliased(item.first, FootprintBytes(target));
        alias_.ClearCpuDirty(item.first, FootprintBytes(target));
    }
    targets.clear();
    return true;
}

void RenderTargetCache::AfterDrawTail(RenderTarget& t, const DrawIr& ir, const render::DrawResult& res,
                                      int render_h, uint64_t gfx_epoch) {
    if (!res.drew) return;
    const uint32_t fpsm = ir.raster.frame.psm;
    const bool is_16bit_target = RenderTarget::Is16BitColor(t.identity.render_psm);
    // The draw's rect: the vertex bbox within the scissor (registers inclusive).
    LogicalRect dr;
    bool have_rect = res.bbox_valid;
    if (have_rect) {
        const DrawIr::Scissor& sc = ir.raster.scissor;
        dr.x0 = std::max(int(std::floor(res.bbox_tri[0])), int(sc.x0));
        dr.y0 = std::max(int(std::floor(res.bbox_tri[1])), int(sc.y0));
        dr.x1 = std::min(int(std::ceil(res.bbox_tri[2])), int(sc.x1) + 1);
        dr.y1 = std::min(int(std::ceil(res.bbox_tri[3])), int(sc.y1) + 1);
    }
    // A fog-lane draw into the companion of a 32-bit target (CT16 frame with a
    // keep mask): its lane bytes reach VRAM at once through the 5551 merge.
    if (!is_16bit_target && res.alt16_route && fpsm == 0x02 && t.coherence.sync_keep_mask16 != 0 &&
        have_rect)
        readback_.DepositAlt16Lane(be_, t, vram_, t.coherence.sync_keep_mask16, dr, ir.raster.frame.fbw,
                                   gfx_epoch);
    // A 16-bit-class target's rows are written back lazily: recorded here, flushed
    // by the first consumer that reads under its footprint.
    if (is_16bit_target) ledger_.Record(t.identity.fbp, FootprintBytes(t), gfx_epoch);
    // The rows this draw covered are GPU-authoritative again (32 bpp worst case).
    const uint32_t valid = ir.raster.frame.fbw * uint32_t(std::max(0, render_h)) * 4u;
    alias_.ClearCpuDirty(ir.raster.frame.fbp, valid);
}

uint32_t RenderTargetCache::FlushPending(PendingRtSyncLedger::Reason reason, uint32_t start,
                                         uint32_t size, bool reader_pages_only, uint32_t hold_fbp) {
    if (ledger_.Empty()) return 0;
    const VramImage& vram = vram_;
    std::vector<PendingRtSyncLedger::Entry> taken =
        ledger_.Take(start, size, [&](const PendingRtSyncLedger::Entry& e) {
            if (e.fbp == hold_fbp) { held_++; return true; }
            return reader_pages_only && !vram.NeedsEagerDeposit(VramImage::Span{e.start, e.size});
        });
    uint32_t n = 0;
    for (const PendingRtSyncLedger::Entry& e : taken) {
        RenderTarget* t = Find(e.fbp);
        if (!t) continue;   // the target went away with its entry still recorded
        ledger_.CountFlush(reason);
        Touch(e.fbp);
        readback_.SyncRtToVram(be_, *t, vram_, e.epoch, nullptr, 0);
        ++n;
    }
    return n;
}

void RenderTargetCache::DestroyAll() {
    FlushDeferred();
    for (auto& kv : targets_) kv.second->Destroy(be_);
    targets_.clear();
}

uint32_t RenderTargetCache::SyncDrawn(RenderTarget& t, uint64_t batch_epoch, uint32_t enc_psm,
                                      uint32_t dest_fbp) {
    // The same early-outs as the writeback itself: a target without valid or
    // drawn content deposits nothing and its tail does not run.
    Touch(t.identity.fbp);
    if (!t.coherence.HasValidContent() || !t.coherence.HasDrawnSinceSync()) return 0;
    const uint32_t staged = readback_.SyncRtToVram(be_, t, vram_, batch_epoch, nullptr, 0, enc_psm, dest_fbp);
    // The tail: the drawn rect this entry would flush was just deposited and
    // reset, so the deferred entry for the deposit base is moot.
    ledger_.Remove(dest_fbp == ReadbackEngine::kSameFbp ? t.identity.fbp : dest_fbp);
    return staged;
}

void RenderTargetCache::SyncAll() {
    FlushDeferred();
    // Colour only, batch epoch 0: at save time every page with any recorded EE
    // upload keeps its CPU bytes (the live epoch would be ahead of every page
    // stamp and disarm the newer-upload veto). Every synced target's deferred
    // entry goes with its drawn rect, so the ledger is empty afterwards.
    for (auto& kv : targets_) SyncDrawn(*kv.second, 0);
}

}  // namespace gs
}  // namespace ps2r

#include "engine/dmac/dmac_deferred_vif1.h"

#include "engine/ee/mem/scratchpad.h"
#include "engine/gif/gif_arbiter.h"

DmacDeferredVif1::DmacDeferredVif1(ChainWalker& walker, GifArbiter& arbiter)
    : walker_(walker), arbiter_(arbiter) {}

void DmacDeferredVif1::SetRunner(RunChain fn, void* user) {
    run_ = fn;
    run_user_ = user;
}

void DmacDeferredVif1::Park(const DmacChannelRegs& regs) {
    active_ = true;
    snapshot_.chcr = regs.chcr;
    snapshot_.madr = regs.madr;
    snapshot_.qwc = regs.qwc;
    snapshot_.tadr = regs.tadr;
    snapshot_.asr0 = regs.asr0;
    snapshot_.asr1 = regs.asr1;
}

void DmacDeferredVif1::Clear() {
    active_ = false;
    snapshot_ = DmacChannelRegs{};
    tails_.clear();
}

void DmacDeferredVif1::FlushDeferredVif1(DmacChannelRegs& vif1, FlushReason reason) {
    static_cast<void>(reason);
    if (!active_) return;
    active_ = false;
    // The kick-time registers come back (STR set, mode / TTE / ASP as kicked)
    // so the walk sees exactly what the game wrote; SADR is not part of a kick.
    vif1.chcr = snapshot_.chcr;
    vif1.madr = snapshot_.madr;
    vif1.qwc = snapshot_.qwc;
    vif1.tadr = snapshot_.tadr;
    vif1.asr0 = snapshot_.asr0;
    vif1.asr1 = snapshot_.asr1;
    if (run_) run_(run_user_);
    vif1.chcr &= ~DmacChcr::kStr;
    // The walk's PATH2 (and any queued PATH3) packets reach the GS now.
    if (arbiter_.hasPending()) arbiter_.drain();
}

bool DmacDeferredVif1::IsSaneRewrite(uint64_t now_lo, uint64_t recorded_lo) {
    if (now_lo == recorded_lo) return false;
    if (now_lo == 0 || now_lo == ~0ull) return false;
    const uint32_t qwc = static_cast<uint32_t>(now_lo & 0xFFFFu);
    const uint32_t id = static_cast<uint32_t>((now_lo >> 28) & 7u);
    const uint32_t addr = static_cast<uint32_t>((now_lo >> 32) & 0x7FFFFFF0u);
    const bool addr_needed = id == DmacTagId::kRefe || id == DmacTagId::kNext || id == DmacTagId::kRef ||
                             id == DmacTagId::kRefs || id == DmacTagId::kCall;
    const bool addr_ok = !addr_needed || Scratchpad::Contains(addr) || (addr & 0x0FFFFFFFu) < 0x02000000u;
    return qwc <= kMaxSaneQwc && addr_ok;
}

void DmacDeferredVif1::ChaseVif1Tail(DmacChannelRegs& vif1) {
    FlushDeferredVif1(vif1, FlushReason::FrameSync);
    if (tails_.empty()) return;
    std::vector<DmacVif1TailRecord> recs;
    recs.swap(tails_);
    bool resumed_any = false;
    for (const DmacVif1TailRecord& rec : recs) {
        const uint64_t now_lo = walker_.ReadTagWord(rec.end_tadr, rec.from_spr);
        if (!IsSaneRewrite(now_lo, rec.end_tag_lo)) continue;
        vif1.tadr = rec.end_tadr | (rec.from_spr ? Scratchpad::kBase : 0u);
        vif1.chcr = rec.chcr | DmacChcr::kStr;
        vif1.qwc = 0;
        if (run_) run_(run_user_);
        resumed_any = true;
    }
    tails_.clear();  // records a resumed walk appended are dropped with the batch
    if (resumed_any && arbiter_.hasPending()) arbiter_.drain();
}

void DmacDeferredVif1::RecordTail(const DmacVifWalkResult& res, uint32_t chcr_before_str_clear) {
    // Only an in-place terminator slot (END, QWC 0) can be extended by the game;
    // an END with payload was consumed and resuming there would double-send it.
    if (!res.completed || res.last_id != DmacTagId::kEnd || (res.last_tag_lo & 0xFFFFu) != 0) return;
    for (DmacVif1TailRecord& r : tails_) {
        if (r.end_tadr == res.last_tag_tadr && r.from_spr == res.last_tag_from_spr) {
            r.end_tag_lo = res.last_tag_lo;
            r.chcr = chcr_before_str_clear;
            return;
        }
    }
    tails_.push_back({res.last_tag_tadr, res.last_tag_from_spr, res.last_tag_lo, chcr_before_str_clear});
}

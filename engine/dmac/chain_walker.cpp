#include "engine/dmac/chain_walker.h"

#include <cstring>
#include <utility>

#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/gif/gif_arbiter.h"
#include "engine/vif/vif.h"
#include "engine/vif/vif_regs.h"

namespace {

uint64_t Load64(const uint8_t* p) {
    uint64_t v = 0;
    if (p) std::memcpy(&v, p, 8);
    return v;
}

// The tag's ADDR field, 16-byte aligned; the SPR bit lives in bit 63.
uint32_t TagAddr(uint64_t lo) { return static_cast<uint32_t>((lo >> 32) & 0x7FFFFFFFu) & ~0xFu; }
bool TagSpr(uint64_t lo) { return (lo >> 63) != 0; }
uint32_t TagQwc(uint64_t lo) { return static_cast<uint32_t>(lo & 0xFFFFu); }
uint32_t TagId(uint64_t lo) { return static_cast<uint32_t>((lo >> 28) & 7u); }
bool TagIrq(uint64_t lo) { return ((lo >> 31) & 1u) != 0; }

}  // namespace

ChainWalker::ChainWalker(MainMemory& ram, Scratchpad& spad, Vif& vif, GifArbiter& arbiter)
    : ram_(ram), spad_(spad), vif_(vif), arbiter_(arbiter) {}

void ChainWalker::SetVif1StagedFeed(StagedFeed feed, StagedFlush flush, StagedResetLatch reset, void* user) {
    staged_feed_ = feed;
    staged_flush_ = flush;
    staged_reset_ = reset;
    staged_user_ = user;
}

void ChainWalker::SetVif1TagObserver(TagObserver observer, std::function<void()> end) {
    tag_observer_ = std::move(observer);
    walk_end_observer_ = std::move(end);
}

void ChainWalker::FlushVif1StagedFeed() {
    FlushStaged(static_cast<int>(DmacChannelId::Vif1));
}

uint64_t ChainWalker::ReadTagWord(uint32_t tadr, bool from_spr) const {
    if (from_spr) return Load64(spad_.Pointer(Scratchpad::Offset(tadr), 8));
    return Load64(ram_.Pointer(tadr & kRamMask, 8));
}

const uint8_t* ChainWalker::VifSpan(uint32_t addr, bool from_spr, size_t bytes) const {
    if (from_spr) return spad_.Pointer(Scratchpad::Offset(addr), bytes);
    return ram_.Pointer(addr & kRamMask, bytes);
}

uint8_t* ChainWalker::Translate(uint32_t addr, size_t bytes) const {
    if (Scratchpad::Contains(addr)) return spad_.Pointer(Scratchpad::Offset(addr), bytes);
    return ram_.Pointer(MainMemory::Phys(addr), bytes);
}

void ChainWalker::FeedVif(int ch, const uint8_t* data, size_t bytes, bool tte) {
    const int v = (ch == static_cast<int>(DmacChannelId::Vif1)) ? 1 : 0;
    if (v == 1 && staged_feed_) {
        staged_feed_(staged_user_, data, bytes, tte);
        return;
    }
    if (tte) vif_.SetTteFeed(v, true);
    vif_.ProcessData(v, data, bytes);
    if (tte) vif_.SetTteFeed(v, false);
}

void ChainWalker::FlushStaged(int ch) {
    if (ch == static_cast<int>(DmacChannelId::Vif1) && staged_flush_) staged_flush_(staged_user_);
}

void ChainWalker::VifNormal(int ch, DmacChannelRegs& regs) {
    if (regs.qwc == 0) return;
    const size_t bytes = static_cast<size_t>(regs.qwc) * 16;
    // Normal mode reads RAM through the 25-bit window only (no scratchpad routing).
    const uint8_t* p = ram_.Pointer(regs.madr & kRamMask, bytes);
    if (!p) return;
    FeedVif(ch, p, bytes, false);
    FlushStaged(ch);
    regs.madr += static_cast<uint32_t>(bytes);
    regs.qwc = 0;
}

DmacVifWalkResult ChainWalker::WalkVifChain(int ch, DmacChannelRegs& regs) {
    using namespace DmacChcr;
    DmacVifWalkResult res;
    const int v = (ch == static_cast<int>(DmacChannelId::Vif1)) ? 1 : 0;
    const bool staged = (v == 1) && staged_feed_ != nullptr;

    // Every walk starts with an empty word latch.
    if (staged)
        staged_reset_(staged_user_);
    else
        vif_.ResetLatch(v);

    // FDR set = the VIF1 FIFO is reversed (VIF1 -> memory); nothing is fed and
    // the channel is not completed. Not tested under the staged feed.
    if (!staged && (vif_.regs(v).stat & VifStat::kFdr)) {
        regs.chcr &= ~kStr;
        FlushStaged(ch);
        if (v == 1 && walk_end_observer_) walk_end_observer_();
        return res;
    }
    const bool tte = (regs.chcr & kTte) != 0;
    const bool tie = (regs.chcr & kTie) != 0;
    if (Mode(regs.chcr) != kModChain) {
        regs.chcr &= ~kStr;
        FlushStaged(ch);
        if (v == 1 && walk_end_observer_) walk_end_observer_();
        return res;
    }

    uint32_t tadr = regs.tadr & kTadrMask;
    bool from_spr = Scratchpad::Contains(regs.tadr);
    if (v == 1 && tadr == 0) {
        regs.chcr &= ~kStr;
        FlushStaged(ch);
        if (walk_end_observer_) walk_end_observer_();
        return res;
    }

    bool tag_end = false;
    int tag_count = 0;
    while (!tag_end && tag_count < kMaxTags) {
        const uint32_t prev_tadr = tadr;
        const uint32_t base = from_spr ? Scratchpad::Offset(tadr) : (tadr & kRamMask);
        const uint64_t lo = from_spr ? Load64(spad_.Pointer(base, 8)) : Load64(ram_.Pointer(base, 8));
        const uint64_t hi = from_spr ? Load64(spad_.Pointer(base + 8, 8)) : Load64(ram_.Pointer(base + 8, 8));
        res.last_tag_lo = lo;
        res.last_tag_tadr = prev_tadr;
        res.last_tag_from_spr = from_spr;

        // VIF terminator: both tag halves all-ones (never-written memory).
        if (lo == ~0ull && hi == ~0ull) {
            tag_end = true;
            break;
        }
        const uint32_t qwc = TagQwc(lo);
        const uint32_t id = TagId(lo);
        const bool irq = TagIrq(lo);
        const uint32_t addr = TagAddr(lo);
        const bool spr = TagSpr(lo);
        res.last_id = id;

        // CHCR.TAG mirrors the tag's upper half.
        regs.chcr = (regs.chcr & 0xFFFFu) | (static_cast<uint32_t>(lo) & kTagMask);

        uint32_t src_addr = 0;
        bool src_from_spr = false;
        bool do_transfer = false;
        uint32_t next_tadr = tadr;
        bool next_from_spr = from_spr;
        const uint32_t after_data = tadr + 16 + qwc * 16;
        switch (id) {
            case DmacTagId::kRefe:
                src_addr = addr;
                src_from_spr = spr;
                next_tadr = tadr + 16;
                tag_end = true;
                do_transfer = true;
                break;
            case DmacTagId::kCnt:
                src_addr = tadr + 16;
                src_from_spr = from_spr;
                next_tadr = after_data;
                do_transfer = true;
                break;
            case DmacTagId::kNext:
                src_addr = tadr + 16;
                src_from_spr = from_spr;
                next_tadr = addr;
                next_from_spr = spr;
                do_transfer = qwc > 0;
                break;
            case DmacTagId::kRef:
            case DmacTagId::kRefs:
                src_addr = addr;
                src_from_spr = spr;
                next_tadr = tadr + 16;
                do_transfer = true;
                break;
            case DmacTagId::kCall: {
                src_addr = tadr + 16;
                src_from_spr = from_spr;
                const uint32_t asp = Asp(regs.chcr);
                if (asp == 0) {
                    regs.asr0 = after_data;
                    regs.chcr = WithAsp(regs.chcr, 1);
                } else if (asp == 1) {
                    regs.asr1 = after_data;
                    regs.chcr = WithAsp(regs.chcr, 2);
                }
                // ASP 2: the two-deep stack is full; no push, the jump still happens.
                next_tadr = addr;
                next_from_spr = spr;
                do_transfer = true;
                break;
            }
            case DmacTagId::kRet: {
                src_addr = tadr + 16;
                src_from_spr = from_spr;
                const uint32_t asp = Asp(regs.chcr);
                if (asp == 2) {
                    next_tadr = regs.asr1;
                    regs.chcr = WithAsp(regs.chcr, 1);
                } else if (asp == 1) {
                    next_tadr = regs.asr0;
                    regs.chcr = WithAsp(regs.chcr, 0);
                } else {
                    tag_end = true;  // empty stack: the chain ends after this tag's data
                }
                do_transfer = true;
                break;
            }
            case DmacTagId::kEnd:
                src_addr = tadr + 16;
                src_from_spr = from_spr;
                next_tadr = after_data;
                tag_end = true;
                do_transfer = qwc > 0;
                break;
            default:
                tag_end = true;
                break;
        }
        regs.madr = src_addr;
        regs.tadr = next_tadr;
        regs.qwc = qwc;

        if (tte) {
            // The TTE quadword: the tag's upper two words, zero-padded to 16 bytes.
            uint8_t qw[16];
            std::memset(qw, 0, sizeof(qw));
            std::memcpy(qw, &hi, 8);
            FeedVif(ch, qw, 16, true);
        }
        if (do_transfer && qwc > 0) {
            const size_t bytes = static_cast<size_t>(qwc) * 16;
            const uint8_t* p = VifSpan(src_addr, src_from_spr, bytes);
            if (p) {
                FeedVif(ch, p, bytes, false);
                regs.madr += static_cast<uint32_t>(bytes);
                regs.qwc = 0;
            }
        }
        if (v == 1 && regs.qwc == 0 && tag_observer_) tag_observer_(base, from_spr, tag_count == 0);
        if (irq && tie) tag_end = true;

        tadr = next_tadr;
        from_spr = next_from_spr;
        tag_count++;
    }
    FlushStaged(ch);
    if (v == 1 && walk_end_observer_) walk_end_observer_();
    res.completed = true;
    res.tag_count = tag_count;
    return res;
}

bool ChainWalker::WalkGifChain(DmacChannelRegs& regs) {
    using namespace DmacChcr;
    if (!(regs.chcr & kStr)) return false;
    if (Mode(regs.chcr) != kModChain) return true;

    std::vector<uint8_t> payload = arbiter_.acquireBuffer();
    payload.clear();
    uint32_t cur = regs.tadr;  // the full address: the scratchpad prefix routes tag reads
    bool tag_end = false;
    int tag_count = 0;
    while (!tag_end && tag_count < kMaxTags) {
        const uint64_t lo = Load64(Translate(cur, 8));
        const uint32_t qwc = TagQwc(lo);
        const uint32_t id = TagId(lo);
        const bool irq = TagIrq(lo);
        const uint32_t addr = TagAddr(lo);
        const bool spr = TagSpr(lo);
        // GIF terminator: an all-ones low word or a QWC of 0xFFFF.
        if (lo == ~0ull || qwc == 0xFFFFu) {
            tag_end = true;
            break;
        }
        regs.chcr = (regs.chcr & 0xFFFFu) | (static_cast<uint32_t>(lo) & kTagMask);
        const uint32_t bytes = qwc * 16;
        uint32_t data_addr = 0;
        switch (id) {
            case DmacTagId::kRefe:
                data_addr = addr;
                cur += 16;
                tag_end = true;
                break;
            case DmacTagId::kCnt:
                data_addr = cur + 16;
                cur += 16 + bytes;
                break;
            case DmacTagId::kNext:
                data_addr = cur + 16;
                cur = addr;
                break;
            case DmacTagId::kRef:
            case DmacTagId::kRefs:
                data_addr = addr;
                cur += 16;
                break;
            case DmacTagId::kCall: {
                data_addr = cur + 16;
                const uint32_t asp = Asp(regs.chcr);
                if (asp == 0) {
                    regs.asr0 = cur + 16 + bytes;
                    regs.chcr = WithAsp(regs.chcr, 1);
                } else if (asp == 1) {
                    regs.asr1 = cur + 16 + bytes;
                    regs.chcr = WithAsp(regs.chcr, 2);
                }
                cur = addr;
                break;
            }
            case DmacTagId::kRet: {
                data_addr = cur + 16;
                const uint32_t asp = Asp(regs.chcr);
                if (asp == 2) {
                    cur = regs.asr1;
                    regs.chcr = WithAsp(regs.chcr, 1);
                } else if (asp == 1) {
                    cur = regs.asr0;
                    regs.chcr = WithAsp(regs.chcr, 0);
                } else {
                    tag_end = true;
                }
                break;
            }
            case DmacTagId::kEnd:
                data_addr = cur + 16;
                cur += 16 + bytes;
                tag_end = true;
                break;
            default:
                tag_end = true;
                break;
        }
        regs.madr = data_addr;
        regs.tadr = cur;
        regs.qwc = qwc;
        if (bytes > 0) {
            // REFE/REF honour the SPR bit; every other id routes by the address prefix.
            const bool use_spad = (id == DmacTagId::kRefe || id == DmacTagId::kRef)
                                      ? spr
                                      : Scratchpad::Contains(data_addr);
            const uint8_t* src = use_spad ? spad_.Pointer(Scratchpad::Offset(data_addr), bytes)
                                          : ram_.Pointer(MainMemory::Phys(data_addr), bytes);
            if (src) {
                payload.insert(payload.end(), src, src + bytes);
                regs.qwc = 0;
                regs.madr += bytes;
            }
        }
        if (irq && (regs.chcr & kTie)) tag_end = true;
        tag_count++;
    }
    if (!payload.empty())
        arbiter_.submitOwned(GifPath::Path3, std::move(payload));
    else
        arbiter_.recycleBuffer(std::move(payload));
    return true;
}

void ChainWalker::GifNormal(DmacChannelRegs& regs) {
    if (regs.qwc == 0) return;
    const size_t bytes = static_cast<size_t>(regs.qwc) * 16;
    const uint8_t* src = Translate(regs.madr, bytes);
    if (src) arbiter_.submit(GifPath::Path3, src, bytes);
    regs.madr += static_cast<uint32_t>(bytes);
    regs.qwc = 0;
}

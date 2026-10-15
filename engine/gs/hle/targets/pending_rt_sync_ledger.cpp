// gs/hle/targets/pending_rt_sync_ledger.cpp -- record / remove / overlap of the
// deferred 16-bit writebacks.
#include "engine/gs/hle/targets/pending_rt_sync_ledger.h"

#include <algorithm>

#include "engine/gs/vram/vram_image.h"

namespace ps2r {
namespace gs {

void PendingRtSyncLedger::Record(uint32_t fbp, uint32_t size, uint64_t epoch) {
    deferred_++;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].fbp != fbp) continue;
        // Latest deferring draw wins; the target may have grown.
        Entry e = entries_[i];
        e.epoch = epoch;
        if (size > e.size) e.size = size;
        entries_.erase(entries_.begin() + static_cast<long>(i));
        entries_.push_back(e);
        return;
    }
    Entry e;
    e.fbp = fbp;
    e.start = fbp;
    e.size = size;
    e.epoch = epoch;
    entries_.push_back(e);
}

void PendingRtSyncLedger::Remove(uint32_t fbp) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->fbp == fbp) { entries_.erase(it); return; }
    }
}

bool PendingRtSyncLedger::Has(uint32_t fbp) const {
    for (const Entry& e : entries_) if (e.fbp == fbp) return true;
    return false;
}

std::vector<PendingRtSyncLedger::Entry> PendingRtSyncLedger::Take(
    uint32_t start, uint32_t size, const std::function<bool(const Entry&)>& skip) {
    std::vector<Entry> out;
    for (size_t i = 0; i < entries_.size();) {
        const Entry e = entries_[i];
        if ((skip && skip(e)) || !Overlaps(e, start, size)) { ++i; continue; }
        entries_.erase(entries_.begin() + static_cast<long>(i));
        out.push_back(e);
    }
    return out;
}

bool PendingRtSyncLedger::Overlaps(const Entry& e, uint32_t start, uint32_t size) {
    if (start == 0 && size == 0) return true;
    constexpr uint64_t kV = VramImage::kBytes;
    const uint64_t a0 = start % kV, a1 = a0 + std::min<uint64_t>(size, kV);
    const uint64_t b0 = e.start % kV, b1 = b0 + std::min<uint64_t>(e.size, kV);
    // Plain overlap, or one span wrapping past the 4 MiB end into the other.
    return (a0 < b1 && b0 < a1) || (a0 + kV < b1) || (b0 + kV < a1);
}

}  // namespace gs
}  // namespace ps2r

// gs/hle/targets/pending_rt_sync_ledger.h -- PendingRtSyncLedger: the deferred
// 16-bit colour writebacks.
//
// A 16-bit-class FRAME draw does not write its rows back at once: the target is
// recorded here with its whole footprint and the draw's EE-order epoch (a second
// draw into the same target re-records it at the back with the newer epoch and
// the larger footprint). A consumer that is about to READ VRAM under a recorded
// footprint -- a texture decode over the span, a target-serve rescue, a mirror
// refresh, a frame over the pages, a scanout, a destroy, the present -- takes the
// overlapping entries out (in recording order) and the cache writes each back at
// ITS recorded epoch. The present takes only entries whose pages a texture read
// is known to observe (the reader band); everything else stays deferred.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace ps2r {
namespace gs {

class PendingRtSyncLedger {
public:
    // Save: the checkpoint boundary's settle -- every deferred entry deposited at
    // its recorded epoch so the captured mirror owes nothing (the SyncAll contract
    // without the batch-epoch-0 colour writeback of every drawn target).
    enum class Reason : uint8_t { Decode, Ftt, Refresh, PreDraw, Scanout, Destroy, Present, Save, kCount };
    struct Entry {
        uint32_t fbp = 0;
        uint32_t start = 0;
        uint32_t size = 0;      // whole target footprint in its render format
        uint64_t epoch = 0;     // the deferring draw's gfx epoch (latest wins)
    };

    void Record(uint32_t fbp, uint32_t size, uint64_t epoch);
    void Remove(uint32_t fbp);
    bool Has(uint32_t fbp) const;
    bool Empty() const { return entries_.empty(); }
    size_t Count() const { return entries_.size(); }
    const std::vector<Entry>& Entries() const { return entries_; }

    // Whether entry overlaps the wrapped span [start, start+size); start == size ==
    // 0 selects everything.
    static bool Overlaps(const Entry& e, uint32_t start, uint32_t size);
    // Take the overlapping entries out in order; `skip` (may be empty) lets the
    // caller hold entries back (the reader-band gate, the stencil-recipe hold).
    std::vector<Entry> Take(uint32_t start, uint32_t size, const std::function<bool(const Entry&)>& skip);
    void CountFlush(Reason r) { flushes_[static_cast<size_t>(r)]++; }
    uint64_t Flushes(Reason r) const { return flushes_[static_cast<size_t>(r)]; }
    uint64_t Deferred() const { return deferred_; }

private:
    std::vector<Entry> entries_;
    uint64_t flushes_[static_cast<size_t>(Reason::kCount)] = {};
    uint64_t deferred_ = 0;
};

}  // namespace gs
}  // namespace ps2r

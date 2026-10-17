// StallModel -- the entry points over the ONE body and the lane bookkeeping
// that is not per bundle. Rules in the header.
#include "engine/vu/stall_model.h"

#include <cstring>

void StallModel::Apply(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes) {
    ApplyBody<kDynamic>(s, regs, pipes);
}

void StallModel::MaterializeFmac(const VuDecodedSlot* slots, size_t close_idx, uint32_t executed, uint64_t cycle) {
    const uint32_t kmax = executed < 3u ? executed : 3u;
    for (uint32_t k = kmax; k >= 1; --k) {  // oldest first: the newest write owns its lanes
        const VuDecodedSlot& q = slots[close_idx - k];
        for (int w = 0; w < q.n_wr; ++w) PushFmac(q.wr_reg[w], q.wr_mask[w], cycle + 1 - k);
    }
}

void StallModel::MaterializeFmacIssued(const VuDecodedSlot* const* bundles, const uint64_t* issue, int n) {
    for (int i = 0; i < n; ++i) {
        const VuDecodedSlot& q = *bundles[i];
        for (int w = 0; w < q.n_wr; ++w) PushFmac(q.wr_reg[w], q.wr_mask[w], issue[i]);
    }
}

// Program end: the clock passes the newest writeback (every other one retired
// before it), then the lanes are forgotten -- a program that starts later on a
// rewound clock (a replay tool's LoadCase) must not see this run's writebacks.
void StallModel::FlushFmacWb(FlagPipeline& pipes) {
    uint64_t& cycle = pipes.cycle();
    if (fmac_newest_ > cycle) cycle = fmac_newest_;
    std::memset(fmac_retire_, 0, sizeof(fmac_retire_));
    fmac_newest_ = 0;
}

void StallModel::FlushIaluWb(FlagPipeline& pipes) {
    uint64_t& cycle = pipes.cycle();
    if (ialu_newest_ > cycle) cycle = ialu_newest_;
    std::memset(ialu_retire_, 0, sizeof(ialu_retire_));
    ialu_newest_ = 0;
}

void StallModel::Reset() {
    std::memset(fmac_retire_, 0, sizeof(fmac_retire_));
    fmac_newest_ = 0;
    std::memset(ialu_retire_, 0, sizeof(ialu_retire_));
    ialu_newest_ = 0;
}

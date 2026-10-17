// StallModel -- ring maintenance and the entry points over the ONE body.
// Rules in the header.
#include "engine/vu/stall_model.h"

void StallModel::Apply(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes) {
    ApplyBody<kDynamic>(s, regs, pipes);
}

void StallModel::MaterializeFmac(const VuDecodedSlot* slots, size_t close_idx, uint32_t executed, uint64_t cycle) {
    fmac_wb_n_ = 0;
    fmac_wb_regs_ = 0;
    fmac_wb_min_retire_ = 0;
    const uint32_t kmax = executed < 3u ? executed : 3u;
    for (uint32_t k = kmax; k >= 1; --k) {
        const VuDecodedSlot& q = slots[close_idx - k];
        for (int w = 0; w < q.n_wr; ++w) {
            const uint64_t retire = cycle + 1 - k + static_cast<uint64_t>(kFmacWbLatency);
            fmac_wb_[fmac_wb_n_++] = FmacWb{retire, q.wr_reg[w], q.wr_mask[w]};
            fmac_wb_regs_ |= 1u << q.wr_reg[w];
            if (fmac_wb_n_ == 1 || retire < fmac_wb_min_retire_) fmac_wb_min_retire_ = retire;
        }
    }
}

void StallModel::MaterializeFmacIssued(const VuDecodedSlot* const* bundles, const uint64_t* issue, int n) {
    fmac_wb_n_ = 0;
    fmac_wb_regs_ = 0;
    fmac_wb_min_retire_ = 0;
    for (int i = 0; i < n; ++i) {
        const VuDecodedSlot& q = *bundles[i];
        for (int w = 0; w < q.n_wr; ++w) PushFmac(q.wr_reg[w], q.wr_mask[w], issue[i]);
    }
}

void StallModel::FlushFmacWb(FlagPipeline& pipes) {
    uint64_t& cycle = pipes.cycle();
    for (int i = 0; i < fmac_wb_n_; ++i) {
        if (fmac_wb_[i].retire > cycle) cycle = fmac_wb_[i].retire;
    }
    fmac_wb_n_ = 0;
    fmac_wb_regs_ = 0;
    fmac_wb_min_retire_ = 0;
}

void StallModel::FlushIaluWb(FlagPipeline& pipes) {
    uint64_t& cycle = pipes.cycle();
    for (int i = 0; i < ialu_wb_n_; ++i) {
        if (ialu_wb_[i].retire > cycle) cycle = ialu_wb_[i].retire;
    }
    ialu_wb_n_ = 0;
}

void StallModel::Reset() {
    fmac_wb_n_ = 0;
    fmac_wb_regs_ = 0;
    fmac_wb_min_retire_ = 0;
    ialu_wb_n_ = 0;
}

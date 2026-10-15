// StallModel -- ring maintenance and the entry points over the ONE body.
// Rules in the header.
#include "engine/vu/stall_model.h"

void StallModel::Apply(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes) {
    ApplyBody<kDynamic>(s, regs, pipes);
}

void StallModel::ApplyClassed(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes) {
    switch (s.stall_class) {
        case 1: ApplySpec<kLower | kRd | kWr1>(s, regs, pipes); break;
        case 2: ApplySpec<kLower | kRd | kWr2>(s, regs, pipes); break;
        case 3: ApplySpec<kLower | kRd>(s, regs, pipes); break;
        case 4: ApplySpec<kLower | kWr1>(s, regs, pipes); break;
        case 5: ApplySpec<kLower | kWr2>(s, regs, pipes); break;
        case 6: ApplySpec<kLower>(s, regs, pipes); break;
        case 7: ApplySpec<kRd | kWr1>(s, regs, pipes); break;
        case 8: ApplySpec<kRd>(s, regs, pipes); break;
        case 9: ApplySpec<kWr1>(s, regs, pipes); break;
        case 10: ApplySpec<0>(s, regs, pipes); break;
        case 11: ApplySpec<kLower | kRd | kWr1 | kIaluWr>(s, regs, pipes); break;
        case 12: ApplySpec<kLower | kRd | kIaluWr>(s, regs, pipes); break;
        case 13: ApplySpec<kLower | kWr1 | kIaluWr>(s, regs, pipes); break;
        case 14: ApplySpec<kLower | kIaluWr>(s, regs, pipes); break;
        default: Apply(s, regs, pipes); break;
    }
}

void StallModel::ApplyPrunePush(const VuDecodedSlot& s, FlagPipeline& pipes) {
    const uint64_t cycle = pipes.cycle();
    if (fmac_wb_n_ != 0 && fmac_wb_min_retire_ <= cycle) PruneFmac(cycle);
    for (int w = 0; w < s.n_wr; ++w) PushFmac(s.wr_reg[w], s.wr_mask[w], cycle);
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

void StallModel::PruneFmac(uint64_t cycle) {
    int keep = 0;
    uint32_t regs = 0;
    uint64_t mn = ~0ull;
    for (int i = 0; i < fmac_wb_n_; ++i) {
        if (fmac_wb_[i].retire > cycle) {
            fmac_wb_[keep] = fmac_wb_[i];
            regs |= 1u << fmac_wb_[keep].reg;
            if (fmac_wb_[keep].retire < mn) mn = fmac_wb_[keep].retire;
            ++keep;
        }
    }
    fmac_wb_n_ = keep;
    fmac_wb_regs_ = regs;
    fmac_wb_min_retire_ = mn;
}

void StallModel::PruneIalu(uint64_t cycle) {
    int keep = 0;
    for (int i = 0; i < ialu_wb_n_; ++i) {
        if (ialu_wb_[i].retire > cycle) ialu_wb_[keep++] = ialu_wb_[i];
    }
    ialu_wb_n_ = keep;
}

// A full ring drops its soonest-retiring entry (the last entry moves into its slot).
void StallModel::PushFmac(uint8_t reg, uint8_t mask, uint64_t cycle) {
    if (fmac_wb_n_ == kFmacDepth) {
        int old = 0;
        for (int i = 1; i < kFmacDepth; ++i) {
            if (fmac_wb_[i].retire < fmac_wb_[old].retire) old = i;
        }
        fmac_wb_[old] = fmac_wb_[fmac_wb_n_ - 1];
        --fmac_wb_n_;
    }
    const uint64_t retire = cycle + kFmacWbLatency;
    fmac_wb_[fmac_wb_n_++] = FmacWb{retire, reg, mask};
    fmac_wb_regs_ |= 1u << reg;
    if (fmac_wb_n_ == 1 || retire < fmac_wb_min_retire_) fmac_wb_min_retire_ = retire;
}

void StallModel::PushIalu(uint8_t vi, uint64_t cycle) {
    if (ialu_wb_n_ == kIaluDepth) {
        int old = 0;
        for (int i = 1; i < kIaluDepth; ++i) {
            if (ialu_wb_[i].retire < ialu_wb_[old].retire) old = i;
        }
        ialu_wb_[old] = ialu_wb_[ialu_wb_n_ - 1];
        --ialu_wb_n_;
    }
    ialu_wb_[ialu_wb_n_++] = IaluWb{cycle + kIaluLwLatency, vi};
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

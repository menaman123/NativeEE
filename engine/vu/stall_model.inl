// stall_model.inl -- the ONE stall body as a template over the class mask.
// Included from stall_model.h only.
#include "engine/platform/host/platform.h"

template <uint8_t M>
void StallModel::ApplyBody(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes) {
    constexpr bool kDyn = (M & kDynamic) != 0;
    uint64_t& cycle = pipes.cycle();

    // 1. Drop FMAC writebacks that have already retired.
    if (fmac_wb_n_ != 0 && fmac_wb_min_retire_ <= cycle) PruneFmac(cycle);

    // 2. Drop retired IALU writebacks (only bundles with a lower word look).
    if constexpr (kDyn) {
        if (!s.i_bit && ialu_wb_n_ != 0) PruneIalu(cycle);
    } else if constexpr ((M & kLower) != 0) {
        if (ialu_wb_n_ != 0) PruneIalu(cycle);
    }

    // 3. FMAC read stall: a read lane overlapping an in-flight write waits for it.
    if constexpr (kDyn || (M & kRd) != 0) {
        if (s.n_rd != 0 && fmac_wb_n_ != 0 && (s.rd_regs & fmac_wb_regs_) != 0) {
            for (int r = 0; r < s.n_rd; ++r) {
                for (int i = 0; i < fmac_wb_n_; ++i) {
                    const FmacWb& wb = fmac_wb_[i];
                    if (wb.reg == s.rd_reg[r] && (wb.mask & s.rd_mask[r]) != 0 && wb.retire > cycle) {
                        cycle = wb.retire;
                    }
                }
            }
        }
    }

    if constexpr (kDyn || (M & kBranch) != 0) {
        // 4. IALU branch stall: a branch reading a VI still being loaded waits.
        if (s.ialu_branch_rd != 0 && ialu_wb_n_ != 0) {
            for (int i = 0; i < ialu_wb_n_; ++i) {
                if ((s.ialu_branch_rd & (1u << ialu_wb_[i].vi)) != 0 && ialu_wb_[i].retire > cycle) {
                    cycle = ialu_wb_[i].retire;
                }
            }
        }
    }
    if constexpr (kDyn) {
        // 5. EFU busy-wait: the pending P is released one cycle early.
        if (s.efu_pipe_op) pipes.ReleasePEarly(regs);
        // 6. FDIV busy-wait: the pending Q is waited for and committed.
        if (s.fdiv_pipe_op) pipes.FlushQPipe(regs);
    }

    // 7. Queue this bundle's VF writebacks.
    if constexpr (kDyn) {
        for (int w = 0; w < s.n_wr; ++w) PushFmac(s.wr_reg[w], s.wr_mask[w], cycle);
    } else {
        if constexpr ((M & (kWr1 | kWr2)) != 0) PushFmac(s.wr_reg[0], s.wr_mask[0], cycle);
        if constexpr ((M & kWr2) != 0) PushFmac(s.wr_reg[1], s.wr_mask[1], cycle);
    }

    // 8. Queue an ILW / ILWR VI writeback.
    if constexpr (kDyn) {
        if (s.ialu_wr_vi >= 0) PushIalu(static_cast<uint8_t>(s.ialu_wr_vi), cycle);
    } else if constexpr ((M & kIaluWr) != 0) {
        PushIalu(static_cast<uint8_t>(s.ialu_wr_vi), cycle);
    }
}

template <uint8_t M>
void StallModel::ApplySpec(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes) {
    ApplyBody<static_cast<uint8_t>(M & ~kDynamic)>(s, regs, pipes);
}

// ---- the per-bundle entries and the ring maintenance, inlined for the tiers ----

PS2R_FORCE_INLINE void StallModel::ApplyClassed(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes) {
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
        case 15: ApplySpec<kLower | kBranch>(s, regs, pipes); break;
        case 16: ApplySpec<kLower | kBranch | kRd>(s, regs, pipes); break;
        case 17: ApplySpec<kLower | kBranch | kWr1>(s, regs, pipes); break;
        case 18: ApplySpec<kLower | kBranch | kRd | kWr1>(s, regs, pipes); break;
        default: Apply(s, regs, pipes); break;
    }
}

PS2R_FORCE_INLINE void StallModel::ApplyPrunePush(const VuDecodedSlot& s, FlagPipeline& pipes) {
    const uint64_t cycle = pipes.cycle();
    if (fmac_wb_n_ != 0 && fmac_wb_min_retire_ <= cycle) PruneFmac(cycle);
    for (int w = 0; w < s.n_wr; ++w) PushFmac(s.wr_reg[w], s.wr_mask[w], cycle);
}

PS2R_FORCE_INLINE void StallModel::PruneFmac(uint64_t cycle) {
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

inline void StallModel::PruneIalu(uint64_t cycle) {
    int keep = 0;
    for (int i = 0; i < ialu_wb_n_; ++i) {
        if (ialu_wb_[i].retire > cycle) ialu_wb_[keep++] = ialu_wb_[i];
    }
    ialu_wb_n_ = keep;
}

// A full ring drops its soonest-retiring entry (the last entry moves into its slot).
PS2R_FORCE_INLINE void StallModel::PushFmac(uint8_t reg, uint8_t mask, uint64_t cycle) {
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

inline void StallModel::PushIalu(uint8_t vi, uint64_t cycle) {
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

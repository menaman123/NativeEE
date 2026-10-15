// stall_model.inl -- the ONE stall body as a template over the class mask.
// Included from stall_model.h only.

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

    if constexpr (kDyn) {
        // 4. IALU branch stall: a branch reading a VI still being loaded waits.
        if (s.ialu_branch_rd != 0 && ialu_wb_n_ != 0) {
            for (int i = 0; i < ialu_wb_n_; ++i) {
                if ((s.ialu_branch_rd & (1u << ialu_wb_[i].vi)) != 0 && ialu_wb_[i].retire > cycle) {
                    cycle = ialu_wb_[i].retire;
                }
            }
        }
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

// stall_model.inl -- the ONE stall body as a template over the class mask.
// Included from stall_model.h only.
#include "engine/platform/host/platform.h"

template <uint8_t M>
void StallModel::ApplyBody(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes) {
    constexpr bool kDyn = (M & kDynamic) != 0;
    uint64_t& cycle = pipes.cycle();

    // 1-2. (The OLD ring's prunes: a retired writeback is inert here, nothing to drop.)

    // 3. FMAC read stall: a read lane overlapping an in-flight write waits for it.
    if constexpr (kDyn || (M & kRd) != 0) {
        if (s.n_rd != 0 && fmac_newest_ > cycle) {
            for (int r = 0; r < s.n_rd; ++r) {
                const uint64_t retire = ReadStall(s.rd_reg[r], s.rd_mask[r], cycle);
                if (retire > cycle) cycle = retire;
            }
        }
    }

    if constexpr (kDyn || (M & kBranch) != 0) {
        // 4. IALU branch stall: a branch reading a VI still being loaded waits.
        if (s.ialu_branch_rd != 0 && ialu_newest_ > cycle) {
            for (uint32_t bits = s.ialu_branch_rd, vi = 0; bits != 0; bits >>= 1, ++vi) {
                if ((bits & 1u) != 0 && ialu_retire_[vi] > cycle) cycle = ialu_retire_[vi];
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

// ---- the per-bundle entries and the lane bookkeeping, inlined for the tiers ----

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
    for (int w = 0; w < s.n_wr; ++w) PushFmac(s.wr_reg[w], s.wr_mask[w], cycle);
}

// The largest retire among the lanes `mask` selects (bit 3 = lane 0 ... bit 0 = lane 3).
PS2R_FORCE_INLINE uint64_t StallModel::ReadStall(uint8_t reg, uint8_t mask, uint64_t) const {
    const uint64_t* r = fmac_retire_[reg];
    uint64_t m = 0;
    if (mask & 8) m = r[0];
    if ((mask & 4) && r[1] > m) m = r[1];
    if ((mask & 2) && r[2] > m) m = r[2];
    if ((mask & 1) && r[3] > m) m = r[3];
    return m;
}

PS2R_FORCE_INLINE void StallModel::PushFmac(uint8_t reg, uint8_t mask, uint64_t cycle) {
    const uint64_t retire = cycle + kFmacWbLatency;
    uint64_t* r = fmac_retire_[reg];
    if (mask & 8) r[0] = retire;
    if (mask & 4) r[1] = retire;
    if (mask & 2) r[2] = retire;
    if (mask & 1) r[3] = retire;
    fmac_newest_ = retire;
}

PS2R_FORCE_INLINE void StallModel::PushIalu(uint8_t vi, uint64_t cycle) {
    const uint64_t retire = cycle + kIaluLwLatency;
    ialu_retire_[vi & 0xF] = retire;
    ialu_newest_ = retire;
}

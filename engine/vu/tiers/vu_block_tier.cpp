// VuBlockTier -- the program table, the block builder with its static stall
// schedule, and the block loop. Rules in the header; the instruction bodies
// are the cached tier's resolved tables (vu_cached_tier_exec.cpp).
#include "engine/vu/tiers/vu_block_tier.h"

#include <chrono>
#include <cstring>

#include "engine/platform/host/platform.h"
#include "engine/vu/span_classifier.h"
#include "engine/vu/tiers/vu_interpreter_tier.h"
#include "engine/vu/vu_chop_round_guard.h"
#include "engine/vu/vu_float.h"
#include "engine/vu/vu_image_hash.h"
#include "engine/vu/vu_unit.h"

static_assert(StallModel::kFmacWbLatency == 4, "the static schedule's recurrence is written for a 4-cycle writeback");

namespace {

uint32_t Le32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

constexpr int kNopSledBundles = 4;  // four all-zero bundles end the run (uninitialised micro memory)

// The stall model entries as plain functions (one per static class; the
// dynamic body for class 0): what an op's `stall` pointer resolves to.
template <uint8_t M>
void StallSpec(StallModel& m, const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes) {
    m.ApplySpec<M>(s, regs, pipes);
}
void StallDynamic(StallModel& m, const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes) { m.Apply(s, regs, pipes); }

// The entry for a class: the ApplySpec instantiation of its mask (the
// StallClassMask table), the dynamic body for class 0 / unknown.
VuBlockOp::StallFn StallFnForClass(uint8_t cls) {
    using S = StallModel;
    switch (VuBlockTier::StallClassMask(cls)) {
        case S::kLower | S::kRd | S::kWr1: return &StallSpec<S::kLower | S::kRd | S::kWr1>;
        case S::kLower | S::kRd | S::kWr2: return &StallSpec<S::kLower | S::kRd | S::kWr2>;
        case S::kLower | S::kRd: return &StallSpec<S::kLower | S::kRd>;
        case S::kLower | S::kWr1: return &StallSpec<S::kLower | S::kWr1>;
        case S::kLower | S::kWr2: return &StallSpec<S::kLower | S::kWr2>;
        case S::kLower: return &StallSpec<S::kLower>;
        case S::kRd | S::kWr1: return &StallSpec<S::kRd | S::kWr1>;
        case S::kRd: return &StallSpec<S::kRd>;
        case S::kWr1: return &StallSpec<S::kWr1>;
        case 0: return &StallSpec<0>;
        case S::kLower | S::kRd | S::kWr1 | S::kIaluWr: return &StallSpec<S::kLower | S::kRd | S::kWr1 | S::kIaluWr>;
        case S::kLower | S::kRd | S::kIaluWr: return &StallSpec<S::kLower | S::kRd | S::kIaluWr>;
        case S::kLower | S::kWr1 | S::kIaluWr: return &StallSpec<S::kLower | S::kWr1 | S::kIaluWr>;
        case S::kLower | S::kIaluWr: return &StallSpec<S::kLower | S::kIaluWr>;
        case S::kLower | S::kBranch: return &StallSpec<S::kLower | S::kBranch>;
        case S::kLower | S::kBranch | S::kRd: return &StallSpec<S::kLower | S::kBranch | S::kRd>;
        case S::kLower | S::kBranch | S::kWr1: return &StallSpec<S::kLower | S::kBranch | S::kWr1>;
        case S::kLower | S::kBranch | S::kRd | S::kWr1: return &StallSpec<S::kLower | S::kBranch | S::kRd | S::kWr1>;
        default: return &StallDynamic;
    }
}

// Does any write of `w` land on a lane `s` reads (the stall model's step 3 test)?
bool WriteHitsRead(const VuDecodedSlot& w, const VuDecodedSlot& s) {
    for (int r = 0; r < s.n_rd; ++r) {
        for (int i = 0; i < w.n_wr; ++i) {
            if (w.wr_reg[i] == s.rd_reg[r] && (w.wr_mask[i] & s.rd_mask[r]) != 0) return true;
        }
    }
    return false;
}

}  // namespace

VuBlockTier::VuBlockTier(VuInterpreterTier& generic, uint64_t cycle_cap) : generic_(generic), cycle_cap_(cycle_cap) {}

// ---- the static facts ------------------------------------------------------

int VuBlockTier::StallClassMask(uint8_t cls) {
    using S = StallModel;
    switch (cls) {
        case 1: return S::kLower | S::kRd | S::kWr1;
        case 2: return S::kLower | S::kRd | S::kWr2;
        case 3: return S::kLower | S::kRd;
        case 4: return S::kLower | S::kWr1;
        case 5: return S::kLower | S::kWr2;
        case 6: return S::kLower;
        case 7: return S::kRd | S::kWr1;
        case 8: return S::kRd;
        case 9: return S::kWr1;
        case 10: return 0;
        case 11: return S::kLower | S::kRd | S::kWr1 | S::kIaluWr;
        case 12: return S::kLower | S::kRd | S::kIaluWr;
        case 13: return S::kLower | S::kWr1 | S::kIaluWr;
        case 14: return S::kLower | S::kIaluWr;
        case 15: return S::kLower | S::kBranch;
        case 16: return S::kLower | S::kBranch | S::kRd;
        case 17: return S::kLower | S::kBranch | S::kWr1;
        case 18: return S::kLower | S::kBranch | S::kRd | S::kWr1;
        default: return -1;
    }
}

bool VuBlockTier::IsTerminal(const VuDecodedSlot& s) {
    if (s.e_bit || s.is_nop_pair) return true;
    return !s.i_bit && !s.discard_lower && VuDecoder::IsBranchKind(s.lower_kind);
}

void VuBlockTier::ResolveOp(VuBlockOp& op, const VuDecodedSlot& s) {
    op.slot = &s;
    op.upper = s.upper_kind == VUK_U_NOP ? nullptr : VuCachedTier::UpperFastFn(s.upper_kind);
    const bool lower_issues = !s.i_bit && !s.discard_lower && s.lower_kind != VUK_L_NOP;
    op.lower = lower_issues ? VuCachedTier::LowerFastFn(s.lower_kind) : nullptr;
    op.stall = StallFnForClass(s.stall_class);
    op.s_delta = 0;
    // What the model would do for this bundle when nothing of it can stall:
    // no VF read and no IALU write -> only the ring hygiene (skipped: a retired
    // entry never stalls and is the first an overflow drops) and its own VF
    // pushes (done inline).
    const bool dynamic = s.stall_class == 0 || s.ialu_branch_rd != 0;
    if (dynamic || s.n_rd != 0 || s.ialu_wr_vi >= 0) op.stall_kind = VuBlockOp::kStallModel;
    else op.stall_kind = s.n_wr != 0 ? VuBlockOp::kStallPush : VuBlockOp::kStallNone;
    uint16_t f = 0;
    if (s.i_bit) f |= VuBlockOp::kIBit;
    if (!s.i_bit && s.hide_upper) f |= VuBlockOp::kHideUpper;
    if (s.touch_hw) f |= VuBlockOp::kTouchHw;
    if (s.ialu_wr_vi >= 0) f |= VuBlockOp::kIaluWr;
    if (lower_issues && s.lower_kind == VUK_L_XTOP) f |= VuBlockOp::kXtop;
    if (lower_issues && s.lower_kind == VUK_L_XGKICK) f |= VuBlockOp::kXgkick;
    // The model alone knows: an unclassed bundle (FDIV / EFU waits among them)
    // and the branch classes (the IALU ring read).
    if (dynamic) f |= VuBlockOp::kDynamic;
    if (s.is_nop_pair) f |= VuBlockOp::kNopPair;
    if (s.e_bit) f |= VuBlockOp::kEBit;
    op.flags = f;
    op.ialu_vi = s.ialu_wr_vi >= 0 ? static_cast<uint8_t>(s.ialu_wr_vi) : 0;
    op.upper_vf_write = s.upper_vf_write >= 0 ? static_cast<uint8_t>(s.upper_vf_write) : 0;
}

uint32_t VuBlockTier::StaticStall(const VuDecodedSlot& s, const VuDecodedSlot* back1, const VuDecodedSlot* back2,
                                  const VuDecodedSlot* back3, uint32_t stall_back1, uint32_t stall_back2) {
    if (s.n_rd == 0) return 0;
    // The op before issued one cycle before this op's pre-stall cycle, the
    // one before that `2 + stall_back1` cycles before, the third `3 +
    // stall_back1 + stall_back2`; a writeback retires 4 cycles after issue and
    // stalls a reader only while `retire > cycle`.
    uint32_t stall = 0;
    if (back1 != nullptr && WriteHitsRead(*back1, s)) stall = 3;
    if (back2 != nullptr && 2 > stall_back1 && 2 - stall_back1 > stall && WriteHitsRead(*back2, s)) stall = 2 - stall_back1;
    const uint32_t gap3 = stall_back1 + stall_back2;
    if (back3 != nullptr && 1 > gap3 && 1 - gap3 > stall && WriteHitsRead(*back3, s)) stall = 1 - gap3;
    return stall;
}

void VuBlockTier::Schedule(VuBlockOp* ops, size_t n) {
    for (size_t k = 0; k < n; ++k) {
        const VuDecodedSlot* b1 = k >= 1 ? ops[k - 1].slot : nullptr;
        const VuDecodedSlot* b2 = k >= 2 ? ops[k - 2].slot : nullptr;
        const VuDecodedSlot* b3 = k >= 3 ? ops[k - 3].slot : nullptr;
        const uint32_t s1 = k >= 1 ? ops[k - 1].s_delta : 0u;
        const uint32_t s2 = k >= 2 ? ops[k - 2].s_delta : 0u;
        ops[k].s_delta = static_cast<uint8_t>(StaticStall(*ops[k].slot, b1, b2, b3, s1, s2));
    }
}

// ---- the program table -----------------------------------------------------

void VuBlockTier::DecodeImage(const uint8_t* micro, size_t bytes, VuBlockProgram& prog) {
    const size_t n_slots = bytes / 8;
    prog.slots.resize(n_slots);
    for (size_t i = 0; i < n_slots; ++i) {
        prog.slots[i] = VuDecoder::DecodeVuSlot(Le32(micro + i * 8), Le32(micro + i * 8 + 4));
    }
    VuDecodedSlot* slots = prog.slots.data();
    SpanClassifier::ComputeRunLenTouchHw(slots, n_slots);  // touch_hw (run_len is unused here)
    SpanClassifier::ComputeStallClasses(slots, n_slots);
    prog.block_at.assign(n_slots, nullptr);
    prog.blocks.clear();
    prog.native = nullptr;
}

VuBlockProgram* VuBlockTier::Acquire(VuUnit& unit) { return Lookup(unit); }

VuBlockProgram* VuBlockTier::Lookup(VuUnit& unit) {
    const uint64_t gen = unit.micro_generation();
    if (current_ != nullptr && current_->generation == gen) {  // same epoch: no hash, no scan
        ++current_->exec_count;
        return current_;
    }
    const Vu1State& st = unit.state();
    const uint64_t h = VuImageHash::Fast(st.micro, static_cast<size_t>(st.mem_mask + 1));
    VuBlockProgram* found = nullptr;
    if (current_ != nullptr && current_->hash == h) {
        found = current_;
    } else {
        for (auto& p : table_) {
            if (p->hash == h) {
                found = p.get();
                break;
            }
        }
    }
    if (found != nullptr) {
        found->generation = gen;
        ++found->exec_count;
        current_ = found;
        return current_;
    }
    if (table_.size() >= kTableCap) {
        size_t victim = 0;
        for (size_t i = 1; i < table_.size(); ++i) {
            if (table_[i]->exec_count < table_[victim]->exec_count) victim = i;
        }
        if (table_[victim].get() == current_) current_ = nullptr;
        table_.erase(table_.begin() + static_cast<std::ptrdiff_t>(victim));
    }
    std::unique_ptr<VuBlockProgram> prog(new VuBlockProgram());
    prog->hash = h;
    prog->generation = gen;
    prog->exec_count = 1;
    const Vu1State& dst = unit.state();
    DecodeImage(dst.micro, static_cast<size_t>(dst.mem_mask + 1), *prog);
    current_ = prog.get();
    table_.push_back(std::move(prog));
    return current_;
}

VuBlock* VuBlockTier::BuildBlock(VuBlockProgram& prog, size_t slot_idx) {
    if (VuBlock* have = prog.block_at[slot_idx]) return have;
    std::unique_ptr<VuBlock> blk(new VuBlock());
    blk->pc = static_cast<uint32_t>(slot_idx * 8);
    const size_t n_slots = prog.slots.size();
    const VuBlockOpResolver resolve = prog.resolve != nullptr ? prog.resolve : &VuBlockTier::ResolveOp;
    size_t i = slot_idx;
    for (;;) {
        const VuDecodedSlot& s = prog.slots[i];
        VuBlockOp op;
        resolve(op, s);
        blk->ops.push_back(op);
        const bool last_slot = i + 1 >= n_slots;  // the image end: the next pc wraps, the block does not
        // A NOP pair is a block of its own (the sled count is per block), so the
        // block before one ends there.
        const bool next_is_nop = !last_slot && prog.slots[i + 1].is_nop_pair;
        if (IsTerminal(s) || last_slot || next_is_nop || blk->ops.size() >= static_cast<size_t>(kMaxBlockOps)) {
            blk->n_body = static_cast<int>(blk->ops.size());
            blk->e_bit = s.e_bit;
            const bool branch = op.lower != nullptr && VuDecoder::IsBranchKind(s.lower_kind);
            if (branch && !s.e_bit) {  // (a branch on the E-bit bundle is ignored)
                blk->branch_idx = blk->n_body - 1;
                const uint32_t pc_t = static_cast<uint32_t>(i * 8);
                if (s.lower_kind == VUK_L_JR || s.lower_kind == VUK_L_JALR) {
                    blk->static_target = -1;
                } else {
                    blk->static_target = static_cast<int32_t>(
                        static_cast<uint32_t>(static_cast<int32_t>(pc_t + 8) + s.l_imm11 * 8) & kPcMask);
                }
            }
            // The delay slot joins the block when it does not wrap: whichever way
            // the branch goes it is the next bundle to issue, its own branch / E
            // bit are ignored and it never advances TPC.
            if ((branch || s.e_bit) && !last_slot) {
                VuBlockOp ds;
                resolve(ds, prog.slots[i + 1]);
                blk->ops.push_back(ds);
                blk->has_ds = true;
            }
            break;
        }
        ++i;
    }
    Schedule(blk->ops.data(), blk->ops.size());
    // The sled facts the loop reads per block, cached off the ops array (the
    // native path never touches the ops otherwise). Structural: the slot's.
    blk->head_nop = (blk->ops[0].flags & VuBlockOp::kNopPair) != 0;
    blk->ds_nop = blk->has_ds && (blk->ops[static_cast<size_t>(blk->n_body)].flags & VuBlockOp::kNopPair) != 0;
    VuBlock* raw = blk.get();
    prog.block_at[slot_idx] = raw;
    prog.blocks.push_back(std::move(blk));
    return raw;
}

VuBlock* VuBlockTier::Build(VuBlockProgram& prog, size_t slot_idx) {
    ++stats_.blocks_built;
    return BuildBlock(prog, slot_idx);
}

VuBlock* VuBlockTier::BlockAt(VuBlockProgram& prog, uint32_t pc) {
    const size_t idx = pc >> 3;
    VuBlock* b = prog.block_at[idx];
    return b != nullptr ? b : Build(prog, idx);
}

void VuBlockTier::ClearNative(VuBlockProgram& prog) {
    for (auto& blk : prog.blocks) blk->native = nullptr;
    prog.native = nullptr;
}

void VuBlockTier::SetResolver(VuBlockProgram& prog, VuBlockOpResolver resolve) {
    prog.resolve = resolve;
    const VuBlockOpResolver r = resolve != nullptr ? resolve : &VuBlockTier::ResolveOp;
    for (auto& blk : prog.blocks) {
        for (VuBlockOp& op : blk->ops) r(op, *op.slot);
        Schedule(blk->ops.data(), blk->ops.size());
    }
}

// ---- execution -------------------------------------------------------------

void VuBlockTier::Execute(VuUnit& unit, uint32_t start_pc) { Execute(unit, start_pc, cycle_cap_); }

void VuBlockTier::Execute(VuUnit& unit, uint32_t start_pc, uint64_t cycle_limit) {
    const uint32_t pc = start_pc & kPcMask;
    if (unit.id() != 1 || (pc & 7u) != 0) {  // not this tier's: the reference runs it
        VuInterpreterTier::RunOptions opt;
        opt.cycle_limit = cycle_limit;
        generic_.Run(unit, start_pc, opt);
        return;
    }
    Run(unit, pc, *Lookup(unit), cycle_limit);  // at every entry (MSCAL / MSCNT / resume)
}

void VuBlockTier::ExecuteAcquired(VuUnit& unit, uint32_t pc, VuBlockProgram& prog) { Run(unit, pc, prog, cycle_cap_); }

void VuBlockTier::Run(VuUnit& unit, uint32_t pc, VuBlockProgram& prog, uint64_t cycle_limit) {
    const auto t0 = std::chrono::steady_clock::now();
    VuChopRoundGuard round_guard;  // FMAC/FDIV/EFU truncate for the whole run
    Vu1State& st = unit.state();
    VUregs& regs = st.regs;

    regs.VF[0].x = 0.0f;
    regs.VF[0].y = 0.0f;
    regs.VF[0].z = 0.0f;
    regs.VF[0].w = 1.0f;
    st.suspended = false;
    st.xtop_read_count = 0;
    st.xtop_exit_requested = false;
    st.xgkick_has_fired = false;
    st.xgkick_count = 0;

    const uint64_t before = bundles_;
    Loop(unit, pc, prog, cycle_limit);
    prog.bundles += bundles_ - before;
    ++stats_.runs;
    stats_.run_ns += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
}

void VuBlockTier::DrainAll(VuUnit& unit) { generic_.DrainAll(unit); }

void VuBlockTier::FoldStats(const Stats& acc) {
    stats_.blocks_run += acc.blocks_run;
    stats_.ops_static += acc.ops_static;
    stats_.ops_model += acc.ops_model;
    stats_.blocks_native += acc.blocks_native;
    stats_.ops_native += acc.ops_native;
}

template <bool kSched>
PS2R_FORCE_INLINE int VuBlockTier::RunOps(VuUnit& unit, const VuBlockOp* ops, int n, uint32_t pc, int branch_idx,
                                          bool& taken, uint32_t& target, Stats& acc) {
    Vu1State& st = unit.state();
    VUregs& regs = st.regs;
    FlagPipeline& pipes = unit.flags();
    StallModel& stalls = unit.stalls();
    XgKick* const xgk = unit.xgkick();
    VuInterpreterTier& g = generic_;
    uint64_t& cycle = pipes.cycle();
    taken = false;
    bool t = false;
    uint32_t tg = 0;
    const uint64_t bundles_before = bundles_;
    // The kick's next pacing deadline (re-read after a Pace and after an XGKICK).
    constexpr uint64_t kNever = ~0ull;
    uint64_t pace_due = (xgk != nullptr && xgk->armed()) ? xgk->next_due() : kNever;

    uint64_t issue[kSched ? kMaxBlockOps + 1 : 1];  // issue cycle per op (the ring rebuild on leaving static mode)
    bool is_static = false;
    int streak = 0;
    int guard = 0;  // ops still to consult the model after a post-issue clock advance
    uint64_t ops_static = 0;

    // Leaving static mode after op `i - 1`: the FMAC ring as the model would hold it.
    auto materialize = [&](int i) {
        const int k = i < kRingWindow ? i : kRingWindow;
        const VuDecodedSlot* bundles[kRingWindow];
        for (int j = 0; j < k; ++j) bundles[j] = ops[i - k + j].slot;
        stalls.MaterializeFmacIssued(bundles, issue + (i - k), k);
        is_static = false;
        streak = 0;
    };

    int i = 0;
    for (; i < n; ++i) {
        const VuBlockOp& op = ops[i];
        const VuDecodedSlot& s = *op.slot;
        const uint16_t f = op.flags;
        ++cycle;

        if (kSched && is_static && (f & VuBlockOp::kDynamic) == 0) {
            cycle += op.s_delta;
            if (f & VuBlockOp::kIaluWr) stalls.QueueIalu(op.ialu_vi, cycle);
            ++ops_static;
        } else {
            if (kSched && is_static) materialize(i);
            if (op.stall_kind == VuBlockOp::kStallModel) {
                const uint64_t before = cycle;
                op.stall(stalls, s, regs, pipes);
                if (kSched) streak = (cycle - before == op.s_delta) ? streak + 1 : 0;
            } else {
                if (op.stall_kind == VuBlockOp::kStallPush) stalls.ApplyPrunePush(s, pipes);
                if (kSched) ++streak;  // a stall of 0, as scheduled
            }
            if (kSched) {
                if (guard > 0) --guard;
                // Static mode pays (the ring rebuild at its end) only over a few more ops.
                if (guard == 0 && streak >= kMatchStreak && i + 1 >= kWarmupOps && n - (i + 1) >= kMinStaticOps)
                    is_static = true;
            }
        }
        const uint64_t issued = cycle;
        if (kSched) issue[i] = issued;

        // Drain what the elapsed cycles allow (the kick's own quantum gate).
        if (cycle >= pace_due) {
            xgk->Pace(st.data, cycle);
            pace_due = xgk->armed() ? xgk->next_due() : kNever;
        }

        if (f & VuBlockOp::kIBit) {
            if (op.upper != nullptr) op.upper(g, unit, s);  // the upper reads the OLD I
            else VuFloat::ClampStoreACC(regs.ACC);
            regs.I = s.lower;
        } else if (f & VuBlockOp::kHideUpper) {
            // Both words read at the same stage: the lower sees the pre-upper VF.
            const int uvw = op.upper_vf_write;
            const VR_reg vf_pre = regs.VF[uvw];
            if (op.upper != nullptr) op.upper(g, unit, s);
            else VuFloat::ClampStoreACC(regs.ACC);
            const VR_reg vf_post = regs.VF[uvw];
            regs.VF[uvw] = vf_pre;
            if (op.lower != nullptr) op.lower(g, unit, s, pc, t, tg);
            regs.VF[uvw] = vf_post;
        } else {
            if (op.upper != nullptr) op.upper(g, unit, s);
            else VuFloat::ClampStoreACC(regs.ACC);
            if (op.lower != nullptr) op.lower(g, unit, s, pc, t, tg);
        }
        if (i == branch_idx) {
            taken = t;
            target = tg;
        }
        if (f & VuBlockOp::kTouchHw) PinHardwired(regs);
        if (f & VuBlockOp::kXgkick) pace_due = xgk->armed() ? xgk->next_due() : kNever;
        if (kSched && cycle != issued) {
            // A post-issue clock advance (a second XGKICK's stall, a WAITQ /
            // WAITP flush): the schedule's one-cycle issue gaps no longer hold
            // for the three ops that can still read this op's writebacks.
            if (is_static) materialize(i + 1);
            streak = 0;
            guard = kRingWindow;
        }
        pc += 8;
        if ((f & VuBlockOp::kXtop) && st.xtop_exit_requested) {
            ++i;
            break;
        }
    }
    if (kSched && is_static) materialize(i);
    // The last bundle executed (a diagnostic the probes and the replay tool
    // print; the cached tier records it per bundle).
    if (i > 0) st.cur_bundle_pc = pc - 8u;
    bundles_ = bundles_before + static_cast<uint64_t>(i);
    acc.ops_static += ops_static;
    acc.ops_model += static_cast<uint64_t>(i) - ops_static;
    return i;
}

void VuBlockTier::Loop(VuUnit& unit, uint32_t pc, VuBlockProgram& prog, uint64_t cycle_limit) {
    Vu1State& st = unit.state();
    VUregs& regs = st.regs;
    FlagPipeline& pipes = unit.flags();
    int consecutive_nops = 0;
    Stats acc;  // this run's counters, folded into stats_ at the end

    // The XTOP yield: the run ends at this bundle boundary with TPC at the
    // last normal advance (inside a block's body that is the yielding op's
    // own address; a delay slot or a block's first op leaves it).
    auto yield = [&](uint32_t op_pc, bool set_tpc) {
        st.xtop_exit_requested = false;
        st.xtop_read_count = 0;
        if (set_tpc) regs.TPC = op_pc;
        DrainAll(unit);
        FoldStats(acc);
    };
    // The delay slot of a branch on the image's last bundle (wrapped to 0,
    // not merged into its block): one op of the block at `ds_pc`. 0 = ran,
    // 1 = the NOP sled ended the run, 2 = it yielded (drained).
    auto delay_slot = [&](uint32_t ds_pc) -> int {
        const VuBlock* blk = BlockAt(prog, ds_pc);
        --cycle_limit;
        if (blk->head_nop) {
            if (++consecutive_nops >= kNopSledBundles) {
                ++pipes.cycle();
                ++bundles_;
                return 1;
            }
        } else {
            consecutive_nops = 0;
        }
        bool t = false;
        uint32_t tg = 0;
        ++acc.blocks_run;
        if (RunOps<false>(unit, blk->ops.data(), 1, ds_pc, -1, t, tg, acc) < 1) {
            yield(ds_pc, false);
            return 2;
        }
        return 0;
    };

    VuBlock* blk = BlockAt(prog, pc);
    while (cycle_limit > 0) {
        int n_body = blk->n_body;
        if (PS2R_UNLIKELY(blk->head_nop)) {  // a NOP pair is a block of its own
            if (++consecutive_nops >= kNopSledBundles) {
                ++pipes.cycle();
                ++bundles_;
                break;
            }
        } else {
            consecutive_nops = blk->ds_nop ? 1 : 0;
        }
        ++acc.blocks_run;
        bool taken = false;
        uint32_t target = 0;
        bool cut = false;
        int n = static_cast<int>(blk->ops.size());
        if (blk->native != nullptr && static_cast<uint64_t>(n) <= cycle_limit) {
            // The generated REGION function (the hot path: 99.99 % of the blocks
            // of a pack-fed run): this block and the blocks it can reach over
            // fall-through / static targets within the cap (the budget), the
            // register file in locals. It reports the block whose end it
            // reached (native_exit_pc; its verdict in taken / target) -- the
            // loop re-syncs there -- or the head it stopped at for lack of
            // budget (native_resume: that block has not run; the loop runs it).
            // Nothing of the ops array is read here (the sled facts are the
            // block's own fields); the XTOP / re-sync / resume tails are cold.
            st.native_budget = cycle_limit > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(cycle_limit);
            st.native_exit_pc = pc;
            st.native_resume = false;
            const int done = blk->native(unit, generic_, taken, target);
            bundles_ += static_cast<uint64_t>(done);
            ++acc.blocks_native;
            acc.ops_native += static_cast<uint64_t>(done);
            cycle_limit -= static_cast<uint64_t>(done);
            if (PS2R_UNLIKELY(st.native_exit_pc != pc)) {
                pc = st.native_exit_pc;
                blk = BlockAt(prog, pc);
                n_body = blk->n_body;
                n = static_cast<int>(blk->ops.size());
                consecutive_nops = blk->ds_nop ? 1 : 0;
            }
            if (PS2R_UNLIKELY(st.native_resume)) {
                st.native_resume = false;
                continue;  // the block at pc has not run: the loop's turn (cut or whole)
            }
            if (PS2R_UNLIKELY(st.xtop_exit_requested)) {
                // The index, in the block the run ended in, of the last op executed.
                const int idx = static_cast<int>((st.cur_bundle_pc - pc) & kPcMask) / 8;
                yield(pc + 8u * static_cast<uint32_t>(idx), idx > 0 && !(blk->has_ds && idx == n_body));
                return;
            }
        } else {
            const VuBlockOp* ops = blk->ops.data();
            if (static_cast<uint64_t>(n) > cycle_limit) {  // the cap cuts the block
                n = static_cast<int>(cycle_limit);
                cut = true;
            }
            const int done = n > kWarmupOps + kMinStaticOps - 1
                                 ? RunOps<true>(unit, ops, n, pc, blk->branch_idx, taken, target, acc)
                                 : RunOps<false>(unit, ops, n, pc, blk->branch_idx, taken, target, acc);
            cycle_limit -= static_cast<uint64_t>(done);
            if (st.xtop_exit_requested) {
                const int idx = done - 1;
                yield(pc + 8u * static_cast<uint32_t>(idx), idx > 0 && !(blk->has_ds && idx == n_body));
                return;
            }
        }
        const uint32_t pc_after_body = (pc + 8u * static_cast<uint32_t>(n_body)) & kPcMask;
        if (PS2R_UNLIKELY(cut)) {
            // TPC = the first unexecuted bundle; an E-bit bundle does not advance
            // it (TPC stays at the advance before it).
            if (n == n_body && blk->e_bit) {
                if (n_body > 1) regs.TPC = pc + 8u * static_cast<uint32_t>(n_body - 1);
            } else {
                regs.TPC = (pc + 8u * static_cast<uint32_t>(n)) & kPcMask;
            }
            break;
        }
        if (blk->e_bit) {
            // The E bit on the image's last bundle: the reference ends the run
            // there (the delay slot's address is past the micro memory).
            if (!blk->has_ds) break;
            regs.TPC = (pc_after_body + 8u) & kPcMask;
            break;
        }
        regs.TPC = pc_after_body;
        if (taken) {
            if (PS2R_UNLIKELY(!blk->has_ds)) {
                if (cycle_limit == 0) break;
                const int r = delay_slot(pc_after_body);
                if (r == 1) break;
                if (r == 2) return;
            }
            if (blk->static_target >= 0) {
                pc = static_cast<uint32_t>(blk->static_target);
                if (blk->jump == nullptr) blk->jump = BlockAt(prog, pc);
                blk = blk->jump;
            } else {
                pc = target & kPcMask;
                blk = BlockAt(prog, pc);
            }
            continue;
        }
        // Not taken: the delay-slot bundle was an ordinary advance.
        pc = blk->has_ds ? ((pc_after_body + 8u) & kPcMask) : pc_after_body;
        regs.TPC = pc;
        if (blk->fall == nullptr) blk->fall = BlockAt(prog, pc);
        blk = blk->fall;
    }
    DrainAll(unit);
    FoldStats(acc);
}

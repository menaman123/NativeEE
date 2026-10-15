// VuCachedTier -- the program table and the bundle loop with the superblock
// run body. Rules in the header; the fast instruction bodies live in
// vu_cached_tier_exec.cpp.
#include "engine/vu/tiers/vu_cached_tier.h"

#include <cstring>

#include "engine/vu/span_classifier.h"
#include "engine/vu/stall_model.h"
#include "engine/vu/tiers/vu_interpreter_tier.h"
#include "engine/vu/vu0/lib_vu0_sin_cos.h"
#include "engine/vu/vu_chop_round_guard.h"
#include "engine/vu/vu_image_hash.h"
#include "engine/vu/vu_unit.h"

static_assert(StallModel::kFmacWbLatency == SpanClassifier::kFmacWbLatencyShared &&
                  StallModel::kIaluLwLatency == SpanClassifier::kIaluLwLatencyShared,
              "the span classifier's fold window assumes the stall model's latencies");

namespace {

uint32_t Le32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

constexpr int kNopSledBundles = 4;  // four all-zero bundles end the run (uninitialised micro memory)

inline void PinHardwired(VUregs& regs) {
    regs.VF[0].x = 0.0f;
    regs.VF[0].y = 0.0f;
    regs.VF[0].z = 0.0f;
    regs.VF[0].w = 1.0f;
    regs.VI[0] = 0;
}

}  // namespace

VuCachedTier::VuCachedTier(VuInterpreterTier& generic, uint64_t cycle_cap)
    : generic_(generic), cycle_cap_(cycle_cap) {}

void VuCachedTier::DecodeProgram(const VuUnit& unit, VuCachedProgram& prog) const {
    const Vu1State& st = unit.state();
    const size_t n_slots = static_cast<size_t>(st.mem_mask + 1) / 8;  // the unit's reachable image only
    prog.slots.resize(n_slots);
    for (size_t i = 0; i < n_slots; ++i) {
        prog.slots[i] = VuDecoder::DecodeVuSlot(Le32(st.micro + i * 8), Le32(st.micro + i * 8 + 4));
    }
    VuDecodedSlot* slots = prog.slots.data();
    SpanClassifier::ComputeRunLenTouchHw(slots, n_slots);
    SpanClassifier::ComputeStallClasses(slots, n_slots);
    SpanClassifier::ComputeFoldClasses(slots, n_slots);
    SpanClassifier::ComputeSpanRem(slots, n_slots, prog.span_rem);
}

const VuCachedProgram* VuCachedTier::Acquire(VuUnit& unit) { return Lookup(unit); }

VuCachedProgram* VuCachedTier::Lookup(VuUnit& unit) {
    const uint64_t gen = unit.micro_generation();
    if (current_ != nullptr && current_->generation == gen) {  // same epoch: no hash, no scan
        ++current_->exec_count;
        return current_;
    }
    const Vu1State& st = unit.state();
    const uint64_t h = VuImageHash::Fast(st.micro, static_cast<size_t>(st.mem_mask + 1));
    VuCachedProgram* found = nullptr;
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
    std::unique_ptr<VuCachedProgram> prog(new VuCachedProgram());
    prog->hash = h;
    prog->generation = gen;
    prog->exec_count = 1;
    DecodeProgram(unit, *prog);
    current_ = prog.get();
    table_.push_back(std::move(prog));
    return current_;
}

void VuCachedTier::Execute(VuUnit& unit, uint32_t start_pc) { Execute(unit, start_pc, cycle_cap_); }

void VuCachedTier::ResumeIfWaiting(VuUnit& unit) {
    Vu1State& st = unit.state();
    if (!st.suspended) return;
    st.suspended = false;
    Execute(unit, st.suspended_tpc);
}

void VuCachedTier::Execute(VuUnit& unit, uint32_t start_pc, uint64_t cycle_limit) {
    VuChopRoundGuard round_guard;  // FMAC/FDIV/EFU truncate for the whole run
    Vu1State& st = unit.state();
    VUregs& regs = st.regs;
    const uint32_t pc = start_pc & kPcMask;
    if (unit.id() != 1 && LibVu0SinCos::TryExecute(st.micro, st.mem_mask + 1, regs, pc)) return;

    VuCachedProgram* prog = Lookup(unit);  // at every entry (MSCAL / MSCNT / VCALLMS / resume)

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
    Loop(unit, pc, *prog, cycle_limit);
    prog->bundles += bundles_ - before;
}

void VuCachedTier::DrainAll(VuUnit& unit) { generic_.DrainAll(unit); }

void VuCachedTier::ExecBundle(VuUnit& unit, const VuDecodedSlot& s, uint32_t pc, bool& branch_taken,
                              uint32_t& branch_target) {
    VUregs& regs = unit.regs();
    if (!s.i_bit) {
        const int uvw = s.upper_vf_write;
        VR_reg vf_pre = {};
        if (s.hide_upper) vf_pre = regs.VF[uvw];
        ExecUpperFast(unit, s);
        VR_reg vf_post = {};
        if (s.hide_upper) {
            // Both words read at the same stage: the lower sees the pre-upper VF.
            vf_post = regs.VF[uvw];
            regs.VF[uvw] = vf_pre;
        }
        // Same VF written by both words: the upper wins and the lower is not issued.
        if (!s.discard_lower) ExecLowerFast(unit, s, pc, branch_taken, branch_target);
        if (s.hide_upper) regs.VF[uvw] = vf_post;
    } else {
        ExecUpperFast(unit, s);  // the upper reads the OLD I
        regs.I = s.lower;
    }
}

void VuCachedTier::Loop(VuUnit& unit, uint32_t pc, const VuCachedProgram& prog, uint64_t cycle_limit) {
    Vu1State& st = unit.state();
    VUregs& regs = st.regs;
    FlagPipeline& pipes = unit.flags();
    StallModel& stalls = unit.stalls();
    XgKick* const xgk = unit.xgkick();
    const bool is_vu1 = unit.id() == 1;
    const uint8_t* code = st.micro;
    const uint32_t code_size = st.mem_mask + 1;
    const VuDecodedSlot* const slots = prog.slots.data();
    const bool have_span = !prog.span_rem.empty();

    bool in_delay_slot = false;
    bool end_pending = false;
    uint32_t branch_target = 0;
    int consecutive_nops = 0;
    uint32_t spin_self_pc = 0xFFFFFFFFu;
    uint32_t spin_self_key = 0;
    int spin_self_count = 0;
    VuDecodedSlot fly_slot;

    // Drain what the elapsed cycles allow (once per bundle; the kick's own quantum gate).
    auto pace = [&]() {
        if (xgk != nullptr && xgk->armed() && pipes.cycle() >= xgk->next_due()) xgk->Pace(st.data, pipes.cycle());
    };

    while (cycle_limit-- > 0) {
        if (pc + 8 > code_size) break;  // past the micro memory (VU0: its 4 KB) the run ends
        ++pipes.cycle();
        ++bundles_;

        const VuDecodedSlot* sp;
        if ((pc & 7u) == 0) {
            sp = &slots[pc >> 3];
        } else {
            fly_slot = VuDecoder::DecodeVuSlot(Le32(code + pc), Le32(code + pc + 4));  // dynamic class, no run
            sp = &fly_slot;
        }
        const VuDecodedSlot& s = *sp;

        if (!in_delay_slot && !end_pending && s.run_len >= kMinRunLen) {
            // ---- superblock run: run_len - 1 straight-line bundles (no branch, E bit,
            // NOP pair or generic lower), the terminal left to the per-bundle path ----
            consecutive_nops = 0;
            uint32_t remaining = static_cast<uint32_t>(s.run_len) - 1u;
            const VuDecodedSlot* sp2 = sp;
            int fold_pos = 0;  // bundles since run entry
            bool backstop = false;
            bool dummy_taken = false;
            uint32_t dummy_target = 0;
            for (;;) {
                if (fold_pos >= kWarmupBundles && have_span) {
                    uint32_t span = prog.span_rem[static_cast<size_t>(sp2 - slots)];
                    if (span > remaining) span = remaining;
                    if (span >= kMinSpan) {
                        // Span: every bundle proven foldable; the stall model is not
                        // consulted and the FMAC ring is rebuilt at the close.
                        uint32_t span_left = span;
                        uint32_t span_executed = 0;
                        bool span_run_done = false;
                        bool cut = false;
                        while (span_left-- > 0) {
                            const VuDecodedSlot& s2 = *sp2;
                            ++fold_pos;
                            pace();
                            ExecBundle(unit, s2, pc, dummy_taken, dummy_target);
                            if (s2.touch_hw) PinHardwired(regs);
                            pc += 8;  // unmasked inside a run (runs never cross the image end)
                            ++span_executed;
                            if (--remaining == 0) {
                                span_run_done = true;
                                break;
                            }
                            if (span_left == 0) break;  // span complete, the run continues
                            if (cycle_limit == 0) {     // cap reached mid-span: the ring as the model would hold it
                                if (span_executed >= kMinSpan) {
                                    stalls.MaterializeFmac(slots, pc >> 3, span_executed, pipes.cycle());
                                } else {
                                    const uint64_t saved = pipes.cycle();
                                    for (uint32_t k = span_executed; k >= 1; --k) {
                                        const VuDecodedSlot& q = slots[(pc >> 3) - k];
                                        if (q.n_wr > 0) {
                                            pipes.cycle() = saved + 1 - k;
                                            stalls.ApplyPrunePush(q, pipes);
                                        }
                                    }
                                    pipes.cycle() = saved;
                                }
                                regs.TPC = pc;
                                cut = true;
                                break;
                            }
                            --cycle_limit;
                            ++pipes.cycle();
                            ++bundles_;
                            sp2 = &slots[pc >> 3];
                        }
                        if (cut) {
                            backstop = true;
                            break;
                        }
                        stalls.MaterializeFmac(slots, pc >> 3, span_executed, pipes.cycle());
                        if (span_run_done) break;
                        if (cycle_limit == 0) {
                            regs.TPC = pc;
                            backstop = true;
                            break;
                        }
                        --cycle_limit;
                        ++pipes.cycle();
                        ++bundles_;
                        sp2 = &slots[pc >> 3];
                        continue;
                    }
                }
                const VuDecodedSlot& s2 = *sp2;
                if (fold_pos < kWarmupBundles || s2.fold_class == 0) {
                    stalls.ApplyClassed(s2, regs, pipes);  // warmup positions always run the model
                } else if (s2.fold_class == 16) {
                    stalls.ApplyPrunePush(s2, pipes);      // writer fold: nothing in flight to read
                }                                          // fold 15: no writes, nothing in flight to read
                ++fold_pos;
                pace();
                ExecBundle(unit, s2, pc, dummy_taken, dummy_target);
                if (s2.touch_hw) PinHardwired(regs);
                pc += 8;
                if (--remaining == 0) break;
                if (cycle_limit == 0) {
                    regs.TPC = pc;
                    backstop = true;
                    break;
                }
                --cycle_limit;
                ++pipes.cycle();
                ++bundles_;
                sp2 = &slots[pc >> 3];
            }
            if (backstop) break;  // the cap: TPC = first unexecuted bundle, then the drain
            pc &= kPcMask;
            regs.TPC = pc;
            continue;  // the terminal bundle is charged by the outer loop
        }

        // ---- per-bundle path: the interpreter loop over the predecoded slot ----
        if (s.is_nop_pair) {
            if (++consecutive_nops >= kNopSledBundles) break;
        } else {
            consecutive_nops = 0;
        }
        bool this_branch_taken = false;
        uint32_t this_branch_target = 0;

        stalls.ApplyClassed(s, regs, pipes);
        pace();

        st.cur_bundle_pc = pc;
        ExecBundle(unit, s, pc, this_branch_taken, this_branch_target);

        if (st.xtop_exit_requested) {
            // XTOP yield: the run ends at this bundle boundary, TPC as set at the last advance.
            st.xtop_exit_requested = false;
            st.xtop_read_count = 0;
            DrainAll(unit);
            return;
        }

        PinHardwired(regs);

        if (in_delay_slot) {
            pc = branch_target & kPcMask;
            in_delay_slot = false;
            if (end_pending) {
                regs.TPC = (pc + 8) & kPcMask;
                break;
            }
            continue;
        }
        if (s.e_bit) {
            end_pending = true;
            in_delay_slot = true;
            branch_target = pc + 8;
            pc += 8;
            continue;
        }
        if (!is_vu1 && this_branch_taken && this_branch_target == pc) {
            // VU0 spinning on itself over a VI the EE owns: park until the EE writes it.
            const uint32_t lower = s.lower;
            const uint32_t br_op = (lower >> 25) & 0x7Fu;
            if (br_op >= 0x28 && br_op <= 0x2F) {
                const uint32_t key = static_cast<uint32_t>(regs.VI[(lower >> 11) & 0xF]) |
                                     (static_cast<uint32_t>(regs.VI[(lower >> 16) & 0xF]) << 16);
                if (spin_self_pc == pc && spin_self_key == key) {
                    ++spin_self_count;
                } else {
                    spin_self_pc = pc;
                    spin_self_key = key;
                    spin_self_count = 1;
                }
                if (spin_self_count >= 2) {
                    st.suspended = true;
                    st.suspended_tpc = pc;
                    regs.TPC = pc;
                    DrainAll(unit);
                    return;
                }
            }
        }
        if (this_branch_taken) {
            in_delay_slot = true;
            branch_target = this_branch_target;
        }
        pc = (pc + 8) & kPcMask;
        regs.TPC = pc;
    }
    DrainAll(unit);
}

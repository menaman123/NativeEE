// VuInterpreterTier -- the bundle loop. Rules in the header; the upper and
// lower instruction bodies live in vu_interpreter_tier_upper.cpp and
// vu_interpreter_tier_lower.cpp.
#include "engine/vu/tiers/vu_interpreter_tier.h"

#include <cstring>

#include "engine/vu/vu0/lib_vu0_sin_cos.h"
#include "engine/vu/vu_chop_round_guard.h"
#include "engine/vu/vu_decoder.h"
#include "engine/vu/vu_float.h"
#include "engine/vu/vu_unit.h"

namespace {

uint32_t Le32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

constexpr int kNopSledBundles = 4;  // four all-zero bundles end the run (uninitialised micro memory)

}  // namespace

VuInterpreterTier::VuInterpreterTier(uint64_t cycle_cap) : cycle_cap_(cycle_cap) {}

VR_reg VuInterpreterTier::Clamp4(const VR_reg& v) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.I[k] = VuFloat::ClampBits1(v.I[k]);
    return r;
}

void VuInterpreterTier::PinHardwired(VUregs& regs) {
    regs.VF[0].x = 0.0f;
    regs.VF[0].y = 0.0f;
    regs.VF[0].z = 0.0f;
    regs.VF[0].w = 1.0f;
    regs.VI[0] = 0;
}

void VuInterpreterTier::DrainAll(VuUnit& u) {
    VUregs& regs = u.regs();
    FlagPipeline& pipes = u.flags();
    pipes.FlushClipPipe(regs);
    pipes.FlushMacPipe(regs);
    pipes.FlushQPipe(regs);
    u.stalls().FlushFmacWb(pipes);
    u.stalls().FlushIaluWb(pipes);
    pipes.FlushPPipe(regs);
    if (XgKick* xgk = u.xgkick()) xgk->Flush(u.state().data, pipes.cycle(), false);
}

void VuInterpreterTier::Run(VuUnit& unit, uint32_t start_pc) {
    RunOptions opt;
    opt.cycle_limit = cycle_cap_;
    Run(unit, start_pc, opt);
}

void VuInterpreterTier::ResumeIfWaiting(VuUnit& unit) {
    Vu1State& st = unit.state();
    if (!st.suspended) return;
    st.suspended = false;
    Run(unit, st.suspended_tpc);
}

void VuInterpreterTier::Run(VuUnit& unit, uint32_t start_pc, const RunOptions& opt) {
    VuChopRoundGuard round_guard;  // FMAC/FDIV/EFU truncate for the whole run
    Vu1State& st = unit.state();
    VUregs& regs = st.regs;
    FlagPipeline& pipes = unit.flags();
    const bool is_vu1 = unit.id() == 1;
    const uint8_t* code = st.micro;
    const uint32_t code_size = st.mem_mask + 1;

    uint32_t pc = start_pc & kPcMask;
    if (!is_vu1 && LibVu0SinCos::TryExecute(code, code_size, regs, pc)) return;

    regs.VF[0].x = 0.0f;
    regs.VF[0].y = 0.0f;
    regs.VF[0].z = 0.0f;
    regs.VF[0].w = 1.0f;

    bool in_delay_slot = false;
    bool end_pending = false;
    uint32_t branch_target = 0;
    int consecutive_nops = 0;
    uint32_t spin_self_pc = 0xFFFFFFFFu;
    uint32_t spin_self_key = 0;
    int spin_self_count = 0;

    st.suspended = false;
    st.xtop_read_count = 0;
    st.xtop_exit_requested = false;
    st.xgkick_has_fired = false;
    st.xgkick_count = 0;

    uint64_t cycle_limit = opt.cycle_limit;
    while (cycle_limit-- > 0) {
        if (pc + 8 > code_size) break;  // past the micro memory (VU0: its 4 KB) the run ends
        ++pipes.cycle();
        ++bundles_;

        const uint32_t lower = Le32(code + pc);
        const uint32_t upper = Le32(code + pc + 4);
        if (lower == 0 && upper == 0) {
            if (++consecutive_nops >= kNopSledBundles) break;
        } else {
            consecutive_nops = 0;
        }
        const bool i_bit = ((upper >> 31) & 1u) != 0;
        const bool e_bit = ((upper >> 30) & 1u) != 0;

        bool this_branch_taken = false;
        uint32_t this_branch_target = 0;

        const VuDecodedSlot slot = VuDecoder::DecodeVuSlot(lower, upper);
        unit.stalls().Apply(slot, regs, pipes);
        if (XgKick* xgk = unit.xgkick()) xgk->Pace(st.data, pipes.cycle());

        st.cur_bundle_pc = pc;
        if (!i_bit) {
            const int uvw = slot.upper_vf_write;
            VR_reg vf_pre = {};
            if (slot.hide_upper) vf_pre = regs.VF[uvw];
            RunUpper(unit, upper);
            VR_reg vf_post = {};
            if (slot.hide_upper) {
                // Both words read at the same stage: the lower sees the pre-upper VF.
                vf_post = regs.VF[uvw];
                regs.VF[uvw] = vf_pre;
            }
            // Same VF written by both words: the upper wins and the lower is not issued.
            if (!slot.discard_lower) RunLower(unit, lower, pc, this_branch_taken, this_branch_target);
            if (slot.hide_upper) regs.VF[uvw] = vf_post;
        } else {
            RunUpper(unit, upper);  // the upper reads the OLD I
            regs.I = lower;
        }

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
        if (e_bit) {
            end_pending = true;
            in_delay_slot = true;
            branch_target = pc + 8;
            pc += 8;
            continue;
        }
        if (!is_vu1 && this_branch_taken && this_branch_target == pc) {
            // VU0 spinning on itself over a VI the EE owns: park until the EE writes it.
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

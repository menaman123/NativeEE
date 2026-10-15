// VuCachedTier -- the fast instruction bodies keyed on the decoded kinds.
// Each body is the interpreter's body for that instruction with the decode and
// the unused work removed; every arithmetic expression and every VuFloat /
// FlagPipeline kernel is the one the interpreter uses (single-rounding
// std::fmaf for the MADD/MSUB family, clamped copies of the VF sources, ACC
// re-clamped before every upper, MAC flags judged on the raw result). Kinds
// without a fast form run the interpreter's body.
#include "engine/vu/tiers/vu_cached_tier.h"

#include <cmath>
#include <cstring>

#include "engine/vu/tiers/vu_interpreter_tier.h"
#include "engine/vu/vu_float.h"
#include "engine/vu/vu_unit.h"

namespace {

inline uint32_t Le32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
inline void StoreLe32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, sizeof(v)); }

inline float Fma(float a, float b, float c) { return std::fmaf(a, b, c); }

inline VR_reg Clamp4(const VR_reg& v) {
    VR_reg r;
    for (int k = 0; k < 4; ++k) r.I[k] = VuFloat::ClampBits1(v.I[k]);
    return r;
}

inline float OpAdd(float a, float b) { return a + b; }
inline float OpSub(float a, float b) { return a - b; }
inline float OpMul(float a, float b) { return a * b; }

// Quadword byte address of (vi + imm) with the unit's data mask.
inline uint32_t QwAddr(uint16_t vi, int32_t imm, uint32_t mask) {
    return static_cast<uint32_t>((static_cast<int32_t>(vi) + imm) * 16) & mask;
}

// ILW word offset: the first selected lane in X,Y,Z priority, else W.
inline uint32_t IlwOffset(uint32_t dest) {
    if (dest & vu_dest::X) return 0;
    if (dest & vu_dest::Y) return 4;
    if (dest & vu_dest::Z) return 8;
    return 12;
}

inline int32_t SignExtend5(uint32_t v) {
    v &= 0x1Fu;
    return static_cast<int32_t>(static_cast<int8_t>((v & 0x10u) ? (v | 0xE0u) : v));
}

}  // namespace

void VuCachedTier::ExecUpperFast(VuUnit& u, const VuDecodedSlot& s) {
    VUregs& regs = u.regs();
    FlagPipeline& pipes = u.flags();
    const uint32_t fd = s.u_fd, fs = s.u_fs, ft = s.u_ft, bc = s.u_bc;
    const uint8_t dest = s.u_dest;

    // The FMAC-family write: dest lanes of fd take the clamped result (VF00
    // protected), the flags judge the raw result.
    VR_reg result = {};
    auto write_fd = [&]() {
        if (fd != 0) VuFloat::WriteMaskClamp(regs.VF[fd], result.I, dest);
        pipes.UpdateMACFlags(regs, result, dest);
    };
    auto lanes1 = [&](const VR_reg& src1, float sc, float (*op)(float, float)) {
        for (int k = 0; k < 4; ++k) result.F[k] = op(src1.F[k], sc);
    };
    auto lanes2 = [&](const VR_reg& src1, const VR_reg& src2, float (*op)(float, float)) {
        for (int k = 0; k < 4; ++k) result.F[k] = op(src1.F[k], src2.F[k]);
    };
    auto minmax_bc = [&](uint32_t bits, VuFloat::MinMaxFn fn) {
        const uint32_t b4[4] = {bits, bits, bits, bits};
        VuFloat::ApplyMinMaxRaw(regs.VF[fd], regs.VF[fs], b4, dest, fd == 0, fn);
    };
    // ACC-family tail: lanes outside dest keep the incoming ACC, flags judge
    // the raw ACC, then the dest lanes are clamped in place.
    auto acc_tail = [&](const VR_reg& acc_in) {
        for (int k = 0; k < 4; ++k) {
            if (!(dest & (8u >> k))) regs.ACC.I[k] = acc_in.I[k];
        }
        pipes.UpdateMACFlags(regs, regs.ACC, dest);
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) regs.ACC.I[k] = VuFloat::ClampBits1(regs.ACC.I[k]);
        }
    };
    auto write_ft_vec = [&](const VR_reg& v) {
        if (ft == 0) return;
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) regs.VF[ft].I[k] = v.I[k];
        }
    };

    switch (s.upper_kind) {
        case VUK_U_NOP:
            VuFloat::ClampStoreACC(regs.ACC);
            break;
        case VUK_U_ADD_BC: case VUK_U_SUB_BC: case VUK_U_MUL_BC:
        case VUK_U_MADD_BC: case VUK_U_MSUB_BC: {
            const VR_reg s1 = Clamp4(regs.VF[fs]);
            const float bv = VuFloat::Bits2F(VuFloat::ClampBits1(regs.VF[ft].I[bc]));
            VuFloat::ClampStoreACC(regs.ACC);
            const VR_reg& acc = regs.ACC;
            switch (s.upper_kind) {
                case VUK_U_ADD_BC: lanes1(s1, bv, OpAdd); break;
                case VUK_U_SUB_BC: lanes1(s1, bv, OpSub); break;
                case VUK_U_MUL_BC: lanes1(s1, bv, OpMul); break;
                case VUK_U_MADD_BC: for (int k = 0; k < 4; ++k) result.F[k] = Fma(s1.F[k], bv, acc.F[k]); break;
                default:            for (int k = 0; k < 4; ++k) result.F[k] = Fma(-s1.F[k], bv, acc.F[k]); break;
            }
            write_fd();
            break;
        }
        case VUK_U_ADD: case VUK_U_SUB: case VUK_U_MUL: case VUK_U_MADD: case VUK_U_MSUB: {
            const VR_reg s1 = Clamp4(regs.VF[fs]);
            const VR_reg s2 = Clamp4(regs.VF[ft]);
            VuFloat::ClampStoreACC(regs.ACC);
            const VR_reg& acc = regs.ACC;
            switch (s.upper_kind) {
                case VUK_U_ADD: lanes2(s1, s2, OpAdd); break;
                case VUK_U_SUB: lanes2(s1, s2, OpSub); break;
                case VUK_U_MUL: lanes2(s1, s2, OpMul); break;
                case VUK_U_MADD: for (int k = 0; k < 4; ++k) result.F[k] = Fma(s1.F[k], s2.F[k], acc.F[k]); break;
                default:         for (int k = 0; k < 4; ++k) result.F[k] = Fma(-s1.F[k], s2.F[k], acc.F[k]); break;
            }
            write_fd();
            break;
        }
        case VUK_U_MULQ: {
            const VR_reg s1 = Clamp4(regs.VF[fs]);
            VuFloat::ClampStoreACC(regs.ACC);
            lanes1(s1, pipes.VisibleQ(regs), OpMul);
            write_fd();
            break;
        }
        case VUK_U_MUL_I: case VUK_U_ADD_I: {
            const VR_reg s1 = Clamp4(regs.VF[fs]);
            VuFloat::ClampStoreACC(regs.ACC);
            const float i_val = VuFloat::ClampFloat(VuFloat::Bits2F(regs.I));
            lanes1(s1, i_val, s.upper_kind == VUK_U_MUL_I ? OpMul : OpAdd);
            write_fd();
            break;
        }
        case VUK_U_MAX_BC:
            VuFloat::ClampStoreACC(regs.ACC);
            minmax_bc(regs.VF[ft].I[bc], VuFloat::FpMax);
            break;
        case VUK_U_MINI_BC:
            VuFloat::ClampStoreACC(regs.ACC);
            minmax_bc(regs.VF[ft].I[bc], VuFloat::FpMin);
            break;
        case VUK_U_MINI_I:
            VuFloat::ClampStoreACC(regs.ACC);
            minmax_bc(regs.I, VuFloat::FpMin);
            break;
        case VUK_U_MAX:
            VuFloat::ClampStoreACC(regs.ACC);
            VuFloat::ApplyMinMaxRaw(regs.VF[fd], regs.VF[fs], regs.VF[ft].I, dest, fd == 0, VuFloat::FpMax);
            break;
        case VUK_U_MULA_BC: case VUK_U_MADDA_BC: case VUK_U_MSUBA_BC: case VUK_U_MULA: case VUK_U_MADDA: {
            VuFloat::ClampStoreACC(regs.ACC);
            const VR_reg acc_in = regs.ACC;
            const VR_reg s1 = Clamp4(regs.VF[fs]);
            const VR_reg s2 = Clamp4(regs.VF[ft]);
            const VR_reg acc_c = Clamp4(acc_in);
            const float bv = s2.F[bc];
            switch (s.upper_kind) {
                case VUK_U_MULA_BC:  for (int k = 0; k < 4; ++k) regs.ACC.F[k] = OpMul(s1.F[k], bv); break;
                case VUK_U_MADDA_BC: for (int k = 0; k < 4; ++k) regs.ACC.F[k] = Fma(s1.F[k], bv, acc_c.F[k]); break;
                case VUK_U_MSUBA_BC: for (int k = 0; k < 4; ++k) regs.ACC.F[k] = Fma(-s1.F[k], bv, acc_c.F[k]); break;
                case VUK_U_MULA:     for (int k = 0; k < 4; ++k) regs.ACC.F[k] = OpMul(s1.F[k], s2.F[k]); break;
                default:             for (int k = 0; k < 4; ++k) regs.ACC.F[k] = Fma(s1.F[k], s2.F[k], acc_c.F[k]); break;
            }
            acc_tail(acc_in);
            break;
        }
        case VUK_U_FTOI0: case VUK_U_FTOI4: {
            VuFloat::ClampStoreACC(regs.ACC);
            const VR_reg src = regs.VF[fs];  // raw lanes
            const float scale = (s.upper_kind == VUK_U_FTOI0) ? 1.0f : 16.0f;
            VR_reg v = {};
            for (int k = 0; k < 4; ++k) v.I[k] = VuFloat::FtoiSat(src.F[k] * scale);
            write_ft_vec(v);
            break;
        }
        case VUK_U_ITOF0: {
            VuFloat::ClampStoreACC(regs.ACC);
            const VR_reg src = regs.VF[fs];
            VR_reg v = {};
            for (int k = 0; k < 4; ++k) v.F[k] = static_cast<float>(static_cast<int32_t>(src.I[k])) / 1.0f;
            write_ft_vec(v);
            break;
        }
        case VUK_U_CLIP: {
            VuFloat::ClampStoreACC(regs.ACC);
            const VR_reg src1 = regs.VF[fs];
            const VR_reg src2 = regs.VF[ft];
            generic_.RunClip(u, src1, src2);
            break;
        }
        default:
            generic_.RunUpper(u, s.upper);
            break;
    }
}

void VuCachedTier::ExecLowerFast(VuUnit& u, const VuDecodedSlot& s, uint32_t pc, bool& branch_taken,
                                 uint32_t& branch_target) {
    VUregs& regs = u.regs();
    FlagPipeline& pipes = u.flags();
    uint8_t* data = u.state().data;
    const uint32_t mask = u.mem_mask();
    const uint32_t ft = s.l_ft, fs = s.l_fs;
    const uint32_t vit = ft & 0xFu, vis = fs & 0xFu, vid = s.l_fd & 0xFu;
    const uint32_t dest = s.l_dest;
    const int32_t imm11 = s.l_imm11;
    branch_taken = false;

    auto set_vi = [&](uint32_t idx, uint32_t v) {
        if (idx != 0) regs.VI[idx] = static_cast<uint16_t>(v);
    };
    auto rel_target = [&]() {
        return static_cast<uint32_t>(static_cast<int32_t>(pc + 8) + imm11 * 8) & kPcMask;
    };
    auto link = [&]() { return static_cast<uint16_t>(((pc + 16) / 8) & VuInterpreterTier::kLinkMask); };
    auto cond_branch = [&](bool cond) {
        if (cond) {
            branch_taken = true;
            branch_target = rel_target();
        }
    };
    auto write_masked_vec = [&](uint32_t reg, const VR_reg& v) {
        if (reg == 0) return;
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) regs.VF[reg].I[k] = v.I[k];
        }
    };
    auto write_masked_all = [&](uint32_t reg, uint32_t bits) {
        if (reg == 0) return;
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) regs.VF[reg].I[k] = bits;
        }
    };
    auto store_masked = [&](uint32_t addr, const VR_reg& v) {
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) StoreLe32(data + addr + 4 * k, v.I[k]);
        }
    };
    auto load_masked = [&](uint32_t reg, uint32_t addr) {
        if (reg == 0) return;
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) regs.VF[reg].I[k] = Le32(data + addr + 4 * k);
        }
    };

    switch (s.lower_kind) {
        case VUK_L_NOP:
            break;
        case VUK_L_LQ: load_masked(ft, QwAddr(regs.VI[vis], imm11, mask)); break;
        case VUK_L_SQ: store_masked(QwAddr(regs.VI[vit], imm11, mask), regs.VF[fs]); break;
        case VUK_L_ILW: {
            const uint32_t addr = QwAddr(regs.VI[vis], imm11, mask);
            set_vi(vit, Le32(data + addr + IlwOffset(dest)));
            break;
        }
        case VUK_L_ISW: {
            const uint32_t addr = QwAddr(regs.VI[vis], imm11, mask);
            const uint32_t word = regs.VI[vit];
            for (int k = 0; k < 4; ++k) {
                if (dest & (8u >> k)) StoreLe32(data + addr + 4 * k, word);
            }
            break;
        }
        case VUK_L_IADDIU: set_vi(vit, regs.VI[vis] + s.l_imm15u); break;
        case VUK_L_ISUBIU: set_vi(vit, regs.VI[vis] - s.l_imm15u); break;
        case VUK_L_IBEQ: cond_branch(regs.VI[vis] == regs.VI[vit]); break;
        case VUK_L_IBNE: cond_branch(regs.VI[vis] != regs.VI[vit]); break;
        case VUK_L_IADD: set_vi(vid, regs.VI[vis] + regs.VI[vit]); break;
        case VUK_L_ISUB: set_vi(vid, regs.VI[vis] - regs.VI[vit]); break;
        case VUK_L_IAND: set_vi(vid, regs.VI[vis] & regs.VI[vit]); break;
        case VUK_L_IOR: set_vi(vid, regs.VI[vis] | regs.VI[vit]); break;
        case VUK_L_IADDI:
            set_vi(vit, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(regs.VI[vis])) +
                                              SignExtend5(s.l_fd)));
            break;
        case VUK_L_MOVE: write_masked_vec(ft, regs.VF[fs]); break;
        case VUK_L_MR32: {
            const VR_reg src = regs.VF[fs];  // copied first: fs may equal ft
            VR_reg v = {};
            v.x = src.y; v.y = src.z; v.z = src.w; v.w = src.x;
            write_masked_vec(ft, v);
            break;
        }
        case VUK_L_MTIR: set_vi(vit, regs.VF[fs].I[s.l_fsf] & 0xFFFFu); break;
        case VUK_L_MFIR:
            write_masked_all(ft, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(regs.VI[vis]))));
            break;
        case VUK_L_LQI: {
            const uint32_t addr = (static_cast<uint32_t>(regs.VI[vis]) * 16u) & mask;
            load_masked(ft, addr);
            set_vi(vis, regs.VI[vis] + 1u);
            break;
        }
        case VUK_L_SQI: {
            const uint32_t addr = (static_cast<uint32_t>(regs.VI[vit]) * 16u) & mask;
            store_masked(addr, regs.VF[fs]);
            set_vi(vit, regs.VI[vit] + 1u);
            break;
        }
        case VUK_L_RGET: write_masked_all(ft, regs.R); break;
        case VUK_L_RINIT: regs.R = (regs.VF[fs].I[s.l_fsf] & 0x7FFFFFu) | 0x3F800000u; break;
        case VUK_L_B: cond_branch(true); break;
        case VUK_L_BAL:
            set_vi(vit, link());
            cond_branch(true);
            break;
        case VUK_L_JR:
            branch_taken = true;
            branch_target = (static_cast<uint32_t>(regs.VI[vis]) * 8u) & kPcMask;
            break;
        case VUK_L_JALR:  // the target is read before the link is written
            branch_target = (static_cast<uint32_t>(regs.VI[vis]) * 8u) & kPcMask;
            set_vi(vit, link());
            branch_taken = true;
            break;
        case VUK_L_DIV: generic_.DoDiv(u, fs, s.l_fsf, ft, (s.lower >> 23) & 3u); break;
        case VUK_L_FCGET: set_vi(vit, pipes.VisibleClip(regs) & 0xFFFu); break;
        case VUK_L_WAITQ: pipes.FlushQPipe(regs); break;
        default:
            generic_.RunLower(u, s.lower, pc, branch_taken, branch_target);
            break;
    }
}

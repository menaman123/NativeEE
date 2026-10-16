// VuCachedTier -- the fast instruction bodies keyed on the decoded kinds.
// Each body is the interpreter's body for that instruction with the decode and
// the unused work removed; the arithmetic goes through the VuFloat lane
// kernels (Add4/Sub4/Mul4/Fma4/Fms4: the interpreter's per-lane expressions,
// single-rounding fma for the MADD/MSUB family; NEON twins under PS2R_SIMD),
// with clamped copies of the VF sources, ACC re-clamped before every upper and
// the MAC flags judged on the raw result. Kinds without a fast form run the
// interpreter's body.
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

// ITOFn / FTOIn scale by the bc field (0/4/12/15 fractional bits): the interpreter's kScale.
const float kItofFtoiScale[4] = {1.0f, 16.0f, 4096.0f, 32768.0f};

// The 12-bit immediate of the FS* instructions: bit 21 above the low 11 bits.
inline uint32_t Imm12(uint32_t l) { return ((l >> 10) & 0x800u) | (l & 0x7FFu); }

inline int32_t SignExtend5(uint32_t v) {
    v &= 0x1Fu;
    return static_cast<int32_t>(static_cast<int8_t>((v & 0x10u) ? (v | 0xE0u) : v));
}

}  // namespace

// FMAC result family (fd written): the VuFloat lane kernels compute the raw
// result from clamped source copies and the re-clamped ACC; the dest lanes of
// fd take the clamped result (VF00 protected) and the flags judge the raw one.
void VuCachedTier::ExecUpperFmac(VuUnit& u, const VuDecodedSlot& s) {
    VUregs& regs = u.regs();
    const uint32_t fd = s.u_fd, fs = s.u_fs, ft = s.u_ft, bc = s.u_bc;
    const uint8_t dest = s.u_dest;
    const VR_reg s1 = VuFloat::ClampVec(regs.VF[fs]);
    VuFloat::ClampStoreACC(regs.ACC);
    VR_reg result;
    switch (s.upper_kind) {
        case VUK_U_ADD_BC: case VUK_U_SUB_BC: case VUK_U_MUL_BC: case VUK_U_MADD_BC: case VUK_U_MSUB_BC: {
            const VR_reg bv = VuFloat::Splat4(VuFloat::Bits2F(VuFloat::ClampBits1(regs.VF[ft].I[bc])));
            switch (s.upper_kind) {
                case VUK_U_ADD_BC: result = VuFloat::Add4(s1, bv); break;
                case VUK_U_SUB_BC: result = VuFloat::Sub4(s1, bv); break;
                case VUK_U_MUL_BC: result = VuFloat::Mul4(s1, bv); break;
                case VUK_U_MADD_BC: result = VuFloat::Fma4(s1, bv, regs.ACC); break;
                default:            result = VuFloat::Fms4(s1, bv, regs.ACC); break;
            }
            break;
        }
        case VUK_U_ADD: case VUK_U_SUB: case VUK_U_MUL: case VUK_U_MADD: case VUK_U_MSUB: {
            const VR_reg s2 = VuFloat::ClampVec(regs.VF[ft]);
            switch (s.upper_kind) {
                case VUK_U_ADD: result = VuFloat::Add4(s1, s2); break;
                case VUK_U_SUB: result = VuFloat::Sub4(s1, s2); break;
                case VUK_U_MUL: result = VuFloat::Mul4(s1, s2); break;
                case VUK_U_MADD: result = VuFloat::Fma4(s1, s2, regs.ACC); break;
                default:         result = VuFloat::Fms4(s1, s2, regs.ACC); break;
            }
            break;
        }
        case VUK_U_MULQ: result = VuFloat::Mul4(s1, VuFloat::Splat4(u.flags().VisibleQ(regs))); break;
        case VUK_U_MUL_I: result = VuFloat::Mul4(s1, VuFloat::Splat4(VuFloat::ClampFloat(VuFloat::Bits2F(regs.I)))); break;
        default:          result = VuFloat::Add4(s1, VuFloat::Splat4(VuFloat::ClampFloat(VuFloat::Bits2F(regs.I)))); break;  // ADD_I
    }
    if (fd != 0) VuFloat::WriteMaskClamp(regs.VF[fd], result.I, dest);
    u.flags().UpdateMACFlags(regs, result, dest);
}

// ACC family: lanes outside dest keep the incoming (clamped) ACC, the flags
// judge the raw ACC, then the dest lanes are clamped in place.
void VuCachedTier::ExecUpperAcc(VuUnit& u, const VuDecodedSlot& s) {
    VUregs& regs = u.regs();
    const uint32_t fs = s.u_fs, ft = s.u_ft, bc = s.u_bc;
    const uint8_t dest = s.u_dest;
    VuFloat::ClampStoreACC(regs.ACC);
    const VR_reg acc_in = regs.ACC;
    const VR_reg s1 = VuFloat::ClampVec(regs.VF[fs]);
    const VR_reg s2 = VuFloat::ClampVec(regs.VF[ft]);
    const VR_reg acc_c = VuFloat::ClampVec(acc_in);
    const VR_reg bv = VuFloat::Splat4(s2.F[bc]);
    VR_reg acc;
    switch (s.upper_kind) {
        case VUK_U_ADDA_BC:  acc = VuFloat::Add4(s1, bv); break;
        case VUK_U_SUBA_BC:  acc = VuFloat::Sub4(s1, bv); break;
        case VUK_U_MULA_BC:  acc = VuFloat::Mul4(s1, bv); break;
        case VUK_U_MADDA_BC: acc = VuFloat::Fma4(s1, bv, acc_c); break;
        case VUK_U_MSUBA_BC: acc = VuFloat::Fms4(s1, bv, acc_c); break;
        case VUK_U_MULA:     acc = VuFloat::Mul4(s1, s2); break;
        default:             acc = VuFloat::Fma4(s1, s2, acc_c); break;  // MADDA
    }
    for (int k = 0; k < 4; ++k) {
        if (!(dest & (8u >> k))) acc.I[k] = acc_in.I[k];
    }
    u.flags().UpdateMACFlags(regs, acc, dest);
    VuFloat::WriteMaskClamp(regs.ACC, acc.I, dest);  // dest lanes clamped in place; the others are acc_in already
}

void VuCachedTier::ExecUpperFast(VuUnit& u, const VuDecodedSlot& s) {
    VUregs& regs = u.regs();
    const uint32_t fd = s.u_fd, fs = s.u_fs, ft = s.u_ft, bc = s.u_bc;
    const uint8_t dest = s.u_dest;

    auto minmax_bc = [&](uint32_t bits, bool is_max) {
        VR_reg b4;
        b4.I[0] = b4.I[1] = b4.I[2] = b4.I[3] = bits;
        VuFloat::ApplyMinMax4(regs.VF[fd], regs.VF[fs], b4, dest, fd == 0, is_max);
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
        case VUK_U_ADD_BC: case VUK_U_SUB_BC: case VUK_U_MUL_BC: case VUK_U_MADD_BC: case VUK_U_MSUB_BC:
        case VUK_U_ADD: case VUK_U_SUB: case VUK_U_MUL: case VUK_U_MADD: case VUK_U_MSUB:
        case VUK_U_MULQ: case VUK_U_MUL_I: case VUK_U_ADD_I:
            ExecUpperFmac(u, s);
            break;
        case VUK_U_ADDA_BC: case VUK_U_SUBA_BC: case VUK_U_MULA_BC: case VUK_U_MADDA_BC: case VUK_U_MSUBA_BC:
        case VUK_U_MULA: case VUK_U_MADDA:
            ExecUpperAcc(u, s);
            break;
        case VUK_U_MAX_BC:
            VuFloat::ClampStoreACC(regs.ACC);
            minmax_bc(regs.VF[ft].I[bc], true);
            break;
        case VUK_U_MINI_BC:
            VuFloat::ClampStoreACC(regs.ACC);
            minmax_bc(regs.VF[ft].I[bc], false);
            break;
        case VUK_U_MINI_I:
            VuFloat::ClampStoreACC(regs.ACC);
            minmax_bc(regs.I, false);
            break;
        case VUK_U_MAX:
            VuFloat::ClampStoreACC(regs.ACC);
            VuFloat::ApplyMinMax4(regs.VF[fd], regs.VF[fs], regs.VF[ft], dest, fd == 0, true);
            break;
        case VUK_U_MINI:
            VuFloat::ClampStoreACC(regs.ACC);
            VuFloat::ApplyMinMax4(regs.VF[fd], regs.VF[fs], regs.VF[ft], dest, fd == 0, false);
            break;
        case VUK_U_FTOI0: case VUK_U_FTOI4: case VUK_U_FTOI_N:  // raw lanes * (1 / 16 / 4096 / 32768 by bc), saturating truncation
            VuFloat::ClampStoreACC(regs.ACC);
            write_ft_vec(VuFloat::Ftoi4Scaled(regs.VF[fs], kItofFtoiScale[bc]));
            break;
        case VUK_U_ITOF0: case VUK_U_ITOF_N:  // raw integer lanes -> float / scale
            VuFloat::ClampStoreACC(regs.ACC);
            write_ft_vec(VuFloat::Itof4Scaled(regs.VF[fs], kItofFtoiScale[bc]));
            break;
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
        if (dest == vu_dest::XYZW) {  // the whole quadword (the lanes are little-endian words in order)
            std::memcpy(data + addr, v.I, 16);
            return;
        }
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) StoreLe32(data + addr + 4 * k, v.I[k]);
        }
    };
    auto load_masked = [&](uint32_t reg, uint32_t addr) {
        if (reg == 0) return;
        if (dest == vu_dest::XYZW) {
            std::memcpy(regs.VF[reg].I, data + addr, 16);
            return;
        }
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
        // Clip-flag tests write VI01 directly (the interpreter's bodies; imm24 = the low 24 bits).
        case VUK_L_FCEQ: regs.VI[1] = (pipes.VisibleClip(regs) == (s.lower & 0xFFFFFFu)) ? 1 : 0; break;
        case VUK_L_FCSET: pipes.SetClipImmediate(regs, s.lower & 0xFFFFFFu); break;
        case VUK_L_FCAND: regs.VI[1] = ((pipes.VisibleClip(regs) & (s.lower & 0xFFFFFFu)) != 0) ? 1 : 0; break;
        case VUK_L_FCOR: regs.VI[1] = ((pipes.VisibleClip(regs) | (s.lower & 0xFFFFFFu)) == 0xFFFFFFu) ? 1 : 0; break;
        // STATUS / MAC readers: retire the due entries first; imm12 = {bit 21, bits 0-10}.
        case VUK_L_FSEQ:
            pipes.RetireMacStatus(regs);
            set_vi(vit, ((regs.status_flag & 0xFFFu) == Imm12(s.lower)) ? 1 : 0);
            break;
        case VUK_L_FSSET:  // sticky bits replaced, the current six kept
            pipes.RetireMacStatus(regs);
            regs.status_flag = static_cast<uint16_t>((regs.status_flag & 0x3Fu) | (Imm12(s.lower) & 0xFC0u));
            break;
        case VUK_L_FSAND:
            pipes.RetireMacStatus(regs);
            set_vi(vit, (regs.status_flag & 0xFFFu) & Imm12(s.lower));
            break;
        case VUK_L_FSOR:
            pipes.RetireMacStatus(regs);
            set_vi(vit, (regs.status_flag & 0xFFFu) | Imm12(s.lower));
            break;
        case VUK_L_FMEQ:
            pipes.RetireMacStatus(regs);
            set_vi(vit, (regs.mac_flag == regs.VI[vis]) ? 1 : 0);
            break;
        case VUK_L_FMAND:
            pipes.RetireMacStatus(regs);
            set_vi(vit, regs.mac_flag & regs.VI[vis]);
            break;
        case VUK_L_FMOR:
            pipes.RetireMacStatus(regs);
            set_vi(vit, regs.mac_flag | regs.VI[vis]);
            break;
        case VUK_L_IBLTZ: cond_branch(static_cast<int16_t>(regs.VI[vis]) < 0); break;
        case VUK_L_IBGTZ: cond_branch(static_cast<int16_t>(regs.VI[vis]) > 0); break;
        case VUK_L_IBLEZ: cond_branch(static_cast<int16_t>(regs.VI[vis]) <= 0); break;
        case VUK_L_IBGEZ: cond_branch(static_cast<int16_t>(regs.VI[vis]) >= 0); break;
        case VUK_L_XTOP: {  // + the VU1 yield rule
            Vu1State& st = u.state();
            set_vi(vit, u.vif_top());
            if (u.id() == 1 && ++st.xtop_read_count > VuInterpreterTier::kXtopYieldReads) st.xtop_exit_requested = true;
            break;
        }
        case VUK_L_XITOP: set_vi(vit, u.vif_itop()); break;  // reads the ITOPS shadow
        case VUK_L_XGKICK:  // VU1 only
            if (XgKick* xgk = u.xgkick()) {
                Vu1State& st = u.state();
                xgk->Arm(data, regs.VI[vis], pipes.cycle());
                ++st.xgkick_count;
                st.xgkick_has_fired = true;
                st.xtop_read_count = 0;
            }
            break;
        default:
            generic_.RunLower(u, s.lower, pc, branch_taken, branch_target);
            break;
    }
}
